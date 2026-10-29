#pragma once

// `config/worldbeyond.json`: NetherObsidianSpikes, NoMoreLavaPockets, ChorusVegetation, SpiralSpires.
// Keys and defaults are Quark 4.1-488's (docs/spec/worldbeyond.md §5).

#include <string>
#include <string_view>
#include <vector>

namespace lp::feature::worldbeyond {

/// Zeta's DimensionConfig: dimension names ("minecraft:the_nether"), white- or blacklist.
struct DimensionConfig {
    bool                     isBlacklist = false;
    std::vector<std::string> dimensions;

    [[nodiscard]] bool canSpawnHere(int dimId) const;
};

/// Zeta's BiomeTagConfig / StrictBiomeConfig.
struct BiomeTagConfig {
    bool                     isBlacklist = true;
    std::vector<std::string> tags;
};
struct StrictBiomeConfig {
    bool                     isBlacklist = true;
    std::vector<std::string> biomes;
};

/// Zeta's CompoundBiomeConfig: a biome must match both `tags` and `biomes`.
struct CompoundBiomeConfig {
    BiomeTagConfig    tags;
    StrictBiomeConfig biomes;

    /// `biome` is a Java-style name ("minecraft:end_highlands"), `dimId` gives the dimension tags.
    [[nodiscard]] bool canSpawn(std::string_view biome, int dimId) const;
};

struct WorldBeyondConfig {
    int version = 1;

    // NetherObsidianSpikes
    bool            enableNetherObsidianSpikes = true;
    double          chancePerChunk             = 0.1;
    double          bigSpikeChance             = 0.03;
    int             triesPerChunk              = 4;
    bool            bigSpikeSpawners           = true;
    DimensionConfig netherObsidianSpikesDimensions{false, {"minecraft:the_nether"}};

    // NoMoreLavaPockets
    bool enableNoMoreLavaPockets = true;

    // ChorusVegetation
    bool   enableChorusVegetation    = true;
    int    chorusVegetationRarity    = 150;
    int    chorusVegetationRadius    = 7;
    int    chunkAttempts             = 120;
    double highlandsChance           = 1.0;
    double midlandsChance            = 0.2;
    double otherEndBiomesChance      = 0;
    double passiveTeleportChance     = 0.2;
    double endermiteSpawnChance      = 0.01;
    double teleportDuplicationChance = 0.01;

    // SpiralSpires
    bool                enableSpiralSpires = true;
    DimensionConfig     spiralSpiresDimensions{false, {"minecraft:the_end"}};
    CompoundBiomeConfig biomes{
        {true, {}},
        {false, {"minecraft:end_highlands"}}
    };
    int  spiralSpiresRarity     = 200;
    int  spiralSpiresRadius     = 15;
    bool enableMyaliteViaducts  = true;
    int  myaliteConduitDistance = 24; ///< Quark clamps it to 2..1024
    bool renewableMyalite       = true;
};

/// The current config snapshot (lock-free, safe on world generation threads; stays valid).
[[nodiscard]] WorldBeyondConfig const& config();
/// Copies `lp::config::get<WorldBeyondConfig>()` into the snapshot `config()` returns (server thread:
/// after loading, `/quark reload` and selftests that change values).
void publishConfig();

/// "minecraft:overworld" / "minecraft:the_nether" / "minecraft:the_end" for 0 / 1 / 2.
[[nodiscard]] std::string dimensionName(int dimId);

/// U8's CompressedBlocks Blaze Lantern is enabled (reads `config/storage.json`, default true).
[[nodiscard]] bool blazeLanternEnabled();
/// Forgets cached values read from other units' config files (after `/quark reload`).
void resetCaches();

} // namespace lp::feature::worldbeyond
