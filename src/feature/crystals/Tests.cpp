// U12 crystals selftests (dx 1200..1299; the unit's row is z = 0..4).

#include <cmath>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaMainBlockActorComponent.h"
#include "mc/world/phys/AABB.h"

#include "feature/crystals/Beacon.h"
#include "feature/crystals/Common.h"
#include "feature/crystals/Magnets.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace lp::feature::crystals {
// CorundumGen.cpp selftest seams
bool                caveShapeContains(BlockPos const& src, BlockPos const& p);
std::pair<int, int> caveColors(BlockPos const& src);
bool                caveBiomeAllowed(std::string const& biome);
std::vector<BlockPos> caveSourcesNear(int cx, int cz, int radius);
std::string           caveGenStats();
} // namespace lp::feature::crystals

namespace {

using lp::selftest::Context;
using namespace lp::feature::crystals;
namespace bs = lp::game::blockstates;
using lp::core::GlobalPos;

GlobalPos at(GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }
BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }
Block const& blockAt(Context& ctx, GlobalPos const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }
std::string idAt(Context& ctx, GlobalPos const& p) { return blockAt(ctx, p).getTypeName(); }

/// Places a block with typed states (restored to air after the test).
bool place(Context& ctx, GlobalPos const& p, std::string const& id, bs::Changes const& states = {}) {
    auto const* b = bs::of(id, states);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}
/// Remembers a strip of positions to clear after the test.
void cleanupLine(Context& ctx, GlobalPos from, int dx, int dy, int dz, int n) {
    for (int i = 0; i < n; ++i) ctx.cleanupBlock(at(from, dx * i, dy * i, dz * i));
}

/// The trait value that makes a magnet push towards `face` (its looking direction is the opposite).
std::string magnetLook(int pushFace) { return std::string(faceToName(oppositeFace(pushFace))); }

} // namespace

LP_REGISTER_SELFTEST("crystals.corundum_blocks", [](Context& ctx) {
    for (int c = 0; c < static_cast<int>(kColors.size()); ++c) {
        auto const* co = bs::of(corundumId(c));
        auto const* wx = bs::of(waxedCorundumId(c));
        auto const* pn = bs::of(paneId(c));
        auto const* cl = bs::of(clusterId(c), {{"minecraft:block_face", std::string("north")}});
        auto const* lm = bs::of(lampId(c), {{"lepton:lit", true}});
        ctx.require(co && wx && pn && cl && lm, "all blocks of " + std::string(kColors[c].name) + " exist");
        ctx.check(isCorundum(*co) && !isCorundum(*wx) && isWaxedCorundum(*wx), "corundum / waxed tags");
        ctx.check(isPane(*pn) && isCluster(*cl) && isLamp(*lm), "pane / cluster / lamp tags");
        ctx.check(colorOf(*co) == c && colorOf(*wx) == c && colorOf(*pn) == c && colorOf(*cl) == c && colorOf(*lm) == c,
                  "colour tags");
        ctx.check(clusterFacing(*cl) == 2, "cluster facing from minecraft:block_face");
        ctx.check(beaconColorOf(*co) == kColors[c].beacon && beaconColorOf(*pn) == kColors[c].beacon, "beacon colour");
        ctx.check(beaconColorOf(*lm) == kColors[c].beacon, "a lit lamp tints beams");
        auto const* off = bs::of(lampId(c), {{"lepton:lit", false}});
        ctx.check(off && !beaconColorOf(*off), "an unlit lamp does not tint beams");
        ctx.check(!beaconColorOf(*cl), "clusters redirect, they do not tint");
    }
    ctx.check(bs::of("lepton:magnet") && bs::of("lepton:magnetized_block"), "magnet blocks exist");
    ctx.check(corundumIds().size() == 9, "nine corundum ids");
});

