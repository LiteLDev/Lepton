// U28 worldsurface selftests (dx 2800..2899). Generators are tested on structures built in the
// air above the (flat) test area; world generation itself only on non-flat worlds.
#include <algorithm>
#include <format>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/worldsurface/Climate.h"
#include "feature/worldsurface/Common.h"
#include "feature/worldsurface/Gen.h"
#include "feature/worldsurface/Runtime.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
using namespace lp::feature::worldsurface;
namespace bs = lp::game::blockstates;

BlockSource& overworld(Context& ctx) {
    auto* r = lp::game::regionOf(0);
    ctx.require(r != nullptr, "overworld region");
    return *r;
}

/// The test area: `dx` of our range, raised `up` blocks above the surface.
lp::core::GlobalPos area(Context& ctx, int dx, int up) {
    auto p = ctx.testPos(dx);
    p.y += up;
    return p;
}

/// Fills a box (inclusive) with `id` and registers every cell for cleanup.
void fill(Context& ctx, BlockSource& r, BlockPos a, BlockPos b, std::string const& id, bs::Changes const& states = {}) {
    auto const* block = bs::of(id, states);
    ctx.require(block != nullptr, "block " + id);
    for (int x = std::min(a.x, b.x); x <= std::max(a.x, b.x); ++x)
        for (int y = std::min(a.y, b.y); y <= std::max(a.y, b.y); ++y)
            for (int z = std::min(a.z, b.z); z <= std::max(a.z, b.z); ++z) {
                BlockPos p{x, y, z};
                lp::game::setBlock(r, p, *block);
                ctx.cleanupBlock(lp::game::globalPos(0, p));
            }
}

/// Registers a box for cleanup without changing it (cells generators may write).
void cleanup(Context& ctx, BlockPos a, BlockPos b) {
    for (int x = std::min(a.x, b.x); x <= std::max(a.x, b.x); ++x)
        for (int y = std::min(a.y, b.y); y <= std::max(a.y, b.y); ++y)
            for (int z = std::min(a.z, b.z); z <= std::max(a.z, b.z); ++z) ctx.cleanupBlock(lp::core::GlobalPos{0, x, y, z});
}

int count(BlockSource& r, BlockPos a, BlockPos b, std::string const& id) {
    int n = 0;
    for (int x = std::min(a.x, b.x); x <= std::max(a.x, b.x); ++x)
        for (int y = std::min(a.y, b.y); y <= std::max(a.y, b.y); ++y)
            for (int z = std::min(a.z, b.z); z <= std::max(a.z, b.z); ++z)
                if (nameOf(r.getBlock(BlockPos{x, y, z})) == id) ++n;
    return n;
}

std::string idAt(BlockSource& r, BlockPos const& p) { return nameOf(r.getBlock(p)); }

BlockPos bp(lp::core::GlobalPos const& g) { return lp::game::blockPos(g); }

LP_REGISTER_SELFTEST("worldsurface.blocks", [](Context& ctx) {
    for (auto const* id :
         {"lepton:monster_box",
          "lepton:glow_shroom",
          "lepton:glow_shroom_block",
          "lepton:glow_shroom_stem",
          "lepton:glow_shroom_ring",
          "lepton:glow_lichen_growth",
          "lepton:potted_glow_shroom",
          "lepton:potted_glow_lichen_growth"})
        ctx.check(bs::of(id) != nullptr, std::string(id) + " is registered");
    auto st = genState();
    ctx.check(st->monsterBox && st->glowShroom && st->lichenGrowth, "palette resolved the custom blocks");
    ctx.check(st->shroomCap && st->shroomStem, "huge shroom states resolve");
    for (int f = 0; f < 4; ++f) ctx.check(st->ring[f] != nullptr, std::string("ring facing ") + facingName(f));
    for (int f = 0; f < 6; ++f) ctx.check(st->glowLichen[f] != nullptr, "glow lichen face " + std::to_string(f));
    ctx.check(st->logs.size() == 8, "8 fallen log woods");
    ctx.check(st->ores.size() == 2, "2 fairy ring ores");
    ctx.check(bs::getBool(*st->shroomCap, "lepton:down") == false && bs::getBool(*st->shroomCap, "lepton:up") == true, "cap faces");
});

