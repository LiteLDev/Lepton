#pragma once

// Java-compatible random numbers and noise for U3 newstones (pure C++, no game types):
// java.util.Random / LegacyRandomSource, String.hashCode, Minecraft's float sine table, Simplex /
// PerlinSimplexNoise (2D) and java.awt.Color.HSBtoRGB. Written from the documented algorithms so
// cluster shapes and the myalite colour follow Quark's (Zeta's) definitions bit for bit.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <string_view>
#include <vector>

namespace lp::feature::newstones {

/// java.util.Random (also Minecraft's LegacyRandomSource / WorldgenRandom over it).
class JavaRandom {
public:
    explicit JavaRandom(int64_t seed) { setSeed(seed); }
    void setSeed(int64_t seed) { mSeed = (static_cast<uint64_t>(seed) ^ kMul) & kMask; }

    int32_t next(int bits) {
        mSeed = (mSeed * kMul + 0xBu) & kMask;
        return static_cast<int32_t>(static_cast<int64_t>(mSeed) >> (48 - bits));
    }
    int32_t nextInt() { return next(32); }
    /// Random.nextInt(bound); bound <= 0 returns 0 (Java throws).
    int32_t nextInt(int32_t bound) {
        if (bound <= 0) return 0;
        if ((bound & -bound) == bound) return static_cast<int32_t>((static_cast<int64_t>(bound) * next(31)) >> 31);
        int32_t bits, val;
        do {
            bits = next(31);
            val  = bits % bound;
        } while (static_cast<int64_t>(bits) - val + (bound - 1) > std::numeric_limits<int32_t>::max());
        return val;
    }
    /// RandomGenerator.nextInt(origin, bound) (java.util.Random does not override it).
    int32_t nextInt(int32_t origin, int32_t bound) {
        int32_t r = nextInt();
        if (origin < bound) {
            uint32_t n = static_cast<uint32_t>(bound) - static_cast<uint32_t>(origin);
            uint32_t m = n - 1;
            if ((n & m) == 0) return static_cast<int32_t>((static_cast<uint32_t>(r) & m) + static_cast<uint32_t>(origin));
            if (static_cast<int32_t>(n) > 0) {
                for (uint32_t u = static_cast<uint32_t>(r) >> 1;; u = static_cast<uint32_t>(nextInt()) >> 1) {
                    uint32_t rr = u % n;
                    // Java: u + m - rr < 0 (int overflow) -> retry
                    if (static_cast<int64_t>(u) + m - rr <= std::numeric_limits<int32_t>::max())
                        return static_cast<int32_t>(rr + static_cast<uint32_t>(origin));
                }
            }
            while (r < origin || r >= bound) r = nextInt();
        }
        return r;
    }
    int64_t nextLong() {
        uint64_t hi = static_cast<uint64_t>(static_cast<int64_t>(next(32))) << 32;
        return static_cast<int64_t>(hi + static_cast<uint64_t>(static_cast<int64_t>(next(32))));
    }
    double nextDouble() {
        return static_cast<double>((static_cast<int64_t>(next(26)) << 27) + next(27)) * 0x1.0p-53;
    }
    float nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

private:
    static constexpr uint64_t kMul  = 0x5DEECE66DULL;
    static constexpr uint64_t kMask = (1ULL << 48) - 1;
    uint64_t                  mSeed = 0;
};

/// Java String.hashCode (ASCII/UTF-16 code units of plain ASCII strings).
[[nodiscard]] inline int32_t javaHash(std::string_view s) {
    uint32_t h = 0;
    for (unsigned char c : s) h = 31u * h + c;
    return static_cast<int32_t>(h);
}

/// Java (int) cast of a float/double: truncation with saturation (NaN -> 0).
template <class F>
[[nodiscard]] inline int32_t javaToInt(F v) {
    if (v != v) return 0;
    if (v >= static_cast<F>(2147483647.0)) return std::numeric_limits<int32_t>::max();
    if (v <= static_cast<F>(-2147483648.0)) return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(v);
}

/// Mth.floor / Mth.ceil.
[[nodiscard]] inline int32_t mthFloor(double v) {
    int32_t i = javaToInt(v);
    return v < i ? i - 1 : i;
}
[[nodiscard]] inline int32_t mthCeil(double v) {
    int32_t i = javaToInt(v);
    return v > i ? i + 1 : i;
}

/// Minecraft's Mth.sin / Mth.cos (65536-entry float table).
class MthTable {
public:
    static MthTable const& get() {
        static MthTable const t;
        return t;
    }
    [[nodiscard]] float sin(float v) const { return mSin[static_cast<uint32_t>(javaToInt(v * 10430.378F)) & 65535u]; }
    [[nodiscard]] float cos(float v) const {
        return mSin[static_cast<uint32_t>(javaToInt(v * 10430.378F + 16384.0F)) & 65535u];
    }

private:
    MthTable() : mSin(65536) {
        for (int i = 0; i < 65536; ++i)
            mSin[static_cast<size_t>(i)] = static_cast<float>(std::sin(static_cast<double>(i) * std::numbers::pi * 2.0 / 65536.0));
    }
    std::vector<float> mSin;
};

/// Minecraft's SimplexNoise (2D value; the 3D value is only used to seed PerlinSimplexNoise).
class SimplexNoise {
public:
    explicit SimplexNoise(JavaRandom& r) {
        xo = r.nextDouble() * 256.0;
        yo = r.nextDouble() * 256.0;
        zo = r.nextDouble() * 256.0;
        for (int i = 0; i < 256; ++i) mP[static_cast<size_t>(i)] = i;
        for (int i = 0; i < 256; ++i) {
            int j = r.nextInt(256 - i);
            std::swap(mP[static_cast<size_t>(i)], mP[static_cast<size_t>(i + j)]);
        }
    }

