#pragma once

// Item helpers (server thread): creating stacks by id, comparing them, (de)serializing them.
//
//     auto stack = lp::game::makeItem("minecraft:stick", 16);
//     std::string snbt = lp::game::itemToSnbt(stack);           // store in the world DB
//     ItemStack back   = lp::game::itemFromSnbt(snbt);

#include <string>
#include <string_view>

class ItemStack;
class ItemStackBase;

namespace lp::game {

/// A stack of `count` items `id` ("minecraft:stone", "lepton:...") with data value `aux`. The count
/// is capped at the item's max stack size (64 for most items).
/// Returns an empty stack (isNull()) when the id is unknown (behaviour pack missing).
[[nodiscard]] ItemStack makeItem(std::string_view id, int count = 1, int aux = 0);

/// Full item id of a stack ("" for an empty stack).
[[nodiscard]] std::string itemId(ItemStackBase const& stack);

/// True for an empty stack (null item or count 0).
[[nodiscard]] bool isEmpty(ItemStackBase const& stack);

/// Same item, data and components (enchantments, name, ...): the stacks may merge.
[[nodiscard]] bool canMerge(ItemStackBase const& a, ItemStackBase const& b);

/// The whole stack (count, data, components) as SNBT ("{}" for an empty stack).
[[nodiscard]] std::string itemToSnbt(ItemStackBase const& stack);
/// Inverse of `itemToSnbt` (empty stack on bad input).
[[nodiscard]] ItemStack itemFromSnbt(std::string_view snbt);

} // namespace lp::game