LP_REGISTER_SELFTEST("crystals.waxing", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1200, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(place(ctx, pos, corundumId(0)), "red corundum placed");
    auto& p = static_cast<SimulatedPlayer&>(ctx.player());
    co_await ctx.ticks(10);
    auto comb = lp::game::makeItem("minecraft:honeycomb", 4);
    p.simulateSetItem(comb, true, 0);
    p.simulateUseItemOnBlock(comb, lp::game::blockPos(pos), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    for (int i = 0; i < 10 && idAt(ctx, pos) != waxedCorundumId(0); ++i) co_await ctx.ticks(1);
    ctx.check(idAt(ctx, pos) == waxedCorundumId(0), "honeycomb waxes corundum");
    if (!p.isCreative()) ctx.check(p.getSelectedItem().mCount == 3, "the honeycomb is used up");
    co_await ctx.ticks(10);
    auto axe = lp::game::makeItem("minecraft:iron_axe");
    p.simulateSetItem(axe, true, 0);
    p.simulateUseItemOnBlock(axe, lp::game::blockPos(pos), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    for (int i = 0; i < 10 && idAt(ctx, pos) != corundumId(0); ++i) co_await ctx.ticks(1);
    ctx.check(idAt(ctx, pos) == corundumId(0), "an axe scrapes the wax off");

    // magnets: waxed state
    auto mpos = at(pos, 1, 0, 0);
    ctx.require(place(ctx, mpos, "lepton:magnet"), "magnet placed");
    co_await ctx.ticks(10);
    p.simulateSetItem(comb, true, 0);
    p.simulateUseItemOnBlock(comb, lp::game::blockPos(mpos), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    for (int i = 0; i < 10 && !bs::getBool(blockAt(ctx, mpos), "lepton:waxed").value_or(false); ++i) co_await ctx.ticks(1);
    ctx.check(bs::getBool(blockAt(ctx, mpos), "lepton:waxed").value_or(false), "honeycomb waxes a magnet");
    p.simulateSetItem(ItemStack{}, true, 0);
});

LP_REGISTER_SELFTEST("crystals.growth", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1203, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    auto  bp     = lp::game::blockPos(pos);
    ctx.require(bp.y < 24, "test area is below y 24");
    ctx.require(place(ctx, pos, corundumId(3)), "green corundum placed");
    cleanupLine(ctx, at(pos, 0, 1, 0), 0, 1, 0, 5);
    for (int d = 0; d < 6; ++d) cleanupLine(ctx, lp::game::globalPos(region, offset(offset(bp, 1, 1), d)), 0, 1, 0, 4);
    ctx.check(corundumCanGrow(region, bp), "corundum below y 24 with air above can grow");
    // a random tick grows it 1 in caveCrystalGrowthChance times
    int ticks = 0;
    while (ticks < 400 && idAt(ctx, at(pos, 0, 1, 0)) != corundumId(3)) {
        corundumRandomTick(region, bp);
        ++ticks;
    }
    ctx.check(idAt(ctx, at(pos, 0, 1, 0)) == corundumId(3), "corundum grew upwards");
    int clusters = 0;
    for (int d = 0; d < 6; ++d) {
        auto const& b = region.getBlock(offset(offset(bp, 1, 1), d));
        if (isCluster(b)) {
            ++clusters;
            ctx.check(clusterFacing(b) == d && colorOf(b) == 3, "sprouted clusters face away and share the colour");
        }
    }
    ctx.info("grew after " + std::to_string(ticks) + " random ticks, " + std::to_string(clusters) + " clusters");
    // a column of four stops growing
    for (int y = 1; y < 4; ++y) place(ctx, at(pos, 0, y, 0), corundumId(3));
    lp::game::clearBlock(region, lp::game::blockPos(at(pos, 0, 4, 0)));
    ctx.check(!corundumCanGrow(region, lp::game::blockPos(at(pos, 0, 3, 0))), "four high corundum stops growing");
    ctx.check(corundumCanGrow(region, lp::game::blockPos(at(pos, 0, 2, 0))) == false, "blocked corundum does not grow");
    co_await ctx.ticks(1);
});

LP_REGISTER_SELFTEST("crystals.cluster_support", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = at(ctx.testPos(1206, 2), 0, 2, 0);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto top = at(base, 0, 1, 0);
    ctx.require(place(ctx, base, "minecraft:stone"), "support placed");
    ctx.require(place(ctx, top, clusterId(5), {{"minecraft:block_face", std::string("up")}}), "cluster placed");
    co_await ctx.ticks(3);
    ctx.check(isCluster(blockAt(ctx, top)), "a supported cluster stays");
    ctx.check(clusterCanSurvive(regionOf(ctx), lp::game::blockPos(top)), "clusterCanSurvive with support");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(base));
    for (int i = 0; i < 10 && isCluster(blockAt(ctx, top)); ++i) co_await ctx.ticks(1);
    ctx.check(!isCluster(blockAt(ctx, top)), "the cluster breaks without its support");
    // clean up the drop
    auto box = AABB{Vec3{float(top.x - 2), float(top.y - 4), float(top.z - 2)}, Vec3{float(top.x + 3), float(top.y + 3), float(top.z + 3)}};
    int  drops = 0;
    for (auto* e : lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item")) {
        ++drops;
        lp::game::entities::despawn(*e);
    }
    ctx.check(drops >= 1, "the broken cluster dropped");
});

LP_REGISTER_SELFTEST("crystals.collateral", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto piston = ctx.testPos(1209, 2);
    ctx.require(co_await ctx.ensureLoaded(piston), "test area loaded");
    auto corundum = at(piston, 0, 1, 0);
    auto cluster  = at(corundum, 0, 0, -1); // hangs on the north face, pointing north
    cleanupLine(ctx, at(piston, 0, 1, 0), 0, 1, 0, 4);
    cleanupLine(ctx, at(piston, 0, 1, -1), 0, 1, 0, 4);
    ctx.require(place(ctx, piston, "minecraft:piston", {{"facing_direction", 1}}), "piston facing up");
    ctx.require(place(ctx, corundum, corundumId(1)), "corundum");
    ctx.require(place(ctx, cluster, clusterId(1), {{"minecraft:block_face", std::string("north")}}), "cluster");
    co_await ctx.ticks(5);
    auto power = at(piston, 1, 0, 0);
    ctx.require(place(ctx, power, "minecraft:redstone_block"), "piston powered");
    for (int i = 0; i < 20 && idAt(ctx, at(corundum, 0, 1, 0)) != corundumId(1); ++i) co_await ctx.ticks(1);
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, at(corundum, 0, 1, 0)) == corundumId(1), "the piston pushed the corundum");
    ctx.check(isCluster(blockAt(ctx, at(cluster, 0, 1, 0))), "the cluster moved with its corundum");
    ctx.check(!isCluster(blockAt(ctx, cluster)), "the cluster left its old place");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(power));
    co_await ctx.ticks(4);
});