LP_REGISTER_SELFTEST("worldsurface.config", [](Context& ctx) {
    ctx.check(bedrockBiome("minecraft:dark_forest") == "roofed_forest", "dark forest -> roofed_forest");
    ctx.check(bedrockBiome("minecraft:cherry_grove") == "cherry_grove", "cherry grove keeps its name");
    ctx.check(bedrockBiome("minecraft:snowy_plains") == "ice_plains", "snowy plains -> ice_plains");
    DimensionConfig ow;
    ctx.check(dimensionAllowed(ow, 0) && !dimensionAllowed(ow, 1), "overworld whitelist");
    DimensionConfig black{true, {"minecraft:the_end"}};
    ctx.check(dimensionAllowed(black, 1) && !dimensionAllowed(black, 2), "blacklist");
    auto f = resolve(CompoundBiomeConfig{{true, {}}, {false, {"minecraft:cherry_grove"}}});
    ctx.check(f.allows("cherry_grove") && !f.allows("plains"), "cherry grove biome filter");
    ctx.check(bedrockBiomeSet({"minecraft:dark_forest"}).contains("dark_forest"), "Java names are kept as well");
    auto g = resolve(CompoundBiomeConfig{{false, {"minecraft:is_forest"}}, {true, {}}});
    ctx.check(g.allows("roofed_forest") && g.allows("dark_forest") && !g.allows("desert"), "biome tag filter");
    Rng a(42), b(42);
    bool same = true;
    for (int i = 0; i < 16; ++i) same &= a.next() == b.next();
    ctx.check(same, "Rng is deterministic");
    ctx.check(chunkSeed(1, 2, 3, 4) == chunkSeed(1, 2, 3, 4) && chunkSeed(1, 2, 3, 4) != chunkSeed(1, 2, 4, 3), "chunk seeds");
});

LP_REGISTER_SELFTEST("worldsurface.climate", [](Context& ctx) {
    climate::Target inside{0, 0, 0, -1000, 10500, 0};
    ctx.check(climate::glimmeringDistance(inside) == 0, "a deep cave target lies in Quark's ranges");
    climate::Target surface{0, 0, 0, -1000, 0, 0};
    ctx.check(climate::glimmeringDistance(surface) > 0, "a surface target does not");
    climate::Target dripstone{0, 0, 9000, -1000, 10500, 0};
    ctx.check(climate::glimmeringDistance(dripstone) > 0, "dripstone continentalness is excluded");
    auto const* gen = climate::generator();
    if (!gen) {
        ctx.info("flat world: no multi-noise generator, climate sampling skipped");
        return;
    }
    auto found = climate::locate(*gen, BlockPos{0, 0, 0}, 6400, 64, -56, 64);
    ctx.require(found.has_value(), "a Glimmering Weald within 6400 blocks");
    auto t = climate::sample(*gen, *found);
    ctx.check(t && climate::inRangesBelow(*t), "the located cell is inside the ranges");
    ctx.info(std::format("Glimmering Weald at {} {} {}", found->x, found->y, found->z));
});

