#include "feature/worldsurface/Gen.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/material/Material.h"

namespace lp::feature::worldsurface::gen {

Stats& stats() {
    static Stats s;
    return s;
}

namespace {

/// FacingID (0 down, 1 up, 2 north, 3 south, 4 west, 5 east) of a Java 2D facing (0 S, 1 W, 2 N, 3 E).
constexpr int kFacingId[4] = {3, 4, 2, 5};

BlockPos at(BlockPos const& p, int dx, int dy, int dz) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }

bool inWorld(BlockSource& r, BlockPos const& p) { return p.y >= r.getMinHeight() && p.y < r.getMaxHeight(); }

Block const& blockAt(BlockSource& r, BlockPos const& p) { return r.getBlock(p); }

bool airAt(BlockSource& r, BlockPos const& p) { return inWorld(r, p) && r.getBlock(p).isAir(); }

/// Java BlockPredicate.solid() / BlockState.isSolid() (legacy "solid" material).
bool solidAt(BlockSource& r, BlockPos const& p) {
    if (!inWorld(r, p)) return false;
    auto const& b = r.getBlock(p);
    if (b.isAir() || isLiquid(b)) return false;
    return r.getMaterial(p).mSolid;
}

std::string biomeAt(BlockSource& r, BlockPos const& p) { return biomeName(r.getBiome(p)); }

// ---- flowers of a biome (Java's FLOWER features) -----------------------------------------------------

std::vector<std::string_view> const& flowersOf(std::string const& biome) {
    static std::vector<std::string_view> const none{};
    static std::vector<std::string_view> const def{"minecraft:poppy", "minecraft:poppy", "minecraft:dandelion"};
    static std::vector<std::string_view> const flowerForest{
        "minecraft:dandelion",
        "minecraft:poppy",
        "minecraft:allium",
        "minecraft:azure_bluet",
        "minecraft:red_tulip",
        "minecraft:orange_tulip",
        "minecraft:white_tulip",
        "minecraft:pink_tulip",
        "minecraft:oxeye_daisy",
        "minecraft:cornflower",
        "minecraft:lily_of_the_valley"
    };
    static std::vector<std::string_view> const plains{
        "minecraft:red_tulip",
        "minecraft:orange_tulip",
        "minecraft:white_tulip",
        "minecraft:pink_tulip",
        "minecraft:poppy",
        "minecraft:azure_bluet",
        "minecraft:oxeye_daisy",
        "minecraft:cornflower",
        "minecraft:dandelion",
        "minecraft:dandelion"
    };
    static std::vector<std::string_view> const meadow{
        "minecraft:allium",
        "minecraft:poppy",
        "minecraft:azure_bluet",
        "minecraft:dandelion",
        "minecraft:cornflower",
        "minecraft:oxeye_daisy"
    };
    static std::vector<std::string_view> const swamp{"minecraft:blue_orchid"};
    static std::vector<std::string_view> const cherry{"minecraft:pink_petals"};
    if (biome == "flower_forest") return flowerForest;
    if (biome == "plains" || biome == "sunflower_plains") return plains;
    if (biome == "meadow") return meadow;
    if (biome == "swampland" || biome == "swamp") return swamp;
    if (biome == "cherry_grove") return cherry;
    // biomes without a flower feature in Java
    static std::unordered_set<std::string> const bare{
        "grove", "ice_plains", "ice_plains_spikes", "snowy_plains", "ice_spikes", "badlands", "eroded_badlands",
        "snowy_beach", "stony_shore", "mushroom_fields", "snowy_slopes", "jagged_peaks", "frozen_peaks", "stony_peaks",
        "desert", "mesa", "mesa_bryce", "beach", "cold_beach", "stone_beach", "mushroom_island", "deep_dark",
        "dripstone_caves", "lush_caves", "river", "frozen_river", "ocean", "deep_ocean", "warm_ocean",
        "lukewarm_ocean", "deep_lukewarm_ocean", "cold_ocean", "deep_cold_ocean", "frozen_ocean", "deep_frozen_ocean",
        "mangrove_swamp",
    };
    if (bare.contains(biome)) return none;
    return def;
}

} // namespace

