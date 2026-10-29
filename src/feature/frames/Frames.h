#pragma once

// U24 frames: shared helpers of GlassItemFrame, DyeableItemFrames, MoreBannerLayers and
// MapWashing. See docs/spec/frames.md.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/GlobalPos.h"
#include "feature/frames/FramesConfig.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class ItemStackBase;
class Player;
class Vec3;

namespace lp::feature::frames {

inline constexpr std::string_view kGlassFrame       = "lepton:glass_item_frame";
inline constexpr std::string_view kGlowingGlassFrame = "lepton:glowing_glass_item_frame";
inline constexpr std::string_view kDyedFrame        = "lepton:dyed_item_frame";
inline constexpr std::string_view kDyedGlowFrame    = "lepton:dyed_glow_item_frame";
inline constexpr std::string_view kGlassDisplay     = "lepton:glass_frame";
inline constexpr std::string_view kDyedOverlay      = "lepton:dyed_item_frame";

FramesConfig const& cfg();

// ---- small game helpers (Common.cpp) ---------------------------------------------------------------

/// Plays a vanilla/RP sound event at `pos` for the players around it.
void playSound(BlockSource& region, Vec3 const& pos, std::string const& sound, float volume = 1.f, float pitch = 1.f);
/// Drops `stack` as an item entity at `pos` (small random motion like a broken block's drops).
void dropItem(BlockSource& region, ItemStack const& stack, Vec3 const& pos);
/// Gives `stack` to the player (inventory, else dropped at the player).
void giveOrDrop(Player& player, ItemStack const& stack);
/// Takes one item from the selected hotbar slot (unless creative) and gives `result` back: in
/// the slot when the slot became empty, otherwise into the inventory.
void exchangeOne(Player& player, ItemStack const& result);

/// Java direction index (0 down, 1 up, 2 north, 3 south, 4 west, 5 east) of a `minecraft:block_face`
/// value, -1 when unknown.
[[nodiscard]] int dirFromName(std::string_view name);
[[nodiscard]] std::string_view dirName(int dir);
[[nodiscard]] BlockPos offset(BlockPos const& pos, int dir);
/// The entity yaw that makes a model face `dir` (horizontal directions; 0 for up/down).
[[nodiscard]] float yawFor(int dir);

/// Entity property writes (queued; synced to clients with the entity).
bool setIntProperty(Actor& actor, std::string_view name, int value);
bool setBoolProperty(Actor& actor, std::string_view name, bool value);
bool setFloatProperty(Actor& actor, std::string_view name, float value);
[[nodiscard]] std::optional<int>   intProperty(Actor& actor, std::string_view name);
[[nodiscard]] std::optional<float> floatProperty(Actor& actor, std::string_view name);

/// Points a display entity's body and head at `yaw` and stops it from turning.
void orient(Actor& actor, float yaw);

/// Bedrock's `minecraft:frame` would hang at `target` facing `dir` (its support is target - dir).
[[nodiscard]] bool frameMayPlace(BlockSource& region, BlockPos const& target, int dir);

/// Inverse of `GlobalPos::key()` ("dim:x:y:z").
[[nodiscard]] std::optional<core::GlobalPos> parseKey(std::string_view key);

// ---- recipes ---------------------------------------------------------------------------------------

/// Removes (`enabled` = false) or restores the recipes with these identifiers.
void setRecipesEnabled(std::vector<std::string> const& ids, bool enabled);
[[nodiscard]] bool hasRecipe(std::string const& id);

// ---- cauldrons ---------------------------------------------------------------------------------------

struct CauldronInfo {
    bool water  = false; ///< a cauldron holding water (any colour)
    int  level  = 0;     ///< fill_level 0..6
    bool dyed   = false; ///< water with a dye colour
    bool potion = false; ///< potion liquid
    int  rgb    = 0;     ///< dye colour 0xRRGGBB
};
[[nodiscard]] std::optional<CauldronInfo> cauldronAt(BlockSource& region, BlockPos const& pos);
/// Lowers the water by `levels` fill levels (empty: becomes a plain empty cauldron).
void lowerCauldron(BlockSource& region, BlockPos const& pos, int levels);

/// Dye colour of a dyeable stack (nullopt when it has none).
[[nodiscard]] std::optional<int> itemColor(ItemStackBase const& stack);
/// Sets the dye colour of a dyeable (`minecraft:dyeable`) stack.
void setItemColor(ItemStack& stack, int rgb);

// ---- modules -----------------------------------------------------------------------------------------

namespace glass {
void install();
void uninstall();
void applyConfig();
} // namespace glass
namespace dyed {
void install();
void uninstall();
void applyConfig();
} // namespace dyed
namespace banners {
void install();
void uninstall();
/// The effective loom limit: min(layerLimit, 6) when enabled, else vanilla 6.
[[nodiscard]] int effectiveLimit();
/// Whether a banner with `patterns` layers may get another one.
[[nodiscard]] bool mayAddPattern(int patterns);
} // namespace banners

/// Cauldron interactions of the unit (frame dyeing/washing, map washing). True when handled.
bool onCauldronUse(Player& player, BlockSource& region, BlockPos const& pos);

// ---- selftest hooks --------------------------------------------------------------------------------
namespace testing {
struct GlassRecord {
    std::string  item; ///< SNBT of the held item ("" = empty)
    int          rotation = 0;
    std::int64_t display  = 0;
    int          attachment = 0;
};
[[nodiscard]] std::optional<GlassRecord> glassRecord(core::GlobalPos const& pos);
/// Runs the 100-tick sweep now (survival, sign attachments, displays, maps).
void glassSweep();
[[nodiscard]] int mapUpdates(); ///< map updates done by glass frames so far
/// Uses a glass frame as `player` (what a right click on the block does). True when handled.
bool useGlass(Player& player, core::GlobalPos const& pos);
/// Punches a glass frame as `player`. True when the frame survived (only its item dropped).
bool punchGlass(Player& player, core::GlobalPos const& pos);
/// Removes a glass frame's item without dropping it (test cleanup).
void emptyGlass(core::GlobalPos const& pos);

struct DyedRecord {
    int          rgb  = 0;
    bool         glow = false;
    std::int64_t overlay = 0;
};
[[nodiscard]] std::optional<DyedRecord> dyedRecord(core::GlobalPos const& pos);
/// Places a dyed frame from the player's selected item onto `support`'s face `dir`.
bool placeDyed(Player& player, BlockSource& region, BlockPos const& support, int dir);
void dyedSweep();
} // namespace testing

} // namespace lp::feature::frames
