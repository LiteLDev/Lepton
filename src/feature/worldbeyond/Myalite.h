#pragma once

// SpiralSpires blocks: myalite crystal colour states, viaducts, renewable myalite.

#include <optional>
#include <string_view>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"

class Actor;
class BlockSource;

namespace lp::feature::worldbeyond {

/// Quark's viaduct redirect for a teleport of `entity` whose feet would land in block `target`.
/// Returns the new feet position (block centre + 0.5), or nullopt to keep the original target.
std::optional<Vec3> viaductTarget(BlockSource& region, BlockPos target, Actor const& entity);

/// Sets the colour state of the crystal at `pos` from its position (no-op for other blocks).
bool fixCrystalHue(BlockSource& region, BlockPos const& pos);

/// Corundum names (U12) that grow: "lepton:<colour>_corundum".
[[nodiscard]] bool isGrowingCorundum(std::string_view id);

void installMyalite();
void uninstallMyalite();

} // namespace lp::feature::worldbeyond