// ---- FairyRings ------------------------------------------------------------------------------------------

void fairyRingsChunk(Ctx& c, int x0, int z0) {
    auto& rng = c.rng;
    int   x   = x0 + rng.nextInt(16);
    int   z   = z0 + rng.nextInt(16);
    BlockPos center{x, 128, z};
    if (!inWorld(c.region, center)) return;
    auto const& biome  = biomeAt(c.region, center);
    double      chance = 0;
    if (c.st.forestBiomes.contains(biome)) chance = c.st.config.forestChance;
    else if (c.st.plainsBiomes.contains(biome)) chance = c.st.config.plainsChance;
    if (rng.nextDouble() < chance) {
        BlockPos pos = center;
        while (!isDirt(blockAt(c.region, pos)) && pos.y > 30) pos.y--;
        if (isDirt(blockAt(c.region, pos))) {
            spawnFairyRing(c, at(pos, 0, -1, 0));
            stats().fairyRings++;
        }
    }
}

std::optional<BlockPos> spawnFairyRing(Ctx& c, BlockPos const& pos, Block const* flower) {
    auto&       rng = c.rng;
    auto&       r   = c.region;
    Block const* flowerState = flower;
    if (!flowerState) {
        auto const& list = flowersOf(biomeAt(r, pos));
        if (list.empty()) {
            auto it     = c.st.flowers.find("minecraft:oxeye_daisy");
            flowerState = it == c.st.flowers.end() ? nullptr : it->second;
        } else {
            auto it     = c.st.flowers.find(std::string(list[rng.nextInt(static_cast<int>(list.size()))]));
            flowerState = it == c.st.flowers.end() ? nullptr : it->second;
        }
    }

    for (int xo = -3; xo <= 3; ++xo) {
        for (int zo = -3; zo <= 3; ++zo) {
            int dist = xo * xo + zo * zo;
            if (dist < 7 || dist > 10) {
                for (int yo = 6; yo > -3; --yo) {
                    BlockPos fp = at(pos, xo, yo, zo);
                    if (!inWorld(r, fp)) continue;
                    if (isSmallFlower(blockAt(r, fp))) {
                        if (c.st.air) genSet(r, fp, *c.st.air);
                        break;
                    }
                }
            } else {
                for (int yo = 5; yo > -4; --yo) {
                    BlockPos fp = at(pos, xo, yo, zo);
                    BlockPos up = at(fp, 0, 1, 0);
                    if (!inWorld(r, up)) continue;
                    if (isDirt(blockAt(r, fp)) && airAt(r, up)) {
                        if (flowerState) genSet(r, up, *flowerState);
                        break;
                    }
                }
            }
        }
    }
    BlockPos orePos = at(pos, 0, -(rng.nextInt(10) + 25), 0);
    int      down   = 0;
    while (inWorld(r, orePos) && !isStone(blockAt(r, orePos)) && down < 10) {
        orePos.y--;
        down++;
    }
    if (!inWorld(r, orePos) || !isStone(blockAt(r, orePos)) || c.st.ores.empty()) return std::nullopt;
    Block const* ore = c.st.ores[rng.nextInt(static_cast<int>(c.st.ores.size()))];
    genSet(r, orePos, *ore);
    static constexpr int faces[6][3] = {
        {0, -1, 0},
        {0, 1,  0},
        {0, 0,  -1},
        {0, 0,  1},
        {-1, 0, 0},
        {1, 0,  0}
    };
    for (auto const& f : faces)
        if (rng.nextBool()) genSet(r, at(orePos, f[0], f[1], f[2]), *ore);
    return orePos;
}

