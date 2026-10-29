// U3 newstones world generation (docs/spec/newstones.md §3): Quark's big stone clusters (Zeta
// ClusterBasedGenerator / MultiChunkFeatureGenerator / ClusterShape with BigStoneClusterGenerator),
// the ore pockets (Zeta OreGenerator) and the permafrost underground biome (Quark
// UndergroundStyleGenerator + PermafrostStyle), run in Bedrock's chunk post-processing after the
// chunk's vanilla decoration, for the Overworld, Nether and End generators.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/DividedPos2d.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/source/BiomeArea.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/WorldGenerator.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/v1/NetherGenerator.h"
#include "mc/world/level/levelgen/v1/OverworldGeneratorMultinoise.h"
#include "mc/world/level/levelgen/v1/TheEndGenerator.h"

#include "feature/newstones/NewStones.h"
#include "feature/newstones/Noise.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace lp::feature::newstones {

namespace bs = game::blockstates;

namespace {

constexpr double kPi = std::numbers::pi;

struct Stats {
    std::atomic<uint64_t> chunks{0}, sources{0}, accepted{0}, placed{0}, pockets{0}, permafrost{0};
};
Stats             gStats;
std::atomic<bool> gInstalled{false};

std::string_view stripNs(std::string_view s) {
    if (auto p = s.find(':'); p != std::string_view::npos) s.remove_prefix(p + 1);
    return s;
}

// ---- biomes -------------------------------------------------------------------------------------------

std::unordered_map<std::string_view, std::string_view> const& bedrockToJava() {
    static std::unordered_map<std::string_view, std::string_view> const m{
        {"desert_hills", "desert"},
        {"desert_mutated", "desert"},
        {"extreme_hills", "windswept_hills"},
        {"extreme_hills_edge", "windswept_hills"},
        {"extreme_hills_mutated", "windswept_gravelly_hills"},
        {"extreme_hills_plus_trees", "windswept_forest"},
        {"extreme_hills_plus_trees_mutated", "windswept_forest"},
        {"forest_hills", "forest"},
        {"taiga_hills", "taiga"},
        {"taiga_mutated", "taiga"},
        {"swampland", "swamp"},
        {"swampland_mutated", "swamp"},
        {"deep_warm_ocean", "warm_ocean"},
        {"legacy_frozen_ocean", "frozen_ocean"},
        {"ice_plains", "snowy_plains"},
        {"ice_plains_spikes", "ice_spikes"},
        {"ice_mountains", "snowy_plains"},
        {"mushroom_island", "mushroom_fields"},
        {"mushroom_island_shore", "mushroom_fields"},
        {"cold_beach", "snowy_beach"},
        {"stone_beach", "stony_shore"},
        {"jungle_hills", "jungle"},
        {"jungle_mutated", "jungle"},
        {"jungle_edge", "sparse_jungle"},
        {"jungle_edge_mutated", "sparse_jungle"},
        {"bamboo_jungle_hills", "bamboo_jungle"},
        {"birch_forest_hills", "birch_forest"},
        {"birch_forest_mutated", "old_growth_birch_forest"},
        {"birch_forest_hills_mutated", "old_growth_birch_forest"},
        {"roofed_forest", "dark_forest"},
        {"roofed_forest_mutated", "dark_forest"},
        {"cold_taiga", "snowy_taiga"},
        {"cold_taiga_hills", "snowy_taiga"},
        {"cold_taiga_mutated", "snowy_taiga"},
        {"mega_taiga", "old_growth_pine_taiga"},
        {"mega_taiga_hills", "old_growth_pine_taiga"},
        {"redwood_taiga_mutated", "old_growth_spruce_taiga"},
        {"redwood_taiga_hills_mutated", "old_growth_spruce_taiga"},
        {"savanna_mutated", "windswept_savanna"},
        {"savanna_plateau_mutated", "windswept_savanna"},
        {"mesa", "badlands"},
        {"mesa_bryce", "eroded_badlands"},
        {"mesa_plateau", "badlands"},
        {"mesa_plateau_mutated", "badlands"},
        {"mesa_plateau_stone", "wooded_badlands"},
        {"mesa_plateau_stone_mutated", "wooded_badlands"},
        {"hell", "nether_wastes"},
        {"soulsand_valley", "soul_sand_valley"},
    };
    return m;
}

using NameList = std::vector<std::string_view>;
/// Java 1.21.1 biome tags (vanilla `minecraft:` and NeoForge `c:`), values without "minecraft:";
/// "#tag" entries include another tag.
std::unordered_map<std::string_view, NameList> const& tagTable() {
    static std::unordered_map<std::string_view, NameList> const m{
        {"minecraft:is_ocean",
         {"deep_frozen_ocean", "deep_cold_ocean", "deep_ocean", "deep_lukewarm_ocean", "frozen_ocean", "ocean", "cold_ocean",
          "lukewarm_ocean", "warm_ocean"}},
        {"minecraft:is_deep_ocean", {"deep_frozen_ocean", "deep_cold_ocean", "deep_ocean", "deep_lukewarm_ocean"}},
        {"minecraft:is_beach", {"beach", "snowy_beach"}},
        {"minecraft:is_river", {"river", "frozen_river"}},
        {"minecraft:is_mountain", {"meadow", "frozen_peaks", "jagged_peaks", "stony_peaks", "snowy_slopes", "cherry_grove"}},
        {"minecraft:is_hill", {"windswept_hills", "windswept_forest", "windswept_gravelly_hills"}},
        {"minecraft:is_taiga", {"taiga", "snowy_taiga", "old_growth_pine_taiga", "old_growth_spruce_taiga"}},
        {"minecraft:is_jungle", {"bamboo_jungle", "jungle", "sparse_jungle"}},
        {"minecraft:is_forest",
         {"forest", "flower_forest", "birch_forest", "old_growth_birch_forest", "dark_forest", "grove"}},
        {"minecraft:is_savanna", {"savanna", "savanna_plateau", "windswept_savanna"}},
        {"minecraft:is_badlands", {"badlands", "eroded_badlands", "wooded_badlands"}},
        {"minecraft:is_nether", {"nether_wastes", "soul_sand_valley", "crimson_forest", "warped_forest", "basalt_deltas"}},
        {"minecraft:is_end", {"the_end", "end_highlands", "end_midlands", "small_end_islands", "end_barrens"}},
        {"minecraft:is_overworld",
         {"mushroom_fields", "deep_frozen_ocean", "frozen_ocean", "deep_cold_ocean", "cold_ocean", "deep_ocean", "ocean",
          "deep_lukewarm_ocean", "lukewarm_ocean", "warm_ocean", "stony_shore", "swamp", "mangrove_swamp", "snowy_slopes",
          "snowy_plains", "snowy_beach", "windswept_gravelly_hills", "grove", "windswept_hills", "snowy_taiga",
          "windswept_forest", "taiga", "plains", "meadow", "beach", "forest", "old_growth_spruce_taiga", "flower_forest",
          "birch_forest", "dark_forest", "savanna_plateau", "savanna", "jungle", "badlands", "desert", "wooded_badlands",
          "jagged_peaks", "stony_peaks", "frozen_river", "river", "ice_spikes", "old_growth_pine_taiga",
          "sunflower_plains", "old_growth_birch_forest", "sparse_jungle", "bamboo_jungle", "eroded_badlands",
          "windswept_savanna", "cherry_grove", "frozen_peaks", "dripstone_caves", "lush_caves", "deep_dark"}},
        {"c:is_swamp", {"swamp", "mangrove_swamp"}},
        {"c:is_sandy", {"desert", "badlands", "wooded_badlands", "eroded_badlands", "beach"}},
        {"c:is_snowy",
         {"snowy_beach", "snowy_plains", "ice_spikes", "snowy_taiga", "grove", "snowy_slopes", "jagged_peaks", "frozen_peaks"}},
        {"c:is_cold", {"#c:is_cold/overworld", "#c:is_cold/end"}},
        {"c:is_cold/overworld",
         {"taiga", "old_growth_pine_taiga", "old_growth_spruce_taiga", "windswept_hills", "windswept_gravelly_hills",
          "windswept_forest", "snowy_plains", "ice_spikes", "grove", "snowy_slopes", "jagged_peaks", "frozen_peaks",
          "stony_shore", "snowy_beach", "snowy_taiga", "frozen_river", "cold_ocean", "frozen_ocean", "deep_cold_ocean",
          "deep_frozen_ocean"}},
        {"c:is_cold/end", {"the_end", "small_end_islands", "end_midlands", "end_highlands", "end_barrens"}},
        {"c:is_savanna", {"#minecraft:is_savanna"}},
        {"c:is_jungle", {"#minecraft:is_jungle"}},
        {"c:is_mushroom", {"mushroom_fields"}},
        {"c:is_ocean", {"#minecraft:is_ocean"}},
        {"c:is_deep_ocean", {"#minecraft:is_deep_ocean"}},
        {"c:is_beach", {"#minecraft:is_beach"}},
        {"c:is_river", {"#minecraft:is_river"}},
        {"c:is_mountain", {"#minecraft:is_mountain"}},
        {"c:is_forest", {"#minecraft:is_forest"}},
        {"c:is_taiga", {"#minecraft:is_taiga"}},
        {"c:is_badlands", {"#minecraft:is_badlands"}},
        {"c:is_hill", {"#minecraft:is_hill"}},
        {"c:is_desert", {"desert"}},
        {"c:is_plains", {"plains", "sunflower_plains"}},
        {"c:is_birch_forest", {"birch_forest", "old_growth_birch_forest"}},
        {"c:is_dark_forest", {"dark_forest"}},
        {"c:is_flower_forest", {"flower_forest"}},
        {"c:is_ice", {"ice_spikes"}},
        {"c:is_cave", {"lush_caves", "dripstone_caves", "deep_dark"}},
        {"c:is_nether", {"#minecraft:is_nether"}},
        {"c:is_end", {"#minecraft:is_end"}},
        {"c:is_overworld", {"#minecraft:is_overworld"}},
    };
    return m;
}

bool tagContains(std::string_view tag, std::string_view name, int depth = 0) {
    if (tag.starts_with('#')) tag.remove_prefix(1);
    auto const& t  = tagTable();
    auto        it = t.find(tag);
    if (it == t.end() && tag.find(':') == std::string_view::npos) {
        static std::string const prefixed = std::string("minecraft:");
        it = t.find(prefixed + std::string(tag));
    }
    if (it == t.end()) {
        static std::mutex                      warnMutex;
        static std::unordered_set<std::string> warned;
        std::lock_guard                        lock(warnMutex);
        if (warned.insert(std::string(tag)).second) log().warn("newstones: unknown biome tag '{}' in the config", tag);
        return false;
    }
    for (auto v : it->second) {
        if (v.starts_with('#')) {
            if (depth < 4 && tagContains(v, name, depth + 1)) return true;
        } else if (v == name) return true;
    }
    return false;
}

// ---- generation plan (an immutable snapshot of the config, rebuilt on reload / world change) ----------

struct ClusterGen {
    ClusterInfo                         info;
    std::shared_ptr<PerlinSimplexNoise> noise;
    Block const*                        block = nullptr;
    std::array<Block const*, 32>        tints{}; ///< tinted states (myalite), else empty
    bool                                tinted = false;
};
struct OreGen {
    std::string                  name;
    Block const*                 block = nullptr;
    std::array<Block const*, 32> tints{};
    bool                         tinted = false;
    DimensionsConfig             dims;
    OrePocketConfig              pocket;
};
struct PermafrostGen {
    UndergroundStyleConfig              c;
    std::shared_ptr<PerlinSimplexNoise> noise;
    Block const*                        permafrost = nullptr;
    Block const*                        packedIce  = nullptr;
};
struct ReplaceRule {
    BlockType const* type;
    int              dim; ///< -1: any
};
struct Plan {
    std::vector<ClusterGen>        clusters;
    std::vector<OreGen>            ores;
    std::optional<PermafrostGen>   permafrost;
    std::vector<ReplaceRule>       replace;
    std::vector<BlockType const*>  oreMatch;     ///< Zeta ALL_DIMS_STONE_MATCHER
    std::vector<BlockType const*>  undergroundReplaceable; ///< #quark:underground_biome_replaceable
};

std::mutex                  gPlanMutex;
std::shared_ptr<Plan const> gPlan;
std::atomic<bool>           gDirty{true};

int dimOfName(std::string_view n) {
    n = stripNs(n);
    if (n == "overworld") return 0;
    if (n == "the_nether" || n == "nether") return 1;
    if (n == "the_end" || n == "end") return 2;
    return -2;
}

BlockType const* typeOf(std::string_view id) {
    std::string s(id);
    if (s.starts_with("quark:")) s = "lepton:" + s.substr(6);
    auto const* b = bs::of(s);
    return b ? &b->getBlockType() : nullptr;
}

void fillTints(std::string const& id, std::array<Block const*, 32>& tints, bool& tinted) {
    tinted = isTinted(id);
    if (!tinted) return;
    for (int i = 0; i < tintSteps() && i < 32; ++i) {
        tints[static_cast<size_t>(i)] = bs::of(id, {{std::string(kTintState), i}});
        if (!tints[static_cast<size_t>(i)]) tinted = false;
    }
}

std::shared_ptr<Plan const> buildPlan() {
    auto        p = std::make_shared<Plan>();
    auto const& c = cfg();
    for (auto const& info : clusterInfos()) {
        if (!info.active) continue;
        ClusterGen g;
        g.info  = info;
        g.block = bs::of(info.block);
        if (!g.block) continue; // behavior pack missing
        g.noise = std::make_shared<PerlinSimplexNoise>(static_cast<int64_t>(info.seedXor), -4, 4);
        fillTints(info.block, g.tints, g.tinted);
        p->clusters.push_back(std::move(g));
    }
    // ore pockets: only for stones whose big clusters do not run (Quark's trueEnabledCond)
    struct StoneOre {
        char const*                  name;
        StoneTypeConfig const&       type;
        BigStoneClusterConfig const& big;
    };
    StoneOre const stones[] = {
        {"limestone", c.newStoneTypesLimestone, c.bigStoneClustersLimestone},
        {"jasper", c.newStoneTypesJasper, c.bigStoneClustersJasper},
        {"shale", c.newStoneTypesShale, c.bigStoneClustersShale},
        {"myalite", c.newStoneTypesMyalite, c.bigStoneClustersMyalite},
    };
    for (auto const& s : stones) {
        if (!stoneEnabled(s.name)) continue;
        if (c.enableBigStoneClusters && s.big.enabled) continue;
        std::string id = std::string("lepton:") + s.name;
        for (auto const* pocket : {&s.type.oregenLower, &s.type.oregenUpper}) {
            OreGen o;
            o.name  = s.name;
            o.block = bs::of(id);
            if (!o.block) continue;
            fillTints(id, o.tints, o.tinted);
            o.dims   = s.type.dimensions;
            o.pocket = *pocket;
            p->ores.push_back(std::move(o));
        }
    }
    if (c.enablePermafrost) {
        PermafrostGen g;
        g.c          = c.generationSettings;
        g.noise      = std::make_shared<PerlinSimplexNoise>(static_cast<int64_t>(javaHash("permafrost")), -4, 4);
        g.permafrost = bs::of("lepton:permafrost");
        g.packedIce  = bs::of("minecraft:packed_ice");
        if (g.permafrost && g.packedIce) p->permafrost = std::move(g);
    }
    for (auto const& entry : c.blocksToReplace) {
        std::string_view e = entry;
        int              dim = -1;
        if (auto bar = e.find('|'); bar != std::string_view::npos) {
            dim = dimOfName(e.substr(bar + 1));
            e   = e.substr(0, bar);
        }
        if (stripNs(e) == "air") continue;
        if (auto const* t = typeOf(e)) p->replace.push_back({t, dim});
    }
    for (auto id : {"minecraft:stone", "minecraft:deepslate", "minecraft:netherrack", "minecraft:end_stone"})
        if (auto const* t = typeOf(id)) p->oreMatch.push_back(t);
    for (auto id :
         {"minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff",
          "minecraft:deepslate", "lepton:jasper", "lepton:limestone", "lepton:shale", "minecraft:netherrack",
          "minecraft:end_stone"})
        if (auto const* t = typeOf(id)) p->undergroundReplaceable.push_back(t);
    return p;
}

/// The current plan. It is built on the server thread when the world becomes ready or the config
/// changes (rebuildGenPlan); world generation threads only build it when none exists yet (BDS
/// creates the spawn chunks before the mod is enabled, while nothing changes the config).
std::shared_ptr<Plan const> plan() {
    std::lock_guard lock(gPlanMutex);
    if (gDirty.exchange(false) || !gPlan) gPlan = buildPlan();
    return gPlan;
}

bool hasType(std::vector<BlockType const*> const& v, Block const& b) {
    auto const* t = &b.getBlockType();
    return std::find(v.begin(), v.end(), t) != v.end();
}

bool replaceable(Plan const& p, Block const& b, int dim) {
    auto const* t = &b.getBlockType();
    for (auto const& r : p.replace)
        if (r.type == t && (r.dim < 0 || r.dim == dim)) return true;
    return false;
}

Block const& placeState(Block const& base, std::array<Block const*, 32> const& tints, bool tinted, BlockPos const& pos) {
    if (!tinted) return base;
    auto const* t = tints[static_cast<size_t>(tintStep(pos.x, pos.y, pos.z))];
    return t ? *t : base;
}

void setGen(BlockSource& region, BlockPos const& pos, Block const& b) {
    region.setBlock(pos, b, 2, nullptr, BlockChangeContext{});
    ++gStats.placed;
}

// ---- Zeta ClusterShape --------------------------------------------------------------------------------

struct Shape {
    int    sx, sy, sz;
    double rx, ry, rz;
};

Shape shapeAround(int x, int y, int z, int hSize, int vSize, int hVar, int vVar) {
    JavaRandom r(31LL * (31LL * (31 + x) + y) + z);
    int        rx = hSize + r.nextInt(hVar);
    int        ry = vSize + r.nextInt(vVar);
    int        rz = hSize + r.nextInt(hVar);
    return {x, y, z, static_cast<double>(std::max(rx, 1)), static_cast<double>(std::max(ry, 1)), static_cast<double>(std::max(rz, 1))};
}

bool inEllipsoid(Shape const& s, int x, int y, int z) {
    double dx = (x - s.sx) / s.rx, dy = (y - s.sy) / s.ry, dz = (z - s.sz) / s.rz;
    return dx * dx + dy * dy + dz * dz <= 1;
}

bool inside(Shape const& s, PerlinSimplexNoise const& noise, int x, int y, int z) {
    double dx = static_cast<double>(x - s.sx) / s.rx;
    double dy = static_cast<double>(y - s.sy) / s.ry;
    double dz = static_cast<double>(z - s.sz) / s.rz;
    double r  = dx * dx + dy * dy + dz * dz;
    if (r > 1) return false;
    r            = std::sqrt(r);
    double phi   = std::atan2(dz, dx);
    double theta = r == 0 ? 0 : std::acos(dy / r);
    double xn = phi + s.sx, yn = theta + s.sz;
    double n      = noise.getValue(xn, yn, false);
    double cutoff = 0.75 * kPi;
    if (phi > cutoff) {
        double n0 = noise.getValue(-kPi + s.sx, yn, false);
        n         = n + (phi - cutoff) / (kPi - cutoff) * (n0 - n);
    }
    return (n + 0.5 - r) > 0;
}

// ---- multi-chunk seeding ------------------------------------------------------------------------------

struct Seeds {
    int64_t seed, xSeed, zSeed;
};
Seeds seedsOf(int64_t seed) {
    JavaRandom r(seed);
    int64_t    xs = r.nextLong();
    int64_t    zs = r.nextLong();
    return {seed, xs, zs};
}
JavaRandom chunkRandom(Seeds const& s, int cx, int cz, int32_t seedXor) {
    uint64_t v = static_cast<uint64_t>(s.xSeed) * static_cast<uint64_t>(static_cast<int64_t>(cx))
               + static_cast<uint64_t>(s.zSeed) * static_cast<uint64_t>(static_cast<int64_t>(cz));
    return JavaRandom(static_cast<int64_t>(v) ^ s.seed ^ static_cast<int64_t>(seedXor));
}

/// Per-dimension queries for sources outside the chunk being generated.
struct DimQuery {
    int                                             dim = 0;
    std::function<std::string(int x, int z)>        biome;   ///< Java biome id at the top of the world
    std::function<std::optional<int>(int x, int z)> surface; ///< preliminary surface height
};

/// Biome lookups of cluster sources are repeated by every chunk the cluster reaches: cache them.
std::mutex                                gBiomeMutex;
std::unordered_map<uint64_t, std::string> gBiomeCache;
int64_t                                   gBiomeSeed = 0;

void clearBiomeCache() {
    std::lock_guard lock(gBiomeMutex);
    gBiomeCache.clear();
}

std::string cachedBiome(DimQuery const& q, int64_t seed, int x, int z) {
    auto&    m     = gBiomeMutex;
    auto&    cache = gBiomeCache;
    uint64_t key = (static_cast<uint64_t>(q.dim & 3) << 62) ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 31)
                 ^ static_cast<uint64_t>(static_cast<uint32_t>(z));
    {
        std::lock_guard lock(m);
        if (gBiomeSeed != seed) {
            cache.clear();
            gBiomeSeed = seed;
        }
        if (auto it = cache.find(key); it != cache.end()) return it->second;
    }
    std::string b = q.biome(x, z);
    std::lock_guard lock(m);
    if (gBiomeSeed != seed) return b;
    if (cache.size() > 20000) cache.clear();
    cache.emplace(key, b);
    return b;
}