LP_REGISTER_SELFTEST("crystals.worldgen_shape", [](Context& ctx) {
    BlockPos src{1000, 30, -2000};
    ctx.check(caveShapeContains(src, src), "the source is inside its blob");
    ctx.check(!caveShapeContains(src, BlockPos{src.x + 60, src.y, src.z}), "60 blocks away is outside");
    ctx.check(!caveShapeContains(src, BlockPos{src.x, src.y + 25, src.z}), "25 blocks up is outside");
    int inside = 0;
    for (int x = -50; x <= 50; x += 5)
        for (int z = -50; z <= 50; z += 5) inside += caveShapeContains(src, BlockPos{src.x + x, src.y, src.z + z});
    ctx.check(inside > 100 && inside < 441, "the blob is a large ellipse (" + std::to_string(inside) + " samples)");
    auto [floor, ceil] = caveColors(src);
    ctx.check(floor >= 0 && floor < 9 && ceil >= 0 && ceil < 9 && floor != ceil, "two distinct colours");
    ctx.check(!caveBiomeAllowed("ocean") && !caveBiomeAllowed("minecraft:deep_ocean"), "oceans are blacklisted");
    ctx.check(caveBiomeAllowed("plains") && caveBiomeAllowed("minecraft:taiga"), "land biomes are allowed");
    JavaRandom r(0);
    ctx.check(r.nextInt() == -1155484576, "java.util.Random port matches Java");
});

LP_REGISTER_SELFTEST("crystals.lamp_power", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto lamp = at(ctx.testPos(1216, 2), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(lamp), "test area loaded");
    ctx.require(place(ctx, lamp, lampId(2)), "lamp placed");
    co_await ctx.ticks(3);
    ctx.check(!bs::getBool(blockAt(ctx, lamp), "lepton:lit").value_or(true), "an unpowered lamp is off");
    auto power = at(lamp, 0, 0, 1);
    ctx.require(place(ctx, power, "minecraft:redstone_block"), "power placed");
    for (int i = 0; i < 6 && !bs::getBool(blockAt(ctx, lamp), "lepton:lit").value_or(false); ++i) co_await ctx.ticks(1);
    ctx.check(bs::getBool(blockAt(ctx, lamp), "lepton:lit").value_or(false), "a powered lamp lights up");
    co_await ctx.ticks(4);
    ctx.check(bs::getBool(blockAt(ctx, lamp), "lepton:lit").value_or(false), "it stays lit while powered");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(power));
    co_await ctx.ticks(2);
    ctx.check(bs::getBool(blockAt(ctx, lamp), "lepton:lit").value_or(false), "it stays lit for 4 ticks");
    for (int i = 0; i < 10 && bs::getBool(blockAt(ctx, lamp), "lepton:lit").value_or(false); ++i) co_await ctx.ticks(1);
    ctx.check(!bs::getBool(blockAt(ctx, lamp), "lepton:lit").value_or(true), "then turns off");
});

LP_REGISTER_SELFTEST("crystals.beam_path", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto beacon = ctx.testPos(1220, 2);
    ctx.require(co_await ctx.ensureLoaded(beacon), "test area loaded");
    auto& region = regionOf(ctx);
    auto  bp     = lp::game::blockPos(beacon);
    // straight up: vanilla beam
    auto beam = computeBeam(region, bp);
    ctx.check(!beam.broke && !beam.redirected && !beam.quarkTint && beam.segments.size() == 1, "a clear sky is a plain beam");
    // up 2, east 4 (red cluster), up again (blue cluster pointing up)
    ctx.require(place(ctx, at(beacon, 0, 2, 0), clusterId(0), {{"minecraft:block_face", std::string("east")}}), "first cluster");
    ctx.require(place(ctx, at(beacon, 4, 2, 0), clusterId(4), {{"minecraft:block_face", std::string("up")}}), "second cluster");
    beam = computeBeam(region, bp);
    ctx.check(!beam.broke && beam.redirected, "the clusters redirect the beam");
    ctx.require(beam.segments.size() == 3, "three segments (" + std::to_string(beam.segments.size()) + ")");
    ctx.check(beam.segments[1].dir == 5 && beam.segments[1].offset == BlockPos{0, 2, 0}, "second segment goes east");
    ctx.check(beam.segments[2].dir == 1 && beam.segments[2].toSky, "the last segment reaches the sky");
    // white mixed 1:3 with red = (255, 63, 63)
    ctx.check(beam.segments[1].color == 0xff3f3f, "the red cluster tints the beam (1:3)");
    // tinted glass dims, corundum tints
    ctx.require(place(ctx, at(beacon, 4, 4, 0), "minecraft:tinted_glass"), "tinted glass");
    ctx.require(place(ctx, at(beacon, 4, 6, 0), corundumId(7)), "white corundum");
    beam = computeBeam(region, bp);
    ctx.check(!beam.broke && beam.quarkTint && beam.segments.back().alpha == 170, "tinted glass dims the beam to 2/3");
    // an opaque block breaks the beam
    ctx.require(place(ctx, at(beacon, 4, 8, 0), "minecraft:stone"), "stone");
    beam = computeBeam(region, bp);
    ctx.check(beam.broke && beam.segments.empty(), "an opaque block breaks the beam");
    // bedrock lets it through
    ctx.require(place(ctx, at(beacon, 4, 8, 0), "minecraft:bedrock"), "bedrock");
    beam = computeBeam(region, bp);
    ctx.check(!beam.broke, "bedrock is beacon transparent");
    co_await ctx.ticks(1);
});

