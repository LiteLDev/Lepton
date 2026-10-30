// Selftests of U3 newstones (docs/spec/newstones.md §8). U3 owns dx 300..399.

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/registry/CreativeItemEntry.h"
#include "mc/world/item/registry/CreativeItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/storage/LevelData.h"

#include "feature/newstones/NewStones.h"
#include "feature/newstones/Noise.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
namespace bs     = lp::game::blockstates;
namespace shapes = lp::game::shapes;
namespace ns     = lp::feature::newstones;
using lp::core::GlobalPos;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

bool put(Context& ctx, GlobalPos const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}
Block const& at(Context& ctx, GlobalPos const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }
std::string  idAt(Context& ctx, GlobalPos const& p) { return lp::game::blockIdAt(regionOf(ctx), lp::game::blockPos(p)); }
GlobalPos    off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}
bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

/// Restores the config (and the cross-unit view) when a test ends.
struct ConfigGuard {
    ns::NewStonesConfig saved = lp::config::get<ns::NewStonesConfig>();
    ns::CrossUnit       cross = ns::crossUnit();
    ~ConfigGuard() {
        lp::config::mut<ns::NewStonesConfig>() = saved;
        ns::crossUnit()                        = cross;
        ns::rebuildGenPlan();
    }
};

bool recipeLoaded(Level& level, std::string const& id) {
    for (auto const& [tag, byId] : level.getRecipes().mRecipes.get())
        if (byId.contains(id)) return true;
    return false;
}

std::string str(int v) { return std::to_string(v); }

} // namespace

LP_REGISTER_SELFTEST("newstones.content", [](Context& ctx) {
    ctx.check(ns::blockCount() == 74, "74 blocks, got " + str(ns::blockCount()));
    ctx.check(ns::recipeCount() == 201, "201 recipes, got " + str(ns::recipeCount()));
    int missing = 0, tinted = 0;
    for (auto id : ns::blockIds()) {
        auto const* b = bs::of(id);
        if (!b) {
            ++missing;
            ctx.check(false, "block registered: " + std::string(id));
            continue;
        }
        auto gate = ns::blockGate(id);
        ctx.check(gate != 0, "gate of " + std::string(id));
        auto k = shapes::kindOf(*b);
        if (endsWith(id, "_stairs")) ctx.check(k == shapes::Kind::Stairs, std::string(id) + " is stairs");
        else if (endsWith(id, "_vertical_slab")) {
            ctx.check(k == shapes::Kind::VerticalSlab, std::string(id) + " is a vertical slab");
            ctx.check((gate & ns::G_vslab) != 0, std::string(id) + " gated by vertical slabs");
        } else if (endsWith(id, "_slab")) ctx.check(k == shapes::Kind::Slab, std::string(id) + " is a slab");
        else if (endsWith(id, "_wall")) ctx.check(k == shapes::Kind::Wall, std::string(id) + " is a wall");
        else if (endsWith(id, "_pillar"))
            ctx.check(bs::getString(*b, "minecraft:block_face").has_value(), std::string(id) + " has an axis");
        bool myalite = std::string_view(id).find("myalite") != std::string_view::npos;
        ctx.check(ns::isTinted(id) == myalite, std::string(id) + (myalite ? " is tinted" : " is not tinted"));
        if (ns::isTinted(id)) {
            ++tinted;
            ctx.check(bs::getInt(*b, ns::kTintState) == ns::defaultTint(), std::string(id) + " default tint");
            ctx.check(
                bs::of(id, {{std::string(ns::kTintState), ns::tintSteps() - 1}}) != nullptr,
                std::string(id) + " has every tint"
            );
        }
    }
    ctx.require(missing == 0, "behavior pack loaded");
    ctx.check(tinted == 16, "16 myalite-family blocks, got " + str(tinted));
    ctx.check(ns::tintSteps() == 16, "16 tint steps");
    ctx.check(ns::blockGate("lepton:chiseled_jasper_bricks") == (ns::G_jasper | ns::G_msv | ns::G_bricks | ns::G_chiseled), "chiseled gate");
    ctx.check(ns::blockGate("lepton:shale_pillar") == (ns::G_shale | ns::G_msv | ns::G_pillar), "pillar gate");
    ctx.check(ns::blockGate("lepton:permafrost_bricks_wall") == ns::G_permafrost, "permafrost gate");
    ctx.check(ns::recipeGate("lepton:limestone_pillar") == (ns::G_limestone | ns::G_msv | ns::G_pillar), "pillar recipe gate");
    ctx.check(
        ns::recipeGate("lepton:polished_myalite_vertical_slab_revert") == (ns::G_myalite | ns::G_vslab),
        "vertical slab revert gate"
    );
    ctx.check(ns::recipeGate("lepton:polished_myalite_stairs_from_myalite_stonecutting") == 0, "no raw myalite -> polished stairs cut (Quark)");
    ctx.check(ns::recipeGate("lepton:polished_jasper_stairs_from_jasper_stonecutting") == ns::G_jasper, "raw jasper -> polished stairs cut");
    ctx.check(ns::blockGate("minecraft:stone") == 0 && ns::blockEnabled("minecraft:stone"), "foreign ids are never gated");
});

