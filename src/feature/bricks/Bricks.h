#pragma once

// U4 bricks: MoreBrickTypes, SoulSandstone, MoreMudBlocks, Midori, DuskboundBlocks (Quark 4.1-488).
// The blocks and recipes are pack content (tools/pack/bricks.ps1); this side gates them by config
// and adds the block-tag behaviour Bedrock has no data-driven form for (see docs/spec/bricks.md).

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lp::feature::bricks {

/// `config/bricks.json`. Keys and defaults as in Quark (docs/spec/bricks.md §5).
struct Config {
    int  version                 = 1;
    bool enableMoreBrickTypes    = true;
    bool enableBlueNetherBricks  = true; ///< This also comes with a utility recipe for Red Nether Bricks
    bool enableSandstoneBricks   = true; ///< This also includes Red Sandstone Bricks and Soul Sandstone Bricks
    bool enableCobblestoneBricks = true; ///< This also includes Mossy Cobblestone Bricks
    bool enableBlackstoneBricks  = true; ///< Requires Cobblestone Bricks to be enabled
    bool enableDirtBricks        = true; ///< Requires Cobblestone Bricks to be enabled
    bool enableNetherrackBricks  = true; ///< Requires Cobblestone Bricks to be enabled
    bool enableSoulSandstone     = true;
    bool enableMoreMudBlocks     = true;
    bool enableMidori            = true;
    bool enableDuskboundBlocks   = true;
};

/// What other units' configs say (read from their files by key; true when the unit is absent).
struct Externals {
    bool verticalSlabs = true; ///< vslabs.json enableVerticalSlabs (U1)
    bool spiralSpires  = true; ///< worldbeyond.json enableSpiralSpires (U29)
    bool duskyMyalite  = true; ///< the item lepton:dusky_myalite exists
};

/// A content group of the table in Content.gen.inc ("blue_nether_bricks", "soul_sandstone", ...).
[[nodiscard]] bool groupEnabled(Config const& c, std::string_view group);
/// The group of an item path of this unit ("" for items that are not ours).
[[nodiscard]] std::string_view groupOf(std::string_view path);
/// Whether an item/block path of this unit is enabled (vertical slabs also need U1's module).
[[nodiscard]] bool itemEnabled(Config const& c, Externals const& e, std::string_view path);
/// Whether a recipe of this unit (name without namespace) loads. Unknown names: true.
[[nodiscard]] bool recipeEnabled(Config const& c, Externals const& e, std::string_view name);
/// True when `name` is one of this unit's recipes.
[[nodiscard]] bool isOurRecipe(std::string_view name);

/// The generated content table: (item path, group) and (recipe name, result path).
using Pairs = std::vector<std::pair<std::string_view, std::string_view>>;
[[nodiscard]] Pairs contentItems();
[[nodiscard]] Pairs contentRecipes();

/// The current externals (reads the other units' config files).
[[nodiscard]] Externals currentExternals();

/// Counters of the content gate (for logs and tests).
struct GateStats {
    int recipesSkipped  = 0;
    int creativeSkipped = 0;
};
[[nodiscard]] GateStats const& gateStats();

/// The soul-speed modifier currently applied to a mob by this unit (0 when none).
inline constexpr char const* kSoulSpeedModifier = "lepton:bricks_soul_speed";

} // namespace lp::feature::bricks
