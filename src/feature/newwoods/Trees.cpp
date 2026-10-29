// U7 newwoods trees (docs/spec/newwoods.md §3.4): Java's TreeFeature with Quark's ashen tree
// (straight trunk with several fancy leaf blobs and a leaf cap) and the trumpet tree (Java's fancy
// trunk and foliage placers, the big oak shape). Written from the behaviour of those classes.

#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "feature/newwoods/NewWoods.h"
#include "feature/newwoods/Palette.h"
#include "game/BlockStates.h"

namespace lp::feature::newwoods {

namespace bs = game::blockstates;

// ---- block classes --------------------------------------------------------------------------------------

bool isOurLeaves(Block const& b) { return b.getTypeName().starts_with("lepton:") && hasTag(b, "lepton:nw_leaves"); }

bool isAnyLeaves(Block const& b) {
    auto const& n = b.getTypeName();
    return n.find("leaves") != std::string::npos || isOurLeaves(b);
}

bool isLogLike(Block const& b) {
    auto const& n = b.getTypeName();
    if (n.ends_with("_log") || n.ends_with("_wood") || n.ends_with("_hyphae")) return true;
    if (n.ends_with("_stem"))
        return n != "minecraft:mushroom_stem" && n != "minecraft:melon_stem" && n != "minecraft:pumpkin_stem";
    return hasTag(b, "log");
}

namespace {

/// Java #replaceable_by_trees (Bedrock names).
bool replaceableByTrees(Block const& b) {
    static std::unordered_set<std::string> const kIds{
        "minecraft:short_grass", "minecraft:tall_grass", "minecraft:fern", "minecraft:large_fern", "minecraft:deadbush",
        "minecraft:vine", "minecraft:glow_lichen", "minecraft:sunflower", "minecraft:lilac", "minecraft:rose_bush",
        "minecraft:peony", "minecraft:hanging_roots", "minecraft:pitcher_plant", "minecraft:water", "minecraft:flowing_water",
        "minecraft:seagrass", "minecraft:warped_roots", "minecraft:nether_sprouts", "minecraft:crimson_roots",
        // added to the tag by later Java versions; they exist in this Bedrock version
        "minecraft:short_dry_grass", "minecraft:tall_dry_grass", "minecraft:bush", "minecraft:firefly_bush",
        "minecraft:leaf_litter"};
    return kIds.contains(b.getTypeName());
}

/// Java TreeFeature#validTreePos: air, leaves or a plant trees replace.
bool validTreePos(Block const& b) { return b.isAir() || isAnyLeaves(b) || replaceableByTrees(b); }

/// Java #dirt without grass / mycelium (TrunkPlacer#setDirtAt keeps these).
bool keepsAsDirt(Block const& b) {
    static std::unordered_set<std::string> const kIds{
        "minecraft:dirt", "minecraft:coarse_dirt", "minecraft:podzol", "minecraft:dirt_with_roots", "minecraft:moss_block",
        "minecraft:mud", "minecraft:muddy_mangrove_roots"};
    return kIds.contains(b.getTypeName());
}

struct Placer {
    BlockSource&   region;
    Palette const& pal;
    TreeType const& tree;
    bool           worldgen;
    int            minY, maxY;
    std::vector<BlockPos> logs;

