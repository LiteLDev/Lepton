// VillagerRerollingRework (experimental): seeded first trades and trade rerolling on restock.

#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/util/Random.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/entity/components_json_legacy/EconomyTradeableComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/item/trading/MerchantRecipeList.h"
#include "mc/world/level/Level.h"

#include "feature/misctweaks/Internal.h"
#include "feature/misctweaks/Villagers.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace lp::feature::misctweaks {

namespace {

constexpr int kMaxTier = 4; // Bedrock trade tiers 0..4 (Java levels 1..5)

bool isVillager(Actor const& a) {
    auto const& t = a.getTypeName();
    return t == "minecraft:villager_v2" || t == "minecraft:villager";
}

std::string keyOf(Actor const& a) { return "lp:misctweaks:villager:" + std::to_string(game::entities::idOf(a)); }

nlohmann::json loadData(Actor const& a) {
    auto j = game::db::load(keyOf(a));
    return j && j->is_object() ? *j : nlohmann::json::object();
}

void saveData(Actor const& a, nlohmann::json const& j) { game::db::save(keyOf(a), j); }

bool gScratchGeneration = false; // generating candidates for a reroll: never seed

bool recipesEmpty(EconomyTradeableComponent& comp) {
    auto* list = comp.mOffers.get();
    return !list || list->mRecipeList->empty();
}

/// Quark seeds the villager's random for its first trades of a profession, so breaking and
/// replacing the job site regenerates the same trades. Bedrock's trade generation does not take a
/// random we can seed, so the same result is reached by keeping the first trades generated for
/// each trade table (profession) and handing them out again on every later first generation.
// High: runs outside the other units' _generateTrades hooks (U3 mason, U20 fisherman, U23
// cartographer trades), so what they add is part of the kept first trades and is not re-randomized
// after the kept list is handed out again.
LL_TYPE_INSTANCE_HOOK(
    GenerateTradesHook,
    ll::memory::HookPriority::High,
    EconomyTradeableComponent,
    &EconomyTradeableComponent::_generateTrades,
    bool
) {
    Actor&      owner = mOwner;
    auto const& c     = cfg();
    if (gScratchGeneration || !c.enableVillagerRerollingRework || !c.seedInitialVillagerTrades || !isVillager(owner)
        || onClientSide(owner) || !recipesEmpty(*this) || !game::db::available())
        return origin();
    bool ok    = origin();
    auto* list = mOffers.get();
    std::string table = mGeneratedTablePath.get();
    if (!ok || !list || list->mRecipeList->empty() || table.empty()) return ok;
    auto j     = loadData(owner);
    auto& kept = j["trades"];
    if (!kept.is_object()) kept = nlohmann::json::object();
    if (kept.contains(table) && kept[table].is_string()) {
        auto tag = CompoundTag::fromSnbt(kept[table].get<std::string>());
        if (tag) {
            list->mRecipeList->clear();
            list->load(*tag);
            log().debug("misctweaks: villager {} got its first {} trades again", game::entities::idOf(owner), table);
        }
    } else if (auto ctx = SaveContextFactory::createCloneSaveContext()) {
        if (auto tag = list->createTag(false, *ctx)) {
            kept[table] = tag->toSnbt(SnbtFormat::Minimize, 0);
            saveData(owner, j);
        }
    }
    return ok;
}
/// A random trade of `tier` from the villager's trade table. Bedrock only generates trades into an
/// empty offer slot, so the current list is set aside, a fresh list is generated (every tier, as
/// Bedrock does), one recipe of the tier is picked and the villager's own list is put back.
std::optional<MerchantRecipe> candidateFor(EconomyTradeableComponent& comp, int tier, Random& rnd) {
    std::unique_ptr<MerchantRecipeList> own = std::move(comp.mOffers);
    gScratchGeneration                      = true;
    comp._generateTrades();
    gScratchGeneration = false;
    std::unique_ptr<MerchantRecipeList> scratch = std::move(comp.mOffers);
    comp.mOffers                                = std::move(own);
    if (!scratch) return std::nullopt;
    std::vector<MerchantRecipe const*> sameTier;
    for (auto const& r : scratch->mRecipeList.get())
        if (r.mTier == tier) sameTier.push_back(&r);
    if (sameTier.empty()) return std::nullopt;
    return *sameTier[static_cast<size_t>(rnd.nextInt(static_cast<int>(sameTier.size())))];
}
bool sameTrade(MerchantRecipe const& a, MerchantRecipe const& b) {
    return game::itemToSnbt(a.mBuyA.get()) == game::itemToSnbt(b.mBuyA.get())
        && game::itemToSnbt(a.mBuyB.get()) == game::itemToSnbt(b.mBuyB.get())
        && game::itemToSnbt(a.mSell.get()) == game::itemToSnbt(b.mSell.get());
}

void replaceRecipe(MerchantRecipe& slot, MerchantRecipe const& with) {
    slot.~MerchantRecipe();
    new (&slot) MerchantRecipe(with);
}

LL_TYPE_INSTANCE_HOOK(
    ResupplyHook,
    ll::memory::HookPriority::Normal,
    EconomyTradeableComponent,
    &EconomyTradeableComponent::resupplyTrades,
    void
) {
    Actor& owner = mOwner;
    if (!cfg().enableVillagerRerollingRework || !isVillager(owner) || onClientSide(owner)) return origin();
    auto j   = loadData(owner);
    int  day = owner.getLevel().getTime() / 24000;
    bool newDay = j.value("day", -1) != day;
    if (newDay) { // first restock of the day: Java's resetNumberOfRestocks
        j["day"]     = day;
        j["rerolls"] = 0;
        saveData(owner, j);
    }
    // Before the restock resets the uses: the out-of-stock trades are the ones to reroll.
    if (cfg().rerollOnAnyRestock || newDay) attemptReroll(*this);
    origin();
}

ll::event::ListenerPtr gDie;

} // namespace

