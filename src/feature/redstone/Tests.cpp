// U10 redstone selftests (dx 1000..1099). Structures are built in the air above the test position.

#include <algorithm>
#include <set>
#include <string>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/phys/AABB.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/DispenserBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "ll/api/service/Bedrock.h"

#include "feature/redstone/Redstone.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
using GP       = lp::core::GlobalPos;
namespace bs   = lp::game::blockstates;
namespace rs   = lp::feature::redstone;
using RConfig  = rs::RedstoneConfig;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

GP off(GP p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}
BlockPos bp(GP const& p) { return lp::game::blockPos(p); }

bool put(Context& ctx, GP const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    ctx.cleanupBlock(p);
    if (!b) {
        ctx.check(false, "block " + std::string(id) + " exists with the given states");
        return false;
    }
    return lp::game::setBlock(regionOf(ctx), bp(p), *b);
}
std::string idAt(Context& ctx, GP const& p) { return lp::game::blockIdAt(regionOf(ctx), bp(p)); }

/// The test area: `dx` east of spawn, 6 blocks up (free air on the flat test world).
ll::coro::CoroTask<GP> area(Context& ctx, int dx) {
    GP p = ctx.testPos(dx);
    p.y += 6;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // keep the simulated player out of the structures
    ctx.player().teleport(Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) - 5.0f, static_cast<float>(p.z) + 6.5f},
                          DimensionType(p.dim));
    co_return p;
}

/// Clears a box (cleanup also restores air afterwards).
void clearBox(Context& ctx, GP const& a, int sx, int sy, int sz) {
    for (int x = 0; x < sx; ++x)
        for (int y = 0; y < sy; ++y)
            for (int z = 0; z < sz; ++z) put(ctx, off(a, x, y, z), "minecraft:air");
}

struct ConfigGuard {
    RConfig saved = lp::config::get<RConfig>();
    ~ConfigGuard() { lp::config::mut<RConfig>() = saved; }
};

// ---- data ------------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("redstone.blocks", [](Context& ctx) {
    for (auto id : {"lepton:iron_rod", "lepton:gravisand", "lepton:iron_button", "lepton:gold_button",
                    "lepton:obsidian_pressure_plate", "lepton:redstone_randomizer", "lepton:ender_watcher"}) {
        ctx.check(bs::of(id) != nullptr, std::string(id) + " is registered");
    }
    auto const* rod = bs::of("lepton:iron_rod", {{"minecraft:block_face", "east"}});
    ctx.require(rod != nullptr, "iron rod facing east");
    for (auto tag : {"lepton:collateral_mover", "lepton:collateral_breaker", "lepton:breaks_toretoise_ore"})
        ctx.check(rod->hasTag(HashedString(tag)), std::string("iron rod has ") + tag);
    ctx.check(bs::of("lepton:redstone_randomizer", {{"lepton:powered", "left"}, {"minecraft:cardinal_direction", "west"}}) != nullptr,
              "randomizer states");
    ctx.check(bs::of("lepton:ender_watcher", {{"lepton:watched", true}, {"lepton:power", 15}}) != nullptr, "watcher states");
    auto const* plate = bs::of("lepton:obsidian_pressure_plate");
    ctx.check(plate && plate->hasTag(HashedString("lepton:players_only")), "obsidian plate is players-only");
    ctx.check(rs::testing::movementTypeOf("lepton:redstone_randomizer") == 2, "randomizer pops when pushed");
    ctx.check(rs::testing::movementTypeOf("lepton:iron_button") == 2, "iron button pops when pushed");
    // block entity movability on this Bedrock version (documents the PistonsMoveTileEntities rules)
    std::string immovable;
    for (auto id : {"minecraft:beacon", "minecraft:lodestone", "minecraft:conduit", "minecraft:lectern", "minecraft:jukebox",
                    "minecraft:enchanting_table", "minecraft:ender_chest", "minecraft:mob_spawner", "minecraft:trial_spawner",
                    "minecraft:vault", "minecraft:bell", "minecraft:chest", "minecraft:trapped_chest", "minecraft:barrel",
                    "minecraft:furnace", "minecraft:hopper", "minecraft:dispenser", "minecraft:brewing_stand",
                    "minecraft:chiseled_bookshelf", "minecraft:decorated_pot", "minecraft:crafter", "minecraft:sculk_sensor",
                    "minecraft:sculk_catalyst", "minecraft:sculk_shrieker", "minecraft:campfire", "minecraft:cauldron",
                    "minecraft:noteblock", "minecraft:daylight_detector", "minecraft:command_block", "minecraft:structure_block",
                    "minecraft:end_gateway", "minecraft:respawn_anchor", "minecraft:creaking_heart", "minecraft:shelf"}) {
        bool be = false;
        int  mt = rs::testing::movementTypeOf(id, &be);
        if (mt == 3) immovable += std::string(" ") + id + (be ? "(be)" : "");
    }
    ctx.info("immovable:" + immovable);
});

