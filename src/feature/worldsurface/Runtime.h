#pragma once
// Server-thread side of U28 worldsurface: scheduling, the monster box registry and activation,
// and the Glimmering Weald block behaviours (bone meal, huge-mushroom faces, pots, composter,
// silk touch, fog, stoneling spawns).
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/GlobalPos.h"

class Actor;
class BlockPos;
class BlockSource;
class Player;

namespace lp::feature::worldsurface {

class Rng;

// ---- scheduling (Module.cpp) ----------------------------------------------------------------------
[[nodiscard]] uint64_t tickCount();
void                   later(int ticks, std::function<void()> fn);
/// Runs a command as the server in dimension `dim` (fog, playsound, loot ...).
void                   runCommand(std::string const& command, int dim = 0);
[[nodiscard]] Player*  playerById(int64_t id);

// ---- monster boxes (MonsterBox.cpp) ---------------------------------------------------------------
namespace box {
inline constexpr char const* kSpawnedTag = "lepton:monster_box_spawned";
void install();
void uninstall();
/// Queues boxes placed by world generation (any thread); registered on the next tick.
void queueGenerated(int dim, std::vector<BlockPos> const& positions);
void registerBox(core::GlobalPos const& pos);
[[nodiscard]] std::set<core::GlobalPos> const& registry();
/// Activation progress (0 idle).
[[nodiscard]] int  progress(core::GlobalPos const& pos);
/// One server tick of every box near players.
void tick();
/// Spawns the mobs of a broken box at `pos`; returns how many spawned.
int  spawnMobs(BlockSource& region, BlockPos const& pos, Rng& rng);
/// Entity ids (Bedrock) and weights of the spawn table.
[[nodiscard]] std::vector<std::pair<std::string, int>> const& spawnTable();
void onWorldReady();
void onWorldSave();
void onWorldClosed();
} // namespace box

// ---- Glimmering Weald blocks (Glimmering.cpp) -------------------------------------------------------
namespace glimmer {
void install();
void uninstall();
void tick();
void onWorldClosed();
/// Bone meal on a glow shroom / lichen growth (after consuming the bone meal). Returns true when
/// the block was a valid target (the bone meal is used up).
bool boneMeal(BlockSource& region, BlockPos const& pos, Rng& rng);
/// Turns the faces of the huge-shroom block at `pos` that touch the same block into pores, and
/// the touching faces of those neighbours.
void updateFaces(BlockSource& region, BlockPos const& pos);
/// Compost chance (0..1) of an item id (0 = not compostable here).
[[nodiscard]] double compostChance(std::string const& itemId);
/// Adds one item to a composter at `pos` with the roll `roll` (0..1); returns true if the level rose.
bool compost(BlockSource& region, BlockPos const& pos, double chance, double roll);
/// Potted block id for a plant id ("" if none) and back.
[[nodiscard]] std::string pottedOf(std::string const& plant);
[[nodiscard]] std::string plantOf(std::string const& potted);
/// Whether the player stands in Glimmering Weald (fog bookkeeping).
[[nodiscard]] bool playerInGlimmering(Player const& p);
} // namespace glimmer

} // namespace lp::feature::worldsurface
