// Selftests of U26 mobtweaks (dx 2600..2699). Spec: docs/spec/mobtweaks.md §8.

#include <algorithm>
#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageByActorSource.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/util/Random.h"
#include "mc/deps/core/math/IRandom.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/death/DeathTickingComponent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantResult.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/AnvilBlock.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/world/phys/AABB.h"

#include "feature/mobtweaks/Blocks.h"
#include "feature/mobtweaks/Items.h"
#include "feature/mobtweaks/MobTweaks.h"
#include "feature/mobtweaks/Mobs.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
using namespace lp::feature::mobtweaks;
namespace bs     = lp::game::blockstates;
namespace ent    = lp::game::entities;
namespace shapes = lp::game::shapes;
using Facing     = ScriptModuleMinecraft::ScriptFacing;
using lp::core::GlobalPos;
using lp::game::makeItem;

MobTweaksConfig& mutCfg() { return lp::config::mut<MobTweaksConfig>(); }

/// Restores the unit config when the test ends.
struct ConfigGuard {
    MobTweaksConfig saved = cfg();
    ~ConfigGuard() {
        mutCfg() = saved;
        rebuildCaches();
    }
};

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

GlobalPos off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

bool put(Context& ctx, GlobalPos const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}

std::string idAt(Context& ctx, GlobalPos const& p) { return lp::game::blockIdAt(regionOf(ctx), lp::game::blockPos(p)); }
Block const& at(Context& ctx, GlobalPos const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }

Vec3 center(GlobalPos const& p, float dy = 0.0f) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + dy, static_cast<float>(p.z) + 0.5f};
}

AABB around(GlobalPos const& p, float r) {
    auto c = center(p, 0.5f);
    return AABB{c - Vec3{r, r, r}, c + Vec3{r, r, r}};
}

HurtParameters hurtParams() {
    HurtParameters h{};
    h.mKnockback           = false;
    h.mIgnition            = false;
    h.mExtraKnockbackPower = 0.0f;
    return h;
}

void kill(Actor& a, ActorDamageSource const& src) { a.hurt(src, 1000.0f, hurtParams()); }

/// Item entities in the box with item id `id`, summed counts.
int droppedCount(Context& ctx, GlobalPos const& p, float r, std::string const& id, std::string* name = nullptr) {
    int total = 0;
    for (auto* a : ent::inBox(regionOf(ctx), around(p, r), "minecraft:item")) {
        auto& item = static_cast<ItemActor*>(a)->item();
        if (lp::game::itemId(item) != id) continue;
        total += item.mCount;
        if (name) *name = item.getCustomName();
    }
    return total;
}

void clearItems(Context& ctx, GlobalPos const& p, float r) {
    for (auto* a : ent::inBox(regionOf(ctx), around(p, r), "minecraft:item")) ent::despawn(*a);
}

void clearMobs(Context& ctx, GlobalPos const& p, float r, std::string_view type) {
    for (auto* a : ent::inBox(regionOf(ctx), around(p, r), type)) ent::despawn(*a);
}

/// Hostile mobs need a difficulty above peaceful (the e2e world is peaceful).
struct DifficultyGuard {
    SharedTypes::Legacy::Difficulty saved;
    explicit DifficultyGuard(SharedTypes::Legacy::Difficulty d = SharedTypes::Legacy::Difficulty::Normal)
    : saved(ll::service::getLevel()->getDifficulty()) {
        ll::service::getLevel()->setDifficulty(d);
    }
    ~DifficultyGuard() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(saved);
    }
};

/// Moves the test player away so it doesn't pick up the items a test drops.
void stepAway(Context& ctx, GlobalPos const& p) {
    ctx.player().teleport(center(off(p, 0, 0, -12), 1.0f), DimensionType(p.dim));
}

SimulatedPlayer& simOf(Context& ctx) { return *static_cast<SimulatedPlayer*>(&ctx.player()); }

} // namespace

// ---- config ----------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.config", [](Context& ctx) {
    ConfigGuard guard;
    auto        e = parseBakedEntry("minecraft:stick+minecraft:sharpness@10");
    ctx.check(e && e->item == "minecraft:stick" && e->enchant == "sharpness" && e->level == 10, "item+enchant@level parses");
    ctx.check(!parseBakedEntry("stick@sharpness+1"), "malformed baked entries are rejected");
    auto r = parseRepairEntry("minecraft:diamond_sword=minecraft:diamond,minecraft:emerald");
    ctx.check(r && r->second.size() == 2 && r->second[1] == "minecraft:emerald", "item=a,b parses");

    auto const baked = bakedEnchantments();
    auto        pick  = baked.find("minecraft:golden_pickaxe");
    ctx.require(pick != baked.end(), "golden pickaxe has baked enchantments");
    ctx.check(pick->second.at(static_cast<int>(Enchant::Type::Fortune)) == 2, "golden pickaxe: Fortune II");
    auto sword = baked.find("minecraft:golden_sword");
    ctx.check(
        sword != baked.end() && sword->second.contains(static_cast<int>(Enchant::Type::Looting)),
        "golden sword: Looting"
    );
    ctx.check(repairOverride("minecraft:netherite_sword", "minecraft:diamond") == true, "netherite repaired by diamond");
    ctx.check(repairOverride("minecraft:netherite_sword", "minecraft:netherite_ingot") == false, "not by netherite ingots");
    ctx.check(!repairOverride("minecraft:iron_sword", "minecraft:iron_ingot"), "other items: vanilla");

    mutCfg().fortuneLevel     = 0;
    mutCfg().itemEnchantments = {"minecraft:stick+minecraft:knockback@3"};
    rebuildCaches();
    auto const after = bakedEnchantments();
    ctx.check(!after.contains("minecraft:golden_pickaxe"), "fortuneLevel 0: no baked fortune");
    ctx.check(
        after.contains("minecraft:stick") && after.at("minecraft:stick").at(static_cast<int>(Enchant::Type::Knockback)) == 3,
        "itemEnchantments bake into any item"
    );
});