LP_REGISTER_SELFTEST("newstones.gates", [](Context& ctx) {
    ConfigGuard guard;
    auto&       c   = lp::config::mut<ns::NewStonesConfig>();
    c               = ns::NewStonesConfig{};
    ns::crossUnit() = ns::CrossUnit{};
    ctx.check(ns::blockEnabled("lepton:chiseled_myalite_bricks") && ns::blockEnabled("lepton:permafrost"), "defaults: everything on");
    c.enableShale = false;
    ctx.check(!ns::blockEnabled("lepton:shale") && !ns::blockEnabled("lepton:shale_bricks_wall"), "enableShale off");
    ctx.check(!ns::recipeEnabled("lepton:polished_shale") && !ns::recipeEnabled("lepton:shale_pillar"), "shale recipes off");
    ctx.check(ns::blockEnabled("lepton:jasper"), "other stones stay");
    c                     = ns::NewStonesConfig{};
    c.enableNewStoneTypes = false;
    ctx.check(!ns::blockEnabled("lepton:limestone") && !ns::blockEnabled("lepton:myalite_pillar"), "module off");
    ctx.check(ns::blockEnabled("lepton:permafrost_stairs"), "permafrost stays");
    ctx.check(ns::masonOfferCount() == 0, "no trades with the module off");
    c                  = ns::NewStonesConfig{};
    c.enablePermafrost = false;
    ctx.check(!ns::blockEnabled("lepton:permafrost_bricks") && !ns::recipeEnabled("lepton:permafrost_bricks"), "permafrost off");
    c = ns::NewStonesConfig{};
    ctx.check(ns::masonOfferCount() == 6, "6 mason offers");
    c.addNewStonesToMasonTrades = false;
    ctx.check(ns::masonOfferCount() == 0, "addNewStonesToMasonTrades off");
    c                       = ns::NewStonesConfig{};
    ns::crossUnit().bricks = false;
    ctx.check(!ns::blockEnabled("lepton:limestone_bricks") && !ns::blockEnabled("lepton:chiseled_limestone_bricks"), "U2 bricks off");
    ctx.check(ns::blockEnabled("lepton:limestone_pillar") && ns::blockEnabled("lepton:polished_limestone"), "pillars and polished stay");
    ns::crossUnit()                = ns::CrossUnit{};
    ns::crossUnit().chiseledBricks = false;
    ctx.check(!ns::blockEnabled("lepton:chiseled_jasper_bricks") && ns::blockEnabled("lepton:jasper_bricks"), "U2 chiseled off");
    ns::crossUnit()        = ns::CrossUnit{};
    ns::crossUnit().pillar = false;
    ctx.check(!ns::blockEnabled("lepton:myalite_pillar") && !ns::recipeEnabled("lepton:myalite_pillar_from_myalite_stonecutting"), "U2 pillar off");
    ns::crossUnit()                   = ns::CrossUnit{};
    ns::crossUnit().moreStoneVariants = false;
    ctx.check(!ns::blockEnabled("lepton:shale_bricks") && ns::blockEnabled("lepton:polished_shale"), "U2 module off");
    ns::crossUnit()               = ns::CrossUnit{};
    ns::crossUnit().verticalSlabs = false;
    ctx.check(!ns::blockEnabled("lepton:jasper_vertical_slab") && !ns::recipeEnabled("lepton:jasper_vertical_slab_revert"), "U1 vertical slabs off");
    ctx.check(ns::blockEnabled("lepton:jasper_slab"), "slabs stay");
    // generators follow the module switches
    ns::crossUnit() = ns::CrossUnit{};
    c               = ns::NewStonesConfig{};
    auto active     = [] {
        std::set<std::string> s;
        for (auto const& i : ns::clusterInfos())
            if (i.active) s.insert(i.name);
        return s;
    };
    ctx.check(active() == std::set<std::string>{"calcite", "limestone", "jasper", "shale", "myalite"}, "default clusters");
    c.enableVanillaStoneClusters = true;
    ctx.check(active().size() == 8, "VanillaStoneClusters adds granite, diorite, andesite");
    c.enableBigStoneClusters = false;
    ctx.check(active() == std::set<std::string>{"granite", "diorite", "andesite"}, "BigStoneClusters off");
    c                                  = ns::NewStonesConfig{};
    c.bigStoneClustersJasper.enabled   = false;
    c.enableLimestone                  = false;
    ctx.check(active() == std::set<std::string>{"calcite", "shale", "myalite"}, "per-cluster enabled and stone switches");
});