// ---- big stone clusters -------------------------------------------------------------------------------

int fillPart(BlockSource& region, int dim, Plan const& p, BigStoneClusterConfig const& c, PerlinSimplexNoise const& noise,
             Block const& place, std::array<Block const*, 32> const* tints, bool tinted, Shape const& sh, int cx, int cz) {
    int minH = region.getMinHeight(), maxH = region.getMaxHeight();
    int lo   = std::max(minH + 1, static_cast<int>(std::floor(sh.sy - sh.ry)));
    int hi   = std::min(maxH - 1, static_cast<int>(std::ceil(sh.sy + sh.ry)));
    int n    = 0;
    // Zeta forEachChunkBlock: y in [lower, upper) (the top row of the bounds is not visited)
    for (int bx = 0; bx < 16; ++bx) {
        int x = (cx << 4) + bx;
        if (std::abs(x - sh.sx) > sh.rx) continue;
        for (int y = lo; y < hi; ++y) {
            for (int bz = 0; bz < 16; ++bz) {
                int z = (cz << 4) + bz;
                if (!inEllipsoid(sh, x, y, z)) continue;
                BlockPos    pos{x, y, z};
                auto const& b  = region.getBlock(pos);
                bool        ok = c.generateInAir ? b.isAir() : replaceable(p, b, dim);
                if (!ok || !inside(sh, noise, x, y, z)) continue;
                setGen(region, pos, tints ? placeState(place, *tints, tinted, pos) : place);
                ++n;
            }
        }
    }
    return n;
}