    [[nodiscard]] double getValue(double x, double y) const {
        double const F2 = 0.5 * (std::sqrt(3.0) - 1.0);
        double const G2 = (3.0 - std::sqrt(3.0)) / 6.0;
        double       d0 = (x + y) * F2;
        int          i  = mthFloor(x + d0);
        int          j  = mthFloor(y + d0);
        double       d1 = static_cast<double>(i + j) * G2;
        double       d4 = x - (static_cast<double>(i) - d1);
        double       d5 = y - (static_cast<double>(j) - d1);
        int          k, l;
        if (d4 > d5) k = 1, l = 0;
        else k = 0, l = 1;
        double d6 = d4 - k + G2, d7 = d5 - l + G2;
        double d8 = d4 - 1.0 + 2.0 * G2, d9 = d5 - 1.0 + 2.0 * G2;
        int    i1 = i & 255, j1 = j & 255;
        int    k1 = p(i1 + p(j1)) % 12;
        int    l1 = p(i1 + k + p(j1 + l)) % 12;
        int    i2 = p(i1 + 1 + p(j1 + 1)) % 12;
        return 70.0 * (corner(k1, d4, d5, 0.0, 0.5) + corner(l1, d6, d7, 0.0, 0.5) + corner(i2, d8, d9, 0.0, 0.5));
    }

    [[nodiscard]] double getValue(double x, double y, double z) const {
        double const F3 = 0.3333333333333333, G3 = 0.16666666666666666;
        double       d1 = (x + y + z) * F3;
        int          i = mthFloor(x + d1), j = mthFloor(y + d1), k = mthFloor(z + d1);
        double       d3 = static_cast<double>(i + j + k) * G3;
        double       d7 = x - (i - d3), d8 = y - (j - d3), d9 = z - (k - d3);
        int          l, i1, j1, k1, l1, i2;
        if (d7 >= d8) {
            if (d8 >= d9) l = 1, i1 = 0, j1 = 0, k1 = 1, l1 = 1, i2 = 0;
            else if (d7 >= d9) l = 1, i1 = 0, j1 = 0, k1 = 1, l1 = 0, i2 = 1;
            else l = 0, i1 = 0, j1 = 1, k1 = 1, l1 = 0, i2 = 1;
        } else if (d8 < d9) l = 0, i1 = 0, j1 = 1, k1 = 0, l1 = 1, i2 = 1;
        else if (d7 < d9) l = 0, i1 = 1, j1 = 0, k1 = 0, l1 = 1, i2 = 1;
        else l = 0, i1 = 1, j1 = 0, k1 = 1, l1 = 1, i2 = 0;
        double d10 = d7 - l + G3, d11 = d8 - i1 + G3, d12 = d9 - j1 + G3;
        double d13 = d7 - k1 + F3, d14 = d8 - l1 + F3, d15 = d9 - i2 + F3;
        double d16 = d7 - 1.0 + 0.5, d17 = d8 - 1.0 + 0.5, d18 = d9 - 1.0 + 0.5;
        int    l2 = i & 255, i3 = j & 255, j3 = k & 255;
        int    k3 = p(l2 + p(i3 + p(j3))) % 12;
        int    l3 = p(l2 + l + p(i3 + i1 + p(j3 + j1))) % 12;
        int    i4 = p(l2 + k1 + p(i3 + l1 + p(j3 + i2))) % 12;
        int    j4 = p(l2 + 1 + p(i3 + 1 + p(j3 + 1))) % 12;
        return 32.0
             * (corner(k3, d7, d8, d9, 0.6) + corner(l3, d10, d11, d12, 0.6) + corner(i4, d13, d14, d15, 0.6)
                + corner(j4, d16, d17, d18, 0.6));
    }

