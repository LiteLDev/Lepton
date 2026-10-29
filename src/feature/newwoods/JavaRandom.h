#pragma once

// java.util.Random (48-bit LCG): the random source of the tree generators and the world generation,
// so a seed always gives the same trees.

#include <cstdint>

namespace lp::feature::newwoods {

class JavaRandom {
public:
    explicit JavaRandom(int64_t seed = 0) { setSeed(seed); }

    void setSeed(int64_t seed) { mSeed = (static_cast<uint64_t>(seed) ^ kMultiplier) & kMask; }

    int next(int bits) {
        mSeed = (mSeed * kMultiplier + kAddend) & kMask;
        return static_cast<int>(static_cast<int64_t>(mSeed) >> (48 - bits));
    }

    /// Uniform in [0, bound). bound <= 0 returns 0.
    int nextInt(int bound) {
        if (bound <= 0) return 0;
        if ((bound & -bound) == bound) return static_cast<int>((static_cast<int64_t>(bound) * next(31)) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val  = bits % bound;
        } while (bits - val + (bound - 1) < 0);
        return val;
    }
    /// Uniform in [lo, hi] (inclusive).
    int nextIntBetween(int lo, int hi) { return lo + nextInt(hi - lo + 1); }

    int64_t nextLong() {
        int64_t hi = static_cast<int64_t>(next(32));
        return static_cast<int64_t>((static_cast<uint64_t>(hi) << 32) + static_cast<uint64_t>(static_cast<int64_t>(next(32))));
    }
    float  nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }
    double nextDouble() {
        return static_cast<double>((static_cast<int64_t>(next(26)) << 27) + next(27)) * (1.0 / static_cast<double>(1LL << 53));
    }

private:
    static constexpr uint64_t kMultiplier = 0x5DEECE66DULL;
    static constexpr uint64_t kAddend     = 0xBULL;
    static constexpr uint64_t kMask       = (1ULL << 48) - 1;
    uint64_t                  mSeed       = 0;
};

} // namespace lp::feature::newwoods