void generateClusters(BlockSource& region, DimQuery const& q, Plan const& p, Seeds const& seeds, int cx, int cz) {
    for (auto const& g : p.clusters) {
        auto const& c = g.info.config;
        if (!dimensionAllowed(q.dim, c.dimensions)) continue;
        int radius = c.horizontalSize + c.horizontalVariation;
        if (radius <= 0 || c.rarity <= 0) continue;
        int cr = static_cast<int>(std::ceil(radius / 16.0));
        for (int x = cx - cr; x <= cx + cr; ++x) {
            for (int z = cz - cr; z <= cz + cr; ++z) {
                JavaRandom r = chunkRandom(seeds, x, z, g.info.seedXor);
                if (r.nextInt(c.rarity) != 0) continue;
                int lower = c.minYLevel;
                int range = std::abs(c.maxYLevel - c.minYLevel);
                int sx    = (x << 4) + r.nextInt(16);
                int sy    = (range > 0 ? r.nextInt(range) : 0) + lower;
                int sz    = (z << 4) + r.nextInt(16);
                ++gStats.sources;
                std::string biome = cachedBiome(q, seeds.seed, sx, sz);
                if (biome.empty() || !biomeAllowed(biome, c.biomes)) continue;
                ++gStats.accepted;
                Shape sh = shapeAround(sx, sy, sz, c.horizontalSize, c.verticalSize, c.horizontalVariation, c.verticalVariation);
                fillPart(region, q.dim, p, c, *g.noise, *g.block, &g.tints, g.tinted, sh, cx, cz);
            }
        }
    }
}