LP_REGISTER_SELFTEST("newstones.noise", [](Context& ctx) {
    // java.util.Random reference values
    ctx.check(ns::JavaRandom(0).nextInt() == -1155484576, "Random(0).nextInt()");
    ctx.check(ns::JavaRandom(42).nextInt() == -1170105035, "Random(42).nextInt()");
    ctx.check(ns::JavaRandom(0).nextLong() == -4962768465676381896LL, "Random(0).nextLong()");
    ctx.check(std::abs(ns::JavaRandom(0).nextDouble() - 0.730967787376657) < 1e-15, "Random(0).nextDouble()");
    ctx.check(ns::JavaRandom(42).nextInt(10) == 0, "Random(42).nextInt(10)");
    ctx.check(ns::javaHash("hello") == 99162322, "\"hello\".hashCode()");
    ctx.check(ns::javaHash("") == 0, "\"\".hashCode()");
    ctx.check(ns::hsbToRgb(0.f, 1.f, 1.f) == 0xff0000 && ns::hsbToRgb(2.f / 3.f, 1.f, 1.f) == 0x0000ff, "HSBtoRGB");
    ctx.check(ns::hsbToRgb(-0.25f, 0.7f, 0.8f) == ns::hsbToRgb(0.75f, 0.7f, 0.8f), "HSBtoRGB wraps negative hues");
    auto const& m = ns::MthTable::get();
    ctx.check(m.sin(0.f) == 0.f && std::abs(m.sin(1.5707964f) - 1.f) < 1e-6f && std::abs(m.cos(0.f) - 1.f) < 1e-6f, "Mth.sin/cos table");
    ns::PerlinSimplexNoise a(4543543, -4, 4), b(4543543, -4, 4), c(1, -4, 4);
    double lo = 1e9, hi = -1e9;
    bool   same = true, differs = false;
    for (int i = 0; i < 400; ++i) {
        double x = i * 0.37 - 50, y = i * 0.91 - 120;
        double v = a.getValue(x, y, false);
        same &= v == b.getValue(x, y, false);
        differs |= v != c.getValue(x, y, false);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    ctx.check(same, "noise is deterministic per seed");
    ctx.check(differs, "noise depends on the seed");
    ctx.check(lo >= -1.0 && hi <= 1.0 && hi - lo > 0.3, "noise range " + std::to_string(lo) + ".." + std::to_string(hi));
    // myalite colours: hue in Quark's range, all tint steps reachable somewhere
    std::set<int> steps;
    double        hmin = 1, hmax = -1;
    for (int x = -400; x <= 400; x += 7)
        for (int z = -400; z <= 400; z += 11) {
            double h = ns::myaliteHue(x, 64, z);
            hmin     = std::min(hmin, h);
            hmax     = std::max(hmax, h);
            steps.insert(ns::tintStep(x, 64, z));
        }
    ctx.info("myalite hue range " + std::to_string(hmin) + ".." + std::to_string(hmax) + ", " + str(static_cast<int>(steps.size())) + " steps");
    ctx.check(hmin >= -0.4 && hmax <= -0.1, "hue within -0.4..-0.1");
    ctx.check(steps.size() >= 10, "most tint steps occur");
    ctx.check(ns::myaliteColor(1, 2, 3) == ns::myaliteColor(1, 2, 3), "colour is a function of the position");
    ctx.check(ns::myaliteColor(0, 0, 0) == ns::hsbToRgb(static_cast<float>(ns::myaliteHue(0, 0, 0)), 0.7f, 0.8f), "colour = HSB(hue, 0.7, 0.8)");
});

// Placed myalite (and its shapes) take the tint of their position on the next tick.
LP_REGISTER_SELFTEST("newstones.myalite_tint", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(300);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    std::vector<std::pair<GlobalPos, std::string>> placed;
    int i = 0;
    for (auto const* id : {"lepton:myalite", "lepton:myalite_bricks_stairs", "lepton:polished_myalite_slab", "lepton:myalite_pillar", "lepton:myalite_wall"}) {
        auto p = off(base, 2 * i++, 1, 2);
        ctx.require(put(ctx, p, id), std::string("placed ") + id);
        placed.push_back({p, id});
    }
    auto lime = off(base, 2 * i, 1, 2);
    ctx.require(put(ctx, lime, "lepton:limestone"), "placed limestone");
    co_await ctx.ticks(3);
    for (auto const& [p, id] : placed) {
        int want = ns::tintStep(p.x, p.y, p.z);
        auto got  = bs::getInt(at(ctx, p), ns::kTintState);
        ctx.check(idAt(ctx, p) == id, id + " still there");
        ctx.check(got && *got == want, id + " tint " + str(got.value_or(-1)) + ", expected " + str(want));
    }
    ctx.check(idAt(ctx, lime) == "lepton:limestone", "limestone untouched");
    // a wrong tint set by a command is fixed as well
    auto p0    = placed[0].first;
    int  wrong = (ns::tintStep(p0.x, p0.y, p0.z) + 5) % ns::tintSteps();
    ctx.require(put(ctx, p0, "lepton:myalite", {{std::string(ns::kTintState), wrong}}), "placed with a wrong tint");
    co_await ctx.ticks(3);
    ctx.check(bs::getInt(at(ctx, p0), ns::kTintState) == ns::tintStep(p0.x, p0.y, p0.z), "wrong tint corrected");
});