// ---- SaferCreatures ---------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.safer_creatures", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyGuard difficulty;
    ConfigGuard guard;
    auto        pos = ctx.testPos(2600);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    auto  fall   = [](Actor& a, float d) {
        static_cast<Mob&>(a).causeFallDamageToActor(d, 1.0f, ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Fall, {}));
    };

    auto* rabbit = ent::spawn(region, "minecraft:rabbit", center(pos, 1));
    ctx.require(rabbit, "rabbit spawned");
    auto rabbitId = ent::idOf(*rabbit);
    co_await ctx.ticks(2);
    rabbit = ent::find(rabbitId);
    ctx.require(rabbit, "rabbit alive");
    int hp = rabbit->getHealth();
    fall(*rabbit, 6.0f); // vanilla: 3 damage; minus 5 blocks: none
    co_await ctx.ticks(1);
    rabbit = ent::find(rabbitId);
    ctx.check(rabbit && rabbit->getHealth() == hp, "a rabbit falling 6 blocks takes no damage");

    auto* slime = spawnSlimeOfSize(region, "minecraft:slime", 2, center(pos, 1));
    ctx.require(slime, "slime spawned");
    auto slimeId = ent::idOf(*slime);
    co_await ctx.ticks(2);
    slime = ent::find(slimeId);
    ctx.require(slime, "slime alive");
    int shp = slime->getHealth();
    fall(*slime, 20.0f);
    co_await ctx.ticks(1);
    slime = ent::find(slimeId);
    ctx.check(slime && slime->getHealth() == shp, "slimes take no fall damage (" + std::to_string(shp) + " -> " + std::to_string(slime ? slime->getHealth() : -1) + ")");

    mutCfg().enableSaferCreatures = false;
    auto* rabbit2 = ent::spawn(region, "minecraft:rabbit", center(pos, 1));
    ctx.require(rabbit2, "second rabbit spawned");
    auto id2 = ent::idOf(*rabbit2);
    co_await ctx.ticks(2);
    rabbit2 = ent::find(id2);
    ctx.require(rabbit2, "second rabbit alive");
    int hp2 = rabbit2->getHealth();
    fall(*rabbit2, 6.0f);
    co_await ctx.ticks(1);
    rabbit2 = ent::find(id2);
    ctx.check(!rabbit2 || rabbit2->getHealth() < hp2, "module off: the rabbit is hurt (vanilla)");
    clearMobs(ctx, pos, 6, "minecraft:rabbit");
    clearMobs(ctx, pos, 6, "minecraft:slime");
    clearItems(ctx, pos, 6);
});

// ---- SlimesToMagmaCubes -----------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.slimes_to_magma", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyGuard difficulty;
    auto pos = ctx.testPos(2605);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    clearMobs(ctx, pos, 8, "minecraft:slime");
    clearMobs(ctx, pos, 8, "minecraft:magma_cube");

    auto* probe = spawnSlimeOfSize(region, "minecraft:magma_cube", 2, center(pos, 1));
    ctx.require(probe, "magma cube spawned");
    auto probeId = ent::idOf(*probe);
    co_await ctx.ticks(2);
    probe = ent::find(probeId);
    ctx.check(probe && slimeSizeOf(*probe) == 2, "magma cubes spawn with a chosen size");
    if (probe) ctx.info("size-2 magma cube health " + std::to_string(probe->getHealth()));
    if (probe) ent::despawn(*probe);

    auto* big = spawnSlimeOfSize(region, "minecraft:slime", 4, center(pos, 1));
    ctx.require(big, "big slime spawned");
    auto bigId = ent::idOf(*big);
    co_await ctx.ticks(2);
    big = ent::find(bigId);
    ctx.require(big && slimeSizeOf(*big) == 4, "big slime has size 4");
    kill(*big, ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Magma, {}));
    co_await ctx.ticks(60);
    int cubes  = static_cast<int>(ent::inBox(region, around(pos, 8), "minecraft:magma_cube").size());
    int slimes = static_cast<int>(ent::inBox(region, around(pos, 8), "minecraft:slime").size());
    ctx.info("after a magma death: " + std::to_string(cubes) + " magma cubes, " + std::to_string(slimes) + " slimes");
    ctx.check(cubes >= 2 && cubes <= 4 && slimes == 0, "a slime killed by magma splits into magma cubes");
    for (auto* c : ent::inBox(region, around(pos, 8), "minecraft:magma_cube"))
        ctx.check(slimeSizeOf(*c) == 2, "the magma cubes have half the size (" + std::to_string(slimeSizeOf(*c)) + ")");
    clearMobs(ctx, pos, 8, "minecraft:magma_cube");

    auto* other = spawnSlimeOfSize(region, "minecraft:slime", 2, center(pos, 1));
    ctx.require(other, "second slime spawned");
    kill(*other, ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Override, {}));
    co_await ctx.ticks(60);
    cubes  = static_cast<int>(ent::inBox(region, around(pos, 8), "minecraft:magma_cube").size());
    slimes = static_cast<int>(ent::inBox(region, around(pos, 8), "minecraft:slime").size());
    ctx.check(cubes == 0 && slimes >= 2, "other deaths split into slimes");
    clearMobs(ctx, pos, 8, "minecraft:slime");
    clearMobs(ctx, pos, 8, "minecraft:magma_cube");
    clearItems(ctx, pos, 8);
});