LP_REGISTER_SELFTEST("crystals.beam_entities", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto beacon = at(ctx.testPos(1231, 2), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(beacon), "test area loaded");
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) ctx.require(place(ctx, at(beacon, x, -1, z), "minecraft:iron_block"), "pyramid");
    ctx.require(place(ctx, beacon, "minecraft:beacon"), "beacon placed");
    ctx.require(place(ctx, at(beacon, 0, 3, 0), clusterId(6), {{"minecraft:block_face", std::string("east")}}), "cluster east");
    ctx.require(place(ctx, at(beacon, 3, 3, 0), clusterId(2), {{"minecraft:block_face", std::string("up")}}), "cluster up");
    auto key = lp::game::globalPos(regionOf(ctx), lp::game::blockPos(beacon));
    for (int i = 0; i < 140 && beamEntitiesOf(key).empty(); ++i) co_await ctx.ticks(1);
    ctx.info(std::string("beacon ticked: ") + (beaconTicked(key) ? "yes" : "no") + " at " + key.key());
    auto ids = beamEntitiesOf(key);
    ctx.check(ids.size() >= 3, "the redirected beam is drawn with entities (" + std::to_string(ids.size()) + ")");
    for (auto id : ids) ctx.check(lp::game::entities::find(id) != nullptr, "beam entity is alive");
    // removing the redirect brings back the vanilla beam
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(at(beacon, 0, 3, 0)));
    for (int i = 0; i < 60 && !beamEntitiesOf(key).empty(); ++i) co_await ctx.ticks(1);
    ctx.check(beamEntitiesOf(key).empty(), "a plain beam has no entities");
});

LP_REGISTER_SELFTEST("crystals.magnetism_crawl", [](Context& ctx) {
    rebuildMagnetism();
    ctx.check(isItemMagnetic("minecraft:iron_ingot"), "iron ingots are magnetic");
    ctx.check(isItemMagnetic("minecraft:iron_block"), "iron blocks are crafted from iron");
    ctx.check(isItemMagnetic("minecraft:hopper"), "hoppers are crafted from iron");
    ctx.check(isItemMagnetic("minecraft:copper_block"), "copper blocks are magnetic");
    ctx.check(isItemMagnetic("minecraft:iron_sword"), "iron swords are magnetic");
    ctx.check(isItemMagnetic("minecraft:chainmail_helmet"), "whitelisted chainmail");
    ctx.check(isItemMagnetic("minecraft:cauldron"), "cauldrons");
    ctx.check(!isItemMagnetic("minecraft:stone"), "stone is not magnetic");
    ctx.check(!isItemMagnetic("minecraft:tripwire_hook"), "blacklisted tripwire hooks");
    ctx.check(!isItemMagnetic("minecraft:oak_hanging_sign"), "blacklisted hanging signs");
    ctx.check(!isItemMagnetic("minecraft:map"), "blacklisted maps");
    auto const* iron = bs::of("minecraft:iron_block");
    auto const* stone = bs::of("minecraft:stone");
    auto const* magnet = bs::of("lepton:magnet");
    ctx.check(iron && isBlockMagnetic(*iron), "iron block is a magnetic block");
    ctx.check(stone && !isBlockMagnetic(*stone), "stone is not a magnetic block");
    ctx.check(magnet && !isBlockMagnetic(*magnet), "magnets are not magnetic blocks");
    std::string types;
    for (auto id : {"minecraft:obsidian", "minecraft:hopper", "minecraft:iron_block", "minecraft:torch", "minecraft:bedrock", "minecraft:chest", "minecraft:beacon"})
        if (auto const* b = bs::of(id)) types += std::string(id) + "=" + std::to_string(movementTypeOf(*b)) + " ";
    ctx.info(types);
    auto const* obsidian = bs::of("minecraft:obsidian");
    auto const* torch    = bs::of("minecraft:torch");
    ctx.check(obsidian && movementTypeOf(*obsidian) == 3, "obsidian is immovable");
    ctx.check(torch && movementTypeOf(*torch) == 2, "torches pop");
});