// ---- FallenLogs ------------------------------------------------------------------------------------------

namespace {
enum class Decor { Moss, Vine, Fern };

std::vector<Decor> decorFor(Biome const& biome) {
    double t = std::clamp(static_cast<double>(biome.mTemperature), 0.0, 1.0);
    double h = std::clamp(static_cast<double>(biome.mDownfall), 0.0, 1.0) * t;
    std::vector<Decor> out;
    if (t < 0.3) out.push_back(Decor::Fern);
    if (h > 0.5) out.push_back(Decor::Moss);
    if (h > 0.75) out.push_back(Decor::Vine);
    return out;
}

// side: Java direction from the log to the decoration (facing index 0 S, 1 W, 2 N, 3 E)
void placeDecor(Ctx& c, int side, BlockPos const& sidePos, std::vector<Decor> const& decor) {
    if (decor.empty()) return;
    auto& r = c.region;
    if (!airAt(r, sidePos)) return;
    Decor        d     = decor[c.rng.nextInt(static_cast<int>(decor.size()))];
    Block const* place = nullptr;
    BlockPos     below = at(sidePos, 0, -1, 0);
    bool         ok    = false;
    switch (d) {
    case Decor::Moss:
        place = c.st.mossCarpet;
        // moss carpet survives on any non-air block; Quark also wants a solid one (not water)
        ok = inWorld(r, below) && !blockAt(r, below).isAir() && solidAt(r, below);
        break;
    case Decor::Vine:
        // attached to the log, which lies opposite to `side`
        place = c.st.vine[(side + 2) & 3];
        {
            auto [ax, az] = facingStep((side + 2) & 3);
            BlockPos att  = at(sidePos, ax, 0, az);
            ok            = inWorld(r, att) && sturdy(blockAt(r, att), kFacingId[side & 3]);
        }
        break;
    case Decor::Fern:
        place = c.st.fern;
        ok    = inWorld(r, below) && (isDirt(blockAt(r, below)) || nameOf(blockAt(r, below)) == "minecraft:farmland");
        break;
    }
    if (place && ok) genSet(r, sidePos, *place);
}

bool positionValid(Ctx& c, BlockPos const& pos, int len, int dir) {
    auto& r              = c.region;
    bool  canSpawnOnWater = c.rng.nextFloat() < c.st.config.onWaterChance;
    auto [dx, dz]         = facingStep(dir);
    for (int i = 0; i < len; ++i) {
        BlockPos p = at(pos, dx * i, 0, dz * i);
        if (!inWorld(r, p)) return false;
        auto const& b = blockAt(r, p);
        if (!b.isAir() && !replaceable(r, p, b) && !isFlower(b)) return false;
        BlockPos    below = at(p, 0, -1, 0);
        auto const& bb    = blockAt(r, below);
        if (canSpawnOnWater && isWater(bb)) continue;
        if (!isDirt(bb)) return false;
    }
    return true;
}
} // namespace

void fallenLogsChunk(Ctx& c, int x0, int z0) {
    auto& rng = c.rng;
    int   x   = x0 + rng.nextInt(16);
    int   z   = z0 + rng.nextInt(16);
    BlockPos center{x, 128, z};
    if (!inWorld(c.region, center)) return;
    auto const& biome  = biomeAt(c.region, center);
    int         chance = c.st.reducedBiomes.contains(biome) ? c.st.config.sparseBiomeRarity : c.st.config.rarity;
    if (rng.nextInt(std::max(chance, 1)) == 0) {
        // Java Heightmap OCEAN_FLOOR_WG: the first motion-blocking block from the top, water and
        // leaves excluded
        short    top = c.region.getAboveTopSolidBlock(x, z, false, false);
        BlockPos pos{x, top, z};
        if (placeFallenLog(c, pos)) stats().fallenLogs++;
    }
}

