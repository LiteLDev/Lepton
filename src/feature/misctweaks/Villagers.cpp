// VillagerRerollingRework (experimental): seeded first trades and trade rerolling on restock.

#include <cstring>
#include <random>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Random.h"
#include "mc/util/Random.h"
#include "mc/util/ThreadOwner.h"
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

/// Re-seeds a game Random the way MT19937 seeds (Random keeps its state in plain members).
void seedRandom(Core::Random& r, uint32_t seed) {
    auto& mt = r._mt;
    mt[0]    = seed;
    for (uint32_t i = 1; i < 624; ++i) mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i;
    r._mti                       = 624;
    r.mInitedIdx                 = 624;
    r.mSeed                      = seed;
    r.mHaveNextNextGaussian      = false;
    r.mHaveNextNextGaussianDouble = false;
}

bool gScratchGeneration = false; // generating candidates for a reroll: never seed

bool recipesEmpty(EconomyTradeableComponent& comp) {
    auto* list = comp.mOffers.get();
    return !list || list->mRecipeList->empty();
}

LL_TYPE_INSTANCE_HOOK(
    GenerateTradesHook,
    ll::memory::HookPriority::Normal,
    EconomyTradeableComponent,
    &EconomyTradeableComponent::_generateTrades,
    bool
) {
    Actor& owner = mOwner;
    auto const& c = cfg();
    if (gScratchGeneration || !c.enableVillagerRerollingRework || !c.seedInitialVillagerTrades || !isVillager(owner)
        || onClientSide(owner) || !recipesEmpty(*this))
        return origin();
    uint32_t seed = villagerSeed(owner);
    Core::Random& rnd = owner.getRandom().mRandom->mObject;
    // Save the villager's random, run the generation from the seed, restore it.
    std::vector<std::byte> saved(sizeof(Core::Random));
    std::memcpy(saved.data(), static_cast<void*>(&rnd), sizeof(Core::Random));
    seedRandom(rnd, seed);
    bool ok = origin();
    std::memcpy(static_cast<void*>(&rnd), saved.data(), sizeof(Core::Random));
    log().debug("misctweaks: seeded trades of villager {}", game::entities::idOf(owner));
    return ok;
}

std::optional<MerchantRecipe> candidateFor(EconomyTradeableComponent& comp, int tier, Random& rnd) {
    auto* list = comp.mOffers.get();
    if (!list) return std::nullopt;
    std::vector<MerchantRecipe> saved;
    auto                        savedTiers = list->mTierExpRequirements.get();
    saved.swap(list->mRecipeList.get());
    gScratchGeneration = true;
    comp._generateTrades();
    gScratchGeneration = false;
    if (comp.mOffers.get() != list) {
        log().warn("misctweaks: trade generation replaced the offer list; reroll skipped");
        return std::nullopt; // the old list (and `saved`) cannot be restored safely
    }
    std::vector<MerchantRecipe const*> sameTier;
    for (auto const& r : list->mRecipeList.get())
        if (r.mTier == tier) sameTier.push_back(&r);
    std::optional<MerchantRecipe> out;
    if (!sameTier.empty()) out.emplace(*sameTier[static_cast<size_t>(rnd.nextInt(static_cast<int>(sameTier.size())))]);
    list->mRecipeList.get().swap(saved);
    list->mTierExpRequirements = savedTiers;
    return out;
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
    if (j.value("day", -1) != day) { // first restock of the day: Java's resetNumberOfRestocks
        j["day"]     = day;
        j["rerolls"] = 0;
        saveData(owner, j);
        if (!cfg().rerollOnAnyRestock) attemptReroll(*this);
    }
    origin();
    if (cfg().rerollOnAnyRestock) attemptReroll(*this);
}

ll::event::ListenerPtr gDie;

} // namespace

uint32_t villagerSeed(Actor& villager) {
    auto j = loadData(villager);
    if (!j.contains("seed")) {
        std::random_device rd;
        j["seed"] = static_cast<uint32_t>(rd());
        saveData(villager, j);
    }
    return j["seed"].get<uint32_t>();
}

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
        list           = comp.mOffers.get();
        if (!candidate || !list) continue;
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
