// U7 newwoods world generation (docs/spec/newwoods.md §3.5, §3.6): Quark's BlossomTreeGenerator run
// after the game's chunk decoration, and the azalea log replacement in vanilla azalea trees (grown
// and generated).

#include <algorithm>
#include <atomic>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/AzaleaBlock.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/levelgen/v1/NetherGenerator.h"
#include "mc/world/level/levelgen/v1/OverworldGeneratorMultinoise.h"
#include "mc/world/level/levelgen/v1/TheEndGenerator.h"

#include "feature/newwoods/NewWoods.h"
#include "feature/newwoods/Palette.h"
#include "feature/newwoods/Runtime.h"
#include "game/BlockStates.h"
#include "mod/Log.h"

namespace lp::feature::newwoods {

namespace bs = game::blockstates;

namespace {

struct Stats {
    std::atomic<uint64_t> chunks = 0, rolls = 0, trees = 0, azaleaLogs = 0;
};
Stats& stats() {
    static Stats s;
    return s;
}

std::string_view stripNs(std::string_view s) {
    if (auto p = s.find(':'); p != std::string_view::npos) s.remove_prefix(p + 1);
    return s;
}

/// Java biome tags of Quark's options mapped to Bedrock biome names (docs §3.5).
std::unordered_map<std::string, std::vector<std::string>> const& tagTable() {
    static std::unordered_map<std::string, std::vector<std::string>> const t{
        {"is_snowy",
         {"ice_plains", "ice_plains_spikes", "cold_taiga", "cold_taiga_hills", "cold_taiga_mutated", "cold_beach", "grove",
          "snowy_slopes", "jagged_peaks", "frozen_peaks"}},
        {"is_swamp", {"swampland", "swampland_mutated", "mangrove_swamp"}},
        {"is_savanna", {"savanna", "savanna_plateau", "savanna_mutated", "savanna_plateau_mutated"}},
        {"is_plains", {"plains", "sunflower_plains"}},
        {"is_badlands",
         {"mesa", "mesa_bryce", "mesa_plateau", "mesa_plateau_stone", "mesa_mutated", "mesa_plateau_mutated",
          "mesa_plateau_stone_mutated"}},
        {"is_forest",
         {"forest", "forest_hills", "flower_forest", "birch_forest", "birch_forest_hills", "birch_forest_mutated",
          "birch_forest_hills_mutated", "roofed_forest", "roofed_forest_mutated", "cherry_grove"}},
        {"is_taiga",
         {"taiga", "taiga_hills", "taiga_mutated", "mega_taiga", "mega_taiga_hills", "redwood_taiga_mutated",
          "redwood_taiga_hills_mutated", "cold_taiga", "cold_taiga_hills", "cold_taiga_mutated"}},
        {"is_jungle",
         {"jungle", "jungle_hills", "jungle_edge", "jungle_mutated", "jungle_edge_mutated", "bamboo_jungle", "bamboo_jungle_hills"}},
        {"is_desert", {"desert", "desert_hills", "desert_mutated"}},
        {"is_beach", {"beach", "cold_beach", "stone_beach"}},
        {"is_ocean",
         {"ocean", "deep_ocean", "warm_ocean", "deep_warm_ocean", "lukewarm_ocean", "deep_lukewarm_ocean", "cold_ocean",
          "deep_cold_ocean", "frozen_ocean", "deep_frozen_ocean"}},
        {"is_river", {"river", "frozen_river"}},
        {"is_mountain",
         {"extreme_hills", "extreme_hills_edge", "extreme_hills_mutated", "extreme_hills_plus_trees",
          "extreme_hills_plus_trees_mutated", "meadow", "grove", "snowy_slopes", "jagged_peaks", "frozen_peaks", "stony_peaks",
          "cherry_grove"}},
    };
    return t;
}

bool biomeHasTag(std::string_view biome, std::string_view tag) {
    tag = stripNs(tag);
    auto const& t = tagTable();
    if (auto it = t.find(std::string(tag)); it != t.end())
        return std::find(it->second.begin(), it->second.end(), biome) != it->second.end();
    // a tag without a mapping matches by name ("c:is_cave" -> biomes containing "cave")
    if (tag.starts_with("is_")) tag.remove_prefix(3);
    return !tag.empty() && biome.find(tag) != std::string_view::npos;
}

/// Java #dirt + farmland: what Quark's generator plants on.
bool isSoil(Block const& b) {
    static std::unordered_set<std::string> const kIds{
        "minecraft:grass_block", "minecraft:dirt", "minecraft:coarse_dirt", "minecraft:podzol", "minecraft:dirt_with_roots",
        "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:mycelium", "minecraft:farmland"};
    return kIds.contains(b.getTypeName());
}

/// Blocks the motion-blocking heightmap looks through (no collision) and water can replace.
bool isSoftCover(Block const& b) {
    if (b.isAir()) return true;
    static std::unordered_set<std::string> const kIds{
        "minecraft:short_grass", "minecraft:tall_grass", "minecraft:fern", "minecraft:large_fern", "minecraft:deadbush",
        "minecraft:snow_layer", "minecraft:dandelion", "minecraft:poppy", "minecraft:blue_orchid", "minecraft:allium",
        "minecraft:azure_bluet", "minecraft:red_tulip", "minecraft:orange_tulip", "minecraft:white_tulip",
        "minecraft:pink_tulip", "minecraft:oxeye_daisy", "minecraft:cornflower", "minecraft:lily_of_the_valley",
        "minecraft:sunflower", "minecraft:lilac", "minecraft:rose_bush", "minecraft:peony", "minecraft:sweet_berry_bush",
        "minecraft:pink_petals", "minecraft:brown_mushroom", "minecraft:red_mushroom"};
    return kIds.contains(b.getTypeName());
}

BlossomTreeConfig const& colourConfig(int colour) {
    auto const& c = config();
    switch (colour) {
    case 0:
        return c.blue;
    case 1:
        return c.lavender;
    case 2:
        return c.orange;
    case 3:
        return c.yellow;
    default:
        return c.red;
    }
}

int64_t chunkSeed(uint64_t seed, int cx, int cz, int salt) {
    JavaRandom r(static_cast<int64_t>(seed));
    uint64_t   xs = static_cast<uint64_t>(r.nextLong()) | 1ULL, zs = static_cast<uint64_t>(r.nextLong()) | 1ULL;
    uint64_t   s  = static_cast<uint64_t>(static_cast<int64_t>(cx)) * xs + static_cast<uint64_t>(static_cast<int64_t>(cz)) * zs;
    return static_cast<int64_t>(s ^ seed ^ (0x7A0E5EEDULL * static_cast<uint64_t>(salt + 1)));
}

int axisOf(Block const& oakLog) {
    auto a = bs::getString(oakLog, "pillar_axis").value_or("y");
    return a == "x" ? 0 : (a == "z" ? 2 : 1);
}

} // namespace

bool dimensionAllowed(std::string_view dim, DimensionsConfig const& c) {
    bool in = std::any_of(c.dimensions.begin(), c.dimensions.end(), [&](std::string const& d) { return d == dim; });
    return in != c.isBlacklist;
}

bool biomeAllowed(std::string const& biomeName, BiomesConfig const& c) {
    std::string_view biome = stripNs(biomeName);
    bool             tagHit = std::any_of(c.tags.tags.begin(), c.tags.tags.end(), [&](std::string const& t) { return biomeHasTag(biome, t); });
    bool             nameHit = std::any_of(c.biomes.biomes.begin(), c.biomes.biomes.end(), [&](std::string const& n) { return stripNs(n) == biome; });
    bool             tagsOk  = c.tags.isBlacklist ? !tagHit : tagHit;
    bool             namesOk = c.biomes.isBlacklist ? !nameHit : nameHit;
    return tagsOk && namesOk;
}

BlossomRoll blossomRoll(uint64_t seed, int cx, int cz, int colour, int rarity) {
    JavaRandom r(chunkSeed(seed, cx, cz, colour));
    BlossomRoll out{};
    out.x         = (cx << 4) + r.nextInt(16);
    out.z         = (cz << 4) + r.nextInt(16);
    out.rarityHit = rarity > 0 && r.nextInt(rarity) == 0;
    return out;
}

int generateBlossomChunk(BlockSource& region, int cx, int cz, std::string_view dim, uint64_t seed) {
    auto const& c = config();
    if (!c.enableBlossomTrees) return 0;
    int placed = 0;
    for (int colour = 0; colour < 5; ++colour) {
        auto const& tc = colourConfig(colour);
        if (tc.rarity <= 0 || !dimensionAllowed(dim, tc.dimensions)) continue;
        auto const* tree = &treeTypes()[1 + colour];
        JavaRandom  r(chunkSeed(seed, cx, cz, colour));
        int         x = (cx << 4) + r.nextInt(16), z = (cz << 4) + r.nextInt(16);
        BlockPos    top = region.getHeightmapPos(BlockPos{x, 0, z});
        if (top.y <= region.getMinHeight() || top.y >= region.getMaxHeight()) continue;
        std::string biome = region.getBiome(top).mHash->getString();
        if (!biomeAllowed(biome, tc.biomes)) continue;
        if (r.nextInt(tc.rarity) != 0) continue;
        ++stats().rolls;
        BlockPos ground{x, top.y - 1, z};
        while (ground.y > region.getMinHeight() && isSoftCover(region.getBlock(ground))) --ground.y;
        if (!isSoil(region.getBlock(ground))) continue;
        BlockPos up{x, ground.y + 1, z};
        auto const& upBlock = region.getBlock(up);
        if (!upBlock.isAir() && isSoftCover(upBlock)) {
            if (auto pal = palette(); pal && pal->air) {
                BlockChangeContext ctx{};
                region.setBlock(up, *pal->air, 2, nullptr, ctx);
            }
        }
        if (placeTree(region, up, *tree, r, true)) {
            ++placed;
            ++stats().trees;
        }
    }
    return placed;
}

int replaceAzaleaLogs(BlockSource& region, std::vector<BlockPos> const& positions) {
    auto pal = palette();
    if (!pal || !pal->azaleaLog[1]) return 0;
    int n = 0;
    for (auto const& p : positions) {
        auto const& b = region.getBlock(p);
        if (b.getTypeName() != "minecraft:oak_log") continue;
        BlockChangeContext ctx{};
        if (region.setBlock(p, *pal->azaleaLog[axisOf(b)], 3, nullptr, ctx)) ++n;
    }
    return n;
}

int replaceAzaleaTreesInChunk(BlockSource& region, int cx, int cz) {
    auto const& c = config();
    if (!c.enableAzaleaWood || !c.replaceAzaleaTreeLog) return 0;
    auto pal = palette();
    if (!pal || !pal->azaleaLog[1]) return 0;
    auto isOak     = [](Block const& b) { return b.getTypeName() == "minecraft:oak_log"; };
    auto isAzLeaf  = [](Block const& b) {
        auto const& n = b.getTypeName();
        return n == "minecraft:azalea_leaves" || n == "minecraft:azalea_leaves_flowered";
    };
    std::set<std::tuple<int, int, int>> done;
    int                                 replaced = 0;
    for (int bx = 0; bx < 16; ++bx) {
        for (int bz = 0; bz < 16; ++bz) {
            int      x = (cx << 4) + bx, z = (cz << 4) + bz;
            BlockPos top = region.getHeightmapPos(BlockPos{x, 0, z});
            // walk down through the canopy to the first oak log
            BlockPos p{x, top.y - 1, z};
            int      steps = 0;
            while (steps++ < 12 && p.y > region.getMinHeight()) {
                auto const& b = region.getBlock(p);
                if (isOak(b) || !(b.isAir() || isAnyLeaves(b))) break;
                --p.y;
            }
            if (!isOak(region.getBlock(p)) || done.contains({p.x, p.y, p.z})) continue;
            // the trunk stands on rooted dirt (Bedrock's azalea tree feature) under azalea leaves
            BlockPos base = p;
            while (base.y > region.getMinHeight() && isOak(region.getBlock(BlockPos{base.x, base.y - 1, base.z}))) --base.y;
            if (region.getBlock(BlockPos{base.x, base.y - 1, base.z}).getTypeName() != "minecraft:dirt_with_roots") continue;
            bool leafy = false;
            for (int dx = -1; dx <= 1 && !leafy; ++dx)
                for (int dz = -1; dz <= 1 && !leafy; ++dz)
                    for (int dy = 0; dy <= 2 && !leafy; ++dy)
                        if (isAzLeaf(region.getBlock(BlockPos{p.x + dx, p.y + dy, p.z + dz}))) leafy = true;
            if (!leafy) continue;
            // flood the connected oak logs of this tree (26-neighbourhood, near the trunk)
            std::vector<BlockPos> todo{base}, logs;
            std::set<std::tuple<int, int, int>> seen{{base.x, base.y, base.z}};
            while (!todo.empty()) {
                BlockPos q = todo.back();
                todo.pop_back();
                if (!isOak(region.getBlock(q))) continue;
                logs.push_back(q);
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dz = -1; dz <= 1; ++dz) {
                            BlockPos n{q.x + dx, q.y + dy, q.z + dz};
                            if (std::abs(n.x - base.x) > 4 || std::abs(n.z - base.z) > 4 || n.y < base.y || n.y > base.y + 10) continue;
                            if (seen.insert({n.x, n.y, n.z}).second) todo.push_back(n);
                        }
            }
            for (auto const& q : logs) {
                done.insert({q.x, q.y, q.z});
                auto const&        b = region.getBlock(q);
                BlockChangeContext ctx{};
                if (region.setBlock(q, *pal->azaleaLog[axisOf(b)], 2, nullptr, ctx)) ++replaced;
            }
        }
    }
    stats().azaleaLogs += replaced;
    return replaced;
}

