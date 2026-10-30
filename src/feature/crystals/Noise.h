#pragma once

// Minecraft's SimplexNoise / PerlinSimplexNoise (2D part) seeded with LegacyRandomSource, as used by
// Zeta's ClusterShape for the outline of Quark's underground styles.

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "feature/crystals/Crystals.h"

namespace lp::feature::crystals {

class SimplexNoise {
public:
    explicit SimplexNoise(JavaRandom& r) {
        xo = r.nextDouble() * 256.0;
        yo = r.nextDouble() * 256.0;
        zo = r.nextDouble() * 256.0;
        for (int i = 0; i < 256; ++i) p[i] = i;
        for (int i = 0; i < 256; ++i) {
            int j = r.nextInt(256 - i);
            std::swap(p[i], p[i + j]);
        }
    }

    double getValue(double x, double y) const {
        static double const F2 = 0.5 * (std::sqrt(3.0) - 1.0);
        static double const G2 = (3.0 - std::sqrt(3.0)) / 6.0;
        double              s  = (x + y) * F2;
        int                 i  = floorInt(x + s);
        int                 j  = floorInt(y + s);
        double              t  = (i + j) * G2;
        double              x0 = x - (i - t);
        double              y0 = y - (j - t);
        int                 i1, j1;
        if (x0 > y0) i1 = 1, j1 = 0;
        else i1 = 0, j1 = 1;
        double x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
        double x2 = x0 - 1.0 + 2.0 * G2, y2 = y0 - 1.0 + 2.0 * G2;
        int    ii = i & 255, jj = j & 255;
        int    g0 = perm(ii + perm(jj)) % 12;
        int    g1 = perm(ii + i1 + perm(jj + j1)) % 12;
        int    g2 = perm(ii + 1 + perm(jj + 1)) % 12;
        return 70.0 * (corner(g0, x0, y0, 0.0, 0.5) + corner(g1, x1, y1, 0.0, 0.5) + corner(g2, x2, y2, 0.0, 0.5));
    }

    /// 3D value at the noise's own offsets (PerlinSimplexNoise derives its low-octave seed from it).
    double getValue3(double x, double y, double z) const {
        static double const F3 = 1.0 / 3.0, G3 = 1.0 / 6.0;
        double s = (x + y + z) * F3;
        int    i = floorInt(x + s), j = floorInt(y + s), k = floorInt(z + s);
        double t = (i + j + k) * G3;
        double x0 = x - (i - t), y0 = y - (j - t), z0 = z - (k - t);
        int    i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0) i1 = 1, j1 = 0, k1 = 0, i2 = 1, j2 = 1, k2 = 0;
            else if (x0 >= z0) i1 = 1, j1 = 0, k1 = 0, i2 = 1, j2 = 0, k2 = 1;
            else i1 = 0, j1 = 0, k1 = 1, i2 = 1, j2 = 0, k2 = 1;
        } else if (y0 < z0) i1 = 0, j1 = 0, k1 = 1, i2 = 0, j2 = 1, k2 = 1;
        else if (x0 < z0) i1 = 0, j1 = 1, k1 = 0, i2 = 0, j2 = 1, k2 = 1;
        else i1 = 0, j1 = 1, k1 = 0, i2 = 1, j2 = 1, k2 = 0;
        double x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
        double x2 = x0 - i2 + F3, y2 = y0 - j2 + F3, z2 = z0 - k2 + F3;
        double x3 = x0 - 1.0 + 0.5, y3 = y0 - 1.0 + 0.5, z3 = z0 - 1.0 + 0.5;
        int    ii = i & 255, jj = j & 255, kk = k & 255;
        int    g0 = perm(ii + perm(jj + perm(kk))) % 12;
        int    g1 = perm(ii + i1 + perm(jj + j1 + perm(kk + k1))) % 12;
        int    g2 = perm(ii + i2 + perm(jj + j2 + perm(kk + k2))) % 12;
        int    g3 = perm(ii + 1 + perm(jj + 1 + perm(kk + 1))) % 12;
        return 32.0 * (corner(g0, x0, y0, z0, 0.6) + corner(g1, x1, y1, z1, 0.6) + corner(g2, x2, y2, z2, 0.6)
                       + corner(g3, x3, y3, z3, 0.6));
    }

    double xo = 0, yo = 0, zo = 0;

private:
    static int floorInt(double v) {
        int i = static_cast<int>(v);
        return v < i ? i - 1 : i;
    }
    int           perm(int i) const { return p[i & 255]; }
    static double corner(int g, double x, double y, double z, double base) {
        static constexpr int kGrad[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                             {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
                                             {1, 1, 0}, {0, -1, 1},  {-1, 1, 0}, {0, -1, -1}};
        double t = base - x * x - y * y - z * z;
        if (t < 0) return 0;
        t *= t;
        return t * t * (kGrad[g][0] * x + kGrad[g][1] * y + kGrad[g][2] * z);
    }
    std::array<int, 256> p{};
};

/// PerlinSimplexNoise(LegacyRandomSource(seed), octaves firstOctave..lastOctave) (contiguous list).
class PerlinSimplexNoise {
public:
    PerlinSimplexNoise(int64_t seed, int firstOctave, int lastOctave) {
        JavaRandom random(seed);
        int        i = -firstOctave, j = lastOctave, k = i + j + 1;
        levels.resize(static_cast<size_t>(k));
        auto first = std::make_unique<SimplexNoise>(random);
        auto has   = [&](int o) { return o >= firstOctave && o <= lastOctave; };
        int  l     = j;
        SimplexNoise* zero = first.get();
        if (j >= 0 && j < k && has(0)) levels[static_cast<size_t>(j)] = std::move(first);
        for (int i1 = j + 1; i1 < k; ++i1) {
            if (i1 >= 0 && has(l - i1)) levels[static_cast<size_t>(i1)] = std::make_unique<SimplexNoise>(random);
            else for (int n = 0; n < 262; ++n) random.nextInt();
        }
        if (j > 0) {
            // Java: (long) (value * (double) 9.223372E18F), saturating like Java's double -> long cast
            double  v  = zero->getValue3(zero->xo, zero->yo, zero->zo) * static_cast<double>(9.223372E18F);
            int64_t k1 = v >= 9.2233720368547758e18  ? INT64_MAX
                       : v <= -9.2233720368547758e18 ? INT64_MIN
                                                     : static_cast<int64_t>(v);
            JavaRandom r2(k1);
            for (int j1 = l - 1; j1 >= 0; --j1) {
                if (j1 < k && has(l - j1)) levels[static_cast<size_t>(j1)] = std::make_unique<SimplexNoise>(r2);
                else for (int n = 0; n < 262; ++n) r2.nextInt();
            }
        }
        if (!(j >= 0 && j < k && has(0))) keep = std::move(first);
        inputFactor = std::pow(2.0, j);
        valueFactor = 1.0 / (std::pow(2.0, k) - 1.0);
    }

    double getValue(double x, double y, bool useOffsets) const {
        double d0 = 0.0, d1 = inputFactor, d2 = valueFactor;
        for (auto const& n : levels) {
            if (n) d0 += n->getValue(x * d1 + (useOffsets ? n->xo : 0.0), y * d1 + (useOffsets ? n->yo : 0.0)) * d2;
            d1 /= 2.0;
            d2 *= 2.0;
        }
        return d0;
    }

private:
    std::vector<std::unique_ptr<SimplexNoise>> levels;
    std::unique_ptr<SimplexNoise>              keep;
    double                                     inputFactor = 1, valueFactor = 1;
};

} // namespace lp::feature::crystals