LP_REGISTER_SELFTEST("worldsurface.fairy_ring", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2805, 50);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r   = overworld(ctx);
    BlockPos top = bp(g); // the dirt layer
    fill(ctx, r, {top.x - 4, top.y, top.z - 4}, {top.x + 4, top.y, top.z + 4}, "minecraft:grass_block");
    fill(ctx, r, {top.x, top.y - 45, top.z}, {top.x, top.y - 24, top.z}, "minecraft:stone");
    cleanup(ctx, {top.x - 4, top.y - 46, top.z - 4}, {top.x + 4, top.y + 3, top.z + 4});
    fill(ctx, r, {top.x, top.y + 1, top.z}, {top.x, top.y + 1, top.z}, "minecraft:dandelion");
    auto st    = genState();
    auto poppy = st->flowers.find("minecraft:poppy");
    ctx.require(poppy != st->flowers.end(), "poppy resolved");
    Rng      rng(2805);
    gen::Ctx c{r, *st, rng, 0, {}, nullptr};
    auto     ore = gen::spawnFairyRing(c, BlockPos{top.x, top.y - 1, top.z}, poppy->second);
    int      flowers = count(r, {top.x - 3, top.y + 1, top.z - 3}, {top.x + 3, top.y + 1, top.z + 3}, "minecraft:poppy");
    ctx.check(flowers == 16, std::format("16 ring flowers (got {})", flowers));
    ctx.check(idAt(r, {top.x, top.y + 1, top.z}) != "minecraft:dandelion", "flowers inside the ring are removed");
    ctx.check(idAt(r, {top.x + 3, top.y + 1, top.z}) == "minecraft:poppy", "(3, 0) is on the ring");
    ctx.check(idAt(r, {top.x + 2, top.y + 1, top.z + 1}) != "minecraft:poppy", "(2, 1) is not");
    ctx.require(ore.has_value(), "an ore was placed below");
    auto oreId = idAt(r, *ore);
    ctx.check(oreId == "minecraft:emerald_ore" || oreId == "minecraft:diamond_ore", "the ore is emerald or diamond: " + oreId);
    ctx.check(top.y - 1 - ore->y >= 25 && top.y - 1 - ore->y <= 44, "ore 25-44 blocks below");
});

LP_REGISTER_SELFTEST("worldsurface.fallen_log", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2815, 30);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r = overworld(ctx);
    BlockPos f = bp(g);
    fill(ctx, r, {f.x - 5, f.y, f.z - 5}, {f.x + 5, f.y, f.z + 5}, "minecraft:grass_block");
    cleanup(ctx, {f.x - 6, f.y + 1, f.z - 6}, {f.x + 6, f.y + 2, f.z + 6});
    auto st = genState();
    auto it = std::find_if(st->logs.begin(), st->logs.end(), [](auto const& e) { return e.log == "minecraft:oak_log"; });
    ctx.require(it != st->logs.end(), "oak log entry");
    Rng      rng(2815);
    gen::Ctx c{r, *st, rng, 0, {}, nullptr};
    BlockPos at{f.x, f.y + 1, f.z};
    // A plain log: with U6's hollow logs present the entry would turn hollow at hollowChance.
    auto plain       = *it;
    plain.hollowAxis = {};
    bool placed      = gen::placeFallenLog(c, at, &plain);
    ctx.require(placed, "a log was placed on grass");
    int logs = count(r, {f.x - 4, f.y + 1, f.z - 4}, {f.x + 4, f.y + 1, f.z + 4}, "minecraft:oak_log");
    ctx.check(logs >= 3 && logs <= 4, std::format("3-4 logs (got {})", logs));
    ctx.check(idAt(r, at) == "minecraft:oak_log", "the log starts at the position");
    auto axis = bs::getString(r.getBlock(at), "pillar_axis");
    ctx.check(axis == "x" || axis == "z", "lying log");
    // on stone: nothing
    fill(ctx, r, {f.x - 5, f.y, f.z - 5}, {f.x + 5, f.y, f.z + 5}, "minecraft:stone");
    fill(ctx, r, {f.x - 5, f.y + 1, f.z - 5}, {f.x + 5, f.y + 1, f.z + 5}, "minecraft:air");
    ctx.check(!gen::placeFallenLog(c, at, &plain), "no log on stone");
});

LP_REGISTER_SELFTEST("worldsurface.monster_box_gen", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2825, 20);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r  = overworld(ctx);
    BlockPos p  = bp(g);
    auto     st = genState();
    Rng      rng(2825);
    gen::Ctx c{r, *st, rng, 0, {}, nullptr};
    fill(ctx, r, {p.x, p.y, p.z}, {p.x, p.y, p.z}, "minecraft:stone");
    cleanup(ctx, {p.x, p.y + 1, p.z}, {p.x, p.y + 1, p.z});
    ctx.check(gen::monsterBoxCanPlace(c, {p.x, p.y + 1, p.z}), "air above stone");
    fill(ctx, r, {p.x, p.y, p.z}, {p.x, p.y, p.z}, "minecraft:deepslate");
    ctx.check(gen::monsterBoxCanPlace(c, {p.x, p.y + 1, p.z}), "air above deepslate");
    fill(ctx, r, {p.x, p.y, p.z}, {p.x, p.y, p.z}, "minecraft:dirt");
    ctx.check(!gen::monsterBoxCanPlace(c, {p.x, p.y + 1, p.z}), "not above dirt");
    fill(ctx, r, {p.x, p.y, p.z}, {p.x, p.y, p.z}, "minecraft:stone");
    fill(ctx, r, {p.x, p.y + 1, p.z}, {p.x, p.y + 1, p.z}, "minecraft:water");
    ctx.check(!gen::monsterBoxCanPlace(c, {p.x, p.y + 1, p.z}), "not in water");
    fill(ctx, r, {p.x, p.y + 1, p.z}, {p.x, p.y + 1, p.z}, "minecraft:air");
});