LP_REGISTER_SELFTEST("redstone.recipes", [](Context& ctx) {
    auto level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    std::set<std::string> ids;
    for (auto& [tag, byId] : level->getRecipes().mRecipes.get())
        for (auto& [id, r] : byId) ids.insert(id);
    auto const& c = rs::cfg();
    ctx.check(ids.contains("lepton:iron_rod") == (c.enableIronRod && !c.usePreEndRecipe), "iron rod recipe follows the config");
    ctx.check(ids.contains("lepton:iron_rod_pre_end") == (c.enableIronRod && c.usePreEndRecipe), "pre-End iron rod recipe");
    ctx.check(ids.contains("lepton:gravisand") == c.enableGravisand, "gravisand recipe");
    ctx.check(ids.contains("lepton:iron_button_from_wooden") == (c.enableMetalButtons && c.enableIron), "iron button recipe");
    ctx.check(ids.contains("lepton:gold_button_from_spruce") == (c.enableMetalButtons && c.enableGold), "gold button recipe");
    ctx.check(ids.contains("lepton:obsidian_pressure_plate") == c.enableObsidianPlate, "obsidian plate recipe");
    ctx.check(ids.contains("lepton:redstone_randomizer") == c.enableRedstoneRandomizer, "randomizer recipe");
    ctx.check(ids.contains("lepton:ender_watcher") == c.enableEnderWatcher, "ender watcher recipe");
});

// ---- pistons ---------------------------------------------------------------------------------------

/// Places a piston facing up at `p` and powers it with a redstone block at `p + west`.
ll::coro::CoroTask<> firePiston(Context& ctx, GP const& p, bool sticky = false) {
    put(ctx, p, sticky ? "minecraft:sticky_piston" : "minecraft:piston", {{"facing_direction", 1}});
    co_await ctx.ticks(2);
    put(ctx, off(p, -1, 0, 0), "minecraft:redstone_block");
    co_await ctx.ticks(8);
}

LP_REGISTER_SELFTEST("redstone.iron_rod_breaks", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1000);
    clearBox(ctx, off(p, -1, 0, 0), 2, 5, 1);
    put(ctx, off(p, 0, 1, 0), "lepton:iron_rod", {{"minecraft:block_face", "up"}});
    put(ctx, off(p, 0, 2, 0), "minecraft:cobblestone");
    co_await firePiston(ctx, p);
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "lepton:iron_rod", "the rod moved up (got " + idAt(ctx, off(p, 0, 2, 0)) + ")");
    ctx.check(idAt(ctx, off(p, 0, 3, 0)) == "minecraft:air", "the cobblestone in front was broken, not pushed");
    ctx.check(rs::testing::lastPiston().usedLepton, "the Lepton resolver handled the rod");
});

