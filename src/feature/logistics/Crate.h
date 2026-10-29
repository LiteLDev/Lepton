#pragma once

// Storage Crate internals shared with the selftests.

#include <vector>

#include "core/GlobalPos.h"

class ItemStack;
class Player;

namespace lp::feature::logistics::crate {

inline constexpr int kCols   = 9;             ///< CrateMenu.numCols
inline constexpr int kRows   = 6;             ///< CrateMenu.numRows
inline constexpr int kWindow = kCols * kRows; ///< CrateMenu.displayedSlots

[[nodiscard]] int                    total(core::GlobalPos const& pos);
[[nodiscard]] int                    stackCount(core::GlobalPos const& pos);
[[nodiscard]] std::vector<ItemStack> contents(core::GlobalPos const& pos);
void                                 setContents(core::GlobalPos const& pos, std::vector<ItemStack> stacks);
/// First shown stack of the open crate (-1 when nobody has it open).
[[nodiscard]] int scrollOf(core::GlobalPos const& pos);
/// Opens the crate for `player` (form first for vanilla clients with many stacks).
bool open(Player& player, core::GlobalPos const& pos);
/// Scrolls the open crate by one row. False when it can't scroll that way.
bool scroll(core::GlobalPos const& pos, bool down);

} // namespace lp::feature::logistics::crate
