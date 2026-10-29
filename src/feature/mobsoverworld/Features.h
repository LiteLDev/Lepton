#pragma once

// Internal interface between the mobsoverworld feature files (server thread).

#include <cstdint>
#include <string>

class Actor;
class ActorDamageSource;
class BlockPos;
class ItemStack;
class Player;
class Vec3;

namespace lp::feature::mobsoverworld {

/// Result of a native entity interaction.
enum class Handled { No, Yes };

namespace crabs {
/// Per-tick logic of one crab (`now` = game tick).
void tick(Actor& crab, int64_t now);
/// Jukeboxes and rave checks (every tick; works on a 20 tick period).
void tickWorld(int64_t now);
/// Returns true to cancel the damage; may change `damage` (armour).
bool onHurt(Actor& crab, ActorDamageSource const& source, float& damage);
Handled interact(Player& player, Actor& crab);
/// A player used a Bucket of Crab on a block face: places water + the crab (next tick).
bool useBucket(Player& player, BlockPos const& clicked, int face);
void jukeboxStarted(int dim, BlockPos const& pos);
void jukeboxStopped(int dim, BlockPos const& pos);
/// Lightning growth (also used by the tests): grows when allowed, returns true if it grew.
bool strikeLightning(Actor& crab, int64_t now);
/// Size step 0..8 (scale 1, 3, 5 ... 17).
[[nodiscard]] int sizeOf(Actor& crab);
[[nodiscard]] bool isRaving(Actor& crab);
/// Item stack of a Bucket of Crab holding `crab` (as the bucket pickup makes it).
[[nodiscard]] ItemStack bucketOf(Actor& crab);
/// Spawns the crab stored in a bucket stack at `pos` (dimension `dim`).
Actor* releaseFromBucket(ItemStack const& bucket, int dim, Vec3 const& pos);
/// Drops the state of entities that are gone (periodic).
void prune();
void clear();
} // namespace crabs

namespace shiba {
void    tick(Actor& shiba, int64_t now);
bool    onHurt(Actor& shiba, ActorDamageSource const& source, float& damage);
Handled interact(Player& player, Actor& shiba);
/// After the vanilla interaction ran: sitting toggle / taming side effects.
void    afterInteract(Player& player, Actor& shiba, bool wasTame, bool wasSitting, bool vanillaConsumed);
void    onDeath(Actor& shiba);
/// The dark spot the shiba points at (tests), or false.
bool    hyperfocus(Actor& shiba, BlockPos& out);
/// The arrow being fetched (unique id) or 0 (tests).
int64_t fetching(Actor& shiba);
void    prune();
void    clear();
} // namespace shiba

namespace toretoise {
void    tick(Actor& toretoise, int64_t now);
bool    onHurt(Actor& toretoise, ActorDamageSource const& source, float& damage);
Handled interact(Player& player, Actor& toretoise);
/// Drops the ore mined with `tool` (may be empty) and clears it.
void    dropOre(Actor& toretoise, ItemStack const& tool, Player* miner);
/// Quark's setInLove: eat, heal, tame or regrow.
void    feed(Actor& toretoise, int64_t now);
[[nodiscard]] int oreOf(Actor& toretoise);
void    setOre(Actor& toretoise, int ore);
[[nodiscard]] int angerOf(Actor& toretoise);
void    prune();
void    clear();
} // namespace toretoise

namespace resilience {
void tick(int64_t now);
/// Gives the effect (duration in ticks, amplifier 0 = I).
void apply(Actor& target, int duration, int amplifier);
void clearEffect(Actor& target);
/// Remaining ticks (0 = none) and amplifier.
[[nodiscard]] int remaining(Actor& target, int* amplifier = nullptr);
/// A drink of `itemId` finished (potions, milk).
void onDrink(Player& player, std::string const& itemId);
/// Adds the brewing mixes once per PotionBrewing init.
void addBrewingMixes();
void resetBrewingMixes();
/// Brewing result of `ingredient` into `source` (ids "minecraft:potion" + aux), for tests.
[[nodiscard]] std::string brew(std::string const& source, int sourceAux, std::string const& ingredient, int* aux);
void load();
void save();
void prune();
void clear();
/// Called for every loaded entity each tick (projectile tracking, effect re-application).
void see(Actor& actor, int64_t now);
} // namespace resilience

namespace trades {
void install();
void uninstall();
} // namespace trades

namespace spawning {
/// Writes the spawn rule files (and the oretoise tempt items) of the behaviour pack from the config.
void writePackFiles();
/// Natural spawn check of a Lepton mob at `pos`; false cancels.
[[nodiscard]] bool allowed(std::string const& type, int dim, BlockPos const& pos, bool natural);
} // namespace spawning

} // namespace lp::feature::mobsoverworld