LP_REGISTER_SELFTEST("redstone.iron_rod_prevent", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1004);
    clearBox(ctx, off(p, -1, 0, 0), 2, 5, 1);
    put(ctx, off(p, 0, 1, 0), "lepton:iron_rod", {{"minecraft:block_face", "up"}});
    put(ctx, off(p, 0, 2, 0), "minecraft:obsidian");
    co_await firePiston(ctx, p);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "lepton:iron_rod", "the rod stays (obsidian can't be broken)");
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "minecraft:obsidian", "the obsidian stays");
    // a rod facing sideways is pushed like a normal block (pushes the block in front)
    put(ctx, p, "minecraft:air");
    put(ctx, off(p, -1, 0, 0), "minecraft:air");
    put(ctx, off(p, 0, 2, 0), "minecraft:air");
    put(ctx, off(p, 0, 1, 0), "lepton:iron_rod", {{"minecraft:block_face", "east"}});
    put(ctx, off(p, 0, 2, 0), "minecraft:cobblestone");
    co_await ctx.ticks(2);
    co_await firePiston(ctx, p);
    ctx.check(idAt(ctx, off(p, 0, 3, 0)) == "minecraft:cobblestone", "a sideways rod pushes the block in front");
});

LP_REGISTER_SELFTEST("redstone.chain_drags", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1008);
    clearBox(ctx, off(p, -1, 0, 0), 5, 4, 1);
    put(ctx, off(p, 0, 1, 0), "minecraft:cobblestone");
    std::string chain = bs::of("minecraft:iron_chain") ? "minecraft:iron_chain" : "minecraft:chain";
    put(ctx, off(p, 1, 1, 0), chain, {{"pillar_axis", "x"}});
    put(ctx, off(p, 2, 1, 0), "minecraft:oak_planks");
    co_await firePiston(ctx, p);
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "minecraft:cobblestone", "the pushed block moved");
    ctx.check(idAt(ctx, off(p, 1, 2, 0)) == chain, "the chain attached along x moved with it");
    ctx.check(idAt(ctx, off(p, 2, 2, 0)) == "minecraft:oak_planks", "the block on the chain's other end moved too");
    ctx.check(idAt(ctx, off(p, 2, 1, 0)) == "minecraft:air", "nothing left behind");
});

LP_REGISTER_SELFTEST("redstone.chain_axis", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1012);
    clearBox(ctx, off(p, -1, 0, 0), 4, 4, 1);
    put(ctx, off(p, 0, 1, 0), "minecraft:cobblestone");
    std::string chain = bs::of("minecraft:iron_chain") ? "minecraft:iron_chain" : "minecraft:chain";
    put(ctx, off(p, 1, 1, 0), chain, {{"pillar_axis", "z"}});
    co_await firePiston(ctx, p);
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "minecraft:cobblestone", "the pushed block moved");
    ctx.check(idAt(ctx, off(p, 1, 1, 0)) == chain, "a chain across the move stays");
});

LP_REGISTER_SELFTEST("redstone.push_limit", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    GP          p = co_await area(ctx, 1016);
    clearBox(ctx, off(p, -1, 0, 0), 2, 16, 1);
    for (int i = 1; i <= 13; ++i) put(ctx, off(p, 0, i, 0), "minecraft:cobblestone");
    co_await firePiston(ctx, p);
    ctx.check(idAt(ctx, off(p, 0, 14, 0)) == "minecraft:air", "13 blocks are too many for the default limit of 12");
    put(ctx, off(p, -1, 0, 0), "minecraft:air");
    co_await ctx.ticks(4);
    lp::config::mut<RConfig>().pistonPushLimit = 13;
    put(ctx, off(p, -1, 0, 0), "minecraft:redstone_block");
    co_await ctx.ticks(8);
    ctx.check(idAt(ctx, off(p, 0, 14, 0)) == "minecraft:cobblestone", "a push limit of 13 moves 13 blocks");
});