int attemptReroll(EconomyTradeableComponent& comp) {
    auto const& c = cfg();
    if (!c.enableVillagerRerollingRework || c.maximumRestocksPerDay == 0 || c.chanceToRerollWhenRestocking == 0) return 0;
    Actor& owner    = comp.mOwner;
    auto   j        = loadData(owner);
    int    restocks = j.value("rerolls", 0);
    int    max      = c.maximumRestocksPerDay;
    if (max > 0 && restocks >= max) return 0;
    auto* list = comp.mOffers.get();
    if (!list) return 0;
    Random& rnd     = owner.getRandom();
    int     changed = 0;
    for (size_t i = 0; i < list->mRecipeList->size(); ++i) {
        MerchantRecipe& offer = list->mRecipeList.get()[i];
        bool outOfStock = offer.mUses >= offer.mMaxUses;
        if (!((c.rerollEvenIfNotOutOfStock && offer.mUses > 0) || outOfStock)) continue;
        int tier = offer.mTier;
        if (tier < 0 || tier > kMaxTier) continue;
        if (rnd.nextDouble() >= c.chanceToRerollWhenRestocking) continue;
        auto candidate = candidateFor(comp, tier, rnd);
        if (!candidate) continue;
        bool duplicate = false;
        for (auto const& other : list->mRecipeList.get())
            if (sameTrade(other, *candidate)) {
                duplicate = true;
                break;
            }
        if (duplicate) continue;
        candidate->mRewardExp = candidate->mRewardExp && list->mRecipeList.get()[i].mRewardExp;
        replaceRecipe(list->mRecipeList.get()[i], *candidate);
        ++restocks;
        ++changed;
        if (max > 0 && restocks >= max) break;
    }
    if (max > 0) {
        j["rerolls"] = restocks;
        saveData(owner, j);
    }
    if (changed) log().info("misctweaks: villager {} rerolled {} trade(s)", game::entities::idOf(owner), changed);
    return changed;
}

void installVillagers() {
    GenerateTradesHook::hook();
    ResupplyHook::hook();
    gDie = ll::event::EventBus::getInstance().emplaceListener<ll::event::MobDieEvent>([](ll::event::MobDieEvent& ev) {
        if (isVillager(ev.self()) && game::db::available()) game::db::erase(keyOf(ev.self()));
    });
}

void uninstallVillagers() {
    if (gDie) ll::event::EventBus::getInstance().removeListener(gDie);
    gDie = nullptr;
    ResupplyHook::unhook();
    GenerateTradesHook::unhook();
}

} // namespace lp::feature::misctweaks