    double xo = 0, yo = 0, zo = 0;

private:
    [[nodiscard]] int p(int i) const { return mP[static_cast<size_t>(i & 255)]; }
    static double     corner(int g, double x, double y, double z, double offset) {
        static constexpr int kGrad[16][3] = {
            {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
            {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};
        double d0 = offset - x * x - y * y - z * z;
        if (d0 < 0.0) return 0.0;
        d0 *= d0;
        return d0 * d0 * (kGrad[g][0] * x + kGrad[g][1] * y + kGrad[g][2] * z);
    }
    std::array<int, 256> mP{};
};

/// Minecraft's PerlinSimplexNoise(new LegacyRandomSource(seed), octaves first..last) (a contiguous
/// octave list, as Zeta and Quark use: -4..4).
class PerlinSimplexNoise {
public:
    PerlinSimplexNoise(int64_t seed, int firstOctave, int lastOctave) {
        JavaRandom random(seed);
        int        i = -firstOctave, j = lastOctave, k = i + j + 1;
        auto       has = [&](int o) { return o >= firstOctave && o <= lastOctave; };
        mLevels.resize(static_cast<size_t>(k));
        auto          first = std::make_unique<SimplexNoise>(random);
        SimplexNoise* zero  = first.get();
        int           l     = j;
        bool          keep  = !(j >= 0 && j < k && has(0));
        if (!keep) mLevels[static_cast<size_t>(j)] = std::move(first);
        for (int i1 = j + 1; i1 < k; ++i1) {
            if (i1 >= 0 && has(l - i1)) mLevels[static_cast<size_t>(i1)] = std::make_unique<SimplexNoise>(random);
            else
                for (int n = 0; n < 262; ++n) random.nextInt();
        }
        if (j > 0) {
            // (long) (value * (double) 9.223372E18F)
            double     v  = zero->getValue(zero->xo, zero->yo, zero->zo) * static_cast<double>(9.223372E18F);
            int64_t    k1 = v != v                     ? 0
                          : v >= 9.2233720368547758e18  ? std::numeric_limits<int64_t>::max()
                          : v <= -9.2233720368547758e18 ? std::numeric_limits<int64_t>::min()
                                                        : static_cast<int64_t>(v);
            JavaRandom r2(k1);
            for (int j1 = l - 1; j1 >= 0; --j1) {
                if (j1 < k && has(l - j1)) mLevels[static_cast<size_t>(j1)] = std::make_unique<SimplexNoise>(r2);
                else
                    for (int n = 0; n < 262; ++n) r2.nextInt();
            }
        }
        if (keep) mKeep = std::move(first);
        mInputFactor = std::pow(2.0, j);
        mValueFactor = 1.0 / (std::pow(2.0, k) - 1.0);
    }

    [[nodiscard]] double getValue(double x, double y, bool useOffsets) const {
        double d0 = 0.0, d1 = mInputFactor, d2 = mValueFactor;
        for (auto const& n : mLevels) {
            if (n) d0 += n->getValue(x * d1 + (useOffsets ? n->xo : 0.0), y * d1 + (useOffsets ? n->yo : 0.0)) * d2;
            d1 /= 2.0;
            d2 *= 2.0;
        }
        return d0;
    }

private:
    std::vector<std::unique_ptr<SimplexNoise>> mLevels;
    std::unique_ptr<SimplexNoise>              mKeep;
    double                                     mInputFactor = 1, mValueFactor = 1;
};

/// java.awt.Color.HSBtoRGB, returns 0xRRGGBB.
[[nodiscard]] inline uint32_t hsbToRgb(float hue, float saturation, float brightness) {
    int r = 0, g = 0, b = 0;
    if (saturation == 0) {
        r = g = b = javaToInt(brightness * 255.0f + 0.5f);
    } else {
        float h = (hue - static_cast<float>(std::floor(hue))) * 6.0f;
        float f = h - static_cast<float>(std::floor(h));
        float p = brightness * (1.0f - saturation);
        float q = brightness * (1.0f - saturation * f);
        float t = brightness * (1.0f - (saturation * (1.0f - f)));
        auto  c = [](float v) { return javaToInt(v * 255.0f + 0.5f); };
        switch (javaToInt(h)) {
        case 0: r = c(brightness), g = c(t), b = c(p); break;
        case 1: r = c(q), g = c(brightness), b = c(p); break;
        case 2: r = c(p), g = c(brightness), b = c(t); break;
        case 3: r = c(p), g = c(q), b = c(brightness); break;
        case 4: r = c(t), g = c(p), b = c(brightness); break;
        case 5: r = c(brightness), g = c(p), b = c(q); break;
        default: break;
        }
    }
    return (static_cast<uint32_t>(r & 255) << 16) | (static_cast<uint32_t>(g & 255) << 8) | static_cast<uint32_t>(b & 255);
}

} // namespace lp::feature::newstones