LP_REGISTER_SELFTEST("newstones.cluster_shape", [](Context& ctx) {
    int  src[3] = {123, 40, -456};
    auto s1     = ns::clusterShape(src, 14, 14, 9, 9);
    auto s2     = ns::clusterShape(src, 14, 14, 9, 9);
    ctx.check(s1.rx == s2.rx && s1.ry == s2.ry && s1.rz == s2.rz, "shape is deterministic");
    ctx.check(s1.rx >= 14 && s1.rx <= 22 && s1.ry >= 14 && s1.ry <= 22 && s1.rz >= 14 && s1.rz <= 22, "radii 14..22");
    int32_t xr     = ns::javaHash("quark:limestone");
    int     in     = 0, total = 0;
    bool    beyond = false;
    for (int x = -30; x <= 30; x += 2)
        for (int y = -30; y <= 30; y += 2)
            for (int z = -30; z <= 30; z += 2) {
                int  p[3] = {src[0] + x, src[1] + y, src[2] + z};
                bool i    = ns::clusterContains(xr, src, s1, p);
                double d  = double(x) * x / (s1.rx * s1.rx) + double(y) * y / (s1.ry * s1.ry) + double(z) * z / (s1.rz * s1.rz);
                if (d <= 1) ++total;
                if (i) ++in;
                if (i && d > 1) beyond = true;
            }
    ctx.check(!beyond, "nothing outside the ellipsoid");
    ctx.info("inside " + str(in) + " of " + str(total) + " ellipsoid samples");
    ctx.check(in > total / 10 && in <= total, "the noise keeps a good part of the ellipsoid");
    int p[3] = {src[0] + 5, src[1], src[2]};
    ctx.check(ns::clusterContains(xr, src, s1, p) == ns::clusterContains(xr, src, s1, p), "isInside is deterministic");
});

LP_REGISTER_SELFTEST("newstones.biomes", [](Context& ctx) {
    ctx.check(ns::javaBiome("swampland") == "minecraft:swamp", "swampland -> swamp");
    ctx.check(ns::javaBiome("minecraft:mesa_bryce") == "minecraft:eroded_badlands", "mesa_bryce -> eroded_badlands");
    ctx.check(ns::javaBiome("cold_taiga") == "minecraft:snowy_taiga", "cold_taiga -> snowy_taiga");
    ctx.check(ns::javaBiome("frozen_peaks") == "minecraft:frozen_peaks", "frozen_peaks stays");
    auto const& c  = lp::config::get<ns::NewStonesConfig>();
    auto        ok = [&](char const* biome, ns::BiomesConfig const& f) { return ns::biomeAllowed(ns::javaBiome(biome), f); };
    auto const  d  = ns::NewStonesConfig{};
    ctx.check(ok("swampland", d.bigStoneClustersLimestone.biomes) && ok("mangrove_swamp", d.bigStoneClustersLimestone.biomes), "limestone: swamps");
    ctx.check(ok("deep_cold_ocean", d.bigStoneClustersLimestone.biomes) && !ok("plains", d.bigStoneClustersLimestone.biomes), "limestone: oceans, not plains");
    ctx.check(ok("desert", d.bigStoneClustersJasper.biomes) && ok("mesa_plateau_stone", d.bigStoneClustersJasper.biomes) && ok("beach", d.bigStoneClustersJasper.biomes), "jasper: sandy and badlands");
    ctx.check(ok("ice_plains", d.bigStoneClustersShale.biomes) && ok("taiga", d.bigStoneClustersShale.biomes) && ok("stone_beach", d.bigStoneClustersShale.biomes), "shale: snowy and cold");
    ctx.check(!ok("forest", d.bigStoneClustersShale.biomes), "shale: not forest");
    ctx.check(ok("jagged_peaks", d.calcite.biomes) && ok("cherry_grove", d.calcite.biomes) && !ok("extreme_hills", d.calcite.biomes), "calcite: mountains");
    ctx.check(ok("savanna_mutated", d.diorite.biomes) && ok("mushroom_island", d.diorite.biomes) && ok("bamboo_jungle", d.diorite.biomes), "diorite");
    ctx.check(ok("roofed_forest", d.andesite.biomes) && !ok("taiga", d.andesite.biomes), "andesite: forests");
    ctx.check(ns::biomeAllowed("minecraft:end_highlands", d.bigStoneClustersMyalite.biomes) && !ns::biomeAllowed("minecraft:end_midlands", d.bigStoneClustersMyalite.biomes), "myalite: end highlands");
    ctx.check(ok("frozen_peaks", d.generationSettings.biomes) && !ok("snowy_slopes", d.generationSettings.biomes), "permafrost: frozen peaks");
    ctx.check(!ns::biomeAllowed("", d.calcite.biomes), "unknown biome never passes");
    ctx.check(ns::dimensionAllowed(0, d.calcite.dimensions) && !ns::dimensionAllowed(2, d.calcite.dimensions), "overworld clusters");
    ctx.check(ns::dimensionAllowed(2, d.bigStoneClustersMyalite.dimensions) && !ns::dimensionAllowed(0, d.newStoneTypesMyalite.dimensions), "end myalite");
    ctx.check(ns::endBiome(10, 10, 80.f) == "minecraft:the_end", "central island");
    ctx.check(ns::endBiome(100, 0, 41.f) == "minecraft:end_highlands", "highlands");
    ctx.check(ns::endBiome(100, 0, 40.f) == "minecraft:end_midlands", "midlands");
    ctx.check(ns::endBiome(0, -100, -21.f) == "minecraft:small_end_islands", "small islands");
    ctx.check(ns::endBiome(0, -100, -5.f) == "minecraft:end_barrens", "barrens");
    (void)c;
});

