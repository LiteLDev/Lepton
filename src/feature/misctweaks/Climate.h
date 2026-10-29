#pragma once

class ClimateParameters;

namespace lp::feature::misctweaks {

/// Replaces the parameters ClimateControlRemover disables with the dummy range [0, 0] (and the
/// offset with 0). Returns true when something changed.
bool neutralizeClimate(ClimateParameters& parameters);

} // namespace lp::feature::misctweaks
