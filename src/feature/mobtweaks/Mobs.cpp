// Mob tweaks: SaferCreatures, SlimesToMagmaCubes, SnowGolemPlayerHeads, VexesDieWithTheirMasters,
// ZombieVillagersOnNormal, DragonScales (drop part). Spec: docs/spec/mobtweaks.md §3.

#include <cmath>
#include <random>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/monster/Slime.h"
#include "mc/world/actor/provider/ActorAttribute.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/end/EndDragonFight.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/death/DeathTickingComponent.h"
#include "mc/world/level/dimension/end/TheEndDimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/mobtweaks/MobTweaks.h"
#include "feature/mobtweaks/Mobs.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::mobtweaks {

namespace ent = game::entities;

namespace {

bool isType(Actor const& a, ActorType t) { return a.getEntityTypeId() == t; }

/// Java: a Zombie subclass (zombie, husk, drowned, zombie villager). Bedrock marks these with the
/// ZombieMonster type bits.
bool isZombie(ActorType t) {
    auto bits = static_cast<int>(ActorType::ZombieMonster);
    return (static_cast<int>(t) & bits) == bits;
}

// ---- SaferCreatures ------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    FallDamageHook,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::$causeFallDamageToActor,
    float,
    float             distance,
    float             multiplier,
    ActorDamageSource source
) {
    auto const& c = cfg();
    if (c.enableSaferCreatures && !isClientSide()) {
        auto t = getEntityTypeId();
        if (t == ActorType::Rabbit) {
            distance = std::max(0.0f, distance - static_cast<float>(c.heightReduction));
        } else if (t == ActorType::Slime && c.enableSlimeFallDamageRemoval) {
            if (distance > 2.0f) mBuiltInComponents->mStateVectorComponent->mPosDelta->y = -2.0f;
            distance = 0.0f;
        }
    }
    return origin(distance, multiplier, std::move(source));
}

// ---- SlimesToMagmaCubes --------------------------------------------------------------------------

std::unordered_set<int64_t>& magmaKilled() {
    static std::unordered_set<int64_t> s;
    return s;
}

LL_TYPE_INSTANCE_HOOK(SlimeRemoveHook, ll::memory::HookPriority::Normal, Slime, &Slime::$remove, void) {
    auto id  = getOrCreateUniqueID().rawID;
    auto it  = magmaKilled().find(id);
    bool hit = it != magmaKilled().end();
    if (hit) magmaKilled().erase(it);
    if (!hit || !cfg().enableSlimesToMagmaCubes || isClientSide() || getEntityTypeId() != ActorType::Slime) {
        origin();
        return;
    }
    // Remember the slimes around before the removal: new ones are children of a vanilla split.
    int   dim    = static_cast<int>(getDimensionId());
    Vec3  center = getPosition();
    int   size   = slimeSizeOf(*this);
    auto  name   = getNameTag();
    AABB  box{center - Vec3{6, 4, 6}, center + Vec3{6, 4, 6}};
    std::unordered_set<int64_t> before;
    for (auto* a : ent::inBox(getDimensionBlockSource(), box, "minecraft:slime")) before.insert(ent::idOf(*a));
    origin();
    later(1, [dim, box, center, size, name, before = std::move(before)] {
        auto* region = game::regionOf(dim);
        if (!region) return;
        int converted = 0;
        for (auto* a : ent::inBox(*region, box, "minecraft:slime")) {
            if (before.contains(ent::idOf(*a)) || !a->isAlive()) continue;
            if (replaceWithMagmaCube(*a, std::max(size / 2, 1))) ++converted; // children: half the size
        }
        // Bedrock did not split a slime that died from magma damage: split it the Java way
        // (Slime#remove: 2..4 children of half the size around the body).
        if (converted == 0 && size > 1) converted = splitInto(*region, "minecraft:magma_cube", size, center, name);
        if (converted > 0) log().info("mobtweaks: slime of size {} split into {} magma cubes", size, converted);
    });
}

void onDieForSlimes(Mob& mob, ActorDamageSource const& source) {
    if (!cfg().enableSlimesToMagmaCubes || !isType(mob, ActorType::Slime)) return;
    if (source.mCause == SharedTypes::Legacy::ActorDamageCause::Magma) magmaKilled().insert(ent::idOf(mob));
}

// ---- SnowGolemPlayerHeads ------------------------------------------------------------------------

void onDieForSnowGolem(Mob& mob, ActorDamageSource const& source) {
    if (!cfg().enableSnowGolemPlayerHeads || !isType(mob, ActorType::SnowGolem)) return;
    auto const& name = mob.getNameTag();
    if (name.empty() || mob.getStatusFlag(ActorFlags::Sheared)) return;
    if (source.getEntityType() != ActorType::Witch && source.getDamagingEntityType() != ActorType::Witch) return;
    auto head = playerHead(name);
    if (game::isEmpty(head)) return;
    auto& region = mob.getDimensionBlockSource();
    mob.getLevel().getSpawner().spawnItem(region, head, nullptr, mob.getPosition(), 0);
    log().info("mobtweaks: snow golem head {}", name);
}