// A cluster replaces only blocksToReplace blocks inside its shape.
LP_REGISTER_SELFTEST("newstones.cluster_place", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(310);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto origin = off(base, 0, 3, 3);
    for (int x = 0; x < 7; ++x)
        for (int y = 0; y < 7; ++y)
            for (int z = 0; z < 7; ++z) {
                auto p = off(origin, x, y, z);
                ctx.require(put(ctx, p, (x + z) % 5 == 0 ? "minecraft:dirt" : "minecraft:stone"), "cube block placed");
            }
    ns::BigStoneClusterConfig c;
    c.horizontalSize = c.verticalSize = 3;
    c.horizontalVariation = c.verticalVariation = 1;
    int  src[3] = {origin.x + 3, origin.y + 3, origin.z + 3};
    auto shape  = ns::clusterShape(src, 3, 3, 1, 1);
    auto const* lime = bs::of("lepton:limestone");
    ctx.require(lime != nullptr, "limestone registered");
    int32_t xr = ns::javaHash("quark:limestone");
    std::set<std::pair<int, int>> chunks;
    for (int x = 0; x < 7; ++x)
        for (int z = 0; z < 7; ++z) chunks.insert({(origin.x + x) >> 4, (origin.z + z) >> 4});
    int n = 0;
    for (auto [cx, cz] : chunks) n += ns::fillClusterPart(regionOf(ctx), c, xr, *lime, src, cx, cz);
    ctx.info("placed " + str(n));
    int wrong = 0, lime_ = 0;
    for (int x = 0; x < 7; ++x)
        for (int y = 0; y < 7; ++y)
            for (int z = 0; z < 7; ++z) {
                auto        p    = off(origin, x, y, z);
                int         q[3] = {p.x, p.y, p.z};
                bool        in   = ns::clusterContains(xr, src, shape, q);
                std::string want = (x + z) % 5 == 0 ? "minecraft:dirt" : (in ? "lepton:limestone" : "minecraft:stone");
                if (idAt(ctx, p) != want) ++wrong;
                if (idAt(ctx, p) == "lepton:limestone") ++lime_;
            }
    ctx.check(wrong == 0, str(wrong) + " blocks differ from the shape");
    ctx.check(lime_ == n && n > 0, "limestone placed inside the shape: " + str(lime_));
    ctx.check(idAt(ctx, off(origin, 3, 8, 3)) == "minecraft:air", "air above untouched");
});

// An air cluster (myalite) fills only air, with the tint of each position.
LP_REGISTER_SELFTEST("newstones.air_cluster", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(320);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto center = off(base, 3, 5, 3);
    for (int x = -3; x <= 3; ++x)
        for (int y = -3; y <= 3; ++y)
            for (int z = -3; z <= 3; ++z) ctx.cleanupBlock(off(center, x, y, z));
    auto stone = off(center, 1, 0, 0);
    ctx.require(put(ctx, stone, "minecraft:stone"), "stone inside");
    ns::BigStoneClusterConfig c;
    c.generateInAir  = true;
    c.horizontalSize = c.verticalSize = 2;
    c.horizontalVariation = c.verticalVariation = 1;
    int         src[3] = {center.x, center.y, center.z};
    auto const* my     = bs::of("lepton:myalite");
    ctx.require(my != nullptr, "myalite registered");
    int32_t                       xr = ns::javaHash("quark:myalite");
    std::set<std::pair<int, int>> chunks;
    for (int x = -3; x <= 3; ++x)
        for (int z = -3; z <= 3; ++z) chunks.insert({(center.x + x) >> 4, (center.z + z) >> 4});
    int n = 0;
    for (auto [cx, cz] : chunks) n += ns::fillClusterPart(regionOf(ctx), c, xr, *my, src, cx, cz);
    auto shape = ns::clusterShape(src, 2, 2, 1, 1);
    int  wrong = 0, count = 0, badTint = 0;
    for (int x = -3; x <= 3; ++x)
        for (int y = -3; y <= 3; ++y)
            for (int z = -3; z <= 3; ++z) {
                auto p    = off(center, x, y, z);
                int  q[3] = {p.x, p.y, p.z};
                bool in   = ns::clusterContains(xr, src, shape, q);
                auto id   = idAt(ctx, p);
                if (p.x == stone.x && p.y == stone.y && p.z == stone.z) {
                    if (id != "minecraft:stone") ++wrong;
                    continue;
                }
                if ((id == "lepton:myalite") != in) ++wrong;
                if (id == "lepton:myalite") {
                    ++count;
                    if (bs::getInt(at(ctx, p), ns::kTintState) != ns::tintStep(p.x, p.y, p.z)) ++badTint;
                }
            }
    ctx.check(wrong == 0, str(wrong) + " blocks differ from the shape");
    ctx.check(count == n && n > 0, "myalite placed: " + str(count));
    ctx.check(badTint == 0, str(badTint) + " blocks with a wrong tint");
});

