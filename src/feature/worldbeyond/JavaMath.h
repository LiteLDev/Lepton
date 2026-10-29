#pragma once

// Java-compatible randomness and noise (pure C++, no game headers), so Lepton's world generation
// draws the same numbers as Quark's generators for a given seed:
// - `JavaRandom`: java.util.Random / Minecraft's LegacyRandomSource (48-bit LCG).
// - `decorationSeed` / `featureSeed`: Minecraft's WorldgenRandom seeding of decoration features.
// - `SimplexNoise`, `PerlinSimplexNoise`: Minecraft's simplex noise (used by Quark's myalite colour).
// These are re-implementations of the documented algorithms, not copies of Quark code.

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace lp::feature::worldbeyond {

class JavaRandom {
public:
    explicit JavaRandom(int64_t seed = 0) { setSeed(seed); }

    void setSeed(int64_t seed) { mSeed = (static_cast<uint64_t>(seed) ^ kMultiplier) & kMask; }

    int next(int bits) {
        mSeed = (mSeed * kMultiplier + 0xBULL) & kMask;
        return static_cast<int>(static_cast<int64_t>(mSeed >> (48 - bits)));
    }
    int nextInt() { return next(32); }
    /// Uniform in [0, bound). `bound` must be positive.
    int nextInt(int bound) {
        if ((bound & -bound) == bound) return static_cast<int>((static_cast<int64_t>(bound) * next(31)) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val  = bits % bound;
        } while (static_cast<int32_t>(static_cast<uint32_t>(bits) - static_cast<uint32_t>(val) + static_cast<uint32_t>(bound - 1)) < 0);
        return val;
    }
    int64_t nextLong() {
        int64_t hi = next(32);
        int64_t lo = next(32);
        return static_cast<int64_t>(static_cast<uint64_t>(hi) << 32) + lo;
    }
    bool  nextBoolean() { return next(1) != 0; }
    float nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }
    /// java.util.Random#nextDouble (53 bits).
    double nextDouble() {
        int64_t a = next(26);
        int64_t b = next(27);
        return static_cast<double>((a << 27) + b) * 0x1.0p-53;
    }
    /// Minecraft's RandomSource#nextDouble (the float multiplier `1.110223E-16F`).
    double nextDoubleMc() {
        int64_t a = next(26);
        int64_t b = next(27);
        return static_cast<double>((a << 27) + b) * static_cast<double>(1.110223E-16F);
    }
    void consumeCount(int n) {
        for (int i = 0; i < n; ++i) next(32);
    }

private:
    static constexpr uint64_t kMultiplier = 0x5DEECE66DULL;
    static constexpr uint64_t kMask       = (1ULL << 48) - 1;
    uint64_t                  mSeed       = 0;
};

/// WorldgenRandom#setDecorationSeed: seeds `r` and returns the decoration seed.
inline int64_t decorationSeed(JavaRandom& r, int64_t levelSeed, int minX, int minZ) {
    r.setSeed(levelSeed);
    int64_t a = static_cast<int64_t>(static_cast<uint64_t>(r.nextLong()) | 1ULL);
    int64_t b = static_cast<int64_t>(static_cast<uint64_t>(r.nextLong()) | 1ULL);
    int64_t s = static_cast<int64_t>(
        (static_cast<uint64_t>(static_cast<int64_t>(minX)) * static_cast<uint64_t>(a)
         + static_cast<uint64_t>(static_cast<int64_t>(minZ)) * static_cast<uint64_t>(b))
        ^ static_cast<uint64_t>(levelSeed)
    );
    r.setSeed(s);
    return s;
}

/// WorldgenRandom#setFeatureSeed.
inline void featureSeed(JavaRandom& r, int64_t decoration, int index, int step) {
    r.setSeed(static_cast<int64_t>(static_cast<uint64_t>(decoration) + static_cast<uint64_t>(index) + 10000ULL * static_cast<uint64_t>(step)));
}

