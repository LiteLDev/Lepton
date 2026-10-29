#pragma once

// Config section "mobsdeep" (config/mobsdeep.json): Quark's StonelingsModule, ForgottenModule,
// FoxhoundModule and WraithModule options with Quark's defaults (docs/spec/mobsdeep.md §5).

#include <string>
#include <vector>

namespace lp::feature::mobsdeep {

/// Quark's CompoundBiomeConfig half: a list of biome ids or tags, white- or blacklisted.
struct BiomeList {
    bool                     isBlacklist = true;
    std::vector<std::string> biomes;
};
struct TagList {
    bool                     isBlacklist = true;
    std::vector<std::string> biomeTags;
};

/// Quark's EntitySpawnConfig / CostSensitiveEntitySpawnConfig.
struct SpawnConfig {
    int       spawnWeight  = 1;
    int       minGroupSize = 1;
    int       maxGroupSize = 1;
    double    spawnCost    = 0.0; ///< cost-sensitive configs only (not used on Bedrock, see spec §7)
    double    maxCost      = 0.0;
    BiomeList biomes;
    TagList   tags;
};

struct DimensionList {
    bool                     isBlacklist = false;
    std::vector<std::string> dimensions{"minecraft:overworld"};
};

struct Config {
    int version = 1;

    // Stonelings
    bool          enableStonelings = true;
    int           maxYLevel        = 0;
    DimensionList dimensions;
    SpawnConfig   stonelingsSpawnConfig{80, 1, 1, 0.0, 0.0, {true, {}}, {true, {"void", "nether", "the_end"}}};
    bool          enableDiamondHeart  = true;
    bool          cautiousStonelings  = false;
    bool          tamableStonelings   = true;
    bool          wealdPathfinderMaps = true;

    // Forgotten
    bool   enableForgotten    = true;
    double forgottenSpawnRate = 0.05;
    int    maxHeightForSpawn  = 0;

    // Foxhound
    bool        enableFoxhound           = true;
    double      tameChance               = 0.05;
    bool        foxhoundsSpeedUpFurnaces = true;
    SpawnConfig foxhoundSpawnConfig{30, 1, 2, 0.0, 0.0, {false, {"minecraft:nether_wastes", "minecraft:basalt_deltas"}}, {true, {}}};
    SpawnConfig lesserSpawnConfig{2, 1, 1, 0.7, 0.15, {false, {"minecraft:soul_sand_valley"}}, {true, {}}};

    // Wraith
    bool                     enableWraith = true;
    std::vector<std::string> wraithSounds{
        "entity.sheep.ambient|entity.sheep.hurt|entity.sheep.death",
        "entity.cow.ambient|entity.cow.hurt|entity.cow.death",
        "entity.pig.ambient|entity.pig.hurt|entity.pig.death",
        "entity.chicken.ambient|entity.chicken.hurt|entity.chicken.death",
        "entity.horse.ambient|entity.horse.hurt|entity.horse.death",
        "entity.cat.ambient|entity.cat.hurt|entity.cat.death",
        "entity.wolf.ambient|entity.wolf.hurt|entity.wolf.death",
        "entity.villager.ambient|entity.villager.hurt|entity.villager.death",
        "entity.polar_bear.ambient|entity.polar_bear.hurt|entity.polar_bear.death",
        "entity.zombie.ambient|entity.zombie.hurt|entity.zombie.death",
        "entity.skeleton.ambient|entity.skeleton.hurt|entity.skeleton.death",
        "entity.spider.ambient|entity.spider.hurt|entity.spider.death",
        "|entity.creeper.hurt|entity.creeper.death",
        "entity.enderman.ambient|entity.enderman.hurt|entity.enderman.death",
        "entity.zombified_piglin.ambient|entity.zombified_piglin.hurt|entity.zombified_piglin.death",
        "entity.witch.ambient|entity.witch.hurt|entity.witch.death",
        "entity.blaze.ambient|entity.blaze.hurt|entity.blaze.death",
        "entity.llama.ambient|entity.llama.hurt|entity.llama.death",
        "|quark:entity.stoneling.cry|quark:entity.stoneling.die",
        "minecraft:entity.frog.ambient|minecraft:entity.frog.hurt|minecraft:entity.frog.death",
    };
    SpawnConfig wraithSpawnConfig{5, 1, 3, 0.7, 0.15, {false, {"minecraft:soul_sand_valley"}}, {true, {}}};
};

[[nodiscard]] Config const& config();

} // namespace lp::feature::mobsdeep
