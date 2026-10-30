#pragma once

// Shared helpers of the misctweaks unit (server and client builds).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "feature/misctweaks/Config.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStackBase;
class Mob;
class MobSpawnerBlockActor;
class Player;
class Vec3;

namespace lp::feature::misctweaks {

// ---- module switches ------------------------------------------------------------------------------

[[nodiscard]] inline bool nerfsOn() { return cfg().enableGameNerfs; }

/// The actor belongs to a client-side level (the client's copy of a local or remote world).
[[nodiscard]] bool onClientSide(Actor const& actor);

// ---- dimensions ----------------------------------------------------------------------------------

/// "minecraft:overworld", "minecraft:the_nether", "minecraft:the_end" (Java names) for dimension ids
/// 0, 1, 2; other ids give "minecraft:dim<n>".
[[nodiscard]] std::string dimensionName(int dimId);
/// True when `name` (Java or Bedrock spelling) names dimension `dimId`.
[[nodiscard]] bool dimensionMatches(std::string_view name, int dimId);

// ---- elytra --------------------------------------------------------------------------------------

/// GameNerfs dimension lock: may elytra glide in the actor's dimension?
[[nodiscard]] bool elytraAllowedIn(Actor const& actor);
/// Java's `canElytraFly`: an elytra with durability left in the chest slot, in an allowed dimension.
[[nodiscard]] bool canElytraFly(Mob const& mob);

/// Sends the player a new velocity (Bedrock players move client-side; this is the knockback path).
void sendMotion(Player& player, Vec3 const& motion);

/// CampfiresBoostElytra: the vertical force for a gliding player whose feet are at `feet`
/// (nullopt: no lit signal campfire below).
[[nodiscard]] std::optional<double>
campfireForce(BlockSource& region, BlockPos const& feet, double boostStrength);
/// Bedrock signal fire rule: a hay bale directly below the campfire.
[[nodiscard]] bool isSignalFire(BlockSource& region, BlockPos const& campfire);

// ---- note blocks ---------------------------------------------------------------------------------

[[nodiscard]] bool isAmethyst(Block const& block);

// ---- game nerfs ----------------------------------------------------------------------------------

/// Mending level of an item (enchantments or stored book enchantments).
[[nodiscard]] int mendingLevel(ItemStackBase const& item);
/// Applies Quark's anvil mending nerf to `result` given the anvil inputs. True when changed.
bool nerfAnvilResult(ItemStackBase const& left, ItemStackBase const& right, ItemStackBase& result);
/// VillageSpawnNerf: chance that the village at block (x, z) is suppressed (Quark's formula
/// p = 1 - d^2 / max^2, then 3 re-picks for a zombie village at 2% each).
[[nodiscard]] bool villageSuppressed(int x, int z, int spawnX, int spawnZ, int maxDist);
/// Does entity type `type` count as non-griefing?
[[nodiscard]] bool isNonGriefing(std::string_view type);

// ---- spawner replacer ----------------------------------------------------------------------------

/// The replacement for spawner mob `from` (normalized "minecraft:..." ids), or "".
[[nodiscard]] std::string spawnerReplacement(std::string_view from);
/// The mob of a monster spawner ("minecraft:pig"), "" when unknown.
[[nodiscard]] std::string spawnerMob(MobSpawnerBlockActor const& spawner);
/// Normalizes an entity id ("pig" -> "minecraft:pig").
[[nodiscard]] std::string normalizeId(std::string_view id);

// ---- install hooks (one pair per file) -----------------------------------------------------------

void installElytra();
void uninstallElytra();
void installNoteBlocks();
void uninstallNoteBlocks();
void installRecipes();
void uninstallRecipes();
void installArmorStands();
void uninstallArmorStands();
void installNerfs();
void uninstallNerfs();
void installSpawners();
void uninstallSpawners();
void installVillagers();
void uninstallVillagers();
void installClimate();
void uninstallClimate();

} // namespace lp::feature::misctweaks