bool placeFallenLog(Ctx& c, BlockPos const& pos, GenState::LogEntry const* forced) {
    auto& r   = c.region;
    auto& rng = c.rng;
    if (!inWorld(r, pos)) return false;
    GenState::LogEntry const* entry = forced;
    if (!entry) {
        auto const&                            biome = biomeAt(r, pos);
        std::vector<GenState::LogEntry const*> matched;
        for (auto const& e : c.st.logs)
            if (e.biomes.contains(biome)) matched.push_back(&e);
        if (matched.empty()) return false;
        entry = matched[rng.nextInt(static_cast<int>(matched.size()))];
    }
    bool hollow = entry->hollowAxis[0] && entry->hollowAxis[1] && rng.nextFloat() < c.st.config.hollowChance;
    auto const& axis = hollow ? entry->hollowAxis : entry->axis;
    if (!axis[0] || !axis[1]) return false;

    for (int attempt = 0; attempt < 5; ++attempt) {
        int dir = rng.nextInt(4); // Java HORIZONTALS: the facing index is our own order
        int len = 3 + rng.nextInt(2);
        if (!positionValid(c, pos, len, dir)) continue;
        Block const& state = *axis[(dir & 1) ? 0 : 1]; // west/east -> x axis, south/north -> z axis
        auto         decor = decorFor(r.getBiome(pos));
        bool         moss  = std::find(decor.begin(), decor.end(), Decor::Moss) != decor.end();
        auto [dx, dz]      = facingStep(dir);
        for (int i = 0; i < len; ++i) {
            BlockPos p = at(pos, dx * i, 0, dz * i);
            genSet(r, p, state);
            if (rng.nextInt(10) < 7 && moss && c.st.mossCarpet) {
                BlockPos above = at(p, 0, 1, 0);
                if (airAt(r, above)) genSet(r, above, *c.st.mossCarpet);
            }
            // the two sides of the log
            int sides[2] = {(dir + 1) & 3, (dir + 3) & 3};
            for (int side : sides) {
                if (rng.nextInt(5) < 3) {
                    auto [sx, sz] = facingStep(side);
                    placeDecor(c, side, at(p, sx, 0, sz), decor);
                }
            }
            if (rng.nextInt(10) < 4) placeDecor(c, dir, at(pos, -dx, 0, -dz), decor);
            if (rng.nextInt(10) < 4) placeDecor(c, (dir + 2) & 3, at(pos, dx * len, 0, dz * len), decor);
        }
        return true;
    }
    return false;
}

// ---- MonsterBox ------------------------------------------------------------------------------------------

bool monsterBoxCanPlace(Ctx& c, BlockPos const& pos) {
    auto& r = c.region;
    if (!inWorld(r, pos)) return false;
    auto const& b = blockAt(r, pos);
    if (!replaceable(r, pos, b) || isLiquid(b)) return false;
    BlockPos below = at(pos, 0, -1, 0);
    if (!inWorld(r, below)) return false;
    auto const& bb = blockAt(r, below);
    return isStoneBased(bb) && sturdy(bb, 1);
}

void monsterBoxChunk(Ctx& c, int x0, int z0) {
    if (!c.st.monsterBox) return;
    auto&  rng    = c.rng;
    auto&  cf     = c.st.config;
    double chance = cf.monsterBoxChancePerChunk;
    while (chance > 0 && rng.nextDouble() <= chance) {
        int      x = x0 + rng.nextInt(16);
        int      y = rng.nextInt(cf.minY, cf.maxY);
        int      z = z0 + rng.nextInt(16);
        BlockPos pos{x, y, z};
        for (int moves = 0; moves < cf.searchRange && pos.y > cf.minY; ++moves) {
            if (monsterBoxCanPlace(c, pos)) {
                genSet(c.region, pos, *c.st.monsterBox);
                if (c.boxes) c.boxes->push_back(pos);
                stats().monsterBoxes++;
                break;
            }
            pos.y--;
        }
        chance -= 1;
    }
}

