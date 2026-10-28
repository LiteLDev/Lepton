#pragma once

// U15 ChestSearching: Quark's search matching (docs/spec/inventory.md §3.3). Used by the client
// overlay; testable on the server.

#include <string>
#include <string_view>

class ItemStack;

namespace lp::feature::inventory {

/// Lower case (ASCII) without `§x` formatting codes.
[[nodiscard]] std::string normalizeSearch(std::string_view s);

/// Quark `ChestSearchingModule.namesMatch(stack, search)`.
[[nodiscard]] bool namesMatch(ItemStack const& stack, std::string_view search);

} // namespace lp::feature::inventory