// ---- ore pockets (Zeta OreGenerator, the classic ore vein) -------------------------------------------

int orePlace(BlockSource& region, JavaRandom& rand, BlockPos const& pos, int size, Plan const& p, Block const& place,
             std::array<Block const*, 32> const* tints, bool tinted) {
    if (size <= 0) return 0;
    auto const& m       = MthTable::get();
    float       angle   = rand.nextFloat() * 3.1415927f;
    float       factor  = static_cast<float>(size) / 8.0F;
    int         minF    = mthCeil((static_cast<float>(size) / 16.0F * 2.0F + 1.0F) / 2.0F);
    double      x1      = static_cast<float>(static_cast<float>(pos.x) + m.sin(angle) * factor);
    double      x2      = static_cast<float>(static_cast<float>(pos.x) - m.sin(angle) * factor);
    double      z1      = static_cast<float>(static_cast<float>(pos.z) + m.cos(angle) * factor);
    double      z2      = static_cast<float>(static_cast<float>(pos.z) - m.cos(angle) * factor);
    double      y1      = pos.y + rand.nextInt(3) - 2;
    double      y2      = pos.y + rand.nextInt(3) - 2;
    int         maxX    = pos.x - mthCeil(factor) - minF;
    int         maxY    = pos.y - 2 - minF;
    int         maxZ    = pos.z - mthCeil(factor) - minF;
    int         search  = 2 * (mthCeil(factor) + minF);
    int         search2 = 2 * (2 + minF);

    bool below = false;
    for (int x = maxX; x <= maxX + search && !below; ++x)
        for (int z = maxZ; z <= maxZ + search; ++z)
            if (maxY <= region.getAboveTopSolidBlock(x, z, false, false)) {
                below = true;
                break;
            }
    if (!below) return 0;

    std::vector<double> info(static_cast<size_t>(size) * 4);
    for (int i = 0; i < size; ++i) {
        float  ratio    = static_cast<float>(i) / static_cast<float>(size);
        double x        = x1 + ratio * (x2 - x1);
        double y        = y1 + ratio * (y2 - y1);
        double z        = z1 + ratio * (z2 - z1);
        double randSize = rand.nextDouble() * static_cast<double>(size) / 16.0;
        double sz       = (static_cast<double>(m.sin(3.1415927f * ratio) + 1.0F) * randSize + 1.0) / 2.0;
        info[static_cast<size_t>(i) * 4]     = x;
        info[static_cast<size_t>(i) * 4 + 1] = y;
        info[static_cast<size_t>(i) * 4 + 2] = z;
        info[static_cast<size_t>(i) * 4 + 3] = sz;
    }
    for (int a = 0; a < size - 1; ++a) {
        if (info[static_cast<size_t>(a) * 4 + 3] <= 0) continue;
        for (int b = a + 1; b < size; ++b) {
            if (info[static_cast<size_t>(b) * 4 + 3] <= 0) continue;
            double dX = info[a * 4u] - info[b * 4u];
            double dY = info[a * 4u + 1] - info[b * 4u + 1];
            double dZ = info[a * 4u + 2] - info[b * 4u + 2];
            double dS = info[a * 4u + 3] - info[b * 4u + 3];
            if (dS * dS > dX * dX + dY * dY + dZ * dZ) {
                if (dS > 0) info[b * 4u + 3] = -1;
                else info[a * 4u + 3] = -1;
            }
        }
    }
    std::vector<uint8_t> seen;
    int                  placed = 0;
    for (int i = 0; i < size; ++i) {
        double s = info[static_cast<size_t>(i) * 4 + 3];
        if (s < 0) continue;
        double x = info[i * 4u], y = info[i * 4u + 1], z = info[i * 4u + 2];
        int    minX = std::max(mthFloor(x - s), maxX), minY = std::max(mthFloor(y - s), maxY), minZ = std::max(mthFloor(z - s), maxZ);
        int    mX = std::max(mthFloor(x + s), minX), mY = std::max(mthFloor(y + s), minY), mZ = std::max(mthFloor(z + s), minZ);
        for (int cx = minX; cx <= mX; ++cx) {
            double xs = (cx + 0.5 - x) / s;
            if (xs * xs >= 1) continue;
            for (int cy = minY; cy <= mY; ++cy) {
                double ys = (cy + 0.5 - y) / s;
                if (xs * xs + ys * ys >= 1) continue;
                for (int cz = minZ; cz <= mZ; ++cz) {
                    double zs = (cz + 0.5 - z) / s;
                    if (xs * xs + ys * ys + zs * zs >= 1) continue;
                    size_t idx = static_cast<size_t>(cx - maxX) + static_cast<size_t>(cy - maxY) * static_cast<size_t>(search)
                               + static_cast<size_t>(cz - maxZ) * static_cast<size_t>(search) * static_cast<size_t>(search2);
                    if (idx >= seen.size()) seen.resize(idx + 1, 0);
                    if (seen[idx]) continue;
                    seen[idx] = 1;
                    BlockPos bp{cx, cy, cz};
                    if (!hasType(p.oreMatch, region.getBlock(bp))) continue;
                    setGen(region, bp, tints ? placeState(place, *tints, tinted, bp) : place);
                    ++placed;
                }
            }
        }
    }
    return placed;
}