// ---- CherryGroveWaterPetals ----------------------------------------------------------------------------

void petalsChunk(Ctx& c, int x0, int z0) {
    auto& rng = c.rng;
    auto& cf  = c.st.config;
    if (rng.nextFloat() < cf.cherryGroveWaterPetalsChancePerChunk) {
        for (int i = 0; i < cf.triesPerChunk; ++i) {
            BlockPos pos{x0 + rng.nextInt(16), 70, z0 + rng.nextInt(16)};
            if (!inWorld(c.region, pos)) continue;
            if (!c.st.petalBiomes.allows(biomeAt(c.region, pos))) continue;
            while (pos.y > 10) {
                if (isWater(blockAt(c.region, at(pos, 0, -1, 0)))) {
                    placePetals(c, pos);
                    stats().petalPatches++;
                    break;
                }
                pos.y--;
            }
        }
    }
}

void placePetals(Ctx& c, BlockPos const& corner) {
    auto& r   = c.region;
    auto& rng = c.rng;
    auto& cf  = c.st.config;
    bool  carpet = cf.useCarpet;
    if (carpet && !c.st.cherryCarpet) return;
    if (!carpet && !c.st.petals[4][0]) return;

    std::vector<BlockPos> core{corner};
    int                   xs = cf.size, zs = cf.size;
    if (cf.sizeVariation > 0) {
        int sizes[3] = {cf.size - cf.sizeVariation, cf.size, cf.size + cf.sizeVariation};
        xs           = sizes[rng.between(0, 2)];
        zs           = sizes[rng.between(0, 2)];
    }
    for (int l = 0; l < xs; ++l)
        for (int w = 0; w < zs; ++w) core.push_back(at(corner, l, 0, w));

    auto placeable = [&](BlockPos const& p) {
        return inWorld(r, p) && airAt(r, p) && isWater(blockAt(r, at(p, 0, -1, 0)));
    };
    if (carpet) {
        for (auto const& p : core)
            if (placeable(p)) genSet(r, p, *c.st.cherryCarpet);
        return;
    }
    for (auto const& p : core)
        if (placeable(p)) {
            auto const* b = c.st.petals[4][rng.between(0, 3)];
            if (b) genSet(r, p, *b);
        }
    // borders: air cells next to the core, per direction (north, east, south, west)
    static constexpr int dirs[4] = {2, 3, 0, 1};
    for (int d : dirs) {
        auto [dx, dz] = facingStep(d);
        std::vector<BlockPos> border;
        for (auto const& p : core) {
            BlockPos n = at(p, dx, 0, dz);
            if (airAt(r, n)) border.push_back(n);
        }
        int ccw = (d + 3) & 3;
        for (auto const& p : border) {
            if (!isWater(blockAt(r, at(p, 0, -1, 0)))) continue;
            Block const* b = nullptr;
            if (rng.nextFloat() < cf.edgeVariation) b = c.st.petals[rng.nextBool() ? 1 : 3][ccw];
            else b = c.st.petals[2][ccw];
            if (b) genSet(r, p, *b);
        }
    }
}

// ---- GlimmeringWeald -------------------------------------------------------------------------------------