namespace {
/// Builds a magnet at `m` pushing towards `face`, powered by a redstone block at `power`.
bool buildMagnet(Context& ctx, GlobalPos const& m, int face, GlobalPos const& power) {
    if (!place(ctx, m, "lepton:magnet", {{"minecraft:facing_direction", magnetLook(face)}})) return false;
    return place(ctx, power, "minecraft:redstone_block");
}
} // namespace

LP_REGISTER_SELFTEST("crystals.magnet_push", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto m = at(ctx.testPos(1240, 2), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(m), "test area loaded");
    cleanupLine(ctx, at(m, 1, 0, 0), 1, 0, 0, 18);
    ctx.require(place(ctx, at(m, 2, 0, 0), "minecraft:iron_block"), "iron block");
    ctx.require(place(ctx, at(m, 1, 0, 1), "minecraft:stone"), "a stone beside the line is ignored");
    ctx.require(buildMagnet(ctx, m, 5, at(m, 0, 0, -1)), "magnet pushing east");
    for (int i = 0; i < 20 && !bs::getBool(blockAt(ctx, m), "lepton:powered").value_or(false); ++i) co_await ctx.ticks(1);
    ctx.check(bs::getBool(blockAt(ctx, m), "lepton:powered").value_or(false), "the magnet is powered");
    int found = -1;
    for (int i = 0; i < 60; ++i) {
        co_await ctx.ticks(1);
        for (int x = 3; x <= 16; ++x)
            if (idAt(ctx, at(m, x, 0, 0)) == "minecraft:iron_block") found = x;
        if (found >= 6) break;
    }
    ctx.check(found >= 6, "the iron block was pushed east (now at +" + std::to_string(found) + ")");
    ctx.check(idAt(ctx, at(m, 2, 0, 0)) != "minecraft:iron_block", "it left its place");
    ctx.check(idAt(ctx, at(m, 1, 0, 1)) == "minecraft:stone", "stone is not moved");
    // the block stops where the force runs out (power 15: up to 15 blocks away)
    co_await ctx.ticks(60);
    found = -1;
    for (int x = 1; x <= 17; ++x)
        if (idAt(ctx, at(m, x, 0, 0)) == "minecraft:iron_block") found = x;
    ctx.check(found == 16, "the block stops 16 blocks away (at +" + std::to_string(found) + ")");
    ctx.check(movingBlockCount() == 0, "no block is left moving");
});

LP_REGISTER_SELFTEST("crystals.magnet_pull", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto m = at(ctx.testPos(1258, 2), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(m), "test area loaded");
    cleanupLine(ctx, at(m, -1, 0, 0), -1, 0, 0, 7);
    // pushing east; the back (west) side pulls towards the magnet
    ctx.require(place(ctx, at(m, -5, 0, 0), "minecraft:copper_block"), "copper block");
    ctx.require(buildMagnet(ctx, m, 5, at(m, 0, 0, -1)), "magnet");
    for (int i = 0; i < 60 && idAt(ctx, at(m, -1, 0, 0)) != "minecraft:copper_block"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(ctx, at(m, -1, 0, 0)) == "minecraft:copper_block", "the back side pulled the copper block to the magnet");
    ctx.check(idAt(ctx, at(m, -5, 0, 0)) != "minecraft:copper_block", "it left its place");
});

LP_REGISTER_SELFTEST("crystals.magnet_conflict", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = at(ctx.testPos(1260, 2), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    auto b = at(a, 6, 0, 0);
    cleanupLine(ctx, at(a, 1, 0, 0), 1, 0, 0, 5);
    ctx.require(place(ctx, at(a, 3, 0, 0), "minecraft:iron_block"), "iron block in the middle");
    // equal opposite forces cancel
    ctx.require(buildMagnet(ctx, a, 5, at(a, 0, 0, -1)), "magnet A pushing east");
    ctx.require(buildMagnet(ctx, b, 4, at(b, 0, 0, -1)), "magnet B pushing west");
    co_await ctx.ticks(30);
    ctx.check(idAt(ctx, at(a, 3, 0, 0)) == "minecraft:iron_block", "equal opposite forces cancel");
    // nearer to A, A's force (15 - 2 + 1 = 14) beats B's (12) until the forces balance in the middle
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(at(a, 3, 0, 0)));
    ctx.require(place(ctx, at(a, 2, 0, 0), "minecraft:iron_block"), "iron block nearer to A");
    co_await ctx.ticks(40);
    int found = -1;
    for (int x = 1; x <= 5; ++x)
        if (idAt(ctx, at(a, x, 0, 0)) == "minecraft:iron_block") found = x;
    ctx.check(found == 3, "the stronger magnet wins until the forces balance (block at +" + std::to_string(found) + ")");
});

