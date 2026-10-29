#pragma once

#include <string>
#include <vector>

namespace lp::feature::misctweaks {

/// `config/misctweaks.json` (docs/spec/misctweaks.md §5). Field names are Quark's option names.
struct MiscTweaksConfig {
    int version = 1;

    // BetterElytraRocket
    bool enableBetterElytraRocket = true;

    // CampfiresBoostElytra
    bool   enableCampfiresBoostElytra = true;
    double boostStrength              = 0.5;
    double maxSpeed                   = 1;

    // MoreNoteBlockSounds
    bool enableMoreNoteBlockSounds = true;
    bool enableAmethystSound       = true;

    // AutomaticRecipeUnlock
    bool                     enableAutomaticRecipeUnlock = true;
    std::vector<std::string> ignoredRecipes;
    bool                     forceLimitedCrafting     = false;
    bool                     disableRecipeBook        = false;
    bool                     skipEmptyRecipes         = true;
    bool                     filterRecipeAdvancements = true;

    // ArmedArmorStands
    bool enableArmedArmorStands = true;

    // GameNerfs (experimental)
    bool                     enableGameNerfs              = false;
    bool                     nerfMending                  = true;
    bool                     noNerfForMendingTwo          = false;
    bool                     disableIronFarms             = true;
    bool                     disableIceRoads              = true;
    bool                     disableWoolDrops             = true;
    bool                     enableSelectiveMobGriefing   = true;
    bool                     enableDimensionLockedElytra  = true;
    bool                     disableFallingBlockDupe      = true;
    bool                     disablePistonPhysicsExploits = true;
    bool                     disableMushroomBlockRemoval  = true;
    bool                     disableTripwireHookDupe      = true;
    bool                     villageSpawnNerf             = false;
    int                      villageSpawnNerfDistance     = 7000;
    std::vector<std::string> nonGriefingEntities{"minecraft:creeper", "minecraft:enderman"};
    std::vector<std::string> elytraAllowedDimensions{"minecraft:the_end"};

    // SpawnerReplacer (experimental)
    bool                     enableSpawnerReplacer = false;
    std::vector<std::string> replaceMobs;

    // VillagerRerollingRework (experimental)
    bool   enableVillagerRerollingRework = false;
    bool   seedInitialVillagerTrades     = true;
    double chanceToRerollWhenRestocking  = 0.25;
    int    maximumRestocksPerDay         = 3;
    bool   rerollOnAnyRestock            = false;
    bool   rerollEvenIfNotOutOfStock     = false;

    // MicrocraftingHelper (client; not ported, see the spec)
    bool enableMicrocraftingHelper = true;

    // AdjustableChat (experimental, client)
    bool enableAdjustableChat = false;
    int  horizontalShift      = 0;
    int  verticalShift        = 0;

    // ClimateControlRemover (experimental)
    bool enableClimateControlRemover = false;
    bool disableTemperature          = false;
    bool disableHumidity             = false;
    bool disableContinentalness      = false;
    bool disableErosion              = false;
    bool disableDepth                = false;
    bool disableWeirdness            = false;
    bool disableOffset               = false;

    // NarratorReadout (experimental, client)
    bool enableNarratorReadout = false;

    // OverlayShader (experimental, client; not ported, see the spec)
    bool        enableOverlayShader = false;
    std::string shader              = "none";
};

[[nodiscard]] MiscTweaksConfig const& cfg();

} // namespace lp::feature::misctweaks