LP_REGISTER_SELFTEST("redstone.te_blacklist", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    GP          p = co_await area(ctx, 1020);
    clearBox(ctx, off(p, -1, 0, 0), 2, 4, 1);
    put(ctx, off(p, 0, 1, 0), "minecraft:trapped_chest");
    co_await firePiston(ctx, p);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:trapped_chest", "trapped chests are in Quark's movement blacklist");
    put(ctx, off(p, -1, 0, 0), "minecraft:air");
    co_await ctx.ticks(4);
    put(ctx, off(p, 0, 1, 0), "minecraft:barrel");
    co_await ctx.ticks(2);
    put(ctx, off(p, -1, 0, 0), "minecraft:redstone_block");
    co_await ctx.ticks(8);
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "minecraft:barrel", "other block entities move");
});

LP_REGISTER_SELFTEST("redstone.chests_together", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1024);
    clearBox(ctx, off(p, -1, 0, 0), 4, 4, 1);
    put(ctx, off(p, 0, 1, 0), "minecraft:chest", {{"minecraft:cardinal_direction", "south"}});
    put(ctx, off(p, 1, 1, 0), "minecraft:chest", {{"minecraft:cardinal_direction", "south"}});
    co_await ctx.ticks(4);
    auto partner = rs::testing::chestPartnerOf(regionOf(ctx), bp(off(p, 0, 1, 0)));
    if (!partner) {
        ctx.info("the chests did not pair by setBlock; skipping the together check");
        co_return;
    }
    co_await firePiston(ctx, p);
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "minecraft:chest", "the pushed chest moved");
    ctx.check(idAt(ctx, off(p, 1, 2, 0)) == "minecraft:chest", "its other half moved with it");
});

// ---- gravisand -------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("redstone.gravisand_falls", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1030);
    clearBox(ctx, off(p, 0, 0, 0), 2, 6, 1);
    put(ctx, p, "minecraft:stone");
    put(ctx, off(p, 0, 4, 0), "lepton:gravisand");
    co_await ctx.ticks(6);
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "lepton:gravisand", "unpowered gravisand doesn't fall");
    put(ctx, off(p, 1, 4, 0), "minecraft:redstone_block");
    co_await ctx.ticks(40);
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "minecraft:air", "powered gravisand fell");
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "lepton:gravisand", "it landed on the stone (got " + idAt(ctx, off(p, 0, 1, 0)) + ")");
});

LP_REGISTER_SELFTEST("redstone.gravisand_floats", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1034);
    clearBox(ctx, off(p, 0, 0, 0), 2, 7, 1);
    put(ctx, p, "minecraft:stone");
    put(ctx, off(p, 0, 1, 0), "lepton:gravisand");
    put(ctx, off(p, 0, 5, 0), "minecraft:stone");
    co_await ctx.ticks(2);
    put(ctx, off(p, 1, 1, 0), "minecraft:redstone_block");
    co_await ctx.ticks(40);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:air", "blocked gravisand left");
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "lepton:gravisand", "it fell up to the ceiling (got " + idAt(ctx, off(p, 0, 4, 0)) + ")");
});

LP_REGISTER_SELFTEST("redstone.gravisand_chain", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1038);
    clearBox(ctx, off(p, -1, 0, 0), 4, 6, 1);
    put(ctx, p, "minecraft:stone");
    put(ctx, off(p, 1, 0, 0), "minecraft:stone");
    put(ctx, off(p, 0, 4, 0), "lepton:gravisand");
    put(ctx, off(p, 1, 4, 0), "lepton:gravisand");
    co_await ctx.ticks(2);
    put(ctx, off(p, -1, 4, 0), "minecraft:redstone_block");
    co_await ctx.ticks(45);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "lepton:gravisand", "the powered gravisand fell");
    ctx.check(idAt(ctx, off(p, 1, 1, 0)) == "lepton:gravisand", "the unpowered gravisand next to it fell along");
});