/// Java's `(long) d` conversion (saturating, NaN -> 0).
inline int64_t javaD2L(double d) {
    if (std::isnan(d)) return 0;
    if (d >= 9223372036854775807.0) return INT64_MAX;
    if (d <= -9223372036854775808.0) return INT64_MIN;
    return static_cast<int64_t>(d);
}

/// Java's `Mth.floor`.
inline int jfloor(double v) {
    int i = static_cast<int>(v);
    return v < static_cast<double>(i) ? i - 1 : i;
}

class SimplexNoise {
public:
    explicit SimplexNoise(JavaRandom& r) {
        xo = r.nextDoubleMc() * 256.0;
        yo = r.nextDoubleMc() * 256.0;
        zo = r.nextDoubleMc() * 256.0;
        for (int i = 0; i < 256; ++i) mP[i] = i;
        for (int i = 0; i < 256; ++i) {
            int j        = r.nextInt(256 - i);
            int t        = mP[i];
            mP[i]        = mP[i + j];
            mP[i + j]    = t;
        }
    }

    double getValue(double x, double y) const {
        double const f2 = 0.5 * (std::sqrt(3.0) - 1.0);
        double const g2 = (3.0 - std::sqrt(3.0)) / 6.0;
        double       s  = (x + y) * f2;
        int          i  = jfloor(x + s);
        int          j  = jfloor(y + s);
        double       t  = static_cast<double>(i + j) * g2;
        double       x0 = x - (i - t);
        double       y0 = y - (j - t);
        int          i1, j1;
        if (x0 > y0) {
            i1 = 1;
            j1 = 0;
        } else {
            i1 = 0;
            j1 = 1;
        }
        double x1 = x0 - i1 + g2, y1 = y0 - j1 + g2;
        double x2 = x0 - 1.0 + 2.0 * g2, y2 = y0 - 1.0 + 2.0 * g2;
        int    ii = i & 255, jj = j & 255;
        int    g0 = p(ii + p(jj)) % 12;
        int    g1 = p(ii + i1 + p(jj + j1)) % 12;
        int    gg = p(ii + 1 + p(jj + 1)) % 12;
        return 70.0 * (corner(g0, x0, y0, 0.0, 0.5) + corner(g1, x1, y1, 0.0, 0.5) + corner(gg, x2, y2, 0.0, 0.5));
    }

    double getValue(double x, double y, double z) const {
        double const f3 = 1.0 / 3.0;
        double const g3 = 1.0 / 6.0;
        double       s  = (x + y + z) * f3;
        int          i = jfloor(x + s), j = jfloor(y + s), k = jfloor(z + s);
        double       t  = static_cast<double>(i + j + k) * g3;
        double       x0 = x - (i - t), y0 = y - (j - t), z0 = z - (k - t);
        int          i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0) {
                i1 = 1, j1 = 0, k1 = 0, i2 = 1, j2 = 1, k2 = 0;
            } else if (x0 >= z0) {
                i1 = 1, j1 = 0, k1 = 0, i2 = 1, j2 = 0, k2 = 1;
            } else {
                i1 = 0, j1 = 0, k1 = 1, i2 = 1, j2 = 0, k2 = 1;
            }
        } else if (y0 < z0) {
            i1 = 0, j1 = 0, k1 = 1, i2 = 0, j2 = 1, k2 = 1;
        } else if (x0 < z0) {
            i1 = 0, j1 = 1, k1 = 0, i2 = 0, j2 = 1, k2 = 1;
        } else {
            i1 = 0, j1 = 1, k1 = 0, i2 = 1, j2 = 1, k2 = 0;
        }
        double x1 = x0 - i1 + g3, y1 = y0 - j1 + g3, z1 = z0 - k1 + g3;
        double x2 = x0 - i2 + f3, y2 = y0 - j2 + f3, z2 = z0 - k2 + f3;
        double x3 = x0 - 1.0 + 0.5, y3 = y0 - 1.0 + 0.5, z3 = z0 - 1.0 + 0.5;
        int    ii = i & 255, jj = j & 255, kk = k & 255;
        int    ga = p(ii + p(jj + p(kk))) % 12;
        int    gb = p(ii + i1 + p(jj + j1 + p(kk + k1))) % 12;
        int    gc = p(ii + i2 + p(jj + j2 + p(kk + k2))) % 12;
        int    gd = p(ii + 1 + p(jj + 1 + p(kk + 1))) % 12;
        return 32.0
             * (corner(ga, x0, y0, z0, 0.6) + corner(gb, x1, y1, z1, 0.6) + corner(gc, x2, y2, z2, 0.6)
                + corner(gd, x3, y3, z3, 0.6));
    }

    double xo = 0, yo = 0, zo = 0;