// Quark's PermafrostStyle: floors and ceilings become permafrost, other stone packed ice.
LP_REGISTER_SELFTEST("newstones.permafrost_style", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    lp::config::mut<ns::NewStonesConfig>().enablePermafrost = true;
    ns::rebuildGenPlan();
    auto base = ctx.testPos(330);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto o = off(base, 0, 3, 3); // a 5x5x5 stone box with a 3x2x3 hole in the middle
    for (int x = 0; x < 5; ++x)
        for (int y = 0; y < 5; ++y)
            for (int z = 0; z < 5; ++z) {
                bool hole = x >= 1 && x <= 3 && z >= 1 && z <= 3 && y >= 2 && y <= 3;
                auto p    = off(o, x, y, z);
                if (hole) {
                    ctx.cleanupBlock(p);
                    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(p)); // usually air already
                } else ctx.require(put(ctx, p, "minecraft:stone"), "box placed");
            }
    ns::JavaRandom r(1);
    for (int x = 0; x < 5; ++x)
        for (int y = 0; y < 5; ++y)
            for (int z = 0; z < 5; ++z) ns::permafrostFill(regionOf(ctx), lp::game::blockPos(off(o, x, y, z)), &r);
    ctx.check(idAt(ctx, off(o, 2, 1, 2)) == "lepton:permafrost", "floor under the hole: " + idAt(ctx, off(o, 2, 1, 2)));
    ctx.check(idAt(ctx, off(o, 2, 4, 2)) == "lepton:permafrost", "ceiling (and top) of the box: " + idAt(ctx, off(o, 2, 4, 2)));
    ctx.check(idAt(ctx, off(o, 0, 2, 2)) == "minecraft:packed_ice", "wall: " + idAt(ctx, off(o, 0, 2, 2)));
    ctx.check(idAt(ctx, off(o, 0, 2, 0)) == "minecraft:packed_ice", "outer edge: " + idAt(ctx, off(o, 0, 2, 0)));
    ctx.check(idAt(ctx, off(o, 2, 0, 2)) == "lepton:permafrost", "bottom over air is a ceiling: " + idAt(ctx, off(o, 2, 0, 2)));
    int stone = 0;
    for (int x = 0; x < 5; ++x)
        for (int y = 0; y < 5; ++y)
            for (int z = 0; z < 5; ++z) stone += idAt(ctx, off(o, x, y, z)) == "minecraft:stone";
    ctx.check(stone == 0, str(stone) + " stone left");
    auto hole = idAt(ctx, off(o, 2, 2, 2));
    ctx.check(hole == "minecraft:air" || hole == "lepton:permafrost", "the hole stays open (or holds a pillar)");
});

// Zeta's ore vein replaces stone only.
LP_REGISTER_SELFTEST("newstones.ore_pocket", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(340);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto o = off(base, 0, 3, 3);
    for (int x = 0; x < 9; ++x)
        for (int y = 0; y < 9; ++y)
            for (int z = 0; z < 9; ++z)
                ctx.require(put(ctx, off(o, x, y, z), y == 4 ? "minecraft:dirt" : "minecraft:stone"), "cube placed");
    auto const* sh = bs::of("lepton:shale");
    ctx.require(sh != nullptr, "shale registered");
    int n = ns::placeOrePocket(regionOf(ctx), lp::game::blockPos(off(o, 4, 4, 4)), 16, *sh, 12345);
    int shale = 0, dirt = 0;
    for (int x = 0; x < 9; ++x)
        for (int y = 0; y < 9; ++y)
            for (int z = 0; z < 9; ++z) {
                auto id = idAt(ctx, off(o, x, y, z));
                shale += id == "lepton:shale";
                dirt += id == "minecraft:dirt";
            }
    ctx.info("ore pocket placed " + str(n));
    ctx.check(n > 0 && shale == n, "shale placed in the cube: " + str(shale) + " / " + str(n));
    ctx.check(dirt == 81, "dirt is never replaced");
    ctx.check(ns::placeOrePocket(regionOf(ctx), lp::game::blockPos(off(o, 4, 4, 4)), 16, *sh, 12345) == 0, "a second vein finds no stone where the first was");
});