bool placeHugeGlowShroom(BlockSource& r, GenState const& st, Rng& rng, BlockPos const& pos) {
    if (!st.shroomCap || !st.shroomStem) return false;
    BlockPos below = at(pos, 0, -1, 0);
    if (!inWorld(r, below) || nameOf(blockAt(r, below)) != "minecraft:deepslate") return false;
    constexpr int baseHeight = 2, heightRand = 4, capRadius = 1;
    int           stem1      = baseHeight;
    int           stem2      = rng.nextInt(heightRand);
    bool          bigCap     = rng.nextDouble() < 0.6;
    int           stemHeight = stem1 + stem2;
    int           total      = stemHeight + (bigCap ? 2 : 1);
    for (int k = 1; k < total; ++k) {
        int h = k < stemHeight ? 2 : capRadius + 1;
        for (int i = -h; i <= h; ++i)
            for (int j = -h; j <= h; ++j)
                if (!airAt(r, at(pos, i, k, j))) return false;
    }
    BlockPos p = pos;
    for (int i = 0; i < stem1; ++i) {
        genSet(r, p, *st.shroomStem);
        p.y++;
    }
    if (stem2 > 0) {
        auto [dx, dz] = facingStep(rng.nextInt(4));
        p             = at(p, dx, 0, dz);
    }
    for (int i = 0; i < stem2; ++i) {
        genSet(r, p, *st.shroomStem);
        p.y++;
    }
    int ringHeight = std::min(2, stem2);
    for (int i = 0; i < ringHeight; ++i) {
        for (int f = 0; f < 4; ++f) {
            auto [dx, dz] = facingStep(f);
            if (st.ring[f]) genSet(r, at(p, dx, -(i + 1), dz), *st.ring[f]);
        }
    }
    for (int i = -capRadius; i <= capRadius; ++i)
        for (int j = -capRadius; j <= capRadius; ++j) genSet(r, at(p, i, 0, j), *st.shroomCap);
    if (bigCap) genSet(r, at(p, 0, 1, 0), *st.shroomCap);
    stats().hugeShrooms++;
    return true;
}

void glowShroomAt(Ctx& c, BlockPos const& pos) {
    if (!placeHugeGlowShroom(c.region, c.st, c.rng, pos) && c.st.glowShroom) {
        genSet(c.region, pos, *c.st.glowShroom);
        stats().glowShrooms++;
    }
}

void glowExtrasAt(Ctx& c, BlockPos const& pos) {
    auto& r   = c.region;
    auto& rng = c.rng;
    // FacingID order: down, up, north, south, west, east
    static constexpr int dirs[6][3] = {
        {0, -1, 0},
        {0, 1,  0},
        {0, 0,  -1},
        {0, 0,  1},
        {-1, 0, 0},
        {1, 0,  0}
    };
    auto deepslate = [&](BlockPos const& p) { return inWorld(r, p) && nameOf(blockAt(r, p)) == "minecraft:deepslate"; };
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j)
            for (int k = -6; k <= 6; ++k) {
                BlockPos p = at(pos, i, k, j);
                if (!(rng.nextFloat() < 0.5f) || !airAt(r, p)) continue;
                double res = rng.nextDouble();
                if (res > 0.85) {
                    if (deepslate(at(p, 0, -1, 0)) && c.st.lichenGrowth) genSet(r, p, *c.st.lichenGrowth);
                } else if (res > 0.35) {
                    for (int f = 0; f < 6; ++f) {
                        if (deepslate(at(p, dirs[f][0], dirs[f][1], dirs[f][2]))) {
                            if (c.st.glowLichen[f]) genSet(r, p, *c.st.glowLichen[f]);
                            break;
                        }
                    }
                }
            }
}

