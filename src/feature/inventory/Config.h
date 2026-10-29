#pragma once

// U15 inventory: `config/inventory.json` (docs/spec/inventory.md §5). Quark's option names and
// defaults; one `enable<Module>` per Quark module.

#include <string>
#include <vector>

namespace lp::feature::inventory {

/// RGBA colour, 0..1 (Quark `RGBAColorConfig`).
struct RgbaColor {
    double r = 0, g = 0, b = 0, a = 0.67;
};

struct InventoryConfig {
    int version = 1;

    // InventorySorting
    bool enableInventorySorting        = true;
    bool enablePlayerInventory         = true;
    bool enablePlayerInventoryInChests = true;
    bool enableChests                  = true;
    /// Play a click when sorting inventories using keybindings.
    bool satisfyingClick = true;

    // EasyTransferring
    bool enableEasyTransferring = true;
    bool enableShiftLock        = true;

    // ChestSearching
    bool      enableChestSearching = true;
    RgbaColor overlayColor{};

    // HotbarChanger
    bool   enableHotbarChanger = true;
    double animationTime       = 7; ///< ticks

    // AutomaticToolRestock
    bool enableAutomaticToolRestock = true;
    /// Important Enchantments: special priority when finding a replacement.
    std::vector<std::string> enchantNames{"minecraft:silk_touch", "minecraft:fortune", "minecraft:infinity",
                                          "minecraft:luck_of_the_sea", "minecraft:looting"};
    /// Replace tools with tools of the same type but not the same item.
    bool enableLooseMatching = true;
    /// Compare enchantments to find a replacement.
    bool enableEnchantMatching = true;
    /// Allow pulling items from one hotbar slot to another.
    bool checkHotbar      = false;
    bool unstackablesOnly = false;
    /// Items in this list are ignored by the restock feature.
    std::vector<std::string> ignoredItems{"quark:trowel", "botania:exchange_rod", "botania:dirt_rod",
                                          "botania:skydirt_rod", "botania:cobble_rod"};

    // QuickArmorSwapping
    bool enableQuickArmorSwapping = true;
    bool swapOffHand              = true;

    // ExpandedItemInteractions
    bool enableExpandedItemInteractions = true;
    bool enableArmorInteraction         = true;
    bool enableShulkerBoxInteraction    = true;
    bool enableLavaInteraction          = true;
    bool allowOpeningShulkerBoxes       = true;
    bool allowRotatingBundles           = true;
    /// Use left clicks instead of right clicks for all Expanded Item Interactions.
    bool invertClicks = false;

    // ItemSharing
    bool enableItemSharing = true;
    int  cooldown          = 100; ///< ticks
    bool renderItemsInChat = true;

    /// Quark general config: container screens need at least this many container slots for the
    /// sort / transfer / filter buttons.
    int chestButtonSlotTarget = 27;
};

[[nodiscard]] InventoryConfig const& cfg();

} // namespace lp::feature::inventory
