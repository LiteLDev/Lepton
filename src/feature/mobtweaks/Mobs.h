#pragma once

// Mob tweak helpers shared with the selftests (server thread).

#include <cstdint>
#include <string>
#include <unordered_set>

class Actor;
class BlockSource;
class ItemStack;
class Vec3;

namespace lp::feature::mobtweaks {

/// A player head named "<name>'s Head" (SnowGolemPlayerHeads).
[[nodiscard]] ItemStack playerHead(std::string const& name);
/// Spawns a slime-like mob ("minecraft:slime", "minecraft:magma_cube") of size 1, 2 or 4.
Actor* spawnSlimeOfSize(BlockSource& region, std::string const& type, int size, Vec3 const& pos);
/// Spawns the 2..4 children of a slime of `size` dying at `at` (Java Slime#remove). Returns the count.
int splitInto(BlockSource& region, std::string const& type, int size, Vec3 const& at, std::string const& name);
/// The size (1, 2 or 4) of a slime-like mob, from its collision box.
[[nodiscard]] int slimeSizeOf(Actor const& slime);
/// Replaces a slime by a magma cube of `size` (SlimesToMagmaCubes). A freshly spawned slime's
/// collision box is only sized on its next tick, so the caller passes the size.
bool replaceWithMagmaCube(Actor& slime, int size);
/// Runs the villager's vanilla zombie transformation (ZombieVillagersOnNormal).
bool zombify(Actor& villager);
/// DragonScales: an ender dragon `ticks` into its death animation. A dragon dying in the End after
/// one was killed before drops one scale when the animation reaches tick 100.
inline constexpr int kDragonScaleTick = 100;
void onDragonDying(int64_t dragonId, int ticks, int dim, Vec3 const& pos);
/// Spawns one dragon scale item at `pos`.
void dropDragonScale(int dim, Vec3 const& pos);

namespace testing {
/// Dragons (unique ids) treated as respawned dragons when their death starts (selftests).
std::unordered_set<int64_t>& forceRespawned();
} // namespace testing

} // namespace lp::feature::mobtweaks