// ---- VexesDieWithTheirMasters --------------------------------------------------------------------

void onDieForVexes(Mob& mob) {
    if (!cfg().enableVexesDieWithTheirMasters) return;
    auto id    = mob.getOrCreateUniqueID();
    int  dim   = static_cast<int>(mob.getDimensionId());
    int  count = 0;
    for (auto* vex : ent::ofType("minecraft:vex", dim)) {
        if (vex == &mob || vex->isDead() || !vex->isAlive()) continue;
        if (vex->getOwnerId() != id) continue;
        vex->kill();
        ++count;
    }
    if (count > 0) log().info("mobtweaks: {} vexes died with their master", count);
}

// ---- ZombieVillagersOnNormal ---------------------------------------------------------------------

bool killsVillagerOnNormal(Mob& mob, ActorDamageSource const& src, float damage) {
    if (!cfg().enableZombieVillagersOnNormal || mob.isClientSide()) return false;
    auto t = mob.getEntityTypeId();
    if (t != ActorType::VillagerV2 && t != ActorType::Villager) return false;
    if (mob.getLevel().getDifficulty() != SharedTypes::Legacy::Difficulty::Normal) return false;
    if (!isZombie(src.getDamagingEntityType()) && !isZombie(src.getEntityType())) return false;
    return !mob.isDead() && damage >= static_cast<float>(mob.getHealth()); // the killing blow
}

/// Java Zombie#killedEntity converts the villager instead of letting it die; Bedrock's own
/// conversion (Hard, and 50% on Normal) works the same way through villager_v2's become_zombie.
LL_TYPE_INSTANCE_HOOK(
    VillagerHurtHook,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::$_hurt,
    ActorHurtResult,
    ActorDamageSource const& source,
    float                    damage,
    HurtParameters const&    params
) {
    if (killsVillagerOnNormal(*this, source, damage) && zombify(*this)) {
        ActorHurtResult none;
        none.mHurtOrDamage         = false;
        none.mShouldAllowKnockback = false;
        return none;
    }
    return origin(source, damage, params);
}

// ---- DragonScales (drop) -------------------------------------------------------------------------

/// The End fight's "a dragon was killed before" flag, sampled every tick while no dragon is dying
/// (Java reads it at death tick 100, before the dying dragon itself sets it at tick 200; Bedrock
/// may set it when the death starts, so the value from before the death is used).
struct FightSnapshot {
    uint64_t tick      = 0;
    bool     previously = false;
};
FightSnapshot& fightSnapshot() {
    static FightSnapshot s;
    return s;
}

/// Dying dragons: unique id -> drops a scale (decided at the first death tick seen).
std::unordered_map<int64_t, bool>& dyingDragons() {
    static std::unordered_map<int64_t, bool> m;
    return m;
}

EndDragonFight* endFight() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto dim = level->getDimension(2).lock();
    if (!dim) return nullptr;
    auto& fight = static_cast<TheEndDimension&>(*dim).mDragonFight;
    return fight ? fight.get() : nullptr;
}

void sampleFight() {
    auto& dying = dyingDragons();
    if (!dying.empty() && tickNow() % 20 == 0) std::erase_if(dying, [](auto const& e) { return ent::find(e.first) == nullptr; });
    if (!dying.empty()) return;
    auto* fight = endFight();
    fightSnapshot() = {tickNow(), fight && fight->mPreviouslyKilled};
}

/// Every 10 ticks: dying ender dragons (their ECS death timer) get their scale scheduled for death
/// tick 100.
void pollDragons() {
    if (!cfg().enableDragonScales) return;
    for (auto* dragon : ent::ofType("minecraft:ender_dragon")) {
        auto death = dragon->getEntityContext().tryGetComponent<DeathTickingComponent>();
        if (!death) continue;
        onDragonDying(ent::idOf(*dragon), death->mTicks, static_cast<int>(dragon->getDimensionId()), dragon->getPosition());
    }
}

} // namespace

// ---- shared (also used by selftests) -------------------------------------------------------------

