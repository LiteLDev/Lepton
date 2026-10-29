#pragma once

// config/enchanting.json: MatrixEnchanting, AncientTomes, ColorRunes (Quark 4.1-488 defaults).

#include <string>
#include <vector>

namespace lp::feature::enchanting {

struct EnchantingConfig {
    int version = 1;

    // ---- MatrixEnchanting ----------------------------------------------------------------------
    bool                     enableMatrixEnchanting          = true;
    int                      maxBookshelves                  = 15;
    int                      piecePriceScale                 = 9;
    int                      bookEnchantability              = 12;
    int                      baseMaxPieceCount               = 3;
    int                      baseMaxPieceCountBook           = 1;
    int                      minLevelCutoff                  = 8;
    int                      chargePerLapis                  = 4;
    double                   minLevelScaleFactor             = 1.2;
    double                   minLevelScaleFactorBook         = 2.0;
    double                   dupeMultiplier                  = 1.4;
    double                   incompatibleMultiplier          = 0.0;
    bool                     allowBooks                      = true;
    bool                     allowTreasures                  = false;
    bool                     allowUndiscoverableEnchantments = false;
    std::vector<std::string> treasureWhitelist;
    std::vector<std::string> undiscoverableWhitelist;
    bool                     showTooltip                = true;
    bool                     normalizeRarity            = true;
    bool                     allowUnderwaterEnchanting  = true;
    bool                     allowShortBlockEnchanting  = true;
    bool                     allowZeroEnchantability    = false;
    bool                     soulCandlesInvert          = true;
    std::vector<std::string> disallowedEnchantments;
    /// One entry per wool colour (white, orange, ..., black): enchantments the candle of that colour
    /// boosts ("id") or dampens ("-id"), comma separated.
    std::vector<std::string> influencesList = {
        "minecraft:unbreaking",
        "minecraft:fire_protection",
        "minecraft:knockback,minecraft:punch",
        "minecraft:feather_falling",
        "minecraft:looting,minecraft:fortune,minecraft:luck_of_the_sea",
        "minecraft:blast_protection",
        "minecraft:silk_touch,minecraft:channeling",
        "minecraft:bane_of_arthropods",
        "minecraft:protection",
        "minecraft:respiration,minecraft:loyalty,minecraft:infinity",
        "minecraft:sweeping_edge,minecraft:multishot",
        "minecraft:efficiency,minecraft:sharpness,minecraft:lure,minecraft:power,minecraft:impaling,minecraft:quick_charge",
        "minecraft:aqua_affinity,minecraft:depth_strider,minecraft:riptide",
        "minecraft:thorns,minecraft:piercing",
        "minecraft:fire_aspect,minecraft:flame",
        "minecraft:smite,minecraft:projectile_protection",
    };
    /// "blockstate;strength;color;enchantments", e.g.
    /// "minecraft:sea_pickle[cluster_count=0,dead_bit=false];1;008000;minecraft:aqua_affinity,-minecraft:thorns".
    std::vector<std::string> statesToInfluences;
    bool                     allowInfluencing     = true;
    int                      influenceMax         = 4;
    double                   influencePower       = 0.125;
    bool                     automaticallyConvert = true;

    // ---- AncientTomes --------------------------------------------------------------------------
    bool                     enableAncientTomes = true;
    std::vector<std::string> lootTables         = {
        "minecraft:chests/stronghold_library,20",
        "minecraft:chests/simple_dungeon,20",
        "minecraft:chests/bastion_treasure,25",
        "minecraft:chests/woodland_mansion,15",
        "minecraft:chests/nether_bridge,0",
        "minecraft:chests/underwater_ruin_big,0",
        "minecraft:chests/underwater_ruin_small,0",
        "minecraft:chests/ancient_city,4",
        "lepton:monster_box,5",
    };
    int                      ancientTomesItemQuality    = 2;
    int                      normalUpgradeCost          = 10;
    int                      limitBreakUpgradeExtraCost = 20;
    int                      maxLimitBreakLevels        = 1;
    std::vector<std::string> enchantNames               = {
        "minecraft:feather_falling", "minecraft:thorns",     "minecraft:sharpness",  "minecraft:smite",
        "minecraft:bane_of_arthropods", "minecraft:knockback", "minecraft:fire_aspect", "minecraft:looting",
        "minecraft:sweeping_edge",   "minecraft:efficiency", "minecraft:unbreaking", "minecraft:fortune",
        "minecraft:power",           "minecraft:punch",      "minecraft:luck_of_the_sea", "minecraft:lure",
        "minecraft:loyalty",         "minecraft:riptide",    "minecraft:impaling",   "minecraft:piercing",
        "minecraft:density",         "minecraft:breach",     "minecraft:wind_burst",
    };
    bool overleveledBooksGlowRainbow    = true;
    bool deepslateTweak                 = true;
    bool deepslateTweakNeedsHaste2      = true;
    bool librariansExchangeAncientTomes = true;
    bool curseGear                      = false;
    bool combineWithBooks               = true;
    bool sanityCheck                    = true;

    // ---- ColorRunes ----------------------------------------------------------------------------
    bool enableColorRunes      = true;
    int  dungeonWeight         = 10;
    int  netherFortressWeight  = 8;
    int  jungleTempleWeight    = 8;
    int  desertTempleWeight    = 8;
    int  colorRunesItemQuality = 0;
};

[[nodiscard]] EnchantingConfig const& cfg();

} // namespace lp::feature::enchanting