LP_REGISTER_SELFTEST("worldsurface.monster_box_activate", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto level = ll::service::getLevel();
    ctx.require(level != nullptr, "level");
    auto g = area(ctx, 2830, 0);
    auto box = g;
    box.y += 1;
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto& r = overworld(ctx);
    ctx.require(ctx.setBlock(box, "lepton:monster_box"), "box placed");
    box::registerBox(box);
    bool peaceful = level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful;
    if (peaceful) runCommand("difficulty easy");
    co_await ctx.ticks(2);
    // the player 1.5 blocks above the box's centre: within 2.5 blocks
    auto& p = ctx.player();
    p.teleport(Vec3{box.x + 0.5f, box.y + 1.f, box.z + 0.5f + 1.2f}, DimensionType(0));
    int  seen = 0;
    for (int i = 0; i < 20 && seen == 0; ++i) {
        co_await ctx.ticks(1);
        seen = box::progress(box);
    }
    ctx.check(seen > 0, "the box activates when a player is near");
    for (int i = 0; i < 60 && idAt(r, bp(box)) == "lepton:monster_box"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(r, bp(box)) != "lepton:monster_box", "the box breaks after 40 ticks");
    co_await ctx.ticks(1);
    AABB aabb{Vec3{box.x - 6.f, box.y - 6.f, box.z - 6.f}, Vec3{box.x + 7.f, box.y + 7.f, box.z + 7.f}};
    int  tagged = 0;
    for (auto* a : lp::game::entities::inBox(r, aabb)) {
        if (a && a->hasTag(box::kSpawnedTag)) {
            ++tagged;
            lp::game::entities::despawn(*a);
        }
    }
    ctx.check(tagged >= cfg().minMobCount && tagged <= cfg().maxMobCount, std::format("{}-{} box mobs (got {})", cfg().minMobCount, cfg().maxMobCount, tagged));
    if (peaceful) runCommand("difficulty peaceful");
});

LP_REGISTER_SELFTEST("worldsurface.huge_glow_shroom", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2840, 30);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r  = overworld(ctx);
    BlockPos f  = bp(g);
    auto     st = genState();
    fill(ctx, r, {f.x - 3, f.y, f.z - 3}, {f.x + 3, f.y, f.z + 3}, "minecraft:deepslate");
    cleanup(ctx, {f.x - 3, f.y + 1, f.z - 3}, {f.x + 3, f.y + 10, f.z + 3});
    Rng  rng(2840);
    bool ok = gen::placeHugeGlowShroom(r, *st, rng, {f.x, f.y + 1, f.z});
    ctx.require(ok, "a huge glow shroom grows on deepslate");
    ctx.check(idAt(r, {f.x, f.y + 1, f.z}) == "lepton:glow_shroom_stem", "stem at the base");
    int caps = count(r, {f.x - 3, f.y + 1, f.z - 3}, {f.x + 3, f.y + 10, f.z + 3}, "lepton:glow_shroom_block");
    ctx.check(caps == 9 || caps == 10, std::format("3x3 cap (+1) (got {})", caps));
    int stems = count(r, {f.x - 3, f.y + 1, f.z - 3}, {f.x + 3, f.y + 10, f.z + 3}, "lepton:glow_shroom_stem");
    ctx.check(stems >= 2 && stems <= 5, std::format("stem 2-5 (got {})", stems));
    // not on stone
    fill(ctx, r, {f.x - 3, f.y + 1, f.z - 3}, {f.x + 3, f.y + 10, f.z + 3}, "minecraft:air");
    fill(ctx, r, {f.x, f.y, f.z}, {f.x, f.y, f.z}, "minecraft:stone");
    ctx.check(!gen::placeHugeGlowShroom(r, *st, rng, {f.x, f.y + 1, f.z}), "not on stone");
});

