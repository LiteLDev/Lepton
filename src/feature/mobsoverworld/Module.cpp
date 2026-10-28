// U20 mobsoverworld module: config, listeners, hooks and the per-tick dispatch to the crab, shiba,
// oretoise and resilience logic.

#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/entity/components_json_legacy/EconomyTradeableComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/alchemy/PotionBrewing.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/item/trading/MerchantRecipeList.h"

#include "feature/mobsoverworld/Common.h"
#include "feature/mobsoverworld/Config.h"
#include "feature/mobsoverworld/Features.h"
#include "feature/mobsoverworld/Messages.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

LP_REGISTER_CONFIG("mobsoverworld", lp::feature::mobsoverworld::Config);
LP_REGISTER_MESSAGE(lp::feature::mobsoverworld::EffectMessage);

namespace lp::feature::mobsoverworld {

Config const& config() { return lp::config::get<Config>(); }

namespace {

bool isServerLevel(Actor const& actor) { return !actor.getLevel().isClientSide(); }

InteractionResult success() {
    InteractionResult r{};
    r.mSuccess = true;
    r.mSwing   = true;
    return r;
}

// ---- hooks ---------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    InteractHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    InteractionResult,
    Actor&      actor,
    Vec3 const& location
) {
    if (!isServerLevel(*this)) return origin(actor, location);
    auto const& type = actor.getTypeName();
    if (type == kCrab) {
        if (crabs::interact(*this, actor) == Handled::Yes) return success();
    } else if (type == kShiba) {
        if (shiba::interact(*this, actor) == Handled::Yes) return success();
        bool wasTame    = actor.isTame();
        bool wasSitting = actor.isSitting();
        auto result     = origin(actor, location);
        shiba::afterInteract(*this, actor, wasTame, wasSitting, result.mSuccess);
        return result;
    } else if (type == kToretoise) {
        if (toretoise::interact(*this, actor) == Handled::Yes) return success();
    }
    return origin(actor, location);
}

LL_TYPE_INSTANCE_HOOK(
    JukeboxStartHook,
    ll::memory::HookPriority::Normal,
    JukeboxBlockActor,
    &JukeboxBlockActor::startPlayingRecord,
    void,
    BlockSource& region
) {
    origin(region);
    if (!region.getLevel().isClientSide()) {
        crabs::jukeboxStarted(static_cast<int>(region.getDimensionId()), getPosition());
    }
}

LL_TYPE_INSTANCE_HOOK(
    JukeboxStopHook,
    ll::memory::HookPriority::Normal,
    JukeboxBlockActor,
    &JukeboxBlockActor::stopPlayingRecord,
    void,
    BlockSource& region
) {
    origin(region);
    if (!region.getLevel().isClientSide()) {
        crabs::jukeboxStopped(static_cast<int>(region.getDimensionId()), getPosition());
    }
}

LL_TYPE_INSTANCE_HOOK(
    CompleteUsingItemHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::completeUsingItem,
    void
) {
    if (!isServerLevel(*this)) return origin();
    std::string id = game::itemId(getSelectedItem());
    origin();
    if (id == "minecraft:milk_bucket" || id.find("resilience_potion") != std::string::npos) {
        resilience::onDrink(*this, id);
    }
}

LL_STATIC_HOOK(BrewingInitHook, ll::memory::HookPriority::Normal, &PotionBrewing::initPotionBrewing, void) {
    resilience::resetBrewingMixes();
    origin();
    resilience::addBrewingMixes();
}

LL_TYPE_INSTANCE_HOOK(
    GenerateTradesHook,
    ll::memory::HookPriority::Normal,
    EconomyTradeableComponent,
    &EconomyTradeableComponent::_generateTrades,
    bool
) {
    bool result = origin();
    auto const& c = config();
    if (!c.enableCrabs || !c.addCrabLegToFishermanTrades) return result;
    if (mGeneratedTablePath->find("fisherman") == std::string::npos) return result;
    auto* offers = getOffers();
    if (!offers) return result;
    auto leg = game::makeItem("lepton:crab_leg", 4);
    if (game::isEmpty(leg)) return result;
    for (auto& r : *offers->mRecipeList) {
        if (r.mBuyA->getTypeName() == "lepton:crab_leg") return result;
    }
    // Quark: EmeraldForItems(crab_leg, 4, 12 uses, 30 xp) at the Expert level.
    MerchantRecipe recipe(ItemInstance(leg), ItemInstance(game::makeItem("minecraft:emerald", 1)));
    recipe.mTier      = 3;
    recipe.mMaxUses   = 12;
    recipe.mTraderExp = 30;
    recipe.mRewardExp = true;
    recipe.mBuyCountA = 4;
    offers->mRecipeList->push_back(recipe);
    log().info("mobsoverworld: fisherman buys crab legs");
    return result;
}

// ---- events --------------------------------------------------------------------------------------

void onTick(ll::event::ServerLevelTickEvent&) {
    auto level = ll::service::getLevel();
    if (!level) return;
    int64_t     now = gameTick();
    auto const& c   = config();
    std::vector<Actor*> crabsList, shibas, toretoises, despawn;
    for (Actor* a : level->getRuntimeActorList()) {
        if (!a || !a->isAlive()) continue;
        auto const& type = a->getTypeName();
        if (type == kCrab) (c.enableCrabs ? crabsList : despawn).push_back(a);
        else if (type == kShiba) (c.enableShiba ? shibas : despawn).push_back(a);
        else if (type == kToretoise) (c.enableToretoise ? toretoises : despawn).push_back(a);
        else resilience::see(*a, now);
    }
    for (Actor* a : crabsList) {
        crabs::tick(*a, now);
        resilience::see(*a, now);
    }
    for (Actor* a : shibas) {
        shiba::tick(*a, now);
        resilience::see(*a, now);
    }
    for (Actor* a : toretoises) {
        toretoise::tick(*a, now);
        resilience::see(*a, now);
    }
    // A disabled module's mobs do not exist (Quark does not register them).
    if (now % 100 == 0) {
        for (Actor* a : despawn) game::entities::despawn(*a);
    }
    crabs::tickWorld(now);
    resilience::tick(now);
}

void onHurt(ll::event::ActorHurtEvent& ev) {
    Actor& actor = ev.self();
    if (!isServerLevel(actor)) return;
    auto const& type   = actor.getTypeName();
    bool        cancel = false;
    if (type == kCrab && config().enableCrabs) cancel = crabs::onHurt(actor, ev.source(), ev.damage());
    else if (type == kShiba && config().enableShiba) cancel = shiba::onHurt(actor, ev.source(), ev.damage());
    else if (type == kToretoise && config().enableToretoise) cancel = toretoise::onHurt(actor, ev.source(), ev.damage());
    if (cancel) ev.cancel();
}

void onDie(ll::event::MobDieEvent& ev) {
    Actor& actor = ev.self();
    if (!isServerLevel(actor)) return;
    if (actor.getTypeName() == kShiba) shiba::onDeath(actor);
    resilience::clearEffect(actor);
}

void onSpawning(ll::event::SpawningMobEvent& ev) {
    auto const& type = ev.identifier().mCanonicalName->getString();
    if (type != kCrab && type != kShiba && type != kToretoise) return;
    BlockPos pos = game::blockPosOf(ev.pos());
    if (!spawning::allowed(type, static_cast<int>(ev.blockSource().getDimensionId()), pos, ev.naturalSpawn())) {
        ev.cancel();
    }
}

void onInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (!isServerLevel(player)) return;
    if (game::itemId(ev.item()) != "lepton:crab_bucket") return;
    if (crabs::useBucket(player, ev.blockPos(), static_cast<int>(ev.face()))) ev.cancel();
}