int64_t decorationSeed(int64_t seed, int blockX, int blockZ) {
    JavaRandom r(seed);
    uint64_t   a = static_cast<uint64_t>(r.nextLong() | 1);
    uint64_t   b = static_cast<uint64_t>(r.nextLong() | 1);
    return static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(blockX)) * a
                                + static_cast<uint64_t>(static_cast<int64_t>(blockZ)) * b)
         ^ seed;
}

void generateOres(BlockSource& region, int dim, Plan const& p, int64_t seed, int cx, int cz) {
    if (p.ores.empty()) return;
    constexpr int kStage = 6; // GenerationStep.Decoration.UNDERGROUND_ORES
    int64_t       deco   = decorationSeed(seed, cx << 4, cz << 4);
    // Zeta: stageNum = step * 10000 is passed as the feature index, and setFeatureSeed adds
    // 10000 * step again (decorationSeed + index + 10000 * step)
    int           index  = kStage * 10000;
    for (auto const& o : p.ores) {
        if (!dimensionAllowed(dim, o.dims)) continue;
        JavaRandom rand(deco + index + 10000LL * kStage);
        ++index;
        double count = o.pocket.clusterCount;
        if (count < 1 && count > 0) count = rand.nextDouble() < count ? 1 : 0;
        for (int i = 0; i < count; ++i) {
            int x = (cx << 4) + rand.nextInt(16);
            int y = o.pocket.minHeight + rand.nextInt(o.pocket.maxHeight - o.pocket.minHeight);
            int z = (cz << 4) + rand.nextInt(16);
            if (orePlace(region, rand, BlockPos{x, y, z}, o.pocket.clusterSize, p, *o.block, &o.tints, o.tinted) > 0)
                ++gStats.pockets;
        }
    }
}

