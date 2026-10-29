#pragma once

// Shared helpers of the mobsoverworld unit (server thread).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Actor;
class BlockPos;
class BlockSource;
class ItemStack;
class Mob;
class Player;
class Vec3;

namespace lp::feature::mobsoverworld {

inline constexpr std::string_view kCrab      = "lepton:crab";
inline constexpr std::string_view kShiba     = "lepton:shiba";
inline constexpr std::string_view kToretoise = "lepton:toretoise";

// ---- entity properties (bp/entities/*.json "properties") ------------------------------------
/// Reads a property through Molang (`q.property('<name>')`); 0 when missing.
[[nodiscard]] int  intProperty(Actor& actor, std::string_view name);
[[nodiscard]] bool boolProperty(Actor& actor, std::string_view name);
/// Queues a property value (applied at the end of the tick, synced to clients when client_sync).
bool setIntProperty(Actor& actor, std::string_view name, int value);
bool setBoolProperty(Actor& actor, std::string_view name, bool value);

// ---- world --------------------------------------------------------------------------------------
[[nodiscard]] int64_t gameTick();
/// 0 peaceful .. 3 hard.
[[nodiscard]] int difficulty();
[[nodiscard]] int randomInt(int bound); ///< [0, bound)
[[nodiscard]] float randomFloat();     ///< [0, 1)

/// Plays a (vanilla) sound definition at `pos` for the players near it.
void playSound(Actor const& near, std::string const& sound, Vec3 const& pos, float volume = 1, float pitch = 1);
/// Spawns a particle effect ("minecraft:heart_particle", ...) `count` times around `pos` (spread).
void particles(Actor const& near, std::string const& effect, Vec3 const& pos, int count = 1, float spreadXZ = 0,
               float spreadY = 0);
/// Block-break particles of the block at `pos` (level event 2001).
void blockBreakParticles(BlockSource& region, BlockPos const& pos);

// ---- actors -------------------------------------------------------------------------------------
[[nodiscard]] Mob* asMob(Actor& actor);
/// The block below the actor's feet.
[[nodiscard]] BlockPos below(Actor const& actor);
/// The player with `uniqueId` (nullptr when gone).
[[nodiscard]] Player* playerById(int64_t uniqueId);
[[nodiscard]] bool    isCreative(Player const& player);
/// Item in the player's selected slot (copy) and writes it back (synced).
void setSelected(Player& player, ItemStack const& stack);
/// Consumes one of the held item unless the player is in creative mode.
void consumeSelected(Player& player);
/// Gives `stack` to the player or drops it at their feet.
void giveOrDrop(Player& player, ItemStack const& stack);
/// Drops `stack` at `pos` (item entity with a small random motion).
void dropItem(BlockSource& region, Vec3 const& pos, ItemStack const& stack);
/// Starts a path to `pos` with `speed` (false when no path).
bool navigateTo(Actor& actor, Vec3 const& pos, float speed);
void stopNavigation(Actor& actor);
/// Makes the mob look at `pos`.
void lookAt(Actor& actor, Vec3 const& pos);
/// Gives the actor an upward push (vertical motion += `dy`).
void pushUp(Actor& actor, float dy);

/// Has the item tag ("minecraft:is_pickaxe", ...).
[[nodiscard]] bool itemHasTag(ItemStack const& stack, std::string_view tag);
[[nodiscard]] bool isWaterBucket(ItemStack const& stack);

/// Dye colour index (Java DyeColor ids, white 0 .. black 15) of a dye item, or -1.
[[nodiscard]] int dyeColorOf(ItemStack const& stack);

} // namespace lp::feature::mobsoverworld
