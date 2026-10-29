#pragma once

// U26 mobtweaks: Quark's tweaks SaferCreatures, SlimesToMagmaCubes, SnowGolemPlayerHeads,
// VexesDieWithTheirMasters, ZombieVillagersOnNormal, DragonScales, GoldToolsHaveFortune,
// DiamondRepair, NoDurabilityOnCosmetics, ShulkerPacking, MagmaKeepsConcretePowder,
// ImprovedSponges and DoubleDoorOpening. Spec: docs/spec/mobtweaks.md.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Item;
class ItemStackBase;

namespace lp::feature::mobtweaks {

/// `config/mobtweaks.json` (keys and defaults: Quark 4.1-488, SPEC §5.1).
struct MobTweaksConfig {
    int version = 1;

    bool   enableSaferCreatures         = true;
    double heightReduction              = 5.0;
    bool   enableSlimeFallDamageRemoval = true;

    bool enableSlimesToMagmaCubes       = true;
    bool enableSnowGolemPlayerHeads     = true;
    bool enableVexesDieWithTheirMasters = true;
    bool enableZombieVillagersOnNormal  = true;
    bool enableDragonScales             = true;

    bool                     enableGoldToolsHaveFortune        = true;
    int                      fortuneLevel                      = 2;
    int                      harvestLevel                      = 2;
    bool                     displayBakedEnchantmentsInTooltip = true;
    bool                     italicTooltip                     = true;
    std::vector<std::string> itemEnchantments{};

    bool                     enableDiamondRepair = true;
    std::vector<std::string> repairChangesList{
        "minecraft:netherite_sword=minecraft:diamond",
        "minecraft:netherite_pickaxe=minecraft:diamond",
        "minecraft:netherite_axe=minecraft:diamond",
        "minecraft:netherite_shovel=minecraft:diamond",
        "minecraft:netherite_hoe=minecraft:diamond",
        "minecraft:netherite_helmet=minecraft:diamond",
        "minecraft:netherite_chestplate=minecraft:diamond",
        "minecraft:netherite_leggings=minecraft:diamond",
        "minecraft:netherite_boots=minecraft:diamond",
    };
    std::vector<std::string> unrepairableItemsList{};
    bool                     enableJeiHints = true;

    bool enableNoDurabilityOnCosmetics = true;
    bool allowCosmeticItems            = true;

    bool enableShulkerPacking           = true;
    bool enableMagmaKeepsConcretePowder = true;

    bool enableImprovedSponges = true;
    int  maximumWaterDrain     = 256;
    int  maximumCrawlDistance  = 10;
    bool enablePlacingOnWater  = true;

    bool enableDoubleDoorOpening = true;
    bool enableDoors             = true;
    bool enableFenceGates        = true;
};

[[nodiscard]] MobTweaksConfig const& cfg();

// ---- parsed lists (rebuilt on every config load) ---------------------------------------------------

/// Enchantment string id ("fortune", "silk_touch") -> Bedrock Enchant::Type value.
using BakedEnchants = std::map<int, int>; ///< enchant type -> level

/// Baked enchantments of an item (GoldToolsHaveFortune), keyed by item full name.
[[nodiscard]] std::unordered_map<std::string, BakedEnchants> const& bakedEnchantments();
/// The baked level of `type` on `stack` (0 when none or the module is off). Cheap: hot path.
[[nodiscard]] int bakedLevel(int enchantType, ItemStackBase const& stack);

/// DiamondRepair: item -> allowed repair items ("minecraft:diamond", ...).
[[nodiscard]] std::unordered_map<std::string, std::vector<std::string>> const& repairChanges();
[[nodiscard]] std::vector<std::string> const&                                   unrepairableItems();
/// nullopt: vanilla decides; otherwise the DiamondRepair answer for `item` repaired with `repair`.
[[nodiscard]] std::optional<bool> repairOverride(std::string const& item, std::string const& repair);

/// Parses "item+enchant@level" (exposed for selftests). Enchant ids may carry "minecraft:".
struct BakedEntry {
    std::string item;
    std::string enchant; ///< without namespace
    int         level = 0;
};
[[nodiscard]] std::optional<BakedEntry> parseBakedEntry(std::string const& entry);
/// Parses "item=repair[,repair...]".
[[nodiscard]] std::optional<std::pair<std::string, std::vector<std::string>>> parseRepairEntry(std::string const& entry);

/// Rebuilds the parsed lists from `cfg()` (also runs after every config load).
void rebuildCaches();

// ---- small shared helpers ------------------------------------------------------------------------

/// Runs `fn` on the server thread after `ticks` server ticks (dropped when the world closes).
void later(int ticks, std::function<void()> fn);
/// Current server tick counter of this unit (increments on every ServerLevelTickEvent).
[[nodiscard]] uint64_t tickNow();

} // namespace lp::feature::mobtweaks