LP_REGISTER_SELFTEST("crystals.magnet_hopper", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto m = at(ctx.testPos(1270, 2), 0, 3, 0);
    ctx.require(co_await ctx.ensureLoaded(m), "test area loaded");
    cleanupLine(ctx, at(m, 1, 0, 0), 1, 0, 0, 6);
    auto hopper = at(m, 2, 0, 0);
    ctx.require(place(ctx, hopper, "minecraft:hopper", {{"facing_direction", 0}}), "hopper facing down");
    auto* be = regionOf(ctx).getBlockEntity(lp::game::blockPos(hopper));
    ctx.require(be && be->_getMainComponent() && be->_getMainComponent()->getContainer(), "hopper container");
    be->_getMainComponent()->getContainer()->setItem(0, lp::game::makeItem("minecraft:iron_ingot", 5));
    ctx.require(buildMagnet(ctx, m, 5, at(m, 0, 0, -1)), "magnet");
    ctx.check(magnetPushAction(regionOf(ctx), lp::game::blockPos(hopper), 5) == 1, "the hopper can be moved");
    int found = -1;
    for (int i = 0; i < 40 && found < 0; ++i) {
        co_await ctx.ticks(1);
        for (int x = 3; x <= 6; ++x)
            if (idAt(ctx, at(m, x, 0, 0)) == "minecraft:hopper") found = x;
    }
    ctx.require(found > 0, "the hopper moved");
    co_await ctx.ticks(1);
    // stop the magnet, then count
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(at(m, 0, 0, -1)));
    co_await ctx.ticks(8);
    int at2 = -1;
    for (int x = 2; x <= 6; ++x)
        if (idAt(ctx, at(m, x, 0, 0)) == "minecraft:hopper") at2 = x;
    ctx.require(at2 > 0, "hopper found");
    auto* nbe = regionOf(ctx).getBlockEntity(lp::game::blockPos(at(m, at2, 0, 0)));
    ctx.require(nbe && nbe->_getMainComponent() && nbe->_getMainComponent()->getContainer(), "moved hopper keeps a container");
    int left = 0;
    auto* c  = nbe->_getMainComponent()->getContainer();
    for (int i = 0; i < c->getContainerSize(); ++i) left += c->getItem(i).mCount;
    int moves = at2 - 2;
    ctx.check(left == 5 - moves, "one item dropped per move (" + std::to_string(left) + " left after " + std::to_string(moves) + " moves)");
    AABB box{Vec3{float(m.x), float(m.y - 6), float(m.z - 2)}, Vec3{float(m.x + 8), float(m.y + 2), float(m.z + 3)}};
    int  dropped = 0;
    for (auto* e : lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item")) {
        ++dropped;
        lp::game::entities::despawn(*e);
    }
    ctx.check(dropped >= 1, "the hopper dropped items below");
    if (auto* left2 = regionOf(ctx).getBlockEntity(lp::game::blockPos(at(m, at2, 0, 0))))
        if (auto* lc = left2->_getMainComponent()->getContainer())
            for (int i = 0; i < lc->getContainerSize(); ++i) lc->setItem(i, ItemStack{});
});

LP_REGISTER_SELFTEST("crystals.magnet_entity", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto m = at(ctx.testPos(1280, 2), 0, 0, 0);
    ctx.require(co_await ctx.ensureLoaded(m), "test area loaded");
    ctx.require(buildMagnet(ctx, m, 5, at(m, 0, 0, -1)), "magnet");
    co_await ctx.ticks(2);
    Actor* item = regionOf(ctx).getLevel().getSpawner().spawnItem(regionOf(ctx), lp::game::makeItem("minecraft:iron_ingot"), nullptr,
                                                                   Vec3{float(m.x) + 2.5f, float(m.y) + 0.1f, float(m.z) + 0.5f}, 0);
    ctx.require(item != nullptr, "iron ingot dropped in front of the magnet");
    auto id = lp::game::entities::idOf(*item);
    float x0 = item->getPosition().x;
    co_await ctx.ticks(30);
    auto* again = lp::game::entities::find(id);
    ctx.require(again != nullptr, "the item is still there");
    float x1 = again->getPosition().x;
    ctx.check(x1 > x0 + 0.3f, "the magnet pushed the iron item away (" + std::to_string(x1 - x0) + ")");
    lp::game::entities::despawn(*again);
});