class MobsOverworldModule {
public:
    static MobsOverworldModule& get() {
        static MobsOverworldModule m;
        return m;
    }

    /// Before the world exists: the spawn rules and the oretoise foods come from the config.
    void load() { spawning::writePackFiles(); }

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        mListeners.push_back(bus.emplaceListener<ll::event::ActorHurtEvent>(onHurt));
        mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>(onDie));
        mListeners.push_back(bus.emplaceListener<ll::event::SpawningMobEvent>(onSpawning));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteractBlock));
        InteractHook::hook();
        JukeboxStartHook::hook();
        JukeboxStopHook::hook();
        CompleteUsingItemHook::hook();
        BrewingInitHook::hook();
        GenerateTradesHook::hook();
        if (game::db::available()) {
            // BDS enables mods after the world is ready (and after brewing was initialised).
            resilience::load();
            resilience::addBrewingMixes();
        }
        game::db::onWorldReady([] {
            resilience::load();
            resilience::addBrewingMixes(); // BDS: brewing was initialised before the mod was enabled
        });
        game::db::onWorldSave([] { resilience::save(); });
        game::db::onWorldClosed([] {
            crabs::clear();
            shiba::clear();
            toretoise::clear();
            resilience::clear();
            resilience::resetBrewingMixes();
        });
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        InteractHook::unhook();
        JukeboxStartHook::unhook();
        JukeboxStopHook::unhook();
        CompleteUsingItemHook::unhook();
        BrewingInitHook::unhook();
        GenerateTradesHook::unhook();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace lp::feature::mobsoverworld

LP_REGISTER_MODULE(lp::feature::mobsoverworld::MobsOverworldModule);