// ---- SnowGolemPlayerHeads ---------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.snow_golem_head", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyGuard difficulty;
    auto pos = ctx.testPos(2610);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    stepAway(ctx, pos);
    clearItems(ctx, pos, 6);
    auto* witch = ent::spawn(region, "minecraft:witch", center(off(pos, 3, 0, 0), 1));
    ctx.require(witch, "witch spawned");
    auto witchId = ent::idOf(*witch);

    auto* golem = ent::spawn(region, "minecraft:snow_golem", center(pos, 1));
    ctx.require(golem, "snow golem spawned");
    golem->setNameTag("Vazkii");
    kill(*golem, ActorDamageByActorSource(*ent::find(witchId), SharedTypes::Legacy::ActorDamageCause::EntityAttack));
    co_await ctx.ticks(5);
    std::string name;
    ctx.check(droppedCount(ctx, pos, 4, "minecraft:player_head", &name) == 1, "a named golem killed by a witch drops a head");
    ctx.check(name == "Vazkii's Head", "the head is named after the golem (got '" + name + "')");
    clearItems(ctx, pos, 6);

    auto* plain = ent::spawn(region, "minecraft:snow_golem", center(pos, 1));
    ctx.require(plain, "second golem spawned");
    kill(*plain, ActorDamageByActorSource(*ent::find(witchId), SharedTypes::Legacy::ActorDamageCause::EntityAttack));
    auto* named = ent::spawn(region, "minecraft:snow_golem", center(pos, 1));
    ctx.require(named, "third golem spawned");
    named->setNameTag("Steve");
    kill(*named, ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Override, {}));
    co_await ctx.ticks(5);
    ctx.check(droppedCount(ctx, pos, 4, "minecraft:player_head") == 0, "no head without a name or without a witch");
    if (auto* w = ent::find(witchId)) ent::despawn(*w);
    clearItems(ctx, pos, 6);
});

// ---- VexesDieWithTheirMasters -----------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.vexes", [](Context& ctx) -> ll::coro::CoroTask<> {
    DifficultyGuard difficulty;
    auto pos = ctx.testPos(2615);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    auto* evoker = ent::spawn(region, "minecraft:evocation_illager", center(pos, 1));
    ctx.require(evoker, "evoker spawned");
    auto evokerId = evoker->getOrCreateUniqueID();
    auto* vex     = ent::spawn(region, "minecraft:vex", center(pos, 3));
    auto* free    = ent::spawn(region, "minecraft:vex", center(pos, 3));
    ctx.require(vex && free, "vexes spawned");
    vex->setOwner(evokerId);
    auto vexId  = ent::idOf(*vex);
    auto freeId = ent::idOf(*free);
    co_await ctx.ticks(2);
    ctx.require(ent::find(vexId) && ent::find(vexId)->getOwnerId() == evokerId, "the vex belongs to the evoker");
    kill(*ent::find(evokerId.rawID), ActorDamageSource(SharedTypes::Legacy::ActorDamageCause::Override, {}));
    co_await ctx.ticks(3);
    auto* v = ent::find(vexId);
    ctx.check(!v || v->isDead() || !v->isAlive(), "the vex dies with its master");
    auto* f = ent::find(freeId);
    ctx.check(f && f->isAlive() && !f->isDead(), "vexes of other masters live on");
    clearMobs(ctx, pos, 8, "minecraft:vex");
    clearMobs(ctx, pos, 8, "minecraft:evocation_illager");
    clearItems(ctx, pos, 8);
});