LP_REGISTER_SELFTEST("crystals.magnet_stonecutter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto m = at(ctx.testPos(1290, 2), 0, 2, 0);
    ctx.require(co_await ctx.ensureLoaded(m), "test area loaded");
    cleanupLine(ctx, at(m, 1, 0, 0), 1, 0, 0, 5);
    cleanupLine(ctx, at(m, 1, 1, 0), 1, 0, 0, 5);
    ctx.require(place(ctx, at(m, 1, 0, 0), "minecraft:stonecutter_block"), "stonecutter");
    ctx.require(place(ctx, at(m, 2, 1, 0), "minecraft:oak_planks"), "planks above its next position");
    ctx.require(buildMagnet(ctx, m, 5, at(m, 0, 0, -1)), "magnet");
    for (int i = 0; i < 20 && idAt(ctx, at(m, 2, 1, 0)) == "minecraft:oak_planks"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(ctx, at(m, 2, 1, 0)) != "minecraft:oak_planks", "the moved stonecutter cut the block above");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(at(m, 0, 0, -1)));
    co_await ctx.ticks(4);
    AABB box{Vec3{float(m.x), float(m.y - 4), float(m.z - 2)}, Vec3{float(m.x + 8), float(m.y + 4), float(m.z + 3)}};
    int  planks = 0;
    for (auto* e : lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item")) {
        planks += static_cast<ItemActor*>(e)->item().getTypeName() == "minecraft:oak_planks";
        lp::game::entities::despawn(*e);
    }
    ctx.check(planks >= 1, "it dropped the planks (silk touch)");
});

// Real world generation: the corundum caves around the nearest sources of a non-flat overworld
// (the flat e2e world has no caves: run the server scenario crystals-worldgen).
LP_REGISTER_SELFTEST("crystals.worldgen_live", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p     = ctx.player();
    auto& level = p.getLevel();
    if (level.getLevelData().getGenerator() != GeneratorType::Overworld) {
        ctx.info("not a generated overworld: skipped");
        co_return;
    }
    p.setPlayerGameType(GameType::Creative);
    auto spawn   = ctx.testPos(1299);
    auto sources = caveSourcesNear(spawn.x >> 4, spawn.z >> 4, 64);
    std::sort(sources.begin(), sources.end(), [&](BlockPos const& a, BlockPos const& b) {
        auto d = [&](BlockPos const& q) { return (q.x - spawn.x) * (q.x - spawn.x) + (q.z - spawn.z) * (q.z - spawn.z); };
        return d(a) < d(b);
    });
    ctx.require(!sources.empty(), "corundum sources near the spawn");
    int found = 0, tried = 0;
    for (auto const& src : sources) {
        if (tried++ >= 16 || found > 0) break;
        GlobalPos top{0, src.x, 200, src.z};
        if (!co_await ctx.ensureLoaded(top)) continue;
        co_await ctx.ticks(60); // neighbours generate, post-processing runs
        auto& region = regionOf(ctx);
        int   here   = 0;
        for (int x = -24; x <= 24; ++x)
            for (int z = -24; z <= 24; ++z)
                for (int y = -14; y <= 14; ++y) {
                    BlockPos q{src.x + x, src.y + y, src.z + z};
                    if (!region.hasChunksAt(q, 0, false)) continue;
                    auto const& b = region.getBlock(q);
                    if (isCorundum(b) || isCluster(b)) ++here;
                }
        ctx.info("source " + std::to_string(src.x) + " " + std::to_string(src.y) + " " + std::to_string(src.z) + ": "
                 + std::to_string(here) + " crystals");
        found += here;
    }
    ctx.info("generator: " + caveGenStats());
    ctx.check(found > 0, "corundum generated in a cave blob");
});
// A gallery of every block of the unit for screenshots (client scenario `crystals`): corundum,
// waxed corundum, sheets, crystal lamps (off / lit), clusters on every face, magnets in every
// orientation (off / powered) and a redirected, tinted beacon beam. With a real player online the
// player is moved in front of each part ("GALLERY view <name>" lines); on a dedicated server the
// gallery is only built and checked.
LP_REGISTER_SELFTEST("crystals.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1200, 0);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto put = [&](int dx, int dy, int dz, std::string const& id, bs::Changes const& st = {}) {
        ctx.check(place(ctx, at(base, dx, dy, dz), id, st), "placed " + id);
    };
    for (int c = 0; c < 9; ++c) {
        put(c, 0, 1, corundumId(c));
        put(c, 1, 1, waxedCorundumId(c));
        put(10 + c, 0, 1, paneId(c));
        put(10 + c, 1, 1, paneId(c));
        put(20 + c, 0, 1, lampId(c));
        put(20 + c, 1, 1, lampId(c));
        put(20 + c, 1, 0, "minecraft:redstone_block"); // lights the upper row
        put(30 + c, 1, 1, "minecraft:polished_deepslate");
        put(30 + c, 2, 1, clusterId(c), {{"minecraft:block_face", std::string("up")}});
        put(30 + c, 0, 1, clusterId(c), {{"minecraft:block_face", std::string("down")}});
        put(30 + c, 1, 2, clusterId(c), {{"minecraft:block_face", std::string("south")}});
    }
    put(41, 1, 1, "minecraft:polished_deepslate");
    put(40, 1, 1, clusterId(4), {{"minecraft:block_face", std::string("west")}});
    put(42, 1, 1, clusterId(0), {{"minecraft:block_face", std::string("east")}});
    // magnets: push down, up, north, south, west, east (front texture = pushing face); upper row powered
    for (int f = 0; f < 6; ++f) {
        put(45 + 2 * f, 0, 2, "lepton:magnet", {{"minecraft:facing_direction", magnetLook(f)}});
        put(45 + 2 * f, 2, 2, "lepton:magnet", {{"minecraft:facing_direction", magnetLook(f)}});
        put(45 + 2 * f, 2, 1, "minecraft:redstone_block");
    }
    // beacon: up 3, east 4 through a red cluster, north 2 through a green one, up through a blue
    // cluster and yellow corundum
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) put(62 + x, 0, 2 + z, "minecraft:iron_block");
    put(62, 1, 2, "minecraft:beacon");
    put(62, 4, 2, clusterId(0), {{"minecraft:block_face", std::string("east")}});
    put(66, 4, 2, clusterId(3), {{"minecraft:block_face", std::string("north")}});
    put(66, 4, 0, clusterId(4), {{"minecraft:block_face", std::string("up")}});
    put(66, 7, 0, corundumId(2));
    co_await ctx.ticks(10);
    ctx.check(bs::getBool(blockAt(ctx, at(base, 20, 1, 1)), "lepton:lit").value_or(false), "the upper lamps are lit");
    ctx.check(!bs::getBool(blockAt(ctx, at(base, 20, 0, 1)), "lepton:lit").value_or(true), "the lower lamps are off");
    auto key = lp::game::globalPos(regionOf(ctx), lp::game::blockPos(at(base, 62, 1, 2)));
    for (int i = 0; i < 140 && beamEntitiesOf(key).empty(); ++i) co_await ctx.ticks(1);
    ctx.check(!beamEntitiesOf(key).empty(), "the redirected beam is drawn");

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](std::string const& name, float dx, float dy, float dz, float pitch, float yaw) {
        ctx.info("GALLERY view " + name);
        if (auto* v = ctx.player().getLevel().getPlayer(uid))
            v->teleport(Vec3{float(base.x) + dx, float(base.y) + dy, float(base.z) + dz}, DimensionType(base.dim), Vec2{pitch, yaw});
    };
    // south of the rows, looking north (east on the right)
    view("corundum", 4.5f, 1.f, 7.5f, 15.f, 180.f);
    co_await ctx.ticks(200);
    view("sheets", 14.5f, 1.f, 7.5f, 15.f, 180.f);
    co_await ctx.ticks(200);
    view("lamps", 24.5f, 1.f, 7.5f, 15.f, 180.f);
    co_await ctx.ticks(200);
    view("clusters", 36.5f, 1.f, 7.5f, 15.f, 180.f);
    co_await ctx.ticks(200);
    view("magnets", 50.5f, 1.f, 8.5f, 15.f, 180.f);
    co_await ctx.ticks(200);
    view("beacon", 64.5f, 1.f, 16.5f, -25.f, 180.f);
    co_await ctx.ticks(200);
    view("beacon-side", 74.5f, 1.f, 10.5f, -20.f, 135.f); // south-east of it, looking north-west
    co_await ctx.ticks(200);
    ctx.info("GALLERY done");
});