private:
    int p(int i) const { return mP[i & 255] & 255; }

    static double corner(int g, double x, double y, double z, double max) {
        static constexpr int kGrad[16][3] = {
            {1, 1, 0},
            {-1, 1, 0},
            {1, -1, 0},
            {-1, -1, 0},
            {1, 0, 1},
            {-1, 0, 1},
            {1, 0, -1},
            {-1, 0, -1},
            {0, 1, 1},
            {0, -1, 1},
            {0, 1, -1},
            {0, -1, -1},
            {1, 1, 0},
            {0, -1, 1},
            {-1, 1, 0},
            {0, -1, -1}
        };
        double d = max - x * x - y * y - z * z;
        if (d < 0.0) return 0.0;
        d *= d;
        return d * d * (kGrad[g][0] * x + kGrad[g][1] * y + kGrad[g][2] * z);
    }

    std::array<int, 512> mP{};
};

/// Minecraft's PerlinSimplexNoise for a contiguous octave range [first, last] (first <= 0 <= last).
class PerlinSimplexNoise {
public:
    PerlinSimplexNoise(JavaRandom& random, int firstOctave, int lastOctave) {
        int i      = -firstOctave;
        int j      = lastOctave;
        int k      = i + j + 1;
        auto first = std::make_unique<SimplexNoise>(random);
        mLevels.resize(k);
        int l = j;
        for (int i1 = l + 1; i1 < k; ++i1) {
            int oct = l - i1;
            if (oct >= firstOctave && oct <= lastOctave) mLevels[i1] = std::make_unique<SimplexNoise>(random);
            else random.consumeCount(262);
        }
        if (j > 0) {
            int64_t    s = javaD2L(first->getValue(first->xo, first->yo, first->zo) * static_cast<double>(9.223372E18F));
            JavaRandom r2(s);
            for (int j1 = l - 1; j1 >= 0; --j1) {
                int oct = l - j1;
                if (j1 < k && oct >= firstOctave && oct <= lastOctave) mLevels[j1] = std::make_unique<SimplexNoise>(r2);
                else r2.consumeCount(262);
            }
        }
        mLevels[l]       = std::move(first); // octave 0
        mInputFactor = std::pow(2.0, j);
        mValueFactor = 1.0 / (std::pow(2.0, k) - 1.0);
    }

    double getValue(double x, double y, bool useOffsets) const {
        double sum = 0.0, in = mInputFactor, val = mValueFactor;
        for (auto const& n : mLevels) {
            if (n) sum += n->getValue(x * in + (useOffsets ? n->xo : 0.0), y * in + (useOffsets ? n->yo : 0.0)) * val;
            in /= 2.0;
            val *= 2.0;
        }
        return sum;
    }

private:
    std::vector<std::unique_ptr<SimplexNoise>> mLevels;
    double                                     mInputFactor = 1, mValueFactor = 1;
};

/// Minecraft's `Mth.sin` / `Mth.cos` (65536-entry lookup table).
inline float mthSin(double v) {
    static std::vector<float> const table = [] {
        std::vector<float> t(65536);
        for (int i = 0; i < 65536; ++i) t[i] = static_cast<float>(std::sin(static_cast<double>(i) * 3.141592653589793 * 2.0 / 65536.0));
        return t;
    }();
    return table[static_cast<int>(javaD2L(v * 10430.378350470453) & 65535)];
}
inline float mthCos(double v) { return mthSin(v + 1.5707963267948966); }

} // namespace lp::feature::worldbeyond