LP_REGISTER_SELFTEST("redstone.gravisand_comparator", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1042);
    clearBox(ctx, off(p, 0, 0, 0), 3, 2, 1);
    put(ctx, off(p, 0, -1, 0), "minecraft:stone");
    put(ctx, off(p, 1, -1, 0), "minecraft:stone");
    // try every comparator orientation: the one reading the gravisand must give 15
    int best = 0;
    for (std::string dir : {"north", "south", "east", "west"}) {
        put(ctx, off(p, 1, 0, 0), "minecraft:air");
        put(ctx, p, "lepton:gravisand");
        put(ctx, off(p, 1, 0, 0), "minecraft:unpowered_comparator", {{"minecraft:cardinal_direction", dir}});
        co_await ctx.ticks(6);
        best = std::max(best, rs::componentStrength(regionOf(ctx), bp(off(p, 1, 0, 0))).value_or(0));
    }
    ctx.check(best == 15, "a comparator reads 15 from gravisand (got " + std::to_string(best) + ")");
    put(ctx, p, "minecraft:air");
});

// ---- buttons, plates ---------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("redstone.metal_buttons", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1046);
    put(ctx, p, "minecraft:stone");
    put(ctx, off(p, 0, 1, 0), "lepton:iron_button", {{"minecraft:block_face", "up"}});
    put(ctx, off(p, 2, 0, 0), "minecraft:stone");
    put(ctx, off(p, 2, 1, 0), "lepton:gold_button", {{"minecraft:block_face", "up"}});
    auto& r = regionOf(ctx);
    ctx.require(lp::game::shapes::press(r, bp(off(p, 0, 1, 0))), "the iron button presses");
    ctx.require(lp::game::shapes::press(r, bp(off(p, 2, 1, 0))), "the gold button presses");
    co_await ctx.ticks(8);
    ctx.check(!bs::getBool(r.getBlock(bp(off(p, 2, 1, 0))), "lepton:pressed").value_or(true), "the gold button released after 4 ticks");
    ctx.check(bs::getBool(r.getBlock(bp(off(p, 0, 1, 0))), "lepton:pressed").value_or(false), "the iron button is still pressed");
    co_await ctx.ticks(90);
    ctx.check(bs::getBool(regionOf(ctx).getBlock(bp(off(p, 0, 1, 0))), "lepton:pressed").value_or(false), "still pressed at 98 ticks");
    co_await ctx.ticks(8);
    ctx.check(!bs::getBool(regionOf(ctx).getBlock(bp(off(p, 0, 1, 0))), "lepton:pressed").value_or(true), "released after 100 ticks");
});

LP_REGISTER_SELFTEST("redstone.obsidian_plate", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = ctx.testPos(1050);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // two plates on the ground: a pig on one, the player on the other
    put(ctx, p, "lepton:obsidian_pressure_plate");
    put(ctx, off(p, 4, 0, 0), "lepton:obsidian_pressure_plate");
    auto* pig = lp::game::entities::spawn(regionOf(ctx), "minecraft:pig", Vec3{p.x + 4.5f, static_cast<float>(p.y), p.z + 0.5f});
    ctx.player().teleport(Vec3{p.x + 0.5f, static_cast<float>(p.y), p.z + 0.5f}, DimensionType(p.dim));
    co_await ctx.ticks(6);
    ctx.check(bs::getBool(regionOf(ctx).getBlock(bp(p)), "lepton:pressed").value_or(false), "a player presses the obsidian plate");
    ctx.check(!bs::getBool(regionOf(ctx).getBlock(bp(off(p, 4, 0, 0))), "lepton:pressed").value_or(true), "a pig doesn't");
    if (pig) lp::game::entities::despawn(*pig);
    ctx.player().teleport(Vec3{p.x + 0.5f, static_cast<float>(p.y), p.z + 8.5f}, DimensionType(p.dim));
    co_await ctx.ticks(30);
    ctx.check(!bs::getBool(regionOf(ctx).getBlock(bp(p)), "lepton:pressed").value_or(true), "released after the player left");
});

// ---- randomizer, ender watcher ---------------------------------------------------------------------------

