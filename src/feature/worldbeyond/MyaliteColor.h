#pragma once

// Quark's myalite colour field (MyaliteColorLogic): a fixed-seed simplex noise over block
// positions gives a hue; saturation 0.7, brightness 0.8. Pure C++.

#include <cmath>

#include "feature/worldbeyond/JavaMath.h"

namespace lp::feature::worldbeyond {

/// Hue in [0, 1) at a block position (Quark: `h = noise * 0.15 - 0.25`, wrapped).
inline double myaliteHue(int bx, int by, int bz) {
    static PerlinSimplexNoise const noise = [] {
        JavaRandom r(4543543);
        return PerlinSimplexNoise(r, -4, 4);
    }();
    float const sp = 0.15f;
    float       x  = static_cast<float>(bx) * sp;
    float       y  = static_cast<float>(by) * sp;
    float       z  = static_cast<float>(bz) * sp;
    double      xv = x + mthSin(z) * 2.0f;
    double      zv = z + mthCos(x) * 2.0f;
    double      yv = y + mthSin(y + 3.1415927f / 4.0f) * 2.0f;
    double      n  = noise.getValue(xv + yv, zv + yv * 2.0, false);
    double      h  = n * (0.3 / 2.0) - 0.3 + 0.05;
    return h - std::floor(h);
}

/// The hue range the 16 crystal colours cover (bucket b has hue 0.6 + b * 0.02).
inline constexpr double kHueMin  = 0.6;
inline constexpr double kHueStep = 0.02;

/// Quantised hue (0..15) of a position.
inline int myaliteHueStep(int bx, int by, int bz) {
    double h = myaliteHue(bx, by, bz);
    if (h < 0.3) h += 1.0; // hues just past 1.0 (red) belong after violet
    int b = static_cast<int>(std::lround((h - kHueMin) / kHueStep));
    return b < 0 ? 0 : (b > 15 ? 15 : b);
}

} // namespace lp::feature::worldbeyond