// ---- ZombieVillagersOnNormal ------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.zombie_villagers", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2620);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    DifficultyGuard difficulty;
    clearMobs(ctx, pos, 6, "minecraft:zombie_villager_v2");

    int converted = 0;
    for (int i = 0; i < 4; ++i) { // vanilla Normal: 50% each; with the module: every time
        auto* zombie   = ent::spawn(region, "minecraft:zombie", center(off(pos, 3, 0, 0), 1));
        auto* villager = ent::spawn(region, "minecraft:villager_v2", center(pos, 1));
        ctx.require(zombie && villager, "zombie and villager spawned");
        auto zid = ent::idOf(*zombie);
        kill(*villager, ActorDamageByActorSource(*zombie, SharedTypes::Legacy::ActorDamageCause::EntityAttack));
        co_await ctx.ticks(10);
        converted += static_cast<int>(ent::inBox(region, around(pos, 4), "minecraft:zombie_villager_v2").size());
        clearMobs(ctx, pos, 6, "minecraft:zombie_villager_v2");
        clearMobs(ctx, pos, 6, "minecraft:villager_v2");
        if (auto* z = ent::find(zid)) ent::despawn(*z);
    }
    ctx.check(converted == 4, "on Normal every villager killed by a zombie converts (" + std::to_string(converted) + "/4)");
    clearItems(ctx, pos, 6);
});

// ---- DragonScales -----------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.dragon_scale", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2625);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(!lp::game::isEmpty(makeItem("lepton:dragon_scale")), "lepton:dragon_scale exists (behaviour pack loaded)");
    auto& region = regionOf(ctx);
    stepAway(ctx, pos);
    clearItems(ctx, pos, 4);

    // The death-tick rule: a respawned dragon drops one scale at tick 100, a first dragon none.
    int64_t again = -2600001, first = -2600002;
    testing::forceRespawned().insert(again);
    onDragonDying(again, 1, pos.dim, center(pos, 1));
    onDragonDying(again, 2, pos.dim, center(pos, 1)); // seen again: nothing new
    onDragonDying(first, 1, pos.dim, center(pos, 1));
    co_await ctx.ticks(97);
    ctx.check(droppedCount(ctx, pos, 3, "lepton:dragon_scale") == 0, "no scale before death tick 100");
    co_await ctx.ticks(5);
    ctx.check(droppedCount(ctx, pos, 3, "lepton:dragon_scale") == 1, "one scale at death tick 100, none for a first dragon");
    clearItems(ctx, pos, 4);

    // A real dragon's death sequence reaches the hook (high above the test area).
    DifficultyGuard difficulty;
    auto  sky    = off(pos, 0, 40, 0);
    auto* dragon = ent::spawn(region, "minecraft:ender_dragon", center(sky));
    ctx.require(dragon, "dragon spawned");
    auto id = ent::idOf(*dragon);
    testing::forceRespawned().insert(id);
    co_await ctx.ticks(5);
    if (auto* d = ent::find(id)) d->kill();
    co_await ctx.ticks(2);
    if (auto* d = ent::find(id)) {
        bool dying = d->getEntityContext().tryGetComponent<DeathTickingComponent>().has_value();
        ctx.info("dragon after kill(): health " + std::to_string(d->getHealth()) + ", death timer " + (dying ? "yes" : "no"));
    }
    co_await ctx.ticks(110);
    ctx.check(droppedCount(ctx, sky, 40, "lepton:dragon_scale") == 1, "the dying dragon dropped a scale");
    testing::forceRespawned().erase(id);
    if (auto* d = ent::find(id)) ent::despawn(*d);
    co_await ctx.ticks(1);
    clearItems(ctx, sky, 45);
    clearMobs(ctx, sky, 45, "minecraft:xp_orb");
});

