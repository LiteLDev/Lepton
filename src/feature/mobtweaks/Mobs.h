#pragma once

// Mob tweak helpers shared with the selftests (server thread).

#include <string>

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
/// Drops a dragon scale where `dragon` is 100 ticks from now (DragonScales).
void scheduleDragonScale(Actor& dragon);

} // namespace lp::feature::mobtweaks
