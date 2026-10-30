#pragma once
// U28 worldsurface config (`config/worldsurface.json`): Quark's options of FairyRings, FallenLogs,
// MonsterBox, GlimmeringWeald and CherryGroveWaterPetals with Quark's defaults (docs/spec/worldsurface.md §5).
#include <map>
#include <string>
#include <vector>

namespace lp::feature::worldsurface {

/// Zeta DimensionConfig: whitelist (or blacklist) of dimension ids.
struct DimensionConfig {
    bool                     isBlacklist = false;
    std::vector<std::string> dimensions  = {"minecraft:overworld"};
};

struct BiomeTagListConfig {
    bool                     isBlacklist = true;
    std::vector<std::string> biomeTags;
};

struct BiomeListConfig {
    bool                     isBlacklist = false;
    std::vector<std::string> biomes;
};

/// Zeta CompoundBiomeConfig: a biome must pass both lists.
struct CompoundBiomeConfig {
    BiomeTagListConfig tags;
    BiomeListConfig    biomes;
};

struct Config {
    int version = 1;

    // ---- FairyRings ----
    bool                     enableFairyRings     = true;
    double                   forestChance         = 0.00625;
    double                   plainsChance         = 0.0025;
    DimensionConfig          fairyRingsDimensions = {};
    std::vector<std::string> oresRaw              = {"minecraft:emerald_ore", "minecraft:diamond_ore"};
    /// Java `#minecraft:is_forest` (a datapack tag in Quark).
    std::vector<std::string> fairyRingsForestBiomes = {
        "minecraft:forest",
        "minecraft:flower_forest",
        "minecraft:birch_forest",
        "minecraft:old_growth_birch_forest",
        "minecraft:dark_forest",
        "minecraft:grove"
    };
    /// Java `#c:is_plains`.
    std::vector<std::string> fairyRingsPlainsBiomes = {"minecraft:plains", "minecraft:sunflower_plains"};

    // ---- FallenLogs ----
    bool                     enableFallenLogs     = true;
    DimensionConfig          fallenLogsDimensions = {};
    double                   hollowChance         = 0.7;
    int                      rarity               = 5;
    double                   onWaterChance        = 0.1;
    int                      sparseBiomeRarity    = 12;
    std::vector<std::string> biomeTags            = {
        "has_fallen_acacia=minecraft:acacia_log",
        "has_fallen_birch=minecraft:birch_log",
        "has_fallen_cherry=minecraft:cherry_log",
        "has_fallen_dark_oak=minecraft:dark_oak_log",
        "has_fallen_jungle=minecraft:jungle_log",
        "has_fallen_mangrove=minecraft:mangrove_log",
        "has_fallen_oak=minecraft:oak_log",
        "has_fallen_spruce=minecraft:spruce_log"
    };
    /// Quark's biome tags (`data/quark/tags/worldgen/biome/has_fallen_*.json`).
    std::map<std::string, std::vector<std::string>> fallenLogsBiomeTagMembers = {
        {"has_fallen_acacia",   {"minecraft:savanna", "minecraft:savanna_plateau"}},
        {"has_fallen_birch",    {"minecraft:forest", "minecraft:birch_forest", "minecraft:old_growth_birch_forest"}},
        {"has_fallen_cherry",   {"minecraft:cherry_grove"}},
        {"has_fallen_dark_oak", {}},
        {"has_fallen_jungle",   {"minecraft:jungle", "minecraft:sparse_jungle"}},
        {"has_fallen_mangrove", {}},
        {"has_fallen_oak",
         {"minecraft:forest",
          "minecraft:flower_forest",
          "minecraft:meadow",
          "minecraft:windswept_forest",
          "minecraft:swamp",
          "minecraft:wooded_badlands"}},
        {"has_fallen_spruce",
         {"minecraft:grove",
          "minecraft:windswept_forest",
          "minecraft:taiga",
          "minecraft:old_growth_spruce_taiga",
          "minecraft:snowy_taiga"}},
    };
    /// Quark's `has_lower_fallen_tree_density` biome tag.
    std::vector<std::string> fallenLogsReducedDensityBiomes = {
        "minecraft:meadow",
        "minecraft:windswept_forest",
        "minecraft:sparse_jungle",
        "minecraft:swamp",
        "minecraft:savanna",
        "minecraft:savanna_plateau"
    };

    // ---- MonsterBox ----
    bool            enableMonsterBox         = true;
    double          monsterBoxChancePerChunk = 0.2;
    int             minY                     = -50;
    int             maxY                     = 0;
    int             minMobCount              = 5;
    int             maxMobCount              = 8;
    DimensionConfig monsterBoxDimensions     = {};
    bool            enableExtraLootTable     = true;
    double          activationRange          = 2.5;
    int             searchRange              = 15;

    // ---- GlimmeringWeald ----
    bool   enableGlimmeringWeald = true;
    /// Commented out in Quark 4.1-488 (no effect there); kept for parity, no effect here either.
    double minDepthRange     = 1.55;
    double maxWeirdnessRange = 2;

    // ---- CherryGroveWaterPetals ----
    bool                enableCherryGroveWaterPetals         = true;
    double              cherryGroveWaterPetalsChancePerChunk = 0.5;
    int                 triesPerChunk                        = 1;
    int                 size                                 = 3;
    int                 sizeVariation                        = 1;
    double              edgeVariation                        = 0.25;
    bool                useCarpet                            = false;
    CompoundBiomeConfig biomes = {{true, {}}, {false, {"minecraft:cherry_grove"}}};
    DimensionConfig     cherryGroveWaterPetalsDimensions = {};
};

[[nodiscard]] Config const& cfg();

} // namespace lp::feature::worldsurface