// ---- GoldToolsHaveFortune ---------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.gold_tools", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    auto        gold  = makeItem("minecraft:golden_pickaxe");
    auto        sword = makeItem("minecraft:golden_sword");
    auto        iron  = makeItem("minecraft:iron_pickaxe");
    ctx.check(EnchantUtils::getEnchantLevel(Enchant::Type::Fortune, gold) == 2, "golden pickaxe: Fortune 2 baked in");
    ctx.check(EnchantUtils::getEnchantLevel(Enchant::Type::Looting, sword) == 2, "golden sword: Looting 2 baked in");
    ctx.check(EnchantUtils::getEnchantLevel(Enchant::Type::Fortune, sword) == 0, "golden sword: no Fortune");
    ctx.check(EnchantUtils::getEnchantLevel(Enchant::Type::Fortune, iron) == 0, "iron pickaxe: nothing baked");
    ctx.check(EnchantUtils::hasEnchant(Enchant::Type::Fortune, gold), "hasEnchant sees the baked enchantment");

    auto gold3 = makeItem("minecraft:golden_pickaxe");
    EnchantmentInstance f3;
    f3.mEnchantType = Enchant::Type::Fortune;
    f3.mLevel       = 3;
    EnchantUtils::applyEnchant(gold3, f3, true);
    ctx.check(EnchantUtils::getEnchantLevel(Enchant::Type::Fortune, gold3) == 3, "a higher real level wins");
    auto silk = makeItem("minecraft:golden_pickaxe");
    EnchantmentInstance st;
    st.mEnchantType = Enchant::Type::SilkTouch;
    st.mLevel       = 1;
    EnchantUtils::applyEnchant(silk, st, true);
    ctx.check(EnchantUtils::getEnchantLevel(Enchant::Type::Fortune, silk) == 0, "baked Fortune yields to Silk Touch");

    auto const* ore = bs::of("minecraft:diamond_ore");
    ctx.require(ore != nullptr, "diamond ore exists");
    ctx.check(gold.mItem->canDestroySpecial(*ore), "harvest level 2: the golden pickaxe can mine diamond ore");
    mutCfg().harvestLevel = 0;
    ctx.check(!gold.mItem->canDestroySpecial(*ore), "harvest level 0: it can't");
    mutCfg().harvestLevel = 2;
    mutCfg().enableGoldToolsHaveFortune = false;
    ctx.check(EnchantUtils::getEnchantLevel(Enchant::Type::Fortune, gold) == 0, "module off: no baked level");
    mutCfg().enableGoldToolsHaveFortune = true;

    // Mining: 12 diamond ores with a golden pickaxe drop diamonds, more than one per ore (Fortune).
    auto pos = ctx.testPos(2630);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    p.setPlayerGameType(GameType::Survival);
    clearItems(ctx, pos, 8);
    int ores = 0;
    for (int i = 0; i < 12; ++i) {
        auto at = off(pos, i % 4, 0, 2 + i / 4);
        if (put(ctx, at, "minecraft:diamond_ore")) ++ores;
    }
    ctx.require(ores == 12, "ores placed");
    stepAway(ctx, pos);
    p.setSelectedItem(makeItem("minecraft:golden_pickaxe"));
    for (int i = 0; i < 12; ++i) {
        auto  at = lp::game::blockPos(off(pos, i % 4, 0, 2 + i / 4));
        auto& r  = regionOf(ctx);
        r.getBlock(at).playerDestroy(p, at); // the drops of a player's break, with the held tool
        lp::game::clearBlock(r, at);
    }
    co_await ctx.ticks(3);
    int diamonds = droppedCount(ctx, off(pos, 2, 0, 3), 6, "minecraft:diamond");
    ctx.info("12 ores gave " + std::to_string(diamonds) + " diamonds");
    ctx.check(idAt(ctx, off(pos, 0, 0, 2)) != "minecraft:diamond_ore", "the ore was mined");
    ctx.check(diamonds > 12, "a golden pickaxe mines diamonds with Fortune");
    clearItems(ctx, pos, 10);
    p.setSelectedItem(ItemStack{});
});

// Baked enchantments are effects only (Java): anvil, enchanting table and grindstone see the real ones.
LP_REGISTER_SELFTEST("mobtweaks.gold_tools_tables", [](Context& ctx) {
    ConfigGuard guard;
    auto        enchant = [](Enchant::Type t, int lvl) {
        EnchantmentInstance e;
        e.mEnchantType = t;
        e.mLevel       = lvl;
        return e;
    };
    auto book = makeItem("minecraft:enchanted_book");
    EnchantUtils::applyEnchant(book, enchant(Enchant::Type::Fortune, 3), true);
    struct Outcome {
        int         silk, fortune, cost;
        std::string out;
        bool        enchanted;
    };
    auto measure = [&] {
        auto    gold = makeItem("minecraft:golden_pickaxe");
        Outcome o;
        o.silk    = static_cast<int>(EnchantUtils::canEnchant(gold, enchant(Enchant::Type::SilkTouch, 1), false).result);
        o.fortune = static_cast<int>(EnchantUtils::canEnchant(gold, enchant(Enchant::Type::Fortune, 1), false).result);
        ItemStack out;
        o.cost = EnchantUtils::combineEnchantedItems(gold, book, out, true);
        o.out       = lp::game::itemToSnbt(out);
        o.enchanted = gold.isEnchanted();
        return o;
    };
    testing::gearScopes() = false;
    auto raw              = measure();
    testing::gearScopes() = true;
    auto on               = measure();
    ctx.info("without the table guards: canEnchant silk " + std::to_string(raw.silk) + ", fortune " + std::to_string(raw.fortune)
             + ", anvil cost " + std::to_string(raw.cost) + (raw.out == on.out ? " (same result)" : " (different result)"));
    mutCfg().enableGoldToolsHaveFortune = false;
    auto off                            = measure();
    ctx.info("canEnchant silk " + std::to_string(on.silk) + "/" + std::to_string(off.silk) + ", fortune "
             + std::to_string(on.fortune) + "/" + std::to_string(off.fortune) + ", anvil cost " + std::to_string(on.cost)
             + "/" + std::to_string(off.cost));
    ctx.check(on.silk == off.silk, "Silk Touch can still be put on a golden pickaxe");
    ctx.check(on.fortune == off.fortune, "Fortune can still be put on a golden pickaxe");
    ctx.check(on.cost == off.cost && on.out == off.out, "the anvil combines a Fortune III book as on a plain pickaxe");
    ctx.check(!on.enchanted, "a golden pickaxe doesn't count as enchanted");
});

