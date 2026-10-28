#pragma once

// AncientTomes and ColorRunes: items, the anvil / smithing combinations (pure functions on stacks)
// and the loot injection.

#include <optional>
#include <string>
#include <vector>

#include "mc/world/item/ItemStack.h"

class Player;

namespace lp::feature::enchanting {

inline constexpr char const* kTomeId = "lepton:ancient_tome";
inline constexpr char const* kRuneId = "lepton:smithing_template_rune";
inline constexpr char const* kRuneTag = "lepton:rune";

// ---- tomes -------------------------------------------------------------------------------------

/// The valid tome enchantments (types), from `enchantNames` (+ sanity check).
[[nodiscard]] std::vector<int> validTomeEnchants();
/// A tome holding `type` at its max level.
[[nodiscard]] ItemStack makeTome(int type);
/// The tome's enchantment (nullopt for "any" tomes and other items).
[[nodiscard]] std::optional<int> tomeEnchant(ItemStackBase const& stack);
[[nodiscard]] bool               isTome(ItemStackBase const& stack);

/// A combination offered by the anvil / smithing helper.
struct Craft {
    std::string kind; ///< "tome", "overlevel", "rune"
    ItemStack   result;
    int         cost = 0;
    bool        usesTome = false;
};

/// Quark's AncientTomesModule.onAnvilUpdate (nullopt = leave it to the vanilla anvil).
[[nodiscard]] std::optional<Craft> anvilCraft(ItemStack const& left, ItemStack const& right);
/// Applies `curseGear` (a random curse) to an anvil result made with a tome.
void addRandomCurse(ItemStack& stack);

// ---- runes -------------------------------------------------------------------------------------

/// Quark's rune colours: the 16 dye colours, "rainbow", "blank".
[[nodiscard]] std::vector<std::string> const& runeColors();
/// The colour a smithing addition gives ("" = not a rune addition).
[[nodiscard]] std::string runeColorOf(ItemStack const& addition);
/// The applied rune colour of a stack (nullopt if none).
[[nodiscard]] std::optional<std::string> appliedRune(ItemStackBase const& stack);
[[nodiscard]] bool                       canHaveRune(ItemStackBase const& stack);
/// Quark's SmithingRuneRecipe (nullopt = not a rune recipe / same colour).
[[nodiscard]] std::optional<Craft> smithingCraft(ItemStack const& templ, ItemStack const& base, ItemStack const& addition);

// ---- loot --------------------------------------------------------------------------------------

/// Extra entries of a loot table's first pool for the given table path ("loot_tables/chests/x.json").
struct LootExtra {
    int                     weight = 0;
    std::string             what; ///< "tome" or "rune"
};
[[nodiscard]] std::vector<LootExtra> lootExtrasFor(std::string const& tableDir);
[[nodiscard]] ItemStack              makeLootItem(std::string const& what);

} // namespace lp::feature::enchanting
