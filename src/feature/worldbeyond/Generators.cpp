#include "feature/worldbeyond/Generators.h"

#include <atomic>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <vector>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/v1/TheEndGenerator.h"

#include "feature/worldbeyond/Blocks.h"
#include "feature/worldbeyond/Chorus.h"

namespace lp::feature::worldbeyond {

namespace {

std::atomic<int64_t> gChunks{0}, gSpikes{0}, gSpires{0}, gChorus{0}, gSprings{0};

constexpr int kStageSurfaceStructures   = 4;
constexpr int kStageUndergroundDecoration = 7;

/// Java's Direction.values() order: DOWN, UP, NORTH, SOUTH, WEST, EAST.
constexpr int kHorizontal[4][2] = {
    {0,  -1},
    {0,  1 },
    {-1, 0 },
    {1,  0 }
};

int chunkOf(int block) { return block >> 4; }

} // namespace

// ---- GenRegion ---------------------------------------------------------------------------------

GenRegion::GenRegion(BlockSource& source, int centerChunkX, int centerChunkZ, int updateFlags)
: mSource(source),
  mCx(centerChunkX),
  mCz(centerChunkZ),
  mFlags(updateFlags),
  mAir(blocks().air) {}

GenRegion GenRegion::unclipped(BlockSource& source) {
    GenRegion r(source, 0, 0, 3);
    r.mClip = false;
    return r;
}

int GenRegion::dimension() const { return static_cast<int>(mSource.getDimensionId()); }

Block const& GenRegion::get(BlockPos const& pos) const {
    if (pos.y < mSource.getMinHeight() || pos.y >= mSource.getMaxHeight() || !mSource.hasChunksAt(pos, 0, false)) {
        return *mAir;
    }
    return mSource.getBlock(pos);
}

bool GenRegion::isAir(BlockPos const& pos) const { return get(pos).isAir(); }

bool GenRegion::canWrite(BlockPos const& pos) const {
    if (pos.y < mSource.getMinHeight() || pos.y >= mSource.getMaxHeight()) return false;
    if (mClip && (std::abs(chunkOf(pos.x) - mCx) > 1 || std::abs(chunkOf(pos.z) - mCz) > 1)) return false;
    return mSource.hasChunksAt(pos, 0, false);
}

bool GenRegion::set(BlockPos const& pos, Block const& block) {
    if (!canWrite(pos)) return false;
    BlockChangeContext ctx{};
    if (mRecord) mRecord->push_back(pos);
    return mSource.setBlock(pos, block, mFlags, nullptr, ctx);
}

// ---- biomes ------------------------------------------------------------------------------------

std::string endBiomeFromHeight(int chunkX, int chunkZ, float islandHeight) {
    if (static_cast<int64_t>(chunkX) * chunkX + static_cast<int64_t>(chunkZ) * chunkZ <= 4096) return "minecraft:the_end";
    double d = (static_cast<double>(islandHeight) - 8.0) / 128.0;
    if (d > 0.25) return "minecraft:end_highlands";
    if (d >= -0.0625) return "minecraft:end_midlands";
    if (d < -0.21875) return "minecraft:small_end_islands";
    return "minecraft:end_barrens";
}

BiomeLookup::BiomeLookup(GenRegion const& region, TheEndGenerator const* endGenerator)
: mRegion(region),
  mEnd(endGenerator) {}

std::string BiomeLookup::at(int x, int y, int z) const {
    if (mRegion.dimension() == 2) {
        int cx = x >> 4, cz = z >> 4;
        if (!mEnd) return "minecraft:the_end";
        return endBiomeFromHeight(cx, cz, mEnd->getIslandHeightValue(cx, cz, 1, 1));
    }
    BlockPos p{x, y, z};
    auto&    src = mRegion.source();
    if (!src.hasChunksAt(p, 0, false)) return {};
    if (p.y < src.getMinHeight()) p.y = src.getMinHeight();
    if (p.y >= src.getMaxHeight()) p.y = src.getMaxHeight() - 1;
    std::string name = src.getBiome(p).mHash->getString();
    return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

// ---- NetherObsidianSpikes ----------------------------------------------------------------------

int generateSpikes(GenRegion& region, JavaRandom& rand, int chunkX, int chunkZ, WorldBeyondConfig const& cfg) {
    int placed = 0;
    if (!(rand.nextFloat() < cfg.chancePerChunk)) return 0;
    auto const& b = blocks();
    for (int i = 0; i < cfg.triesPerChunk; ++i) {
        int      ox = rand.nextInt(16);
        int      oz = rand.nextInt(16);
        BlockPos pos{chunkX * 16 + ox, 50, chunkZ * 16 + oz};
        while (pos.y > 10) {
            if (b.isLava(region.get(pos))) {
                if (placeSpikeAt(region, pos, rand, cfg)) ++placed;
                break;
            }
            pos.y -= 1;
        }
    }
    return placed;
}

bool placeSpikeAt(GenRegion& region, BlockPos const& pos, JavaRandom& rand, WorldBeyondConfig const& cfg) {
    auto const& b = blocks();
    if (!b.obsidian) return false;
    int const heightBelow  = 10;
    int       heightBottom = 3 + rand.nextInt(3);
    int       heightMiddle = 2 + rand.nextInt(4);
    int       heightTop    = 2 + rand.nextInt(3);

    bool addSpawner = false;
    if (rand.nextFloat() < cfg.bigSpikeChance) {
        heightBottom += 7;
        heightMiddle += 8;
        heightTop += 4;
        addSpawner = cfg.bigSpikeSpawners;
    }

    int checkHeight = heightBottom + heightMiddle + heightTop + 2;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
            for (int k = 0; k < checkHeight; ++k) {
                BlockPos     c{pos.x + i - 2, pos.y + k, pos.z + j - 2};
                Block const& s = region.get(c);
                if (!(s.isAir() || b.isLava(s))) return false;
            }

    Block const& obsidian = *b.obsidian;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < heightBottom + heightBelow; ++k) {
                BlockPos p{pos.x + i - 1, pos.y + k - heightBelow, pos.z + j - 1};
                Block const& old = region.get(p); // Java: everything but unbreakable blocks (destroy speed -1)
                if (old.isAir() || old.getBlockType().getDestroySpeed() >= 0.0f) region.set(p, obsidian);
            }

    for (int i = 0; i < heightMiddle; ++i) {
        BlockPos p{pos.x, pos.y + heightBottom + i, pos.z};
        region.set(p, obsidian);
        for (auto const& d : kHorizontal) region.set(BlockPos{p.x + d[0], p.y, p.z + d[1]}, obsidian);
    }

    for (int i = 0; i < heightTop; ++i) {
        BlockPos p{pos.x, pos.y + heightBottom + heightMiddle + i, pos.z};
        region.set(p, obsidian);
        if (addSpawner && i == 0) {
            bool         lantern = b.blazeLantern && blazeLanternEnabled();
            Block const* cap     = lantern ? b.blazeLantern : b.glowstone;
            if (cap) region.set(p, *cap);

            BlockPos sp{p.x, p.y - 1, p.z};
            if (b.spawner && region.set(sp, *b.spawner)) {
                if (auto* be = region.source().getBlockEntity(sp); be && be->getType() == BlockActorType::MobSpawner) {
                    auto& spawner = static_cast<MobSpawnerBlockActor&>(*be);
                    if (auto* ms = spawner.mSpawner.get()) ms->setEntityId(ActorDefinitionIdentifier("minecraft:blaze"));
                }
            }
            BlockPos cp{p.x, p.y - 2, p.z};
            if (b.chest && region.set(cp, *b.chest)) {
                if (auto* be = region.source().getBlockEntity(cp); be && be->getType() == BlockActorType::Chest) {
                    auto& chest = static_cast<ChestBlockActor&>(*be);
                    chest.setLootTable("loot_tables/chests/nether_bridge.json", rand.nextInt());
                }
            }
        }
    }
    gSpikes.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ---- multi-chunk framework ---------------------------------------------------------------------

void forEachMultiChunkSource(
    int64_t                                                                   worldSeed,
    int                                                                       chunkX,
    int                                                                       chunkZ,
    int64_t                                                                   salt,
    int                                                                       radius,
    std::function<bool(JavaRandom&, int, int)> const&                         isSource,
    std::function<void(BlockPos const& source, JavaRandom& ourRandom)> const& part
) {
    if (radius <= 0) return;
    int        chunkRadius = static_cast<int>(std::ceil(radius / 16.0));
    JavaRandom worldRandom(worldSeed);
    uint64_t   xSeed = static_cast<uint64_t>(worldRandom.nextLong());
    uint64_t   zSeed = static_cast<uint64_t>(worldRandom.nextLong());
    auto       seedOf = [&](int x, int z) {
        return static_cast<int64_t>(
            (xSeed * static_cast<uint64_t>(static_cast<int64_t>(x)) + zSeed * static_cast<uint64_t>(static_cast<int64_t>(z)))
            ^ static_cast<uint64_t>(worldSeed) ^ static_cast<uint64_t>(salt)
        );
    };
    JavaRandom ourRandom(seedOf(chunkX, chunkZ));
    for (int x = chunkX - chunkRadius; x <= chunkX + chunkRadius; ++x)
        for (int z = chunkZ - chunkRadius; z <= chunkZ + chunkRadius; ++z) {
            JavaRandom chunkRandom(seedOf(x, z));
            if (isSource(chunkRandom, x * 16, z * 16)) part(BlockPos{x * 16, 0, z * 16}, ourRandom);
        }
}

namespace {
/// BlockPos#closerThan(Vec3i.ZERO, 1050) for a chunk corner (y 0).
bool nearOrigin(int x, int z) {
    double d = static_cast<double>(x) * x + static_cast<double>(z) * z;
    return d < 1050.0 * 1050.0;
}
} // namespace

// ---- ChorusVegetation --------------------------------------------------------------------------

int generateChorus(GenRegion& region, BiomeLookup const& biomes, int64_t seed, int cx, int cz, WorldBeyondConfig const& cfg) {
    auto const& b = blocks();
    if (!b.ready()) return 0;
    int  placed    = 0;
    int  topY      = region.source().getMaxHeight() - 1;
    auto chanceFor = [&](std::string const& biome) {
        if (biome == "minecraft:end_highlands") return cfg.highlandsChance;
        if (biome == "minecraft:end_midlands") return cfg.midlandsChance;
        return cfg.otherEndBiomesChance;
    };
    forEachMultiChunkSource(
        seed,
        cx,
        cz,
        2093,
        cfg.chorusVegetationRadius,
        [&](JavaRandom& r, int x, int z) {
            return !nearOrigin(x, z) && cfg.chorusVegetationRarity > 0 && r.nextInt(cfg.chorusVegetationRarity) == 0
                && biomes.at(x, topY, z) == "minecraft:end_highlands";
        },
        [&](BlockPos const& src, JavaRandom& rand) {
            for (int i = 0; i < cfg.chunkAttempts; ++i) {
                int      ox = rand.nextInt(16);
                int      oz = rand.nextInt(16);
                BlockPos p{cx * 16 + ox, 100, cz * 16 + oz};
                double   chance = chanceFor(biomes.at(p.x, topY, p.z));
                double   dist   = static_cast<double>(p.x - src.x) * (p.x - src.x) + static_cast<double>(p.z - src.z) * (p.z - src.z);
                int      dither = 6 * 6;
                if (dist > dither) chance *= (1.0 - (std::atan((dist - dither) / 50.0) / (std::numbers::pi / 2.0)));
                if (chance > 0 && rand.nextDouble() < chance) {
                    while (p.y > 40) {
                        if (b.isEndStone(region.get(p))) break;
                        p.y -= 1;
                    }
                    BlockPos above{p.x, p.y + 1, p.z};
                    if (b.isEndStone(region.get(p)) && region.isAir(above)) {
                        Block const& plant = rand.nextDouble() < 0.1 ? *b.twist : *b.weeds;
                        if (region.set(above, plant)) {
                            scheduleChorusTick(region.source(), above, plant);
                            ++placed;
                        }
                    }
                }
            }
        }
    );
    gChorus.fetch_add(placed, std::memory_order_relaxed);
    return placed;
}

// ---- SpiralSpires ------------------------------------------------------------------------------

int generateSpires(GenRegion& region, BiomeLookup const& biomes, int64_t seed, int cx, int cz, WorldBeyondConfig const& cfg) {
    auto const& b = blocks();
    if (!b.ready()) return 0;
    int made = 0;
    forEachMultiChunkSource(
        seed,
        cx,
        cz,
        1892,
        cfg.spiralSpiresRadius,
        [&](JavaRandom& r, int x, int z) {
            return !nearOrigin(x, z) && cfg.spiralSpiresRarity > 0 && r.nextInt(cfg.spiralSpiresRarity) == 0;
        },
        [&](BlockPos const& src, JavaRandom& random) {
            int    cornerX = cx * 16, cornerZ = cz * 16;
            double r16     = 16.0 * cfg.spiralSpiresRadius;
            double dist =
                (static_cast<double>(cornerX - src.x) * (cornerX - src.x) + static_cast<double>(cornerZ - src.z) * (cornerZ - src.z))
                / (r16 * r16);
            if (dist > 0.5 && random.nextDouble() < (1.5 - dist)) return;
            int      ox = random.nextInt(16);
            int      oz = random.nextInt(16);
            BlockPos pos{cornerX + ox, 256, cornerZ + oz};
            if (!cfg.biomes.canSpawn(biomes.at(pos.x, pos.y, pos.z), region.dimension())) return;
            while (!b.isEndStone(region.get(pos))) {
                pos.y -= 1;
                if (pos.y < 10) return;
            }
            if (makeSpire(region, random, pos)) ++made;
        }
    );
    gSpires.fetch_add(made, std::memory_order_relaxed);
    return made;
}

bool makeSpire(GenRegion& region, JavaRandom& rand, BlockPos const& pos) {
    auto const& b = blocks();
    if (!b.ready()) return false;
    int    height            = 50 + rand.nextInt(20);
    double heightComposition = 5 + rand.nextDouble() * 1;
    int    start             = -5;
    int    y                 = start;

    for (; y < height; ++y) {
        Block const& s = region.get(BlockPos{pos.x, pos.y + y, pos.z});
        auto const*  t = &s.getBlockType();
        if (!s.isAir() && !(t == b.endStoneType || t == b.cryingObsType || t == b.obsidianType || t == b.crystalType)) return false;
    }
    y = start;

    Block const& edgeAlt = b.myalite ? *b.myalite : *b.dusky;
    for (; y < height; ++y) {
        if (y < 0 && !region.get(BlockPos{pos.x, pos.y + y, pos.z})._isSolid()) continue;
        double r  = std::abs(((std::numbers::pi / 2) - std::atan((std::max(0, y) + 0.5) / (height / heightComposition))) * 4);
        int    ri = static_cast<int>(std::ceil(r));
        for (int i = -ri + 1; i < ri; ++i)
            for (int j = -ri + 1; j < ri; ++j)
                if ((i * i + j * j) <= (ri * ri)) {
                    bool         edge  = i == (-ri + 1) || i == (ri - 1) || j == (-ri + 1) || j == (ri - 1);
                    Block const& state = (edge && rand.nextFloat() < 0.2f) ? edgeAlt : *b.dusky;
                    region.set(BlockPos{pos.x + i, pos.y + y, pos.z + j}, state);
                }
    }

    int steps     = 80 + rand.nextInt(30);
    int substeps  = 10;
    int fullSteps = steps * substeps;
    int deteriorate = static_cast<int>((0.5 + rand.nextDouble() * 0.3) * fullSteps);

    double spin         = 0.12 + rand.nextDouble() * 0.16;
    double spread       = 0.12 + rand.nextDouble() * 0.04;
    double upwardMotion = rand.nextDouble() * 0.2;
    if (rand.nextBoolean()) spin *= -1;

    for (int i = 0; i < fullSteps; ++i) {
        double t  = static_cast<double>(i) * spin;
        int    x  = static_cast<int>(std::sin(t / substeps) * i * spread / substeps);
        int    z  = static_cast<int>(std::cos(t / substeps) * i * spread / substeps);
        int    yp = y + static_cast<int>(std::floor((static_cast<double>(i) / substeps) * upwardMotion + 0.5));

        BlockPos next{pos.x + x, pos.y + yp, pos.z + z};
        float    chance = 1.0f;
        if (i > deteriorate) {
            int deterStep = i - deteriorate;
            int maxSteps  = fullSteps - deteriorate;
            chance -= static_cast<float>(deterStep) / static_cast<float>(maxSteps);
        }
        if (rand.nextFloat() < chance) region.set(next, *crystalFor(next));
    }
    return true;
}

// ---- entry ---------------------------------------------------------------------------------------

void decorateChunk(BlockSource& source, int dimId, int chunkX, int chunkZ, int64_t seed, TheEndGenerator const* endGen) {
    auto const cfg = config();
    if (!blocks().ready()) return;
    gChunks.fetch_add(1, std::memory_order_relaxed);
    GenRegion   region(source, chunkX, chunkZ, 2);
    BiomeLookup biomes(region, endGen);

    // Zeta: WorldgenRandom decoration seed of the chunk, then a feature seed per stage/generator.
    JavaRandom rand;
    int64_t    deco = decorationSeed(rand, seed, chunkX * 16, chunkZ * 16);

    if (cfg.enableSpiralSpires && cfg.spiralSpiresDimensions.canSpawnHere(dimId)) {
        generateSpires(region, biomes, seed, chunkX, chunkZ, cfg);
    }
    if (cfg.enableNetherObsidianSpikes && cfg.netherObsidianSpikesDimensions.canSpawnHere(dimId)) {
        featureSeed(rand, deco, kStageUndergroundDecoration * 10000, kStageUndergroundDecoration);
        generateSpikes(region, rand, chunkX, chunkZ, cfg);
    }
    if (cfg.enableChorusVegetation && dimId == 2) { // Quark: DimensionConfig.end(false), not configurable
        generateChorus(region, biomes, seed, chunkX, chunkZ, cfg);
    }
    (void)kStageSurfaceStructures;
}

GenStats genStats() {
    return GenStats{gChunks.load(), gSpikes.load(), gSpires.load(), gChorus.load(), gSprings.load()};
}

void countSpringRemoved() { gSprings.fetch_add(1, std::memory_order_relaxed); }

} // namespace lp::feature::worldbeyond
