#pragma once

// U15 InventorySorting: Quark's sort order (docs/spec/inventory.md §3.1).

#include <vector>

class Container;
class ItemStack;

namespace lp::feature::inventory {

/// Quark's item categories, in sort order.
enum class Category {
    Torch,
    Pickaxe,
    Shovel,
    Axe,
    Sword,
    GenericTool,
    Armor,
    Bow,
    Crossbow,
    Trident,
    Arrows,
    Potion,
    Minecart,
    Rail,
    Dye,
    Food,
    EnchantedBook,
    AncientTome,
    Any,
    Block,
};

[[nodiscard]] Category categoryOf(ItemStack const& stack);

/// Three-way comparison (< 0: a first), Quark `SortingHandler.stackCompare`.
[[nodiscard]] int compareStacks(ItemStack const& a, ItemStack const& b);

/// Merges and sorts slots [begin, end) of `c`; `locked` slots keep their item and are skipped.
void sortRange(Container& c, int begin, int end, std::vector<int> const& locked = {});

} // namespace lp::feature::inventory
