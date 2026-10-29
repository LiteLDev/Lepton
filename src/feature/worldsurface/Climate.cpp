#include "feature/worldsurface/Climate.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GeneratorType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/BiomeNoiseTarget.h"
#include "mc/world/level/biome/BiomeSource3d.h"
#include "mc/world/level/biome/ClimateParameters.h"
#include "mc/world/level/biome/ParameterList.h"
#include "mc/world/level/biome/TargetPoint.h"
#include "mc/world/level/biome/biomeproviders/BlendedMultiNoiseBiomeProvider.h"
#include "mc/world/level/biome/climate_utils/Parameter.h"
#include "mc/world/level/biome/source/BiomeSource.h"
#include "mc/world/level/biome/source/BiomeSourceType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/synth/OverworldNoises3d.h"
#include "mc/world/level/levelgen/WorldGenerator.h"
#include "mc/world/level/levelgen/v1/OverworldGeneratorMultinoise.h"
#include "mc/world/level/storage/LevelData.h"

namespace lp::feature::worldsurface::climate {

namespace {

constexpr int64_t q(double v) { return static_cast<int64_t>(v * 10000.0); }

// Quark GlimmeringWealdModule.postRegister: temperature, humidity full range; continentalness
// [-1.2, 0.75]; erosion [-0.28, 0.08]; depth [1.03, 1.1]; weirdness full range; offset 0.
std::array<std::array<int64_t, 2>, 6> const kRanges{
    {
     {q(-1.0), q(1.0)},
     {q(-1.0), q(1.0)},
     {q(-1.2), q(0.75)},
     {q(-0.28), q(0.08)},
     {q(1.03), q(1.1)},
     {q(-1.0), q(1.0)},
     }
};

int64_t axisDistance(int64_t t, int64_t min, int64_t max) {
    if (t > max) return t - max;
    if (t < min) return min - t;
    return 0;
}

int64_t distanceTo(Target const& t, ClimateParameters const& p) {
    ClimateUtils::Parameter const* params[6] = {
        &*p.mTemperature,
        &*p.mHumidity,
        &*p.mContinentalness,
        &*p.mErosion,
        &*p.mDepth,
        &*p.mWeirdness,
    };
    int64_t sum = 0;
    for (int i = 0; i < 6; ++i) {
        int64_t d = axisDistance(t[i], params[i]->mMin, params[i]->mMax);
        sum += d * d;
    }
    int64_t off = p.mOffset;
    return sum + off * off;
}

struct Index {
    OverworldGeneratorMultinoise const*                                      gen = nullptr;
    bool                                                                     usable = false;
    std::unordered_map<Biome const*, std::vector<ClimateParameters const*>> points;
};

std::atomic<OverworldGeneratorMultinoise const*> gGen{nullptr};
std::atomic<void*>                               gVtable{nullptr};
std::mutex                                       gIndexMutex;
Index                                            gIndex;

void* vtableOf(void const* obj) { return *static_cast<void* const*>(obj); }

/// The vanilla climate points per biome (built once per generator; thread-safe).
Index const& indexFor(OverworldGeneratorMultinoise const& gen) {
    std::lock_guard lock(gIndexMutex);
    if (gIndex.gen == &gen) return gIndex;
    gIndex        = Index{};
    gIndex.gen    = &gen;
    if (!gen.mBiomeSource) return gIndex;
    BiomeSource const& src = *gen.mBiomeSource;
    if (src.getType() != BiomeSourceType::BiomeSource3d) return gIndex;
    auto const& src3d   = static_cast<BiomeSource3d const&>(src);
    auto const& targets = *src3d.mBiomeProvider->mParameterList->mNoiseTargets;
    for (auto const& t : targets) {
        Biome const* b = t.data;
        if (b) gIndex.points[b].push_back(&*t.climateParameters);
    }
    gIndex.usable = !gIndex.points.empty();
    return gIndex;
}

OverworldNoises3d const* noisesOf(OverworldGeneratorMultinoise const& gen) {
    auto const& opt = *gen.mNoises;
    if (!opt.has_value()) return nullptr;
    return &*opt->mCommonNoise;
}

} // namespace

std::array<std::array<int64_t, 2>, 6> const& glimmeringRanges() { return kRanges; }

int64_t glimmeringDistance(Target const& t) {
    int64_t sum = 0;
    for (int i = 0; i < 6; ++i) {
        int64_t d = axisDistance(t[i], kRanges[i][0], kRanges[i][1]);
        sum += d * d;
    }
    return sum;
}

void setGenerator(OverworldGeneratorMultinoise const* gen) {
    if (!gen) return;
    gVtable.store(vtableOf(static_cast<WorldGenerator const*>(gen)), std::memory_order_relaxed);
    gGen.store(gen, std::memory_order_release);
}

OverworldGeneratorMultinoise const* generator() {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto dim = level->getDimension(0).lock();
    if (!dim) return nullptr;
    WorldGenerator* wg = dim->mWorldGenerator;
    if (!wg) return nullptr;
    if (auto const* known = gGen.load(std::memory_order_acquire);
        known && static_cast<WorldGenerator const*>(known) == wg)
        return known;
    if (void* vt = gVtable.load(std::memory_order_relaxed)) {
        // the hook saw a multi-noise generator: the class is identified by its vtable
        if (vtableOf(wg) != vt) return nullptr;
    } else {
        auto const& data = level->getLevelData();
        if (data.getGenerator() != GeneratorType::Overworld) return nullptr;
        if (data.mDataDrivenGeneratorType->has_value()) return nullptr;
    }
    auto const* gen = static_cast<OverworldGeneratorMultinoise const*>(wg);
    if (!noisesOf(*gen)) return nullptr;
    gGen.store(gen, std::memory_order_release);
    return gen;
}

void reset() {
    gGen.store(nullptr);
    std::lock_guard lock(gIndexMutex);
    gIndex = Index{};
}

std::optional<Target> sample(OverworldGeneratorMultinoise const& gen, BlockPos const& pos) {
    auto const* n = noisesOf(gen);
    if (!n) return std::nullopt;
    BlockPos    cell{pos.x & ~3, pos.y & ~3, pos.z & ~3};
    TargetPoint tp = n->sample(cell);
    return Target{tp.mTemperature, tp.mHumidity, tp.mContinentalness, tp.mErosion, tp.mDepth, tp.mWeirdness};
}

bool inRangesBelow(Target const& t) {
    // Quark's ranges with the depth open downwards: below depth 1.1 no vanilla Overworld climate
    // point is nearer (surface biomes sit at depth 1.0, cave biomes at 0.2-0.9, the deep dark
    // needs erosion < -0.375), so the deep part of the area is Glimmering Weald too.
    auto const& R = kRanges;
    return t[2] >= R[2][0] && t[2] <= R[2][1] && t[3] >= R[3][0] && t[3] <= R[3][1] && t[4] >= R[4][0];
}

bool isGlimmeringWeald(OverworldGeneratorMultinoise const& gen, BlockSource* region, BlockPos const& pos) {
    auto t = sample(gen, pos);
    if (!t) return false;
    // at depth < 1.0 a surface biome's point (depth 1.0) or a cave biome is always nearer
    if ((*t)[4] < q(1.0)) return false;
    int64_t dgw = glimmeringDistance(*t);
    if (dgw == 0) return true;
    if (region) {
        auto const& idx = indexFor(gen);
        if (idx.usable) {
            // Java's rule: the nearest climate point wins. Bedrock already chose the nearest
            // vanilla biome for this cell; Glimmering Weald wins when it is nearer than that one.
            Biome const& vanilla = region->getBiome(pos);
            auto         it      = idx.points.find(&vanilla);
            if (it != idx.points.end()) {
                int64_t best = std::numeric_limits<int64_t>::max();
                for (auto const* p : it->second) best = std::min(best, distanceTo(*t, *p));
                return dgw < best;
            }
        }
    }
    return inRangesBelow(*t);
}

bool chunkMayContain(OverworldGeneratorMultinoise const& gen, int x0, int z0, int minY, int maxY) {
    // Climate noise changes slowly horizontally; a cell can only be Glimmering Weald when a nearby
    // sample is deep (depth >= 1) and close to the continentalness / erosion ranges.
    constexpr int64_t m = q(0.15);
    auto const&       R = kRanges;
    for (int dx : {0, 8, 15})
        for (int dz : {0, 8, 15})
            for (int y = minY; y <= maxY; y += 8) {
                auto t = sample(gen, BlockPos{x0 + dx, y, z0 + dz});
                if (!t) return false;
                auto const& v = *t;
                if (v[4] >= q(0.95) && v[2] >= R[2][0] - m && v[2] <= R[2][1] + m && v[3] >= R[3][0] - m && v[3] <= R[3][1] + m)
                    return true;
            }
    return false;
}
std::optional<BlockPos>
locate(OverworldGeneratorMultinoise const& gen, BlockPos const& from, int radius, int step, int minY, int maxY) {
    step = std::max(step, 4);
    for (int r = 0; r <= radius; r += step) {
        std::optional<BlockPos> best;
        int64_t                 bestD = std::numeric_limits<int64_t>::max();
        auto                    test  = [&](int x, int z) {
            for (int y = minY; y <= maxY; y += 8) {
                BlockPos p{x, y, z};
                auto     t = sample(gen, p);
                if (t && (*t)[4] >= q(1.0) && inRangesBelow(*t)) {
                    int64_t dx = x - from.x, dz = z - from.z;
                    int64_t d  = dx * dx + dz * dz;
                    if (d < bestD) {
                        bestD = d;
                        best  = p;
                    }
                    return;
                }
            }
        };
        if (r == 0) test(from.x, from.z);
        else {
            for (int i = -r; i <= r; i += step) {
                test(from.x + i, from.z - r);
                test(from.x + i, from.z + r);
                if (i != -r && i != r) {
                    test(from.x - r, from.z + i);
                    test(from.x + r, from.z + i);
                }
            }
        }
        if (best) return best;
    }
    return std::nullopt;
}

} // namespace lp::feature::worldsurface::climate