// ---- DiamondRepair ----------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.diamond_repair", [](Context& ctx) {
    ConfigGuard guard;
    auto const& version = ctx.player().getLevel().getLevelData().getBaseGameVersion();
    auto        repairs = [&](char const* item, char const* with) {
        auto a = makeItem(item);
        auto b = makeItem(with);
        return a.mItem->isValidRepairItem(a, b, version);
    };
    ctx.check(repairs("minecraft:netherite_sword", "minecraft:diamond"), "netherite sword + diamond");
    ctx.check(!repairs("minecraft:netherite_sword", "minecraft:netherite_ingot"), "netherite sword + ingot refused");
    ctx.check(repairs("minecraft:netherite_chestplate", "minecraft:diamond"), "netherite chestplate + diamond");
    ctx.check(repairs("minecraft:netherite_pickaxe", "minecraft:diamond"), "netherite pickaxe + diamond");
    ctx.check(repairs("minecraft:iron_sword", "minecraft:iron_ingot"), "iron sword + iron ingot (vanilla)");
    mutCfg().unrepairableItemsList = {"minecraft:iron_sword"};
    rebuildCaches();
    ctx.check(!repairs("minecraft:iron_sword", "minecraft:iron_ingot"), "unrepairable items refuse every material");
    mutCfg().enableDiamondRepair = false;
    ctx.check(repairs("minecraft:netherite_sword", "minecraft:netherite_ingot"), "module off: vanilla rules");
});

// ---- NoDurabilityOnCosmetics ------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.cosmetic_anvil", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2635);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.check(sparesAnvil(ItemStack{}), "renaming (empty material) spares the anvil");
    ctx.check(!sparesAnvil(makeItem("minecraft:iron_ingot")), "repairs don't");
    auto& p  = ctx.player();
    auto  id = p.getOrCreateUniqueID().rawID;
    ctx.require(put(ctx, pos, "minecraft:anvil"), "anvil placed");
    auto const* fresh = &at(ctx, pos);
    for (int i = 0; i < 60; ++i) {
        noteSparedCraft(id);
        AnvilBlock::tryApplyCraftingDamage(p, lp::game::blockPos(pos), p.getRandom());
    }
    co_await ctx.ticks(1);
    ctx.check(&at(ctx, pos) == fresh, "60 rename crafts leave the anvil undamaged");
    for (int i = 0; i < 60 && &at(ctx, pos) == fresh; ++i) {
        AnvilBlock::tryApplyCraftingDamage(p, lp::game::blockPos(pos), p.getRandom());
    }
    co_await ctx.ticks(1);
    ctx.check(&at(ctx, pos) != fresh, "other crafts damage it (vanilla 12%)");
});

// ---- ShulkerPacking ---------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.shulker_packing", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2640);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = regionOf(ctx);
    auto  fill   = [&](GlobalPos const& at) -> Container* {
        auto* be = region.getBlockEntity(lp::game::blockPos(at));
        return be ? static_cast<ChestBlockActor*>(be)->getContainer() : nullptr;
    };

    ctx.require(put(ctx, pos, "minecraft:chest"), "chest placed");
    co_await ctx.ticks(1);
    auto* chest = fill(pos);
    ctx.require(chest && chest->getContainerSize() == 27, "single chest has 27 slots");
    chest->setItem(0, makeItem("minecraft:cobblestone", 10));
    chest->setItem(13, makeItem("minecraft:diamond", 3));
    p.setSelectedItem(makeItem("minecraft:shulker_shell"));
    p.setOffhandSlot(makeItem("minecraft:shulker_shell"));
    ctx.require(packChest(p, region, lp::game::blockPos(pos)), "the chest is packed");
    ctx.check(idAt(ctx, pos) == "minecraft:air", "the chest is gone");
    auto box  = p.getSelectedItem();
    auto snbt = lp::game::itemToSnbt(box);
    ctx.check(lp::game::itemId(box) == "minecraft:undyed_shulker_box", "the player holds a shulker box");
    ctx.check(snbt.find("minecraft:diamond") != std::string::npos && snbt.find("minecraft:cobblestone") != std::string::npos,
              "the box holds the chest's items");
    ctx.check(lp::game::isEmpty(p.getOffhandSlot()), "one shell used from the off-hand");
    ctx.check(droppedCount(ctx, pos, 3, "minecraft:chest") == 0, "the chest drops nothing");

    // Vanilla clients: two shells in the main hand.
    ctx.require(put(ctx, pos, "minecraft:trapped_chest"), "trapped chest placed");
    co_await ctx.ticks(1);
    p.setSelectedItem(makeItem("minecraft:shulker_shell", 3));
    p.setOffhandSlot(ItemStack{});
    ctx.check(packChest(p, region, lp::game::blockPos(pos)), "two main-hand shells pack a trapped chest");
    bool found = false;
    for (int i = 0; i < 36; ++i)
        if (lp::game::itemId(p.getInventory().getItem(i)) == "minecraft:undyed_shulker_box") found = true;
    ctx.check(found, "the box went to the inventory");
    ctx.check(p.getSelectedItem().mCount == 1, "two shells used");

    // Double chests and single shells don't pack.
    auto pos2 = off(pos, 1, 0, 0);
    ctx.require(put(ctx, pos, "minecraft:chest") && put(ctx, pos2, "minecraft:chest"), "double chest placed");
    co_await ctx.ticks(2);
    p.setSelectedItem(makeItem("minecraft:shulker_shell", 2));
    ctx.check(!packChest(p, region, lp::game::blockPos(pos)) || idAt(ctx, pos2) == "minecraft:chest",
              "a double chest is not packed");
    p.setSelectedItem(makeItem("minecraft:shulker_shell", 1));
    ctx.check(!packChest(p, region, lp::game::blockPos(pos2)), "one shell is not enough");
    p.setSelectedItem(ItemStack{});
    p.getInventory().removeAllItems();
    clearItems(ctx, pos, 4);
});