LP_REGISTER_SELFTEST("worldsurface.particles", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2845, 30);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r  = overworld(ctx);
    BlockPos f  = bp(g);
    auto     st = genState();
    ctx.require(st->glowShroom && st->lichenGrowth && st->shroomCap, "palette");
    glimmer::AmbientTypes types{&st->glowShroom->getBlockType(), &st->lichenGrowth->getBlockType(), &st->shroomCap->getBlockType()};
    fill(ctx, r, {f.x, f.y, f.z}, {f.x + 4, f.y, f.z}, "minecraft:deepslate");
    cleanup(ctx, {f.x, f.y + 1, f.z}, {f.x + 4, f.y + 4, f.z});
    fill(ctx, r, {f.x, f.y + 1, f.z}, {f.x, f.y + 1, f.z}, "lepton:glow_lichen_growth");
    fill(ctx, r, {f.x + 2, f.y + 1, f.z}, {f.x + 2, f.y + 1, f.z}, "lepton:glow_shroom");
    fill(ctx, r, {f.x + 4, f.y + 3, f.z}, {f.x + 4, f.y + 3, f.z}, "lepton:glow_shroom_block");
    auto& p = ctx.player();
    Rng   rng(2845);
    ctx.check(glimmer::animateTick(p, r, {f.x, f.y + 1, f.z}, types, rng) == 2, "a lichen growth always sends its spores");
    int shroom = 0, drip = 0;
    for (int i = 0; i < 300; ++i) {
        shroom += glimmer::animateTick(p, r, {f.x + 2, f.y + 1, f.z}, types, rng);
        drip += glimmer::animateTick(p, r, {f.x + 4, f.y + 3, f.z}, types, rng);
    }
    ctx.check(shroom > 10 && shroom < 50, std::format("a glow shroom sparks about 1 in 12 ticks (got {}/300)", shroom));
    ctx.check(drip > 12 && drip < 55, std::format("a shroom block drips about 1 in 10 ticks above air (got {}/300)", drip));
    fill(ctx, r, {f.x + 2, f.y + 2, f.z}, {f.x + 2, f.y + 2, f.z}, "minecraft:deepslate");
    fill(ctx, r, {f.x + 4, f.y + 2, f.z}, {f.x + 4, f.y + 2, f.z}, "minecraft:deepslate");
    int blocked = 0;
    for (int i = 0; i < 100; ++i) {
        blocked += glimmer::animateTick(p, r, {f.x + 2, f.y + 1, f.z}, types, rng);
        blocked += glimmer::animateTick(p, r, {f.x + 4, f.y + 3, f.z}, types, rng);
        blocked += glimmer::animateTick(p, r, {f.x + 1, f.y, f.z}, types, rng);
    }
    ctx.check(blocked == 0, "no particles when covered, and none from other blocks");
});

LP_REGISTER_SELFTEST("worldsurface.glow_extras", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2850, 30);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r  = overworld(ctx);
    BlockPos f  = bp(g);
    auto     st = genState();
    fill(ctx, r, {f.x - 2, f.y, f.z - 2}, {f.x + 2, f.y, f.z + 2}, "minecraft:deepslate");
    cleanup(ctx, {f.x - 2, f.y + 1, f.z - 2}, {f.x + 2, f.y + 7, f.z + 2});
    Rng      rng(2850);
    gen::Ctx c{r, *st, rng, 0, {}, nullptr};
    gen::glowExtrasAt(c, {f.x, f.y + 1, f.z});
    int lichen = count(r, {f.x - 2, f.y + 1, f.z - 2}, {f.x + 2, f.y + 1, f.z + 2}, "minecraft:glow_lichen");
    int growth = count(r, {f.x - 2, f.y + 1, f.z - 2}, {f.x + 2, f.y + 1, f.z + 2}, "lepton:glow_lichen_growth");
    ctx.check(lichen + growth > 0, std::format("lichen ({}) / growths ({}) on the deepslate", lichen, growth));
    int above = count(r, {f.x - 2, f.y + 2, f.z - 2}, {f.x + 2, f.y + 7, f.z + 2}, "lepton:glow_lichen_growth");
    ctx.check(above == 0, "growths only on deepslate");
});