LP_REGISTER_SELFTEST("crystals.recipes", [](Context& ctx) {
    // with the default config the corundum lamp and magnet recipes stay, the crystal lamp and
    // pre-End magnet ones are taken out of the world's recipe list
    auto& recipes = ctx.player().getLevel().getRecipes();
    auto  has     = [&](std::string const& id) {
        for (auto const& [tag, byId] : recipes.mRecipes.get())
            if (byId.contains(id)) return true;
        return false;
    };
    ctx.check(has("lepton:red_corundum_lamp"), "corundum lamp recipe");
    ctx.check(!has("lepton:red_crystal_lamp"), "no amethyst crystal lamp recipe while useCorundum is on");
    ctx.check(has("lepton:magnet") && !has("lepton:magnet_pre_end"), "the post-End magnet recipe only");
    ctx.check(has("lepton:red_corundum_pane") && has("lepton:red_waxed_corundum"), "sheet and waxing recipes");
});

LP_REGISTER_SELFTEST("crystals.dirty_glass", [](Context& ctx) -> ll::coro::CoroTask<> {
    // U8 storage's dirty glass tints beacon beams like Quark's (0x3f2f00); skipped without U8
    auto const* glass = bs::of("lepton:dirty_glass");
    auto const* pane  = bs::of("lepton:dirty_glass_pane");
    if (!glass) {
        ctx.info("lepton:dirty_glass is not registered: skipped");
        co_return;
    }
    ctx.check(beaconColorOf(*glass) == 0x3f2f00u, "dirty glass tints beams");
    ctx.check(!pane || beaconColorOf(*pane) == 0x3f2f00u, "the dirty glass pane tints beams");
    auto beacon = ctx.testPos(1298, 2);
    ctx.require(co_await ctx.ensureLoaded(beacon), "test area loaded");
    ctx.require(place(ctx, at(beacon, 0, 3, 0), "lepton:dirty_glass"), "dirty glass above the beacon spot");
    auto beam = computeBeam(regionOf(ctx), lp::game::blockPos(beacon));
    ctx.check(!beam.broke && beam.quarkTint && beam.segments.size() == 2 && beam.segments.back().color == 0x3f2f00u,
              "the beam above the dirty glass is brownish");
});
