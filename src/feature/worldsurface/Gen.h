#pragma once
// The world generators of U28 worldsurface. Every function writes through `region` only, so the
// same code runs inside the chunk decoration hook (generation threads) and in selftests.
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "feature/worldsurface/Common.h"

class BlockSource;
class OverworldGeneratorMultinoise;

namespace lp::feature::worldsurface::gen {

struct Ctx {
    BlockSource&    region;
    GenState const& st;
    Rng&            rng;
    int             dim = 0;
    /// Glimmering Weald test (nullptr: never).
    std::function<bool(BlockPos const&)> isGlimmering;
    /// Monster boxes placed by this context (registered on the server thread afterwards).
    std::vector<BlockPos>* boxes = nullptr;
};

/// Feature counters since start (`/quark worldgen stats`, selftests).
struct Stats {
    std::atomic<uint64_t> chunks{0}, fairyRings{0}, fallenLogs{0}, monsterBoxes{0}, petalPatches{0}, glimmeringChunks{0},
        hugeShrooms{0}, glowShrooms{0}, lapisVeins{0};
};
[[nodiscard]] Stats& stats();

// ---- per chunk (x0, z0 = the chunk's north-west corner) ------------------------------------------
void fairyRingsChunk(Ctx& c, int x0, int z0);
void fallenLogsChunk(Ctx& c, int x0, int z0);
void monsterBoxChunk(Ctx& c, int x0, int z0);
void petalsChunk(Ctx& c, int x0, int z0);
void glimmeringChunk(Ctx& c, int x0, int z0);

// ---- single features ----------------------------------------------------------------------------------
/// Builds a fairy ring around `pos` (one block below the surface dirt). `flower` (optional) forces
/// the ring's flower. Returns the ore position when an ore was placed.
std::optional<BlockPos> spawnFairyRing(Ctx& c, BlockPos const& pos, Block const* flower = nullptr);
/// A fallen log at `pos` (the cell above the ground). Returns true when placed.
bool placeFallenLog(Ctx& c, BlockPos const& pos, GenState::LogEntry const* forced = nullptr);
/// Java MonsterBoxGenerator.canPlaceHere.
[[nodiscard]] bool monsterBoxCanPlace(Ctx& c, BlockPos const& pos);
/// A water petal (or carpet) patch with its north-west corner at `corner`.
void placePetals(Ctx& c, BlockPos const& corner);
/// GlowShroomsFeature.placeHugeGlowShroom; false when the ground is not deepslate or no room.
bool placeHugeGlowShroom(BlockSource& region, GenState const& st, Rng& rng, BlockPos const& pos);
/// GlowShroomsFeature.place: a huge shroom, else a single glow shroom.
void glowShroomAt(Ctx& c, BlockPos const& pos);
/// GlowExtrasFeature.place.
void glowExtrasAt(Ctx& c, BlockPos const& pos);
/// An ore_lapis vein (size 7) at `pos`.
void lapisVeinAt(Ctx& c, BlockPos const& pos);

} // namespace lp::feature::worldsurface::gen