LP_REGISTER_SELFTEST("redstone.randomizer", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1056);
    clearBox(ctx, off(p, -1, 0, -1), 3, 2, 3);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) put(ctx, off(p, dx, -1, dz), "minecraft:stone");
    // look direction north -> Java FACING south: input from the south, left output west, right east
    put(ctx, p, "lepton:redstone_randomizer", {{"minecraft:cardinal_direction", "north"}});
    put(ctx, off(p, -1, 0, 0), "minecraft:redstone_lamp");
    put(ctx, off(p, 1, 0, 0), "minecraft:redstone_lamp");
    rs::testing::forceRandomizerSide(1);
    co_await ctx.ticks(2);
    put(ctx, off(p, 0, 0, 1), "minecraft:redstone_block");
    co_await ctx.ticks(10);
    ctx.check(bs::getString(regionOf(ctx).getBlock(bp(p)), "lepton:powered").value_or("") == "left", "powered from the back: left");
    ctx.check(idAt(ctx, off(p, -1, 0, 0)) == "minecraft:lit_redstone_lamp", "the left (west) lamp is lit");
    ctx.check(idAt(ctx, off(p, 1, 0, 0)) == "minecraft:redstone_lamp", "the right (east) lamp is off");
    put(ctx, off(p, 0, 0, 1), "minecraft:air");
    co_await ctx.ticks(10);
    ctx.check(bs::getString(regionOf(ctx).getBlock(bp(p)), "lepton:powered").value_or("") == "off", "unpowered: off");
    rs::testing::forceRandomizerSide(0);
    put(ctx, off(p, 0, 0, 1), "minecraft:redstone_block");
    co_await ctx.ticks(10);
    ctx.check(bs::getString(regionOf(ctx).getBlock(bp(p)), "lepton:powered").value_or("") == "right", "next time: right");
    ctx.check(idAt(ctx, off(p, 1, 0, 0)) == "minecraft:lit_redstone_lamp", "the right (east) lamp is lit");
    rs::testing::forceRandomizerSide(-1);
    // side power doesn't count
    put(ctx, off(p, 0, 0, 1), "minecraft:air");
    put(ctx, off(p, -1, 0, 0), "minecraft:redstone_block");
    co_await ctx.ticks(10);
    ctx.check(bs::getString(regionOf(ctx).getBlock(bp(p)), "lepton:powered").value_or("") == "off", "power from the side is ignored");
    // no support: breaks
    put(ctx, off(p, 0, -1, 0), "minecraft:air");
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, p) == "minecraft:air", "a randomizer without support breaks");
});

LP_REGISTER_SELFTEST("redstone.ender_watcher", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1062);
    put(ctx, p, "lepton:ender_watcher");
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    sp->teleport(Vec3{p.x + 0.5f, static_cast<float>(p.y) - 1.62f + 0.5f, p.z + 5.5f}, DimensionType(p.dim));
    co_await ctx.ticks(2);
    sp->simulateLookAt(Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 1.0f}, sim::LookDuration::Instant);
    co_await ctx.ticks(6);
    auto const& b = regionOf(ctx).getBlock(bp(p));
    ctx.check(bs::getBool(b, "lepton:watched").value_or(false), "watched while a player looks at it");
    int power = bs::getInt(b, "lepton:power").value_or(0);
    ctx.check(power >= 13, "looking at the centre gives a strong signal (got " + std::to_string(power) + ")");
    sp->simulateLookAt(Vec3{p.x + 0.95f, p.y + 0.95f, p.z + 1.0f}, sim::LookDuration::Instant);
    co_await ctx.ticks(6);
    int corner = bs::getInt(regionOf(ctx).getBlock(bp(p)), "lepton:power").value_or(0);
    ctx.check(corner < power, "looking near the corner is weaker (" + std::to_string(corner) + ")");
    sp->simulateLookAt(Vec3{p.x + 0.5f, p.y + 10.5f, p.z + 5.5f}, sim::LookDuration::Instant);
    co_await ctx.ticks(6);
    auto const& b2 = regionOf(ctx).getBlock(bp(p));
    ctx.check(!bs::getBool(b2, "lepton:watched").value_or(true) && bs::getInt(b2, "lepton:power").value_or(1) == 0, "idle when nobody looks");
});