// ---- permafrost (Quark UndergroundStyle / BasicUndergroundStyle / PermafrostStyle) --------------------

bool canBeReplaced(BlockSource& region, BlockPos const& pos, Block const& b) {
    return b.isAir() || b.getBlockType().canBeBuiltOver(b, region, pos);
}
bool solidRender(Block const& b) { return !b.isAir() && static_cast<bool>(b.mCachedComponentData->mIsSolid); }

void styleFill(BlockSource& region, BlockPos const& pos, JavaRandom& random, Plan const& p, PermafrostGen const& g) {
    auto const& state = region.getBlock(pos);
    if (state.getBlockType().getDestroySpeed() < 0) return; // unbreakable
    bool inTag      = hasType(p.undergroundReplaceable, state);
    bool canReplace = inTag || canBeReplaced(region, pos, state);
    bool solid      = solidRender(state);
    auto above      = BlockPos{pos.x, pos.y + 1, pos.z};
    auto below      = BlockPos{pos.x, pos.y - 1, pos.z};
    if (solid && canReplace && canBeReplaced(region, above, region.getBlock(above))) {
        // floor: permafrost, sometimes a small pillar of 3..5 permafrost
        setGen(region, pos, *g.permafrost);
        ++gStats.permafrost;
        if (random.nextDouble() < 0.015) {
            int      height = 3 + random.nextInt(3);
            BlockPos q      = pos;
            for (int i = 0; i < height; ++i) {
                q = BlockPos{q.x, q.y + 1, q.z};
                if (!region.getBlock(q).isAir()) break;
                setGen(region, q, *g.permafrost);
            }
        }
        return;
    }
    if (solid && canReplace && canBeReplaced(region, below, region.getBlock(below))) {
        setGen(region, pos, *g.permafrost); // ceiling
        ++gStats.permafrost;
        return;
    }
    bool border = false;
    if (solid && canReplace) {
        static constexpr int kDirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (auto const& d : kDirs) {
            BlockPos    np{pos.x + d[0], pos.y, pos.z + d[1]};
            auto const& nb = region.getBlock(np);
            // Quark: (state != stateAt && isEmptyBlock) || stateAt.canBeReplaced()
            if ((&nb != &state && nb.isAir()) || canBeReplaced(region, np, nb)) {
                border = true;
                break;
            }
        }
    }
    if (border || inTag) setGen(region, pos, *g.packedIce); // wall, or inside (mimicInside)
}

void generatePermafrost(BlockSource& region, DimQuery const& q, Plan const& p, Seeds const& seeds, int cx, int cz) {
    if (!p.permafrost) return;
    auto const& g = *p.permafrost;
    auto const& c = g.c;
    if (!dimensionAllowed(q.dim, c.dimensions) || c.rarity <= 0) return;
    int radius = c.horizontalSize + c.horizontalVariation;
    if (radius <= 0) return;
    int32_t const xr = javaHash("permafrost");
    int           cr = static_cast<int>(std::ceil(radius / 16.0));
    JavaRandom    ours = chunkRandom(seeds, cx, cz, xr);
    std::optional<std::array<int, 256>> heights;
    for (int x = cx - cr; x <= cx + cr; ++x) {
        for (int z = cz - cr; z <= cz + cr; ++z) {
            JavaRandom r = chunkRandom(seeds, x, z, xr);
            if (r.nextInt(c.rarity) != 0) continue;
            int sx = (x << 4) + r.nextInt(16);
            // UndergroundStyleGenerator: Random.nextInt(min, max) (BigStoneClusterGenerator: nextInt(range) + min)
            int sy = c.maxYLevel > c.minYLevel ? r.nextInt(c.minYLevel, c.maxYLevel) : c.minYLevel;
            int sz = (z << 4) + r.nextInt(16);
            ++gStats.sources;
            std::string biome = cachedBiome(q, seeds.seed, sx, sz);
            if (biome.empty() || !biomeAllowed(biome, c.biomes)) continue;
            auto surface = q.surface ? q.surface(sx, sz) : std::nullopt;
            if (!surface || *surface < sy) continue;
            ++gStats.accepted;
            Shape sh = shapeAround(sx, sy, sz, c.horizontalSize, c.verticalSize, c.horizontalVariation, c.verticalVariation);
            if (!heights) {
                heights.emplace();
                for (int bx = 0; bx < 16; ++bx)
                    for (int bz = 0; bz < 16; ++bz)
                        (*heights)[static_cast<size_t>(bx * 16 + bz)] =
                            region.getHeightmapPos(BlockPos{(cx << 4) + bx, 0, (cz << 4) + bz}).y;
            }
            int minH = region.getMinHeight(), maxH = region.getMaxHeight();
            int lo   = std::max(minH + 1, static_cast<int>(std::floor(sh.sy - sh.ry)));
            int hi   = std::min(maxH - 1, static_cast<int>(std::ceil(sh.sy + sh.ry)));
            for (int bx = 0; bx < 16; ++bx) {
                for (int y = lo; y < hi; ++y) {
                    for (int bz = 0; bz < 16; ++bz) {
                        int wx = (cx << 4) + bx, wz = (cz << 4) + bz;
                        if ((*heights)[static_cast<size_t>(bx * 16 + bz)] <= y) continue; // canPlaceAt: below the surface
                        if (!inside(sh, *g.noise, wx, y, wz)) continue;
                        styleFill(region, BlockPos{wx, y, wz}, ours, p, g);
                    }
                }
            }
        }
    }
}

// ---- per-chunk entry ----------------------------------------------------------------------------------

void generateChunk(BlockSource& region, DimQuery const& q, int cx, int cz) {
    if (!gInstalled.load()) return;
    auto p = plan();
    if (p->clusters.empty() && p->ores.empty() && !p->permafrost) return;
    ++gStats.chunks;
    GenerationScope scope;
    int64_t         seed  = static_cast<int64_t>(region.getLevel().getLevelSeed64().mValue);
    Seeds           seeds = seedsOf(seed);
    try {
        generateOres(region, q.dim, *p, seed, cx, cz);           // UNDERGROUND_ORES
        generateClusters(region, q, *p, seeds, cx, cz);          // UNDERGROUND_DECORATION
        generatePermafrost(region, q, *p, seeds, cx, cz);
    } catch (std::exception const& e) {
        log().error("newstones: world generation failed in chunk {} {}: {}", cx, cz, e.what());
    }
}