LP_REGISTER_SELFTEST("newstones.trades", [](Context& ctx) {
    ConfigGuard guard;
    lp::config::mut<ns::NewStonesConfig>() = ns::NewStonesConfig{};
    auto offer = [](char const* buy, int bc, char const* sell, int sc, int tier) {
        MerchantRecipe r(ItemInstance(lp::game::makeItem(buy, bc)), ItemInstance(lp::game::makeItem(sell, sc)));
        r.mTier = tier;
        return r;
    };
    std::vector<MerchantRecipe> list;
    list.push_back(offer("minecraft:clay_ball", 10, "minecraft:emerald", 1, 0));
    for (int i = 0; i < 30; ++i) list.push_back(offer("minecraft:granite", 16, "minecraft:emerald", 1, 2));
    list.push_back(offer("minecraft:quartz", 12, "minecraft:emerald", 1, 3));
    int  calls = 0;
    auto rnd   = [&] { return (calls++ % 2) ? 0.5 : 0.0; }; // always replace, middle pick
    int  n     = ns::applyMasonTrades(list, 0, rnd);
    ctx.check(n == 6, "6 distinct Quark offers replace journeyman offers, got " + str(n));
    ctx.check(list.size() == 32, "the list keeps its size");
    std::set<std::string> sold, bought;
    for (auto const& r : list) {
        if (r.mTier != 2) continue;
        auto s = lp::game::itemId(*r.mSell), b = lp::game::itemId(*r.mBuyA);
        if (s.starts_with("lepton:")) {
            sold.insert(s);
            ctx.check(r.mSell->mCount == 4 && b == "minecraft:emerald" && r.mMaxUses == 16 && r.mTraderExp == 10, s + " for 1 emerald x4");
        }
        if (b.starts_with("lepton:polished_")) {
            bought.insert(b);
            ctx.check(r.mBuyCountA == 16 && s == "minecraft:emerald" && r.mTraderExp == 20, b + " x16 for 1 emerald");
        }
    }
    ctx.check(sold == std::set<std::string>{"lepton:limestone", "lepton:jasper", "lepton:shale"}, "sells the three stones");
    ctx.check(bought.size() == 3, "buys the three polished stones");
    ctx.check(lp::game::itemId(*list.front().mBuyA) == "minecraft:clay_ball" && list.back().mTier == 3, "other tiers untouched");
    ctx.check(ns::applyMasonTrades(list, 0, rnd) == 0, "applied once");
    std::vector<MerchantRecipe> none;
    none.push_back(offer("minecraft:granite", 16, "minecraft:emerald", 1, 2));
    ctx.check(ns::applyMasonTrades(none, 0, [] { return 0.99; }) == 0, "kept with probability 7/13");
});

// The world loaded with the config of this run: loaded recipes and creative items follow the gates.
LP_REGISTER_SELFTEST("newstones.recipes", [](Context& ctx) {
    auto& level = ctx.player().getLevel();
    for (auto const* id :
         {"lepton:polished_limestone",
          "lepton:jasper_bricks",
          "lepton:chiseled_shale_bricks",
          "lepton:myalite_pillar",
          "lepton:permafrost_bricks",
          "lepton:polished_shale_vertical_slab_revert",
          "lepton:limestone_bricks_wall_from_polished_limestone_stonecutting",
          "lepton:permafrost_bricks_slab_from_permafrost_stonecutting"}) {
        bool want = ns::recipeEnabled(id);
        ctx.check(recipeLoaded(level, id) == want, std::string(id) + (want ? " loaded" : " not loaded"));
    }
    auto creative = level.getItemRegistry().getCreativeItemRegistry();
    ctx.require(creative.get() != nullptr, "creative registry");
    bool jasper = false, perma = false;
    for (auto const& entry : creative->mCreativeItems.get()) {
        auto id = lp::game::itemId(entry.mItemInstance.get());
        jasper |= id == "lepton:jasper";
        perma |= id == "lepton:permafrost_bricks";
    }
    ctx.check(jasper == ns::blockEnabled("lepton:jasper"), "jasper in the creative inventory iff enabled");
    ctx.check(perma == ns::blockEnabled("lepton:permafrost_bricks"), "permafrost bricks in the creative inventory iff enabled");
});

// In a generated world, the clusters exist around the sources near the spawn.
LP_REGISTER_SELFTEST("newstones.worldgen_live", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p     = ctx.player();
    auto& level = p.getLevel();
    if (level.getLevelData().getGenerator() != GeneratorType::Overworld) {
        ctx.info("not a generated overworld: skipped");
        co_return;
    }
    p.setPlayerGameType(GameType::Creative);
    auto spawn   = ctx.testPos(399);
    auto sources = ns::sourcesNear(0, spawn.x >> 4, spawn.z >> 4, 32);
    std::erase_if(sources, [](ns::SourceInfo const& s) { return s.name == "permafrost"; });
    std::sort(sources.begin(), sources.end(), [&](ns::SourceInfo const& a, ns::SourceInfo const& b) {
        bool na = a.name != "calcite", nb = b.name != "calcite"; // prefer the new stones
        if (na != nb) return na;
        auto d = [&](ns::SourceInfo const& q) {
            return (int64_t)(q.x - spawn.x) * (q.x - spawn.x) + (int64_t)(q.z - spawn.z) * (q.z - spawn.z);
        };
        return d(a) < d(b);
    });
    ctx.info("sources within 32 chunks: " + str(static_cast<int>(sources.size())));
    ctx.require(!sources.empty(), "cluster sources near the spawn");
    int found = 0, tried = 0;
    for (auto const& src : sources) {
        if (tried++ >= 6 || found > 0) break;
        GlobalPos top{0, src.x, 200, src.z};
        if (!co_await ctx.ensureLoaded(top)) continue;
        co_await ctx.ticks(80); // neighbours generate, post-processing runs
        auto&       region = regionOf(ctx);
        std::string block  = src.name == "calcite" ? "minecraft:calcite" : "lepton:" + src.name;
        int         here   = 0;
        for (int x = -23; x <= 23; ++x)
            for (int z = -23; z <= 23; ++z)
                for (int y = -23; y <= 23; ++y) {
                    BlockPos q{src.x + x, src.y + y, src.z + z};
                    if (!region.hasChunksAt(q, 0, false)) continue;
                    if (region.getBlock(q).getTypeName() == block) ++here;
                }
        ctx.info("NEWSTONES source " + src.name + " " + str(src.x) + " " + str(src.y) + " " + str(src.z) + " (" + src.biome + "): " + str(here) + " blocks");
        found += here;
    }
    ctx.info("generator: " + ns::genStats());
    ctx.check(found > 0, "a big stone cluster generated");
});

