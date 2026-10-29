#pragma once
// Shared helpers of U28 worldsurface: deterministic random numbers, the Java -> Bedrock biome
// names, dimension / biome filters, block classification and the block palette that the
// world-generation threads read (resolved on the server thread).
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "feature/worldsurface/Config.h"

class Biome;
class Block;
class BlockPos;
class BlockSource;

namespace lp::feature::worldsurface {

// ---- random --------------------------------------------------------------------------------------

/// SplitMix64-seeded xoshiro256** (deterministic, fast, thread-local use).
class Rng {
public:
    explicit Rng(uint64_t seed);
    uint64_t next();
    /// [0, bound) (bound <= 0 -> 0).
    int    nextInt(int bound);
    /// [lo, hi) like Java `nextInt(lo, hi)`; hi <= lo -> lo.
    int    nextInt(int lo, int hi);
    /// [lo, hi] inclusive.
    int    between(int lo, int hi);
    double nextDouble();
    float  nextFloat();
    bool   nextBool();

private:
    std::array<uint64_t, 4> s{};
};

/// Seed of generator `salt` for chunk (cx, cz) of a world.
[[nodiscard]] uint64_t chunkSeed(uint64_t worldSeed, int cx, int cz, uint64_t salt);

// ---- biomes / dimensions ---------------------------------------------------------------------------

/// Bedrock biome name for a Java biome id ("minecraft:dark_forest" -> "roofed_forest"). Ids
/// without a mapping lose their namespace ("minecraft:cherry_grove" -> "cherry_grove").
[[nodiscard]] std::string bedrockBiome(std::string_view javaId);
/// Bedrock name of a biome without the "minecraft:" namespace ("forest", "roofed_forest", ...).
[[nodiscard]] std::string biomeName(Biome const& biome);
/// Bedrock biome names of a Java biome tag Lepton knows ("minecraft:is_forest", "c:is_plains", ...).
[[nodiscard]] std::vector<std::string> biomesOfTag(std::string_view javaTag);
/// The ids as Bedrock biome names, each under its legacy Bedrock name and its Java path.
[[nodiscard]] std::unordered_set<std::string> bedrockBiomeSet(std::vector<std::string> const& javaIds);
/// "minecraft:dark_forest" -> "dark_forest".
[[nodiscard]] std::string javaPath(std::string_view javaId);

/// Dimension id ("minecraft:overworld" ...) of a Bedrock dimension number.
[[nodiscard]] std::string_view dimensionName(int dim);
[[nodiscard]] bool             dimensionAllowed(DimensionConfig const& c, int dim);

/// A resolved CompoundBiomeConfig (Bedrock names).
struct BiomeFilter {
    bool                            tagsBlacklist   = true;
    std::unordered_set<std::string> tagBiomes;
    bool                            biomesBlacklist = false;
    std::unordered_set<std::string> biomes;
    [[nodiscard]] bool allows(std::string const& bedrockBiome) const;
};
[[nodiscard]] BiomeFilter resolve(CompoundBiomeConfig const& c);

// ---- block classification (names are Bedrock ids) -------------------------------------------------

[[nodiscard]] std::string const& nameOf(Block const& block);
/// Java `#minecraft:dirt`.
[[nodiscard]] bool isDirt(Block const& block);
/// Java `#c:stones`.
[[nodiscard]] bool isStone(Block const& block);
/// Java `BlockUtils.isStoneBased` (bass-drum instrument or stone map colour).
[[nodiscard]] bool isStoneBased(Block const& block);
/// Java `#minecraft:small_flowers`.
[[nodiscard]] bool isSmallFlower(Block const& block);
/// Java `#minecraft:flowers`.
[[nodiscard]] bool isFlower(Block const& block);
[[nodiscard]] bool isWater(Block const& block);
[[nodiscard]] bool isLiquid(Block const& block);
/// Java `BlockState.canBeReplaced()`.
[[nodiscard]] bool replaceable(BlockSource& region, BlockPos const& pos, Block const& block);
/// Java `isFaceSturdy(UP)` etc. (face: Bedrock FacingID 0 down, 1 up, 2 north, 3 south, 4 west, 5 east).
[[nodiscard]] bool sturdy(Block const& block, int face);
/// The block is air or a liquid-free replaceable (vines, grass, ...).
[[nodiscard]] bool isAirBlock(Block const& block);

/// Sets a block during generation (no neighbour updates, like Java flag 2 on a WorldGenRegion).
bool genSet(BlockSource& region, BlockPos const& pos, Block const& block);
/// Sets a block in the live world (neighbour + client updates).
bool liveSet(BlockSource& region, BlockPos const& pos, Block const& block);

// ---- palette ---------------------------------------------------------------------------------------

/// Everything the generation threads need, built on the server thread from the config.
struct GenState {
    Config config;
    Block const* air = nullptr;

    // FairyRings
    std::unordered_set<std::string> forestBiomes, plainsBiomes;
    std::vector<Block const*>       ores;
    std::unordered_map<std::string, Block const*> flowers; ///< Bedrock flower id -> block

    // FallenLogs
    struct LogEntry {
        std::unordered_set<std::string> biomes;
        std::string                     log;  ///< Bedrock log id
        std::array<Block const*, 2>     axis{}; ///< x, z
        std::array<Block const*, 2>     hollowAxis{};
    };
    std::vector<LogEntry>           logs;
    std::unordered_set<std::string> reducedBiomes;
    Block const*                    fern       = nullptr;
    Block const*                    mossCarpet = nullptr;
    std::array<Block const*, 4>     vine{}; ///< attached towards: 0 south, 1 west, 2 north, 3 east

    // MonsterBox
    Block const* monsterBox = nullptr;

    // GlimmeringWeald
    Block const*                glowShroom       = nullptr;
    Block const*                lichenGrowth     = nullptr;
    Block const*                shroomCap        = nullptr; ///< every face but down
    Block const*                shroomStem       = nullptr; ///< sides only
    std::array<Block const*, 4> ring{};                     ///< facing 0 south, 1 west, 2 north, 3 east
    std::array<Block const*, 6> glowLichen{};               ///< attached to FacingID face
    Block const*                lapisOre         = nullptr;
    Block const*                deepslateLapisOre = nullptr;

    // CherryGroveWaterPetals
    BiomeFilter                                petalBiomes;
    std::array<std::array<Block const*, 4>, 5> petals{}; ///< [amount 1..4][facing 0 S,1 W,2 N,3 E]
    Block const*                               cherryCarpet = nullptr;
};

/// The current generation state (never null after the first world is ready).
[[nodiscard]] std::shared_ptr<GenState const> genState();
/// Rebuilds the generation state from the config (server thread: world ready, config reload).
void rebuildGenState();
/// Rebuilds it from a config snapshot (any thread); `quiet` skips the missing-block warnings.
void rebuildGenState(Config const& config, bool quiet);

/// Java 2D facing index (0 S, 1 W, 2 N, 3 E) -> (dx, dz).
[[nodiscard]] std::pair<int, int> facingStep(int facing);
/// Name of a Java 2D facing ("south", "west", "north", "east").
[[nodiscard]] char const* facingName(int facing);

} // namespace lp::feature::worldsurface
