#pragma once
// Glimmering Weald placement: Quark's climate point evaluated with the game's own Overworld
// climate noise (docs/spec/worldsurface.md §3 "Glimmering Weald").
#include <array>
#include <cstdint>
#include <optional>

class BlockPos;
class BlockSource;
class OverworldGeneratorMultinoise;

namespace lp::feature::worldsurface::climate {

/// A sampled climate target (Java quantized: value * 10000): temperature, humidity,
/// continentalness, erosion, depth, weirdness.
using Target = std::array<int64_t, 6>;

/// Quark's Glimmering Weald parameter ranges [min, max] per axis (quantized), offset 0.
[[nodiscard]] std::array<std::array<int64_t, 2>, 6> const& glimmeringRanges();
/// Squared Java "fitness" distance of a target to Quark's point.
[[nodiscard]] int64_t glimmeringDistance(Target const& t);

/// Quark's ranges with the depth open downwards (the area Glimmering Weald wins without a vanilla
/// comparison).
[[nodiscard]] bool inRangesBelow(Target const& t);

/// Remembers the Overworld multi-noise generator (world-gen hook; any thread).
void setGenerator(OverworldGeneratorMultinoise const* gen);
/// The current world's generator (nullptr: flat / legacy / no world / not resolved yet).
/// Resolves it from the Overworld dimension when needed (server thread).
[[nodiscard]] OverworldGeneratorMultinoise const* generator();
/// Forgets the generator and its biome index (world closed).
void reset();

/// The climate target at a block (sampled at its 4x4x4 cell corner like Java's biome lookup).
[[nodiscard]] std::optional<Target> sample(OverworldGeneratorMultinoise const& gen, BlockPos const& pos);

/// Whether the block's cell belongs to the Glimmering Weald. `region` (optional) gives the
/// vanilla biome Bedrock chose there, whose climate points decide the nearest-point rule;
/// without it the target must lie inside Quark's ranges.
[[nodiscard]] bool isGlimmeringWeald(OverworldGeneratorMultinoise const& gen, BlockSource* region, BlockPos const& pos);

/// Cheap pre-check for a 16x16 column area: false when no cell of it can be Glimmering Weald.
[[nodiscard]] bool chunkMayContain(OverworldGeneratorMultinoise const& gen, int x0, int z0, int minY, int maxY);

/// Nearest Glimmering Weald position (ranges rule), spiral search like `/locate biome`.
[[nodiscard]] std::optional<BlockPos>
locate(OverworldGeneratorMultinoise const& gen, BlockPos const& from, int radius, int step, int minY, int maxY);

} // namespace lp::feature::worldsurface::climate
