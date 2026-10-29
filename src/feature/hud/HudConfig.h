#pragma once

// U16 hud: config section `hud` (config/hud.json). Quark 4.1-488 option names and defaults of
// UsageTicker, ElytraIndicator, ImprovedTooltips, AutoWalkKeybind and BackButtonKeybind.

#include <string>
#include <vector>

namespace lp::feature::hud {

struct HudConfig {
    int version = 1;

    // UsageTicker
    bool enableUsageTicker = true;
    bool invert            = false;
    int  shiftLeft         = 0;
    int  shiftRight        = 0;
    bool enableMainHand    = true;
    bool enableOffHand     = true;
    bool enableArmor       = true;

    // ElytraIndicator
    bool enableElytraIndicator = true;

    // ImprovedTooltips
    bool                     enableImprovedTooltips   = true;
    bool                     attributeTooltips        = true;
    bool                     foodTooltips             = true;
    bool                     shulkerTooltips          = true;
    bool                     mapTooltips              = true;
    bool                     enchantingTooltips       = true;
    bool                     fuelTimeTooltips         = true;
    bool                     shulkerBoxUseColors      = true;
    bool                     shulkerBoxRequireShift   = false;
    bool                     mapRequireShift          = false;
    int                      foodDivisor              = 2;
    bool                     showSaturation           = true;
    int                      foodCompressionThreshold = 4;
    int                      fuelTimeDivisor          = 200;
    bool                     showUpgradeStatus        = true;
    bool                     animateUpDownArrows      = true;
    std::vector<std::string> enchantingStacks         = {
        "minecraft:diamond_sword",
        "minecraft:diamond_pickaxe",
        "minecraft:diamond_shovel",
        "minecraft:diamond_axe",
        "minecraft:diamond_hoe",
        "minecraft:diamond_helmet",
        "minecraft:diamond_chestplate",
        "minecraft:diamond_leggings",
        "minecraft:diamond_boots",
        "minecraft:mace",
        "minecraft:shears",
        "minecraft:bow",
        "minecraft:fishing_rod",
        "minecraft:crossbow",
        "minecraft:trident",
        "minecraft:elytra",
        "minecraft:shield",
        "lepton:pickarang",
    };
    std::vector<std::string> enchantingAdditionalStacks = {};

    // AutoWalkKeybind
    bool enableAutoWalkKeybind = true;
    bool drawHud               = true;
    int  hudHeight             = 10;

    // BackButtonKeybind
    bool enableBackButtonKeybind = true;
    /// Mouse button bound to Back: 4 = X1, 5 = X2 (Quark's default, GLFW button index 4), 0 = none.
    int backMouseButton = 5;
};

[[nodiscard]] HudConfig const& cfg();

} // namespace lp::feature::hud