void lapisVeinAt(Ctx& c, BlockPos const& pos) {
    // Java OreFeature (size 7): blobs along a short random segment, replacing stone / deepslate
    auto&  r    = c.region;
    auto&  rng  = c.rng;
    int    size = 7;
    double ang  = rng.nextDouble() * 3.14159265358979;
    double len  = size / 8.0;
    double x1 = pos.x + 0.5 + std::sin(ang) * len, x2 = pos.x + 0.5 - std::sin(ang) * len;
    double z1 = pos.z + 0.5 + std::cos(ang) * len, z2 = pos.z + 0.5 - std::cos(ang) * len;
    double y1 = pos.y + rng.nextInt(3) - 2, y2 = pos.y + rng.nextInt(3) - 2;
    bool   any = false;
    for (int s = 0; s < size; ++s) {
        double t  = static_cast<double>(s) / size;
        double cx = x1 + (x2 - x1) * t, cy = y1 + (y2 - y1) * t, cz = z1 + (z2 - z1) * t;
        double rr = rng.nextDouble() * size / 16.0;
        double rad = ((std::sin(3.14159265358979 * t) + 1.0) * rr + 1.0) / 2.0;
        for (int x = static_cast<int>(std::floor(cx - rad)); x <= static_cast<int>(std::floor(cx + rad)); ++x)
            for (int y = static_cast<int>(std::floor(cy - rad)); y <= static_cast<int>(std::floor(cy + rad)); ++y)
                for (int z = static_cast<int>(std::floor(cz - rad)); z <= static_cast<int>(std::floor(cz + rad)); ++z) {
                    double ddx = (x + 0.5 - cx) / rad, ddy = (y + 0.5 - cy) / rad, ddz = (z + 0.5 - cz) / rad;
                    if (ddx * ddx + ddy * ddy + ddz * ddz >= 1.0) continue;
                    BlockPos p{x, y, z};
                    if (!inWorld(r, p)) continue;
                    auto const& n = nameOf(blockAt(r, p));
                    if (n == "minecraft:stone" || n == "minecraft:granite" || n == "minecraft:diorite"
                        || n == "minecraft:andesite" || n == "minecraft:tuff") {
                        if (c.st.lapisOre) any |= genSet(r, p, *c.st.lapisOre);
                    } else if (n == "minecraft:deepslate") {
                        if (c.st.deepslateLapisOre) any |= genSet(r, p, *c.st.deepslateLapisOre);
                    }
                }
    }
    if (any) stats().lapisVeins++;
}

void glimmeringChunk(Ctx& c, int x0, int z0) {
    if (!c.isGlimmering) return;
    auto& r      = c.region;
    auto& rng    = c.rng;
    int   bottom = r.getMinHeight();
    int   top    = std::min<int>(256, r.getMaxHeight() - 1);
    bool  any    = false;

    // glow_shrooms: count 125 x 5, in_square, uniform(bottom, 256), scan down 12 for solid below air, +1 y
    for (int n = 0; n < 125 * 5; ++n) {
        int      x = x0 + rng.nextInt(16), z = z0 + rng.nextInt(16);
        int      y = rng.between(bottom, top);
        BlockPos p{x, y, z};
        // EnvironmentScanPlacement(down, solid, only in air, 12): start in air, walk down through air
        if (!airAt(r, p)) continue;
        std::optional<BlockPos> found;
        for (int s = 0; s < 12; ++s) {
            p.y--;
            if (!inWorld(r, p)) break;
            if (!airAt(r, p)) {
                if (solidAt(r, p)) found = p;
                break;
            }
        }
        if (!found) continue;
        BlockPos origin = at(*found, 0, 1, 0);
        if (!inWorld(r, origin) || !c.isGlimmering(origin)) continue;
        any = true;
        glowShroomAt(c, origin);
    }
    // glow_shrooms_extras: count 200, in_square, uniform(bottom, 256), +1 y
    for (int n = 0; n < 200; ++n) {
        BlockPos p{x0 + rng.nextInt(16), rng.between(bottom, top) + 1, z0 + rng.nextInt(16)};
        if (!inWorld(r, p) || !c.isGlimmering(p)) continue;
        any = true;
        glowExtrasAt(c, p);
    }
    // ore_lapis_extra: count 12, in_square, uniform(-64, 0)
    for (int n = 0; n < 12; ++n) {
        BlockPos p{x0 + rng.nextInt(16), rng.between(std::max(-64, bottom), 0), z0 + rng.nextInt(16)};
        if (!inWorld(r, p) || !c.isGlimmering(p)) continue;
        any = true;
        lapisVeinAt(c, p);
    }
    if (any) stats().glimmeringChunks++;
}

} // namespace lp::feature::worldsurface::gen
