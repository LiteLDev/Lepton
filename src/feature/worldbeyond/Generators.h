#pragma once

// World generators of NetherObsidianSpikes, ChorusVegetation and SpiralSpires (docs/spec/worldbeyond.md §3).
// They run inside the game's chunk decoration (any thread) or on the server thread for selftests.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "feature/worldbeyond/Config.h"
#include "feature/worldbeyond/JavaMath.h"

class Block;
class BlockSource;
class TheEndGenerator;

namespace lp::feature::worldbeyond {

/// Block access for a generator: reads anything loaded, writes only within one chunk of the
/// decorated chunk (Java's WorldGenRegion write radius) unless `unclipped`.
class GenRegion {
public:
    GenRegion(BlockSource& source, int centerChunkX, int centerChunkZ, int updateFlags);
    /// Selftests: no clipping, neighbour + client updates.
    static GenRegion unclipped(BlockSource& source);

    [[nodiscard]] Block const& get(BlockPos const& pos) const;
    [[nodiscard]] bool         isAir(BlockPos const& pos) const;
    [[nodiscard]] bool         canWrite(BlockPos const& pos) const;
    bool                       set(BlockPos const& pos, Block const& block);
    [[nodiscard]] BlockSource& source() const { return mSource; }
    [[nodiscard]] int          dimension() const;
    /// Selftests: remember every position written (to clean up afterwards).
    void record(std::vector<BlockPos>* out) { mRecord = out; }

private:
    BlockSource&           mSource;
    int                    mCx, mCz, mFlags;
    bool                   mClip   = true;
    std::vector<BlockPos>* mRecord = nullptr;
    Block const*           mAir    = nullptr;
};

/// Biome lookup in Java names ("minecraft:end_highlands"). In the End the Java End biomes are
/// derived from the island height (needs the End generator; without one, `minecraft:the_end`).
class BiomeLookup {
public:
    BiomeLookup(GenRegion const& region, TheEndGenerator const* endGenerator);
    [[nodiscard]] std::string at(int x, int y, int z) const;

private:
    GenRegion const&       mRegion;
    TheEndGenerator const* mEnd;
};

/// Java End biome of a chunk from its island height (`h` of getIslandHeightValue(cx, cz, 1, 1)).
[[nodiscard]] std::string endBiomeFromHeight(int chunkX, int chunkZ, float islandHeight);

// ---- NetherObsidianSpikes --------------------------------------------------------------------

/// The per-chunk generator (Quark's ObsidianSpikeGenerator#generateChunk). Returns spikes placed.
int generateSpikes(GenRegion& region, JavaRandom& rand, int chunkX, int chunkZ, WorldBeyondConfig const& cfg);
/// One spike on the lava block `pos` (Quark's placeSpikeAt). Returns false if the space is blocked.
bool placeSpikeAt(GenRegion& region, BlockPos const& pos, JavaRandom& rand, WorldBeyondConfig const& cfg);

// ---- multi-chunk features ---------------------------------------------------------------------

/// Zeta's MultiChunkFeatureGenerator around chunk (cx, cz): for every chunk within
/// ceil(radius / 16) chunks, `sources(chunkRandom, cornerX, cornerZ)` may name a source, and
/// `part(sourcePos, ourRandom)` then decorates chunk (cx, cz).
void forEachMultiChunkSource(
    int64_t                                                                   worldSeed,
    int                                                                       chunkX,
    int                                                                       chunkZ,
    int64_t                                                                   salt,
    int                                                                       radius,
    std::function<bool(JavaRandom&, int cornerX, int cornerZ)> const&         isSource,
    std::function<void(BlockPos const& source, JavaRandom& ourRandom)> const& part
);

/// ChorusVegetation in chunk (cx, cz). Returns plants placed.
int generateChorus(GenRegion& region, BiomeLookup const& biomes, int64_t seed, int cx, int cz, WorldBeyondConfig const& cfg);
/// SpiralSpires in chunk (cx, cz). Returns spires made.
int generateSpires(GenRegion& region, BiomeLookup const& biomes, int64_t seed, int cx, int cz, WorldBeyondConfig const& cfg);
/// One spire on the end stone at `pos` (Quark's makeSpike). Returns false if the space is blocked.
bool makeSpire(GenRegion& region, JavaRandom& rand, BlockPos const& pos);

/// Runs every generator of this unit for a decorated chunk (called from the generator hooks).
void decorateChunk(BlockSource& source, int dimId, int chunkX, int chunkZ, int64_t seed, TheEndGenerator const* endGen);

/// Statistics (selftests, logs).
struct GenStats {
    int64_t chunks = 0, spikes = 0, spires = 0, chorus = 0, springsRemoved = 0;
};
[[nodiscard]] GenStats genStats();
void                   countSpringRemoved();

} // namespace lp::feature::worldbeyond