std::string generatorBiome(WorldGenerator const& gen, int x, int z, int maxY) {
    BoundingBox box;
    box.min            = BlockPos{x, maxY - 1, z};
    box.max            = BlockPos{x + 1, maxY, z + 1};
    auto        area   = gen.getBiomeArea(box, 1);
    auto const& biomes = area.mBiomes.get();
    if (biomes.empty() || !biomes[0]) return {};
    return javaBiome(biomes[0]->mHash->getString());
}

DimQuery overworldQuery(WorldGenerator const& gen, int dim, int maxY) {
    DimQuery q;
    q.dim     = dim;
    q.biome   = [&gen, maxY](int x, int z) { return generatorBiome(gen, x, z, maxY); };
    q.surface = [&gen](int x, int z) -> std::optional<int> {
        auto s = gen.getPreliminarySurfaceLevel(DividedPos2d<4>(x >> 2, z >> 2));
        if (!s) return std::nullopt;
        return static_cast<int>(*s);
    };
    return q;
}

DimQuery endQuery(TheEndGenerator const& gen) {
    DimQuery q;
    q.dim   = 2;
    q.biome = [&gen](int x, int z) {
        int cx = x >> 4, cz = z >> 4;
        return endBiome(cx, cz, gen.getIslandHeightValue(cx, cz, 1, 1));
    };
    q.surface = [&gen](int x, int z) -> std::optional<int> {
        auto s = gen.getPreliminarySurfaceLevel(DividedPos2d<4>(x >> 2, z >> 2));
        if (!s) return std::nullopt;
        return static_cast<int>(*s);
    };
    return q;
}

LL_TYPE_INSTANCE_HOOK(
    NewStonesOverworldPostProcessHook,
    ll::memory::HookPriority::Normal,
    OverworldGeneratorMultinoise,
    &OverworldGeneratorMultinoise::$decorateWorldGenPostProcess,
    void,
    Biome const& biome,
    LevelChunk&  lc,
    BlockSource& region,
    Random&      random
) {
    origin(biome, lc, region, random);
    auto const& cp = lc.mPosition.get();
    generateChunk(region, overworldQuery(*this, 0, region.getMaxHeight()), cp.x, cp.z);
}

LL_TYPE_INSTANCE_HOOK(
    NewStonesNetherPostProcessHook,
    ll::memory::HookPriority::Normal,
    NetherGenerator,
    &NetherGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const& biome,
    LevelChunk&  lc,
    BlockSource& region,
    Random&      random
) {
    origin(biome, lc, region, random);
    auto const& cp = lc.mPosition.get();
    generateChunk(region, overworldQuery(*this, 1, region.getMaxHeight()), cp.x, cp.z);
}

LL_TYPE_INSTANCE_HOOK(
    NewStonesEndPostProcessHook,
    ll::memory::HookPriority::Normal,
    TheEndGenerator,
    &TheEndGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const& biome,
    LevelChunk&  lc,
    BlockSource& region,
    Random&      random
) {
    origin(biome, lc, region, random);
    auto const& cp = lc.mPosition.get();
    generateChunk(region, endQuery(*this), cp.x, cp.z);
}

} // namespace

// ---- public -------------------------------------------------------------------------------------------

std::string javaBiome(std::string_view bedrockBiome) {
    auto name = stripNs(bedrockBiome);
    if (name.empty()) return {};
    auto const& m = bedrockToJava();
    if (auto it = m.find(name); it != m.end()) return "minecraft:" + std::string(it->second);
    return "minecraft:" + std::string(name);
}

bool biomeAllowed(std::string_view javaBiomeId, BiomesConfig const& f) {
    auto name = stripNs(javaBiomeId);
    if (name.empty()) return false;
    bool tagMatch = false;
    for (auto const& t : f.tags.tags)
        if (tagContains(t, name)) {
            tagMatch = true;
            break;
        }
    bool tagsOk    = f.tags.isBlacklist ? !tagMatch : tagMatch;
    bool nameMatch = false;
    for (auto const& n : f.biomes.biomes) {
        if (stripNs(n) == name || stripNs(javaBiome(n)) == name) {
            nameMatch = true;
            break;
        }
    }
    bool namesOk = f.biomes.isBlacklist ? !nameMatch : nameMatch;
    return tagsOk && namesOk;
}

bool dimensionAllowed(int dim, DimensionsConfig const& f) {
    bool in = false;
    for (auto const& d : f.dimensions)
        if (dimOfName(d) == dim) in = true;
    return in != f.isBlacklist;
}

std::string endBiome(int chunkX, int chunkZ, float h) {
    if (static_cast<int64_t>(chunkX) * chunkX + static_cast<int64_t>(chunkZ) * chunkZ <= 4096) return "minecraft:the_end";
    if (h > 40.0F) return "minecraft:end_highlands";
    if (h >= 0.0F) return "minecraft:end_midlands";
    return h < -20.0F ? "minecraft:small_end_islands" : "minecraft:end_barrens";
}

std::vector<ClusterInfo> clusterInfos() {
    auto const&              c   = cfg();
    bool                     big = c.enableBigStoneClusters, van = c.enableVanillaStoneClusters;
    std::vector<ClusterInfo> v;
    auto add = [&](char const* name, char const* block, char const* reg, BigStoneClusterConfig const& cc, bool active) {
        v.push_back({name, block, cc, javaHash(reg), active && cc.enabled});
    };
    add("calcite", "minecraft:calcite", "minecraft:calcite", c.calcite, big);
    add("limestone", "lepton:limestone", "quark:limestone", c.bigStoneClustersLimestone, big && stoneEnabled("limestone"));
    add("jasper", "lepton:jasper", "quark:jasper", c.bigStoneClustersJasper, big && stoneEnabled("jasper"));
    add("shale", "lepton:shale", "quark:shale", c.bigStoneClustersShale, big && stoneEnabled("shale"));
    add("myalite", "lepton:myalite", "quark:myalite", c.bigStoneClustersMyalite, big && stoneEnabled("myalite"));
    add("granite", "minecraft:granite", "minecraft:granite", c.granite, van);
    add("diorite", "minecraft:diorite", "minecraft:diorite", c.diorite, van);
    add("andesite", "minecraft:andesite", "minecraft:andesite", c.andesite, van);
    return v;
}

ClusterShapeInfo clusterShape(int src[3], int hSize, int vSize, int hVar, int vVar) {
    auto s = shapeAround(src[0], src[1], src[2], hSize, vSize, hVar, vVar);
    return {static_cast<int>(s.rx), static_cast<int>(s.ry), static_cast<int>(s.rz)};
}

