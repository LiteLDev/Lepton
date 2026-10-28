#pragma once

// U22 throwables: Pickarang, TorchArrow, ParrotEggs, SkullPikes, BottledCloud, SlimeInABucket
// (Quark 4.1-488 "tools" modules). Behaviour: docs/spec/throwables.md.

#include <cstdint>
#include <string>
#include <string_view>

class Actor;
class BlockPos;
class BlockSource;
class ItemStack;
class ItemStackBase;
class Player;
class Vec3;

namespace lp::feature::throwables {

/// Quark's PickarangType options (one object per pickarang kind).
struct PickarangType {
    int    timeout        = 20;
    int    harvestLevel   = 3;
    int    durability     = 800;
    double maxHardness    = 20.0;
    int    attackDamage   = 2;
    int    cooldown       = 10;
    bool   canActAsHoe    = false;
    bool   canActAsShovel = true;
    bool   canActAsAxe    = true;
};

struct ThrowablesConfig {
    int version = 1;

    bool          enablePickarang        = true;
    PickarangType pickarangType          = {20, 3, 800, 20.0, 2, 10, false, true, true};
    PickarangType flamerangType          = {20, 4, 1040, 20.0, 3, 10, false, true, true};
    bool          enableFlamerang        = true;
    bool          neverUseHeartOfDiamond = false;

    bool enableTorchArrow = true;
    bool extinguishOnMiss = false;

    bool   enableParrotEggs = true;
    double chance           = 0.05;
    int    eggTime          = 12000;
    bool   enableKotobirb   = true;

    bool   enableSkullPikes = true;
    double pikeRange        = 5;

    bool enableBottledCloud = true;
    int  cloudLevelBottom   = 191;
    int  cloudLevelTop      = 196;

    bool enableSlimeInABucket = true;
};

[[nodiscard]] ThrowablesConfig const& cfg();

// ---- ids --------------------------------------------------------------------------------------
inline constexpr std::string_view kPickarang      = "lepton:pickarang";
inline constexpr std::string_view kFlamerang      = "lepton:flamerang";
inline constexpr std::string_view kTorchArrow     = "lepton:torch_arrow";
inline constexpr std::string_view kBottledCloud   = "lepton:bottled_cloud";
inline constexpr std::string_view kCloud          = "lepton:cloud";
inline constexpr std::string_view kSlimeBucket    = "lepton:slime_in_a_bucket";
inline constexpr std::string_view kSlimeBucketHot = "lepton:slime_in_a_bucket_excited";
/// Parrot variants in Quark / Bedrock order (0 red_blue .. 4 gray).
inline constexpr std::string_view kParrotVariants[] = {"red_blue", "blue", "green", "yellow_blue", "gray"};

// ---- shared helpers (Common.cpp) --------------------------------------------------------------
/// Random numbers for gameplay rolls (server thread).
[[nodiscard]] double  randomDouble();          ///< [0, 1)
[[nodiscard]] int     randomInt(int bound);    ///< [0, bound)
[[nodiscard]] double  randomGaussian();
[[nodiscard]] Player* playerById(int64_t uniqueId);
[[nodiscard]] bool    isCreative(Player const& player);
/// Plays a resource-pack sound (sound_definitions name) at `pos` for everyone nearby.
void playSound(std::string const& name, Vec3 const& pos, float volume = 1.0f, float pitch = 1.0f);
/// Spawns a particle effect (`minecraft:...` / `lepton:...`) at `pos` in `dim`.
void particle(std::string const& effect, Vec3 const& pos, int dim);
/// Gives `stack` to the player (inventory, else dropped at their feet).
void giveOrDrop(Player& player, ItemStack stack);
/// Puts `stack` into the selected hotbar slot and resyncs.
void setSelected(Player& player, ItemStack const& stack);
/// Writes a synched actor data item (Variant = 2, Scale = 38, MarkVariant = 43) and marks it
/// dirty so clients receive it. Returns false when the actor has no such item.
bool setActorInt(Actor& actor, int id, int value);
bool setActorFloat(Actor& actor, int id, float value);
/// Sets one actor flag (ActorFlags index) and marks the flags dirty.
bool setActorFlag(Actor& actor, int flag, bool value);
/// Bedrock's slime chunk rule.
[[nodiscard]] bool isSlimeChunk(int chunkX, int chunkZ);
/// Every block the player can reach from the eyes: hit block (or NoHit) within `reach`.
struct RayHit {
    bool  hit = false;
    int   x = 0, y = 0, z = 0; ///< hit block, or the block at the end of the ray
    int   face = 0;            ///< hit face (FacingID) when `hit`
};
[[nodiscard]] RayHit rayFromEyes(Player& player, float reach, bool liquids);
[[nodiscard]] bool   isFenceBlock(std::string_view id, BlockSource& region, BlockPos const& pos);

// ---- module parts -------------------------------------------------------------------------
namespace pickarang {
void install();
void uninstall();
void tick();
void onWorldClosed();
bool onUse(Player& player, ItemStack& item); ///< true = consumed (event cancelled)
[[nodiscard]] bool isRang(std::string_view itemId);
/// Pickarangs in flight (tests).
[[nodiscard]] int inFlight();
} // namespace pickarang

namespace torcharrow {
void install();
void uninstall();
void tickTrails();
} // namespace torcharrow

namespace parroteggs {
void install();
void uninstall();
void tick();
void onWorldReady();
void onWorldSave();
void onWorldClosed();
/// Parrot feeding; true = the interaction was consumed.
bool onInteract(Player& player, Actor& target);
/// The projectile `type` hit something at `pos`.
void onEggHit(std::string const& type, Vec3 const& pos, int dim);
[[nodiscard]] int eggTimer(int64_t parrotId);
void              setEggTimer(int64_t parrotId, int ticks);
[[nodiscard]] bool isBaby(int64_t parrotId);
} // namespace parroteggs

namespace skullpikes {
void tick();
void onWorldReady();
void onWorldSave();
void onWorldClosed();
void onPlaced(Player& player, BlockPos const& pos);
[[nodiscard]] int pikeCount();
/// Runs one scare pass now (tests).
void scareNow();
/// Mobs currently running from a pike (tests).
[[nodiscard]] int fleeingCount();
} // namespace skullpikes

namespace cloud {
void tick();
void onWorldReady();
void onWorldSave();
void onWorldClosed();
bool onUse(Player& player, ItemStack& item);
bool onUseOnBlock(Player& player, ItemStack& item, BlockPos const& pos, int face);
[[nodiscard]] int cloudCount();
/// Sets every cloud's remaining life (tests).
void ageAll(int ticksLeft);
} // namespace cloud

namespace slime {
void tick();
bool onInteract(Player& player, Actor& target);
bool onUseOnBlock(Player& player, ItemStack& item, BlockPos const& pos, int face);
} // namespace slime

/// Removes the items of disabled modules from online players (every second).
void purgeDisabled();

namespace testing {
[[nodiscard]] bool recipeOn(std::string_view id);
[[nodiscard]] bool itemOn(std::string_view id);
} // namespace testing

} // namespace lp::feature::throwables