std::string genStats() {
    auto& s = stats();
    return "chunks=" + std::to_string(s.chunks.load()) + " rolls=" + std::to_string(s.rolls.load()) + " trees="
         + std::to_string(s.trees.load()) + " azalea_logs=" + std::to_string(s.azaleaLogs.load());
}

namespace {

std::atomic<bool> gGenInstalled = false;

void postProcess(BlockSource& region, LevelChunk& lc, std::string_view dim) {
    if (!gGenInstalled) return;
    try {
        auto const& cp = lc.mPosition.get();
        ++stats().chunks;
        uint64_t seed = region.getLevelSeed64().mValue;
        generateBlossomChunk(region, cp.x, cp.z, dim, seed);
        if (dim == "minecraft:overworld") replaceAzaleaTreesInChunk(region, cp.x, cp.z);
    } catch (std::exception const& e) {
        log().error("newwoods: chunk generation failed: {}", e.what());
    } catch (...) {
        log().error("newwoods: chunk generation failed");
    }
}

LL_TYPE_INSTANCE_HOOK(
    OverworldGenHook,
    ll::memory::HookPriority::Normal,
    OverworldGeneratorMultinoise,
    &OverworldGeneratorMultinoise::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  source,
    Random&       random
) {
    origin(biome, lc, source, random);
    postProcess(source, lc, "minecraft:overworld");
}

LL_TYPE_INSTANCE_HOOK(
    NetherGenHook,
    ll::memory::HookPriority::Normal,
    NetherGenerator,
    &NetherGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  source,
    Random&       random
) {
    origin(biome, lc, source, random);
    postProcess(source, lc, "minecraft:the_nether");
}

LL_TYPE_INSTANCE_HOOK(
    EndGenHook,
    ll::memory::HookPriority::Normal,
    TheEndGenerator,
    &TheEndGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  source,
    Random&       random
) {
    origin(biome, lc, source, random);
    postProcess(source, lc, "minecraft:the_end");
}

/// Bone meal on an azalea: the oak logs of the grown tree become azalea logs.
LL_TYPE_INSTANCE_HOOK(
    AzaleaGrowHook,
    ll::memory::HookPriority::Normal,
    AzaleaBlock,
    &AzaleaBlock::$onFertilized,
    bool,
    BlockSource&    region,
    BlockPos const& pos,
    Actor*          actor,
    FertilizerType  fType
) {
    auto const& c = config();
    if (region.getLevel().isClientSide() || !c.enableAzaleaWood || !c.replaceAzaleaTreeLog) return origin(region, pos, actor, fType);
    std::set<std::tuple<int, int, int>> before;
    auto box = [&](auto&& fn) {
        for (int dx = -4; dx <= 4; ++dx)
            for (int dy = 0; dy <= 9; ++dy)
                for (int dz = -4; dz <= 4; ++dz) fn(BlockPos{pos.x + dx, pos.y + dy, pos.z + dz});
    };
    box([&](BlockPos const& p) {
        if (region.getBlock(p).getTypeName() == "minecraft:oak_log") before.insert({p.x, p.y, p.z});
    });
    bool r = origin(region, pos, actor, fType);
    std::vector<BlockPos> grown;
    box([&](BlockPos const& p) {
        if (!before.contains({p.x, p.y, p.z}) && region.getBlock(p).getTypeName() == "minecraft:oak_log") grown.push_back(p);
    });
    if (!grown.empty()) {
        int n = replaceAzaleaLogs(region, grown);
        log().info("newwoods: an azalea tree grew with {} azalea logs at {} {} {}", n, pos.x, pos.y, pos.z);
    }
    return r;
}

} // namespace

void installWorldGenHooks() {
    gGenInstalled = true;
    OverworldGenHook::hook();
    NetherGenHook::hook();
    EndGenHook::hook();
    AzaleaGrowHook::hook();
}
void uninstallWorldGenHooks() {
    gGenInstalled = false;
    OverworldGenHook::unhook();
    NetherGenHook::unhook();
    EndGenHook::unhook();
    AzaleaGrowHook::unhook();
}

} // namespace lp::feature::newwoods
