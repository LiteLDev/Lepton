#pragma once

// U20 mobsoverworld configuration (`config/mobsoverworld.json`): Quark's CrabsModule, ShibaModule
// and ToretoiseModule options with Quark's defaults (docs/spec/mobsoverworld.md §5).

#include <string>
#include <vector>

namespace lp::feature::mobsoverworld {

/// Quark's CompoundBiomeConfig: Bedrock biome tags / biome ids, as a white- or blacklist.
struct BiomeConfig {
    bool                     isBlacklist = false;
    std::vector<std::string> biomeTags;
    std::vector<std::string> biomes;
};

/// Quark's EntitySpawnConfig.
struct SpawnConfig {
    int         spawnWeight  = 0;
    int         minGroupSize = 1;
    int         maxGroupSize = 1;
    BiomeConfig biomes;
};

/// Quark's DimensionConfig ("minecraft:overworld", "minecraft:the_nether", "minecraft:the_end").
struct DimensionConfig {
    bool                     isBlacklist = false;
    std::vector<std::string> dimensions;
};

struct Config {
    int version = 1;

    // ---- Crabs ----
    bool        enableCrabs      = true;
    SpawnConfig crabsSpawnConfig = {5, 1, 3, {false, {"beach"}, {}}};
    bool        enableBrewing    = true;
    /// Quark advancement option (advancements are not ported); kept for parity.
    bool        resilienceRequiredForAllEffects = true;
    bool        addCrabLegToFishermanTrades     = true;

    // ---- Shiba ----
    bool        enableShiba             = true;
    SpawnConfig shibaSpawnConfig        = {40, 1, 3, {false, {"mountains"}, {}}};
    bool        ignoreAreasWithSkylight = false;

    // ---- Toretoise ----
    bool                     enableToretoise        = true;
    int                      maxYLevel              = 0;
    int                      cooldownTicks          = 20 * 60;
    std::vector<std::string> foods                  = {"minecraft:glow_berries"};
    /// Quark advancement flag (no gameplay effect in Quark); kept for parity.
    bool                     allowToretoiseToRegrow = true;
    int                      regrowChance           = 3;
    DimensionConfig          dimensions             = {false, {"minecraft:overworld"}};
    SpawnConfig              toretoiseSpawnConfig   = {120, 2, 4, {true, {"nether", "the_end"}, {}}};
};

[[nodiscard]] Config const& config();

} // namespace lp::feature::mobsoverworld