LP_REGISTER_SELFTEST("worldsurface.bonemeal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2855, 30);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r = overworld(ctx);
    BlockPos f = bp(g);
    fill(ctx, r, {f.x - 1, f.y, f.z - 1}, {f.x + 1, f.y, f.z + 1}, "minecraft:deepslate");
    cleanup(ctx, {f.x - 1, f.y + 1, f.z - 1}, {f.x + 1, f.y + 1, f.z + 1});
    fill(ctx, r, {f.x, f.y + 1, f.z}, {f.x, f.y + 1, f.z}, "lepton:glow_lichen_growth");
    Rng rng(2855);
    ctx.check(glimmer::boneMeal(r, {f.x, f.y + 1, f.z}, rng), "bone meal on a lichen growth is used");
    int growths = count(r, {f.x - 1, f.y + 1, f.z - 1}, {f.x + 1, f.y + 1, f.z + 1}, "lepton:glow_lichen_growth");
    ctx.check(growths == 2, std::format("the growth spread to one neighbour (got {})", growths));

    // a glow shroom grows into a huge one within a few tries (40 %)
    BlockPos s{f.x + 6, f.y, f.z};
    fill(ctx, r, {s.x - 3, s.y, s.z - 3}, {s.x + 3, s.y, s.z + 3}, "minecraft:deepslate");
    cleanup(ctx, {s.x - 3, s.y + 1, s.z - 3}, {s.x + 3, s.y + 10, s.z + 3});
    fill(ctx, r, {s.x, s.y + 1, s.z}, {s.x, s.y + 1, s.z}, "lepton:glow_shroom");
    bool grew = false;
    for (int i = 0; i < 30 && !grew; ++i) {
        glimmer::boneMeal(r, {s.x, s.y + 1, s.z}, rng);
        grew = idAt(r, {s.x, s.y + 1, s.z}) == "lepton:glow_shroom_stem";
    }
    ctx.check(grew, "bone meal grows a huge glow shroom");
});

LP_REGISTER_SELFTEST("worldsurface.shroom_faces", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2860, 20);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r = overworld(ctx);
    BlockPos a = bp(g), b{a.x + 1, a.y, a.z};
    fill(ctx, r, a, a, "lepton:glow_shroom_block");
    fill(ctx, r, b, b, "lepton:glow_shroom_block");
    glimmer::updateFaces(r, b);
    ctx.check(bs::getBool(r.getBlock(a), "lepton:east") == false, "east face of the first block is pores");
    ctx.check(bs::getBool(r.getBlock(b), "lepton:west") == false, "west face of the second block is pores");
    ctx.check(bs::getBool(r.getBlock(b), "lepton:east") == true, "outer faces stay skin");
    fill(ctx, r, {a.x, a.y + 1, a.z}, {a.x, a.y + 1, a.z}, "lepton:glow_shroom_stem");
    glimmer::updateFaces(r, {a.x, a.y + 1, a.z});
    ctx.check(bs::getBool(r.getBlock(a), "lepton:up") == true, "a stem does not join a cap block");
});

LP_REGISTER_SELFTEST("worldsurface.water_petals", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto st = genState();
    if (!st->petals[4][0]) {
        ctx.info("lepton:water_pink_petals (U25) not present: Quark places nothing either");
        co_return;
    }
    auto g = area(ctx, 2865, 20);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r = overworld(ctx);
    BlockPos f = bp(g);
    fill(ctx, r, {f.x - 1, f.y - 1, f.z - 1}, {f.x + 8, f.y - 1, f.z + 8}, "minecraft:stone");
    fill(ctx, r, {f.x - 1, f.y, f.z - 1}, {f.x + 8, f.y, f.z + 8}, "minecraft:water");
    cleanup(ctx, {f.x - 2, f.y + 1, f.z - 2}, {f.x + 9, f.y + 1, f.z + 9});
    Rng      rng(2865);
    gen::Ctx c{r, *st, rng, 0, {}, nullptr};
    gen::placePetals(c, {f.x + 2, f.y + 1, f.z + 2});
    int petals = count(r, {f.x - 1, f.y + 1, f.z - 1}, {f.x + 8, f.y + 1, f.z + 8}, "lepton:water_pink_petals");
    ctx.check(petals >= 4, std::format("a petal patch on the water (got {})", petals));
});

