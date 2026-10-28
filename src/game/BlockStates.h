#pragma once

// Typed access to block states by name (server thread), for vanilla and data-driven blocks alike:
// int, bool and string ("enum") states, including trait states such as
// minecraft:cardinal_direction or minecraft:vertical_half.
//
//     auto dir  = blockstates::getString(block, "minecraft:cardinal_direction");   // "north"
//     auto open = blockstates::getBool(block, "lepton:open");
//     Block const* b = blockstates::with(block, {{"lepton:open", true}});           // nullptr: invalid
//     Block const* s = blockstates::of("lepton:u0_probe_stone_stairs", {{"lepton:shape", "outer_left"}});
//
// The values are read from the block's serialization id (its "states" compound) and a new block
// is looked up through Block::tryGetFromRegistry(name, states), so no state indices are involved.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

class Block;

namespace lp::game::blockstates {

using Value = std::variant<int, bool, std::string>;
/// State changes, applied in order: {{"lepton:open", true}, {"lepton:shape", "straight"}}.
using Changes = std::vector<std::pair<std::string, Value>>;

/// Every state of `block` (name -> value), in the order the game lists them.
[[nodiscard]] std::vector<std::pair<std::string, Value>> all(Block const& block);

/// The value of one state (nullopt when the block has no such state).
[[nodiscard]] std::optional<Value>       get(Block const& block, std::string_view state);
[[nodiscard]] std::optional<std::string> getString(Block const& block, std::string_view state);
/// Int states, and bool states read as 0/1.
[[nodiscard]] std::optional<int> getInt(Block const& block, std::string_view state);
/// Bool states, and int states read as != 0.
[[nodiscard]] std::optional<bool> getBool(Block const& block, std::string_view state);

/// `block` with `changes` applied; nullptr if a state is unknown or a value is out of range.
[[nodiscard]] Block const* with(Block const& block, Changes const& changes);
/// The default state of block `id` with `changes` applied; nullptr if the id or a state is unknown.
[[nodiscard]] Block const* of(std::string_view id, Changes const& changes = {});
/// Copies every state of `from` that `to` also has (e.g. a log's axis onto its stripped log).
[[nodiscard]] Block const* copyStates(Block const& from, Block const& to);

} // namespace lp::game::blockstates
