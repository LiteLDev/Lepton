#pragma once

// Unit-internal plumbing of U12 crystals (tick and block-change fan-out shared by the modules).

#include <cstdint>
#include <functional>
#include <vector>

#include "feature/crystals/Crystals.h"

namespace lp::feature::crystals {

/// Called (server thread) for every layer-0 block change on the server.
using ChangeFn = std::function<void(BlockSource&, BlockPos const&, Block const& before, Block const& after)>;
std::vector<ChangeFn>&              changeListeners();
/// Called once per server tick (after the tick counter advanced).
std::vector<std::function<void()>>& tickListeners();
/// Server ticks since the mod was enabled.
[[nodiscard]] uint64_t currentTick();
/// Runs `fn` on the server thread `delay` (>= 1) ticks from now.
void later(uint64_t delay, std::function<void()> fn);
/// Called (server thread) after Block::neighborChanged of any block: (region, pos, block at pos).
using NeighborFn = std::function<void(BlockSource&, BlockPos const&, Block const&)>;
std::vector<NeighborFn>& neighborListeners();

// Corundum internals exposed for the selftests.
[[nodiscard]] bool clusterCanSurvive(BlockSource& region, BlockPos const& pos);
void               corundumRandomTick(BlockSource& region, BlockPos const& pos);
[[nodiscard]] bool corundumCanGrow(BlockSource& region, BlockPos const& pos);

} // namespace lp::feature::crystals