LP_REGISTER_SELFTEST("worldsurface.composter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2870, 1);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto& r = overworld(ctx);
    ctx.require(ctx.setBlock(g, "minecraft:composter"), "composter placed");
    ctx.check(glimmer::compostChance("lepton:glow_shroom") == 0.65, "glow shroom 65 %");
    ctx.check(glimmer::compostChance("lepton:glow_lichen_growth") == 0.5, "lichen growth 50 %");
    ctx.check(glimmer::compostChance("minecraft:dirt") == 0, "dirt is not ours");
    ctx.check(glimmer::compost(r, bp(g), 0.65, 0.99), "an empty composter always takes the first layer");
    ctx.check(bs::getInt(r.getBlock(bp(g)), "composter_fill_level") == 1, "level 1");
    ctx.check(!glimmer::compost(r, bp(g), 0.65, 0.99), "a failed roll keeps the level");
    ctx.check(glimmer::compost(r, bp(g), 0.65, 0.10), "a good roll raises it");
    ctx.check(bs::getInt(r.getBlock(bp(g)), "composter_fill_level") == 2, "level 2");
    ctx.require(ctx.setBlock(g, "minecraft:composter", {{"composter_fill_level", 6}}), "composter at level 6");
    ctx.check(glimmer::compost(r, bp(g), 0.65, 0.10), "level 6 -> 7");
    ctx.check(!glimmer::compost(r, bp(g), 0.65, 0.10), "a full composter takes nothing");
    co_await ctx.ticks(25);
    ctx.check(bs::getInt(r.getBlock(bp(g)), "composter_fill_level") == 8, "a full composter becomes ready");
});

LP_REGISTER_SELFTEST("worldsurface.pot", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2875, 1);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto& r = overworld(ctx);
    ctx.check(glimmer::pottedOf("lepton:glow_shroom") == "lepton:potted_glow_shroom", "potted glow shroom id");
    ctx.check(glimmer::plantOf("lepton:potted_glow_lichen_growth") == "lepton:glow_lichen_growth", "plant of a pot");
    ctx.require(ctx.setBlock(g, "minecraft:flower_pot"), "flower pot placed");
    auto& p = static_cast<SimulatedPlayer&>(ctx.player());
    co_await ctx.ticks(10);
    auto shroom = lp::game::makeItem("lepton:glow_shroom", 1);
    ctx.give(p, shroom, p.getSelectedItemSlot());
    auto hand = lp::game::makeItem("lepton:glow_shroom", 1);
    p.simulateUseItemOnBlock(hand, bp(g), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 0.5f, 0.5f});
    for (int i = 0; i < 10 && idAt(r, bp(g)) != "lepton:potted_glow_shroom"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(r, bp(g)) == "lepton:potted_glow_shroom", "the glow shroom goes into the pot");
    co_await ctx.ticks(10);
    auto stick = lp::game::makeItem("minecraft:stick", 1);
    p.simulateUseItemOnBlock(stick, bp(g), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 0.5f, 0.5f});
    for (int i = 0; i < 10 && idAt(r, bp(g)) != "minecraft:flower_pot"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(r, bp(g)) == "minecraft:flower_pot", "using the pot takes the plant out");
});

LP_REGISTER_SELFTEST("worldsurface.worldgen", [](Context& ctx) {
    auto const* gen = climate::generator();
    if (!gen) {
        ctx.info("flat world: Quark generates nothing in flat worlds; world generation not checked");
        return;
    }
    auto& s = gen::stats();
    ctx.info(std::format(
        "chunks={} fairyRings={} fallenLogs={} monsterBoxes={} glimmeringChunks={}",
        s.chunks.load(),
        s.fairyRings.load(),
        s.fallenLogs.load(),
        s.monsterBoxes.load(),
        s.glimmeringChunks.load()
    ));
    ctx.check(s.chunks.load() > 0, "chunks were decorated by Lepton");
    ctx.check(s.monsterBoxes.load() > 0, "monster boxes were generated");
});