// ---- dispensers --------------------------------------------------------------------------------------------

Container* dispenserContainer(Context& ctx, GP const& p) {
    auto* be = regionOf(ctx).getBlockEntity(bp(p));
    if (!be) return nullptr;
    return static_cast<DispenserBlockActor*>(be)->getContainer();
}

ll::coro::CoroTask<bool> dispense(Context& ctx, GP const& d, int facing, ItemStack const& item) {
    put(ctx, d, "minecraft:dispenser", {{"facing_direction", facing}});
    co_await ctx.ticks(2);
    auto* c = dispenserContainer(ctx, d);
    if (!c) co_return false;
    c->setItem(0, item);
    put(ctx, off(d, 0, 0, -1), "minecraft:redstone_block");
    co_await ctx.ticks(8);
    co_return true;
}

LP_REGISTER_SELFTEST("redstone.dispenser_places", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1066);
    clearBox(ctx, off(p, 0, 0, -1), 1, 3, 2);
    int before = rs::testing::dispenserPlacements();
    ctx.require(co_await dispense(ctx, p, 1, lp::game::makeItem("minecraft:cobblestone", 3)), "dispenser container");
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:cobblestone", "the dispenser placed cobblestone above it");
    auto* c = dispenserContainer(ctx, p);
    ctx.check(c && c->getItem(0).mCount == 2, "one cobblestone was used");
    ctx.check(rs::testing::dispenserPlacements() == before + 1, "one placement");
    // the spot is taken now: the next one stays inside (Quark: no throwing)
    put(ctx, off(p, 0, 0, -1), "minecraft:air");
    co_await ctx.ticks(4);
    put(ctx, off(p, 0, 0, -1), "minecraft:redstone_block");
    co_await ctx.ticks(8);
    ctx.check(c && c->getItem(0).mCount == 2, "a block that can't be placed stays in the dispenser");
});

LP_REGISTER_SELFTEST("redstone.dispenser_seeds", [](Context& ctx) -> ll::coro::CoroTask<> {
    GP p = co_await area(ctx, 1070);
    clearBox(ctx, off(p, 0, 0, -1), 2, 2, 2);
    put(ctx, off(p, 1, -1, 0), "minecraft:farmland");
    // facing_direction 5 = east
    ctx.require(co_await dispense(ctx, p, 5, lp::game::makeItem("minecraft:wheat_seeds", 4)), "dispenser container");
    ctx.check(idAt(ctx, off(p, 1, 0, 0)) == "minecraft:wheat", "seeds were planted on the farmland (got " + idAt(ctx, off(p, 1, 0, 0)) + ")");
});

LP_REGISTER_SELFTEST("redstone.dispenser_blacklist", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    lp::config::mut<RConfig>().blacklist.push_back("minecraft:cobblestone");
    GP p = co_await area(ctx, 1074);
    clearBox(ctx, off(p, 0, 0, -1), 1, 3, 2);
    ctx.require(co_await dispense(ctx, p, 1, lp::game::makeItem("minecraft:cobblestone", 3)), "dispenser container");
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:air", "blacklisted blocks are not placed");
    auto* c = dispenserContainer(ctx, p);
    ctx.check(c && c->getItem(0).mCount == 2, "the blacklisted block was dispensed as an item instead");
    for (auto* a : lp::game::entities::inBox(regionOf(ctx), AABB{Vec3{p.x - 2.f, p.y - 2.f, p.z - 2.f}, Vec3{p.x + 3.f, p.y + 6.f, p.z + 3.f}},
                                             "minecraft:item"))
        lp::game::entities::despawn(*a);
});

} // namespace