    bool loaded(BlockPos const& p) const { return p.y >= minY && p.y < maxY && region.hasChunksAt(p, 0, false); }
    Block const* at(BlockPos const& p) const { return loaded(p) ? &region.getBlock(p) : nullptr; }
    void set(BlockPos const& p, Block const& b) {
        BlockChangeContext ctx{};
        region.setBlock(p, b, worldgen ? 2 : 3, nullptr, ctx);
    }
    bool validPos(BlockPos const& p) const {
        auto const* b = at(p);
        return b && validTreePos(*b);
    }
    /// TrunkPlacer#isFree: validTreePos or a log.
    bool isFree(BlockPos const& p) const {
        auto const* b = at(p);
        return b && (validTreePos(*b) || isLogLike(*b));
    }
    void placeLog(BlockPos const& p, int axis) {
        if (!validPos(p)) return;
        if (auto const* log = pal.log(tree.log, axis)) {
            set(p, *log);
            logs.push_back(p);
        }
    }
    void placeLeaf(BlockPos const& p) {
        if (!validPos(p)) return;
        if (auto const* leaf = pal.generatedLeaves(tree.leaves)) set(p, *leaf);
    }
    /// TrunkPlacer#setDirtAt (force_dirt false).
    void setDirt(BlockPos const& p) {
        auto const* b = at(p);
        if (!b || keepsAsDirt(*b) || !pal.dirt) return;
        set(p, *pal.dirt);
    }
    /// FoliagePlacer#placeLeavesRow with FancyFoliagePlacer#shouldSkipLocation.
    void leavesRow(BlockPos const& center, int range, int localY) {
        for (int dx = -range; dx <= range; ++dx) {
            for (int dz = -range; dz <= range; ++dz) {
                float fx = static_cast<float>(std::abs(dx)) + 0.5f, fz = static_cast<float>(std::abs(dz)) + 0.5f;
                if (fx * fx + fz * fz > static_cast<float>(range * range)) continue;
                placeLeaf(BlockPos{center.x + dx, center.y + localY, center.z + dz});
            }
        }
    }
    /// FancyFoliagePlacer#createFoliage: rows offset .. offset - height; the first and last row use
    /// the radius, the rows between radius + 1.
    void fancyFoliage(BlockPos const& at, int foliageHeight, int radius, int offset) {
        for (int i = offset; i >= offset - foliageHeight; --i) {
            int r = radius + ((i != offset && i != offset - foliageHeight) ? 1 : 0);
            leavesRow(at, r, i);
        }
    }
};

// ---- the fancy trunk (Java FancyTrunkPlacer) -----------------------------------------------------

constexpr int kAxisX = 0, kAxisY = 1, kAxisZ = 2;

int stepsOf(BlockPos const& d) { return std::max(std::abs(d.x), std::max(std::abs(d.y), std::abs(d.z))); }

int logAxis(BlockPos const& start, BlockPos const& end) {
    int dx = std::abs(end.x - start.x), dz = std::abs(end.z - start.z);
    int m  = std::max(dx, dz);
    if (m > 0) return dx == m ? kAxisX : kAxisZ;
    return kAxisY;
}

bool makeLimb(Placer& pl, BlockPos const& start, BlockPos const& end, bool place) {
    if (!place && start == end) return true;
    BlockPos d{end.x - start.x, end.y - start.y, end.z - start.z};
    int      n = stepsOf(d);
    if (n == 0) {
        if (place) pl.placeLog(start, kAxisY);
        return true;
    }
    float fx = static_cast<float>(d.x) / static_cast<float>(n);
    float fy = static_cast<float>(d.y) / static_cast<float>(n);
    float fz = static_cast<float>(d.z) / static_cast<float>(n);
    for (int j = 0; j <= n; ++j) {
        BlockPos p{
            start.x + static_cast<int>(std::floor(0.5f + static_cast<float>(j) * fx)),
            start.y + static_cast<int>(std::floor(0.5f + static_cast<float>(j) * fy)),
            start.z + static_cast<int>(std::floor(0.5f + static_cast<float>(j) * fz))};
        if (place) pl.placeLog(p, logAxis(start, p));
        else if (!pl.isFree(p)) return false;
    }
    return true;
}

float treeShape(int height, int y) {
    if (static_cast<float>(y) < static_cast<float>(height) * 0.3f) return -1.0f;
    float half = static_cast<float>(height) / 2.0f;
    float dy   = half - static_cast<float>(y);
    float f    = std::sqrt(half * half - dy * dy);
    if (dy == 0.0f) f = half;
    else if (std::abs(dy) >= half) return 0.0f;
    return f * 0.5f;
}

bool trimBranches(int height, int localY) { return static_cast<double>(localY) >= static_cast<double>(height) * 0.2; }

struct FoliageCoords {
    BlockPos pos;
    int      branchBase;
};

/// FancyTrunkPlacer#placeTrunk; returns the foliage attachment points.
std::vector<BlockPos> fancyTrunk(Placer& pl, JavaRandom& random, int freeHeight, BlockPos const& start) {
    int j = freeHeight + 2;
    int k = static_cast<int>(std::floor(static_cast<double>(j) * 0.618));
    pl.setDirt(BlockPos{start.x, start.y - 1, start.z});
    int l  = std::min(1, static_cast<int>(std::floor(1.382 + std::pow(static_cast<double>(j) / 13.0, 2.0))));
    int i1 = start.y + k;
    int j1 = j - 5;
    std::vector<FoliageCoords> list;
    list.push_back({BlockPos{start.x, start.y + j1, start.z}, i1});
    for (; j1 >= 0; --j1) {
        float f = treeShape(j, j1);
        if (f < 0.0f) continue;
        for (int k1 = 0; k1 < l; ++k1) {
            double d2 = static_cast<double>(f) * (static_cast<double>(random.nextFloat()) + 0.328);
            double d3 = static_cast<double>(random.nextFloat() * 2.0f) * 3.141592653589793;
            double d4 = d2 * std::sin(d3) + 0.5;
            double d5 = d2 * std::cos(d3) + 0.5;
            BlockPos branch{start.x + static_cast<int>(std::floor(d4)), start.y + j1 - 1, start.z + static_cast<int>(std::floor(d5))};
            BlockPos above{branch.x, branch.y + 5, branch.z};
            if (!makeLimb(pl, branch, above, false)) continue;
            int    l1 = start.x - branch.x, i2 = start.z - branch.z;
            double d6 = static_cast<double>(branch.y) - std::sqrt(static_cast<double>(l1 * l1 + i2 * i2)) * 0.381;
            int    j2 = d6 > static_cast<double>(i1) ? i1 : static_cast<int>(d6);
            BlockPos base{start.x, j2, start.z};
            if (makeLimb(pl, base, branch, false)) list.push_back({branch, base.y});
        }
    }
    makeLimb(pl, start, BlockPos{start.x, start.y + k, start.z}, true);
    // makeBranches
    for (auto const& c : list) {
        BlockPos p{start.x, c.branchBase, start.z};
        if (!(p == c.pos) && trimBranches(j, c.branchBase - start.y)) makeLimb(pl, p, c.pos, true);
    }
    std::vector<BlockPos> out;
    for (auto const& c : list)
        if (trimBranches(j, c.branchBase - start.y)) out.push_back(c.pos);
    return out;
}

/// Quark MultiFoliageStraightTrunkPlacer (foliageDistance 5, maxBlobs 3): logs from `freeHeight`
/// down to 0 (freeHeight + 1 logs), an attachment at the top log and every 6 logs below it.
std::vector<BlockPos> ancientTrunk(Placer& pl, int freeHeight, BlockPos const& start) {
    pl.setDirt(BlockPos{start.x, start.y - 1, start.z});
    std::vector<BlockPos> attach;
    int                   placed = 0, j = 0;
    for (int i = freeHeight; i >= 0; --i) {
        BlockPos t{start.x, start.y + i, start.z};
        pl.placeLog(t, kAxisY);
        if (placed < 3) {
            if (j == 0) {
                attach.push_back(t);
                j = 5;
                ++placed;
            } else --j;
        }
    }
    return attach;
}

/// TreeFeature#getMaxFreeTreeHeight with a 0-wide minimum size (only the trunk column).
int maxFreeHeight(Placer& pl, int trunkHeight, BlockPos const& root) {
    for (int i = 0; i <= trunkHeight + 1; ++i) {
        if (!pl.isFree(BlockPos{root.x, root.y + i, root.z})) return i - 2;
    }
    return trunkHeight;
}

} // namespace

bool placeTree(BlockSource& region, BlockPos const& root, TreeType const& tree, JavaRandom& random, bool worldgen) {
    auto pal = palette();
    if (!pal || !pal->log(tree.log, kAxisY) || !pal->generatedLeaves(tree.leaves)) return false;
    Placer pl{region, *pal, tree, worldgen, region.getMinHeight(), region.getMaxHeight(), {}};

    // TreeFeature#doPlace: trunk height, foliage height, foliage radius
    int height, foliageHeight, radius, offset;
    if (tree.ancient) {
        height        = 17 + random.nextInt(5) + random.nextInt(7); // base 17, rand a 4, rand b 6
        foliageHeight = 2;
        radius        = random.nextIntBetween(2, 4);
        offset        = -3; // OffsetFancyFoliagePlacer
    } else {
        height        = 8 + random.nextInt(11) + random.nextInt(11); // fancy trunk: 8, 10, 10
        foliageHeight = 4;
        radius        = 3;
        offset        = 1;
    }
    int top = root.y + height + 1;
    if (root.y < pl.minY + 1 || top > pl.maxY) return false;
    int free = maxFreeHeight(pl, height, root);
    if (free < height && free < 4) return false; // minimum_size min_clipped_height 4

    std::vector<BlockPos> attachments = tree.ancient ? ancientTrunk(pl, free, root) : fancyTrunk(pl, random, free, root);
    for (auto const& a : attachments) pl.fancyFoliage(a, foliageHeight, radius, offset);

    if (tree.ancient && !pl.logs.empty()) {
        // AncientTreeTopperDecorator: leaves above and around the highest log, where it's air
        auto highest = *std::max_element(pl.logs.begin(), pl.logs.end(), [](BlockPos const& a, BlockPos const& b) { return a.y < b.y; });
        BlockPos around[5] = {
            {highest.x, highest.y + 1, highest.z},
            {highest.x + 1, highest.y, highest.z},
            {highest.x - 1, highest.y, highest.z},
            {highest.x, highest.y, highest.z - 1},
            {highest.x, highest.y, highest.z + 1}};
        for (auto const& p : around) {
            auto const* b = pl.at(p);
            if (b && b->isAir()) {
                if (auto const* leaf = pal->generatedLeaves(tree.leaves)) pl.set(p, *leaf);
            }
        }
    }
    return true;
}

} // namespace lp::feature::newwoods