// ---- MagmaKeepsConcretePowder -----------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.magma_concrete", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2645);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    // powder on magma next to water / powder on stone next to water
    ctx.require(put(ctx, pos, "minecraft:magma") && put(ctx, off(pos, 3, 0, 0), "minecraft:stone"), "bases placed");
    ctx.require(put(ctx, off(pos, 1, 1, 0), "minecraft:glass") && put(ctx, off(pos, 2, 1, 0), "minecraft:glass"), "walls placed");
    ctx.require(put(ctx, off(pos, 0, 1, 1), "minecraft:water") && put(ctx, off(pos, 3, 1, 1), "minecraft:water"), "water placed");
    co_await ctx.ticks(2);
    ctx.require(put(ctx, off(pos, 0, 1, 0), "minecraft:white_concrete_powder"), "powder on magma");
    ctx.require(put(ctx, off(pos, 3, 1, 0), "minecraft:white_concrete_powder"), "powder on stone");
    co_await ctx.ticks(10);
    ctx.check(idAt(ctx, off(pos, 0, 1, 0)) == "minecraft:white_concrete_powder", "powder over magma stays powder");
    ctx.check(idAt(ctx, off(pos, 3, 1, 0)) == "minecraft:white_concrete", "elsewhere it hardens");
    // remove the water first so nothing flows when the test area is cleared
    put(ctx, off(pos, 0, 1, 1), "minecraft:air");
    put(ctx, off(pos, 3, 1, 1), "minecraft:air");
});

// ---- ImprovedSponges --------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.sponges", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2650);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    // A 13x13 pool (169 water blocks) in a stone ring, one block deep.
    constexpr int R = 6;
    auto poolAt = [&](int dx, int dz) { return off(pos, 8 + dx, 0, dz); };
    for (int dx = -R - 1; dx <= R + 1; ++dx)
        for (int dz = -R - 1; dz <= R + 1; ++dz) {
            bool ring = std::abs(dx) == R + 1 || std::abs(dz) == R + 1;
            put(ctx, off(poolAt(dx, dz), 0, -1, 0), "minecraft:stone");
            put(ctx, poolAt(dx, dz), ring ? "minecraft:stone" : "minecraft:water");
        }
    co_await ctx.ticks(2);
    auto countWater = [&] {
        int n = 0;
        for (int dx = -R; dx <= R; ++dx)
            for (int dz = -R; dz <= R; ++dz) {
                auto const& b = at(ctx, poolAt(dx, dz));
                if (b.getTypeName() == "minecraft:water" && bs::getInt(b, "liquid_depth").value_or(1) == 0) ++n;
            }
        return n;
    };
    ctx.require(countWater() == 169, "pool filled");
    put(ctx, poolAt(0, 0), "minecraft:sponge"); // absorbs when placed
    int left = countWater();
    co_await ctx.ticks(2);
    ctx.info("water left after the sponge: " + std::to_string(left));
    // Java BFS from the centre with depth 10: every block with |dx|+|dz| <= 10 (all but 12 corners).
    ctx.check(168 - left > 65, "a sponge drains more than vanilla's 65 blocks");
    ctx.check(left == 12, "it reaches exactly 10 blocks away");
    ctx.check(idAt(ctx, poolAt(0, 0)) == "minecraft:wet_sponge", "the sponge got wet");

    // Placing on water: refill the centre column and look at it.
    for (int dx = -R; dx <= R; ++dx)
        for (int dz = -R; dz <= R; ++dz) put(ctx, poolAt(dx, dz), "minecraft:water");
    co_await ctx.ticks(2);
    auto& p = ctx.player();
    p.teleport(center(poolAt(-2, 0), 2.0f), DimensionType(pos.dim));
    simOf(ctx).simulateLookAt(lp::game::blockPos(poolAt(0, 0)), ::sim::LookDuration::Instant);
    co_await ctx.ticks(2);
    auto target = waterSourceInView(p);
    ctx.require(target.has_value(), "the player looks at a water source");
    ctx.check(*target == lp::game::blockPos(poolAt(0, 0)) || *target == lp::game::blockPos(poolAt(-1, 0)),
              "the first water source along the view");
    ctx.check(placeSpongeOnWater(p, region, *target), "a sponge is placed into the water");
    co_await ctx.ticks(2);
    auto placed = idAt(ctx, lp::core::GlobalPos{pos.dim, target->x, target->y, target->z});
    ctx.check(placed == "minecraft:wet_sponge" || placed == "minecraft:sponge", "the sponge sits in the pool");
    // drain the rest before cleanup
    for (int dx = -R; dx <= R; ++dx)
        for (int dz = -R; dz <= R; ++dz) put(ctx, poolAt(dx, dz), "minecraft:air");
});