// Builds every block of the unit in a row and shows it to the first real player (client e2e
// screenshots); without a real player it only checks the placements.
LP_REGISTER_SELFTEST("worldsurface.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto g = area(ctx, 2880, 0);
    ctx.require(co_await ctx.ensureLoaded(g), "test area loaded");
    auto&    r    = overworld(ctx);
    BlockPos base = bp(g);
    fill(ctx, r, {base.x - 1, base.y - 1, base.z - 2}, {base.x + 28, base.y - 1, base.z + 3}, "minecraft:deepslate");
    cleanup(ctx, {base.x - 1, base.y, base.z - 2}, {base.x + 28, base.y + 8, base.z + 3});
    // the simulated test player would wake the monster box (and stand in the pictures): move it
    // behind the cameras
    ctx.player().teleport(Vec3{base.x + 14.5f, base.y + 1.62f, base.z + 16.5f}, DimensionType(0));
    co_await ctx.ticks(2);
    auto put = [&](int dx, int dy, int dz, std::string const& id, bs::Changes const& st = {}) {
        BlockPos p{base.x + dx, base.y + dy, base.z + dz};
        auto const* b = bs::of(id, st);
        ctx.check(b != nullptr && lp::game::setBlock(r, p, *b), "placed " + id);
    };
    put(1, 0, 0, "lepton:monster_box");
    put(3, 0, 0, "lepton:glow_shroom");
    put(5, 0, 0, "lepton:glow_lichen_growth");
    put(7, 0, 0, "lepton:glow_shroom_block");
    put(9, 0, 0, "lepton:glow_shroom_block", {{"lepton:south", false}, {"lepton:up", false}});
    put(11, 0, 0, "lepton:glow_shroom_stem");
    put(13, 0, 0, "lepton:glow_shroom_stem", {{"lepton:south", false}, {"lepton:up", false}});
    put(15, 0, -1, "minecraft:deepslate");
    put(15, 1, -1, "minecraft:deepslate");
    put(15, 0, 0, "lepton:glow_shroom_ring", {{"minecraft:block_face", std::string("south")}});
    put(15, 1, 0, "lepton:glow_shroom_ring", {{"minecraft:block_face", std::string("south")}});
    put(17, 0, 0, "lepton:potted_glow_shroom");
    put(19, 0, 0, "lepton:potted_glow_lichen_growth");
    put(21, 0, 0, "minecraft:glow_lichen", {{"multi_face_direction_bits", 1}});
    auto st  = genState();
    Rng  rng(7);
    bool big = false;
    for (int i = 0; i < 10 && !big; ++i) big = gen::placeHugeGlowShroom(r, *st, rng, {base.x + 25, base.y, base.z});
    ctx.check(big, "a huge glow shroom for the gallery");

    lp::selftest::Context* c = &ctx;
    Player*                viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto level    = ll::service::getLevel();
    bool peaceful = level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful;
    if (peaceful) runCommand("difficulty easy"); // the monster box shows its flames
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](float dx, float dy, float dz, float pitch, float yaw) {
        if (auto* v = c->player().getLevel().getPlayer(uid))
            v->teleport(Vec3{base.x + dx, base.y + dy, base.z + dz}, DimensionType(0), Vec2{pitch, yaw});
    };
    ctx.info("WSGALLERY view front");
    view(7.f, 1.6f, 7.f, 20.f, 180.f);
    co_await ctx.ticks(120);
    ctx.info("WSGALLERY view right");
    view(20.f, 1.6f, 7.f, 20.f, 180.f);
    co_await ctx.ticks(120);
    ctx.info("WSGALLERY view close");
    view(4.f, 0.6f, 3.6f, 15.f, 180.f);
    co_await ctx.ticks(120);
    ctx.info("WSGALLERY view shroom");
    view(25.5f, 0.f, 5.5f, -20.f, 180.f);
    co_await ctx.ticks(120);
    ctx.info("WSGALLERY done");
    if (peaceful) runCommand("difficulty peaceful");
});

} // namespace