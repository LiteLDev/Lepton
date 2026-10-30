#pragma once

// U12 crystals / Magnets: magnetism queries shared with other units and the selftests.

#include <string_view>

class Block;
class BlockPos;
class BlockSource;

namespace lp::feature::crystals {

/// Quark's MagnetSystem#isItemMagnetic (crafted from the derivation list, whitelist, blacklist).
[[nodiscard]] bool isItemMagnetic(std::string_view itemId);
/// Quark's MagnetSystem#isBlockMagnetic.
[[nodiscard]] bool isBlockMagnetic(Block const& block);
/// Re-crawls the recipes (after a config change).
void rebuildMagnetism();
/// Blocks currently moving (shown as lepton:magnetized_block).
[[nodiscard]] int movingBlockCount();
/// The block's BlockMovableComponent movement type (0 push/pull, 1 push, 2 popped, 3 immovable), -1 without one.
[[nodiscard]] int movementTypeOf(Block const& block);
/// MagnetSystem#getPushAction for the block at pos (0 skip, 1 move, 2 break, 3 prevent).
[[nodiscard]] int magnetPushAction(BlockSource& region, BlockPos const& pos, int moveDir);
/// Moves the block `distance` blocks in front of (push) / behind (pull) the magnet one step, as a
/// block event of the magnet would (selftests).
bool magnetMoveBlock(BlockSource& region, BlockPos const& magnetPos, bool push, int distance);

} // namespace lp::feature::crystals