// ---- DoubleDoorOpening ------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("mobtweaks.double_doors", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2660);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    auto& p      = ctx.player();
    auto  stick  = [] { return makeItem("minecraft:stick"); };
    auto  useOn  = [&](GlobalPos const& at) {
        auto s = stick();
        return simOf(ctx).simulateUseItemOnBlock(s, lp::game::blockPos(at), Facing::North, Vec3{0.5f, 0.5f, 0.0f});
    };
    auto stepTo = [&](GlobalPos const& at) {
        p.teleport(center(off(at, 0, 0, 3)), DimensionType(pos.dim));
        simOf(ctx).simulateLookAt(lp::game::blockPos(at), ::sim::LookDuration::Instant);
    };

    // Lepton doors (generated door of the foundation probe set): A hinge left at x, B hinge right at x+1.
    constexpr char const* kDoor = "lepton:u0_probe_door";
    auto a = off(pos, 0, 0, 0), b = off(pos, 1, 0, 0);
    for (auto [at, right] : {std::pair{a, false}, std::pair{b, true}}) {
        put(ctx, at, kDoor, {{"minecraft:cardinal_direction", "north"}, {"lepton:hinge_right", right}});
        put(ctx, off(at, 0, 1, 0), kDoor,
            {{"minecraft:cardinal_direction", "north"}, {"lepton:hinge_right", right}, {"lepton:upper", true}});
    }
    co_await ctx.ticks(2);
    stepTo(a);
    co_await ctx.ticks(2);
    useOn(a);
    co_await ctx.ticks(6);
    auto open = [&](GlobalPos const& at) {
        auto s = doorState(region, lp::game::blockPos(at));
        return s && s->open;
    };
    ctx.check(open(a), "the used Lepton door opened");
    ctx.check(open(b), "its partner opened too");
    co_await ctx.ticks(10);
    useOn(off(b, 0, 1, 0)); // the upper half of the other door
    co_await ctx.ticks(6);
    ctx.check(!open(a) && !open(b), "both close together");

    // Vanilla oak doors placed by the player (Bedrock picks the hinges).
    auto va = off(pos, 0, 0, 6), vb = off(pos, 1, 0, 6);
    ctx.cleanupBlock(off(va, 0, 1, 0));
    ctx.cleanupBlock(off(vb, 0, 1, 0));
    ctx.cleanupBlock(va);
    ctx.cleanupBlock(vb);
    p.teleport(center(off(va, 0, 0, 3)), DimensionType(pos.dim));
    simOf(ctx).simulateLookAt(lp::game::blockPos(va), ::sim::LookDuration::Instant);
    co_await ctx.ticks(2);
    co_await ctx.ticks(12);
    for (auto const& at : {va, vb}) {
        auto door = makeItem("minecraft:wooden_door");
        simOf(ctx).simulateUseItemOnBlock(door, lp::game::blockPos(off(at, 0, -1, 0)), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
        co_await ctx.ticks(12);
    }
    auto sa = doorState(region, lp::game::blockPos(va));
    auto sb = doorState(region, lp::game::blockPos(vb));
    ctx.require(sa && sb, "two vanilla doors placed (" + idAt(ctx, va) + ", " + idAt(ctx, vb) + ")");
    ctx.info("vanilla doors: a dir " + std::to_string(sa->facing) + " hinge " + std::to_string(sa->hingeRight) + ", b dir "
             + std::to_string(sb->facing) + " hinge " + std::to_string(sb->hingeRight));
    auto pa = doorPartner(region, *sa);
    ctx.check(pa && *pa == lp::game::blockPos(vb), "the partner of door A is door B");
    co_await ctx.ticks(12);
    useOn(va);
    co_await ctx.ticks(6);
    ctx.check(open(va), "the used vanilla door opened");
    ctx.check(open(vb), "its vanilla partner opened too");

    // Fence gates: the one above follows (none below).
    auto ga = off(pos, 0, 0, 10), gb = off(pos, 0, 1, 10);
    put(ctx, ga, "minecraft:fence_gate");
    put(ctx, gb, "minecraft:fence_gate");
    co_await ctx.ticks(2);
    stepTo(ga);
    co_await ctx.ticks(12);
    useOn(ga);
    co_await ctx.ticks(6);
    ctx.check(open(ga), "the used gate opened");
    ctx.check(open(gb), "the gate above followed");
});
