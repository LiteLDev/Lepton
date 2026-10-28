#pragma once

// ChorusVegetation block behaviour (docs/spec/worldbeyond.md §3.4): teleporting chorus weeds and
// chorus twist. Server thread unless noted.

#include <optional>

#include "mc/world/level/BlockPos.h"

class Actor;
class Block;
class BlockSource;

namespace lp::feature::worldbeyond {

class JavaRandom;

/// Schedules the next "random tick" of a chorus plant (uniform 1..2730 ticks, Java's mean random
/// tick interval). Any thread with the region's chunk loaded.
void scheduleChorusTick(BlockSource& region, BlockPos const& pos, Block const& block);

/// Quark's teleport(pos, random, world, state, range, growthChance). Returns the new position.
std::optional<BlockPos> chorusTeleport(BlockSource& region, BlockPos const& pos, Block const& state, int range, double growthChance);

/// Random tick: teleports with `passiveTeleportChance`.
void chorusRandomTick(BlockSource& region, BlockPos const& pos);
/// A living entity inside the block (weeds only): teleport, maybe spawn an endermite.
void chorusEntityInside(BlockSource& region, BlockPos const& pos, Actor& entity);
/// Water next to the block: teleport (range 8, always moves).
void chorusRunAwayFromWater(BlockSource& region, BlockPos const& pos);
/// Bone meal: 3..5 copies far away, then a move.
void chorusBonemeal(BlockSource& region, BlockPos const& pos);

/// The server-side random used by the plants (server thread).
JavaRandom& serverRandom();

void installChorus();
void uninstallChorus();

} // namespace lp::feature::worldbeyond