// Every block of the unit in rows, for screenshots (client scenario `newstones`). With a real player
// online, the player is moved in front of the rows ("NEWSTONES GALLERY view <name>").
LP_REGISTER_SELFTEST("newstones.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(350, 0);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    std::vector<std::vector<std::string>> rows;
    for (std::string s : {"limestone", "jasper", "shale", "myalite"}) {
        std::string b = s + "_bricks", pl = "polished_" + s;
        rows.push_back(
            {"lepton:" + s, "lepton:" + s + "_stairs", "lepton:" + s + "_slab", "lepton:" + s + "_vertical_slab",
             "lepton:" + s + "_wall", "lepton:" + pl, "lepton:" + pl + "_stairs", "lepton:" + pl + "_slab",
             "lepton:" + pl + "_vertical_slab", "lepton:" + b, "lepton:" + b + "_stairs", "lepton:" + b + "_slab",
             "lepton:" + b + "_vertical_slab", "lepton:" + b + "_wall", "lepton:chiseled_" + b, "lepton:" + s + "_pillar"}
        );
    }
    rows.push_back(
        {"lepton:permafrost", "lepton:permafrost_stairs", "lepton:permafrost_slab", "lepton:permafrost_vertical_slab",
         "lepton:permafrost_wall", "lepton:permafrost_bricks", "lepton:permafrost_bricks_stairs",
         "lepton:permafrost_bricks_slab", "lepton:permafrost_bricks_vertical_slab", "lepton:permafrost_bricks_wall"}
    );
    int placed = 0;
    for (size_t line = 0; line < rows.size(); ++line) {
        int x = 0;
        for (auto const& id : rows[line]) {
            bs::Changes ch;
            if (endsWith(id, "_stairs")) ch = {{"minecraft:cardinal_direction", "north"}};
            if (endsWith(id, "_vertical_slab")) ch = {{"lepton:type", "north"}};
            auto p = off(base, x++, 0, 2 + 4 * static_cast<int>(line));
            if (put(ctx, p, id, ch)) ++placed;
            else ctx.check(false, "placed " + id);
        }
    }
    ctx.check(placed == ns::blockCount(), "every block placed: " + str(placed));
    // every tint step of myalite (forced, not position-based) and a wall of position-tinted bricks
    {
        ns::GenerationScope keepForcedTints;
        for (int t = 0; t < ns::tintSteps(); ++t)
            ctx.check(put(ctx, off(base, t, 0, 22), "lepton:myalite", {{std::string(ns::kTintState), t}}), "tint " + str(t));
    }
    for (int x = 0; x < 16; ++x)
        for (int y = 1; y <= 4; ++y) ctx.check(put(ctx, off(base, x, y, 24), "lepton:myalite_bricks"), "brick wall");
    co_await ctx.ticks(3);
    int tints = 0;
    for (int t = 0; t < ns::tintSteps(); ++t) tints += bs::getInt(at(ctx, off(base, t, 0, 22)), ns::kTintState) == t;
    ctx.check(tints == ns::tintSteps(), "forced tint row kept: " + str(tints));

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& pl) {
        if (!pl.isSimulated()) viewer = &pl;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](float dx, float dy, float dz, float pitch, float yaw) {
        if (auto* v = ctx.player().getLevel().getPlayer(uid))
            v->teleport(
                Vec3{static_cast<float>(base.x) + dx, static_cast<float>(base.y) + dy, static_cast<float>(base.z) + dz},
                DimensionType(base.dim),
                Vec2{pitch, yaw}
            );
    };
    auto showView = [&](std::string const& name) -> ll::coro::CoroTask<> {
        ctx.info("NEWSTONES GALLERY view " + name);
        co_await ctx.ticks(100);
    };
    // each stone row from the north side (rows at z +2, +6, ..., +18), looking south, in two halves
    char const* names[] = {"limestone", "jasper", "shale", "myalite", "permafrost"};
    for (int line = 0; line < 5; ++line) {
        for (int half = 0; half < 2; ++half) {
            if (line == 4 && half == 1) break;
            view(4.5f + 8.f * half, 1.f, 2.f + 4.f * line - 3.f, 25.f, 0.f);
            co_await showView(std::string(names[line]) + "-" + str(half + 1));
        }
    }
    view(8.f, 1.f, 19.f, 10.f, 0.f); // the tint row and the brick wall, from behind the permafrost row
    co_await showView("myalite-tints");
    ctx.info("NEWSTONES GALLERY done");
});