int splitInto(BlockSource& region, std::string const& type, int size, Vec3 const& at, std::string const& name) {
    static std::mt19937 rng{std::random_device{}()};
    int   child = size / 2;
    int   count = 2 + std::uniform_int_distribution<int>(0, 2)(rng);
    int   made  = 0;
    for (int l = 0; l < count; ++l) {
        float dx = (static_cast<float>(l % 2) - 0.5f) * static_cast<float>(size) / 4.0f;
        float dz = (static_cast<float>(l / 2) - 0.5f) * static_cast<float>(size) / 4.0f;
        auto* a  = spawnSlimeOfSize(region, type, child, Vec3{at.x + dx, at.y + 0.5f, at.z + dz});
        if (!a) continue;
        if (!name.empty()) a->setNameTag(name);
        ++made;
    }
    return made;
}

int slimeSizeOf(Actor const& a) {
    float w = a.getAABB().max.x - a.getAABB().min.x;
    // Bedrock slime widths: 0.52 * size (size 1, 2, 4)
    int size = static_cast<int>(std::lround(w / 0.52f));
    return size <= 1 ? 1 : (size <= 2 ? 2 : 4);
}


ItemStack playerHead(std::string const& name) {
    auto head = game::makeItem("minecraft:player_head");
    if (game::isEmpty(head)) head = game::makeItem("minecraft:skull", 1, 3); // pre-flattening fallback
    if (game::isEmpty(head)) return head;
    head.setCustomName(Bedrock::Safety::RedactableString(tr("lp.mobtweaks.player_head", "{}'s Head", name)));
    return head;
}

bool replaceWithMagmaCube(Actor& slime, int size) {
    auto& region = slime.getDimensionBlockSource();
    auto* cube   = spawnSlimeOfSize(region, "minecraft:magma_cube", size, slime.getPosition());
    if (!cube) return false;
    if (auto const& tag = slime.getNameTag(); !tag.empty()) cube->setNameTag(tag);
    ent::despawn(slime);
    return true;
}

Actor* spawnSlimeOfSize(BlockSource& region, std::string const& type, int size, Vec3 const& pos) {
    // slime / magma_cube definition events: size, health and collision box of each size
    char const* ev = size >= 4 ? "<spawn_large>" : size >= 2 ? "<spawn_medium>" : "<spawn_small>";
    return ent::spawn(region, type + ev, pos);
}

bool zombify(Actor& villager) {
    // villager_v2's own transformation into minecraft:zombie_villager_v2 (keeps profession, trades
    // and level). The event converts with Bedrock's per-difficulty chance (Normal 50%, Hard 100%):
    // evaluate it as on Hard, as Quark always converts on Normal.
    auto& data  = villager.getLevel().getLevelData();
    auto  saved = villager.getLevel().getDifficulty();
    data.setGameDifficulty(SharedTypes::Legacy::Difficulty::Hard);
    bool ok = ent::triggerEvent(villager, "become_zombie");
    data.setGameDifficulty(saved);
    if (!ok) return false;
    log().info("mobtweaks: villager zombified");
    return true;
}

void onDragonDying(int64_t dragonId, int ticks, int dim, Vec3 const& pos) {
    if (!dyingDragons().emplace(dragonId, true).second) return; // already handled
    // Java: getDragonFight() != null (the End) && hasPreviouslyKilledDragon()
    bool respawned = dim == 2 && fightSnapshot().previously;
    if (testing::forceRespawned().erase(dragonId) > 0) respawned = true;
    if (!respawned || ticks > kDragonScaleTick) return;
    later(kDragonScaleTick - ticks, [dragonId, dim, pos] {
        auto* dragon = ent::find(dragonId);
        dropDragonScale(dim, dragon ? dragon->getPosition() : pos);
    });
}

void dropDragonScale(int dim, Vec3 const& pos) {
    auto* region = game::regionOf(dim);
    auto  level  = ll::service::getLevel();
    if (!region || !level) return;
    auto scale = game::makeItem("lepton:dragon_scale");
    if (game::isEmpty(scale)) return;
    level->getSpawner().spawnItem(*region, scale, nullptr, pos, 0);
    log().info("mobtweaks: dragon scale dropped");
}

namespace testing {
std::unordered_set<int64_t>& forceRespawned() {
    static std::unordered_set<int64_t> s;
    return s;
}
} // namespace testing

class MobsModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>([](ll::event::MobDieEvent& ev) {
            auto& mob = ev.self();
            if (mob.isClientSide()) return;
            onDieForSlimes(mob, ev.source());
            onDieForSnowGolem(mob, ev.source());
            onDieForVexes(mob);
        }));
        FallDamageHook::hook();
        SlimeRemoveHook::hook();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            sampleFight();
            if (tickNow() % 10 == 0) pollDragons();
        }));
        VillagerHurtHook::hook();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        FallDamageHook::unhook();
        SlimeRemoveHook::unhook();
        dyingDragons().clear();
        VillagerHurtHook::unhook();
        magmaKilled().clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::feature::mobtweaks

LP_REGISTER_MODULE(lp::feature::mobtweaks::MobsModule);
