#pragma once

// U12 crystals / BeaconRedirection: the beam walk shared by the server module, the client hook and
// the selftests.

#include <cstdint>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "feature/crystals/Crystals.h"

namespace lp::feature::crystals {

/// Quark's ExtendedBeamSegment: a straight piece of beam starting at `offset` (relative to the
/// beacon) going `height` blocks towards `dir` (FacingID).
struct BeamSegment {
    int      dir    = 1;
    BlockPos offset = {0, 0, 0};
    int      height = 1;
    uint32_t color  = 0xffffff;
    int      alpha  = 255;
    bool     toSky  = false; ///< ended at the heightmap (Java: height + 1000)
};

struct Beam {
    bool                     broke      = false;
    bool                     redirected = false; ///< a cluster turned the beam
    bool                     quarkTint  = false; ///< tinted by Quark blocks or dimmed by tinted glass
    std::vector<BeamSegment> segments;          ///< empty when broken
};

/// Walks the beam of the beacon at `beacon` like Quark's BeaconRedirectionModule#tickBeacon.
[[nodiscard]] Beam computeBeam(BlockSource& region, BlockPos const& beacon);
/// The lepton:beacon_beam entities the server spawned for the beacon (selftests).
[[nodiscard]] std::vector<int64_t> beamEntitiesOf(core::GlobalPos const& beacon);

} // namespace lp::feature::crystals