bool clusterContains(int32_t seedXor, int const src[3], ClusterShapeInfo const& shape, int const pos[3]) {
    static std::mutex                                                   m;
    static std::map<int32_t, std::shared_ptr<PerlinSimplexNoise const>> noises;
    std::shared_ptr<PerlinSimplexNoise const>                           n;
    {
        std::lock_guard lock(m);
        auto&           slot = noises[seedXor];
        if (!slot) slot = std::make_shared<PerlinSimplexNoise const>(static_cast<int64_t>(seedXor), -4, 4);
        n = slot;
    }
    Shape s{src[0], src[1], src[2], static_cast<double>(std::max(shape.rx, 1)), static_cast<double>(std::max(shape.ry, 1)),
            static_cast<double>(std::max(shape.rz, 1))};
    return inside(s, *n, pos[0], pos[1], pos[2]);
}

int fillClusterPart(BlockSource& region, BigStoneClusterConfig const& c, int32_t seedXor, Block const& place, int const src[3], int cx, int cz) {
    auto            p = plan();
    PerlinSimplexNoise noise(static_cast<int64_t>(seedXor), -4, 4);
    std::array<Block const*, 32> tints{};
    bool                         tinted = false;
    fillTints(place.getTypeName(), tints, tinted);
    Shape sh = shapeAround(src[0], src[1], src[2], c.horizontalSize, c.verticalSize, c.horizontalVariation, c.verticalVariation);
    GenerationScope scope;
    int dim = game::globalPos(region, BlockPos{src[0], src[1], src[2]}).dim;
    return fillPart(region, dim, *p, c, noise, place, &tints, tinted, sh, cx, cz);
}

void permafrostFill(BlockSource& region, BlockPos const& pos, void* rand) {
    auto p = plan();
    if (!p->permafrost) return;
    GenerationScope scope;
    styleFill(region, pos, *static_cast<JavaRandom*>(rand), *p, *p->permafrost);
}

int placeOrePocket(BlockSource& region, BlockPos const& pos, int size, Block const& place, int64_t seed) {
    auto                         p = plan();
    JavaRandom                   r(seed);
    std::array<Block const*, 32> tints{};
    bool                         tinted = false;
    fillTints(place.getTypeName(), tints, tinted);
    GenerationScope scope;
    return orePlace(region, r, pos, size, *p, place, &tints, tinted);
}

std::vector<SourceInfo> sourcesNear(int dim, int cx, int cz, int radius) {
    std::vector<SourceInfo> out;
    auto                    level = ll::service::getLevel();
    if (!level) return out;
    auto d = level->getDimension(dim).lock();
    if (!d || !d->mWorldGenerator) return out;
    WorldGenerator const& gen  = *d->mWorldGenerator;
    int                   maxY = d->mHeightRange->mMax;
    DimQuery q = dim == 2 ? endQuery(static_cast<TheEndGenerator const&>(gen)) : overworldQuery(gen, dim, maxY);
    Seeds    seeds = seedsOf(static_cast<int64_t>(level->getLevelSeed64().mValue));
    auto     p     = plan();
    for (auto const& g : p->clusters) {
        auto const& c = g.info.config;
        if (!dimensionAllowed(dim, c.dimensions) || c.rarity <= 0) continue;
        for (int x = cx - radius; x <= cx + radius; ++x) {
            for (int z = cz - radius; z <= cz + radius; ++z) {
                JavaRandom r = chunkRandom(seeds, x, z, g.info.seedXor);
                if (r.nextInt(c.rarity) != 0) continue;
                int range = std::abs(c.maxYLevel - c.minYLevel);
                int sx    = (x << 4) + r.nextInt(16);
                int sy    = (range > 0 ? r.nextInt(range) : 0) + c.minYLevel;
                int sz    = (z << 4) + r.nextInt(16);
                auto biome = q.biome(sx, sz); // not cached: a command scan would flush the generators' cache
                if (biome.empty() || !biomeAllowed(biome, c.biomes)) continue;
                out.push_back({g.info.name, sx, sy, sz, biome});
            }
        }
    }
    if (p->permafrost && dimensionAllowed(dim, p->permafrost->c.dimensions)) {
        auto const&   c  = p->permafrost->c;
        int32_t const xr = javaHash("permafrost");
        for (int x = cx - radius; x <= cx + radius && c.rarity > 0; ++x) {
            for (int z = cz - radius; z <= cz + radius; ++z) {
                JavaRandom r = chunkRandom(seeds, x, z, xr);
                if (r.nextInt(c.rarity) != 0) continue;
                int sx = (x << 4) + r.nextInt(16);
                int sy = c.maxYLevel > c.minYLevel ? r.nextInt(c.minYLevel, c.maxYLevel) : c.minYLevel;
                int sz = (z << 4) + r.nextInt(16);
                auto biome = q.biome(sx, sz); // not cached: a command scan would flush the generators' cache
                if (biome.empty() || !biomeAllowed(biome, c.biomes)) continue;
                auto surface = q.surface(sx, sz);
                if (!surface || *surface < sy) continue;
                out.push_back({"permafrost", sx, sy, sz, biome});
            }
        }
    }
    return out;
}

std::string genStats() {
    return "chunks=" + std::to_string(gStats.chunks.load()) + " sources=" + std::to_string(gStats.sources.load())
         + " accepted=" + std::to_string(gStats.accepted.load()) + " placed=" + std::to_string(gStats.placed.load())
         + " pockets=" + std::to_string(gStats.pockets.load()) + " permafrost=" + std::to_string(gStats.permafrost.load());
}

void invalidateGenPlan() {
    clearBiomeCache();
    gDirty.store(true);
}

void rebuildGenPlan() {
    clearBiomeCache();
    auto p = buildPlan();
    std::lock_guard lock(gPlanMutex);
    gPlan = std::move(p);
    gDirty.store(false);
}

void installWorldGen() {
    gInstalled.store(true);
    NewStonesOverworldPostProcessHook::hook();
    NewStonesNetherPostProcessHook::hook();
    NewStonesEndPostProcessHook::hook();
}

void uninstallWorldGen() {
    gInstalled.store(false);
    NewStonesOverworldPostProcessHook::unhook();
    NewStonesNetherPostProcessHook::unhook();
    NewStonesEndPostProcessHook::unhook();
}

} // namespace lp::feature::newstones
