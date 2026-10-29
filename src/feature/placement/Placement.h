#pragma once

// U17 placement: ReacharoundPlacing, LockRotation, VariantSelector, Trowel, ReplaceScaffolding,
// LongRangePickBlock, EnhancedLadders (docs/spec/placement.md).
//
// Shared by the server parts (src/feature/placement) and the client parts (src/client/placement).
// Everything that only reads blocks (reacharound targets, the variant map, lock profiles) works
// on any BlockSource, so the client mod computes the same thing for its HUD.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockPos.h"

class Actor;
class Block;
class BlockSource;
class BlockType;
class ItemStackBase;
class Player;

namespace lp::feature::placement {

// ---- config ------------------------------------------------------------------------------------

/// Quark's VariantsConfig (experimental VariantSelector).
struct VariantsConfig {
    std::vector<std::string> variantTypes{"slab", "stairs", "wall", "fence", "fence_gate", "vertical_slab"};
    std::vector<std::string> testedMods{"lepton"};
    bool                     printVariantMapToLog = false;
    std::vector<std::string> aliases{"carpet=slab", "pane=fence"};
    std::vector<std::string> stripCandidates{"_planks", "_wool", "_block", "s"};
    std::vector<std::string> manualVariants{"fence_gate,minecraft:oak_planks,minecraft:fence_gate"};
    std::vector<std::string> blacklist{"minecraft:snow", "minecraft:bamboo", "minecraft:bamboo_block"};
};

struct PlacementConfig {
    int version = 1;

    // ReacharoundPlacing
    bool                     enableReacharoundPlacing = true;
    double                   leniency                 = 0.5;
    std::vector<std::string> whitelist;
    std::vector<std::string> blacklist;

    // LockRotation
    bool enableLockRotation              = true;
    bool lockRotationRenderLikeCrossHair = true;
    bool showKeybind                     = true;

    // VariantSelector (experimental)
    bool           enableVariantSelector              = false;
    bool           convertVariantItems                = false;
    bool           enableHammer                       = false;
    bool           showTooltip                        = true;
    bool           alignHudToHotbar                   = false;
    bool           showSimpleHud                      = false;
    bool           showHud                            = true;
    bool           enableGreenTint                    = true;
    bool           overrideHeldItemRender             = true;
    int            hudOffsetX                         = 0;
    int            hudOffsetY                         = 0;
    bool           variantSelectorRenderLikeCrossHair = true;
    bool           smallerArrow                       = false;
    VariantsConfig variants;

    // Trowel
    bool                     enableTrowel = true;
    int                      maxDamage    = 0;
    std::vector<std::string> trowelBlacklist{"minecraft:air"};
    std::vector<std::string> trowelWhitelist;

    // ReplaceScaffolding
    bool enableReplaceScaffolding = true;
    int  maxBounces               = 1;

    // LongRangePickBlock
    bool enableLongRangePickBlock = true;

    // EnhancedLadders
    bool   enableEnhancedLadders = true;
    double fallSpeed             = -0.2;
    bool   allowFreestanding     = true;
    bool   allowDroppingDown     = true;
    bool   allowSliding          = true;
    bool   allowInventorySneak   = true;
};

[[nodiscard]] PlacementConfig const& cfg();
/// Values clamped to Quark's @Config.Min/Max (leniency 0..1, maxDamage >= 0, fallSpeed <= 0).
[[nodiscard]] double leniency();
[[nodiscard]] double fallSpeed();

inline constexpr std::string_view kTrowel = "lepton:trowel";
inline constexpr std::string_view kHammer = "lepton:hammer";

// ---- directions (Bedrock FacingID order = Java Direction order) --------------------------------

enum Dir : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
[[nodiscard]] int              oppositeDir(int d);
[[nodiscard]] std::string_view dirName(int d);
[[nodiscard]] BlockPos         step(BlockPos const& p, int d, int n = 1);
[[nodiscard]] bool             isHorizontal(int d);
/// Java Direction.fromYRot (Bedrock yaw: 0 = south, 90 = west).
[[nodiscard]] int horizontalFromYaw(float yaw);
/// Java Direction.getNearest of a vector.
[[nodiscard]] int nearestDir(Vec3 const& v);
/// Java Entity#getNearestLookingDirections: the 6 directions ordered by how much the look vector
/// points along them (pitch first when it dominates).
[[nodiscard]] std::vector<int> nearestLookingDirections(float pitch, float yaw);

// ---- generic block helpers --------------------------------------------------------------------

[[nodiscard]] std::string idOf(Block const& b);
[[nodiscard]] bool        hasTag(Block const& b, std::string_view tag);
/// Air or a block a placement may replace (Java: isEmptyBlock || canBeReplaced).
[[nodiscard]] bool replaceableAt(BlockSource& region, BlockPos const& pos);
/// The block a block item places (nullptr for other items).
[[nodiscard]] Block const* blockOfItem(ItemStackBase const& stack);
[[nodiscard]] std::string  itemIdOf(ItemStackBase const& stack);

// ---- ray tracing --------------------------------------------------------------------------------

struct Ray {
    bool     hit = false;
    BlockPos pos;
    int      face = 0;
    Vec3     location; ///< absolute hit point
};
/// Outline ray from `from` along `dir` (unit vector) for `range` blocks, no fluids.
[[nodiscard]] Ray clip(BlockSource& region, Vec3 const& from, Vec3 const& dir, float range, Actor* actor);
/// The player's block reach (the game's pick range for its mode).
[[nodiscard]] float reachOf(Player& player);

// ---- ReacharoundPlacing ------------------------------------------------------------------------

struct ReachTarget {
    BlockPos pos;
    int      face = Down; ///< the face "clicked": Down for vertical targets, opposite(dir) else
};
/// Quark's item rule (block items, the trowel, whitelist; never the blacklist).
[[nodiscard]] bool canReacharound(ItemStackBase const& stack);
/// Quark's target search (null when the normal ray hits a block or nothing qualifies).
[[nodiscard]] std::optional<ReachTarget> reacharoundTarget(Player& player);
/// Places the held item at the target as the game would (server). True when a block was placed.
bool doReacharound(Player& player);

// ---- LockRotation -------------------------------------------------------------------------------

struct LockProfile {
    int facing = North; ///< Java's LockProfile.facing (opposite of the clicked face)
    int half   = -1;    ///< -1 none, 0 bottom, 1 top
    bool operator==(LockProfile const&) const = default;
};
/// The profile Quark computes when the key is pressed: from the block face under the crosshair
/// (`ray`), or from the look vector.
[[nodiscard]] LockProfile profileFor(Ray const& ray, Vec3 const& look);
/// Server: the profile of a player, if any.
[[nodiscard]] std::optional<LockProfile> lockProfileOf(Player const& player);
/// Server: sets (nullopt: clears) a player's profile; sends the one-time message (`keyName`
/// shown in it; empty = the vanilla command text).
void setLockProfile(Player& player, std::optional<LockProfile> profile, std::string const& keyName);
/// Re-orients the block at `pos` (just placed by `player`) to `profile`. `face`/`click` are the
/// original click (click relative to the clicked block). Vanilla blocks only; true if changed.
bool applyLockVanilla(BlockSource& region, BlockPos const& pos, Player& player, LockProfile const& profile, int face, Vec3 const& click, int aux);
/// Same for Lepton (data-driven) blocks, after the shapes finished their placement.
bool applyLockCustom(BlockSource& region, BlockPos const& pos, LockProfile const& profile);
/// The block `b` (computed for placement at `pos`) with the lock applied, for code that computes
/// placement states itself (ReplaceScaffolding, Hammer). Returns `b` when nothing applies.
[[nodiscard]] Block const& lockedState(BlockSource& region, BlockPos const& pos, Player& player, Block const& b, int aux);
/// Forgets every profile (config reload, Quark's ZConfigChanged).
void clearLockProfiles();
/// Quark's key press: the same profile as the current one clears it, anything else sets it.
std::optional<LockProfile> toggleLock(Player& player, LockProfile const& next, std::string const& keyName);
/// The profile for the player's current view (server-side ray), as the vanilla command uses.
[[nodiscard]] LockProfile currentProfile(Player& player);

// ---- VariantSelector ----------------------------------------------------------------------------

/// Quark's VariantsConfig maps (rebuilt from the block registry by `rebuildVariants`).
void rebuildVariants();
[[nodiscard]] bool                       variantsEnabled();
[[nodiscard]] std::vector<std::string>   visibleVariants();
[[nodiscard]] bool                       isKnownVariant(std::string const& variant);
/// Variant `variant` of the original block id `original` ("" when none).
[[nodiscard]] std::string variantOf(std::string const& original, std::string const& variant);
/// The original of a block id (the id itself when it is not a variant).
[[nodiscard]] std::string originalOf(std::string const& blockId);
/// The variant name of `possibleVariant` for `base` ("" when it isn't one).
[[nodiscard]] std::string variantNameOf(std::string const& base, std::string const& possibleVariant);
[[nodiscard]] bool        hasVariants(std::string const& blockId);
/// Variants of `blockId` in `variantTypes` order (type, block id).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> allVariants(std::string const& blockId);
/// Quark's getVariantBlockFromAny: the block `blockId` should become for `variant` ("" = no
/// change).
[[nodiscard]] std::string variantFromAny(std::string const& blockId, std::string const& variant);
/// Server: a player's saved variant ("" = original).
[[nodiscard]] std::string savedVariant(Player const& player);
void                      setSavedVariant(Player& player, std::string const& variant);
/// The state `blockId` gets when `player` places it at `pos` clicking `face` at `click` (relative
/// to the clicked block): the block's own rule for vanilla blocks, the generator's rules for
/// Lepton blocks. nullptr when the block doesn't exist.
[[nodiscard]] Block const*
placementState(BlockSource& region, BlockPos const& pos, Player& player, std::string const& blockId, int face, Vec3 const& click);
/// The Hammer used on `pos`: true when the use is consumed.
bool useHammer(Player& player, BlockPos const& pos, int face, Vec3 const& click);
/// Display name of a variant type ("" = Original) in the player's language.
[[nodiscard]] std::string variantLabel(Player const& player, std::string const& type);
/// Vanilla clients: the variant form for the held block.
void openVariantForm(Player& player);

// ---- EnhancedLadders ----------------------------------------------------------------------------

/// `minecraft:ladder` or a `lepton:ladder` block (Quark's `quark:ladders`).
[[nodiscard]] bool isLadder(Block const& b);
/// The ladder's facing (away from its wall).
[[nodiscard]] int          ladderFacing(Block const& b);
[[nodiscard]] Block const* withLadderFacing(Block const& b, int facing);
/// A sturdy wall behind a ladder facing `facing` at `pos`.
[[nodiscard]] bool ladderWallOk(BlockSource& region, BlockPos const& pos, int facing);
/// Quark's canSurviveTweak (wall, or a ladder of the same facing above when free-standing).
[[nodiscard]] bool ladderSurvives(BlockSource& region, BlockPos const& pos, int facing);

// ---- ReplaceScaffolding -------------------------------------------------------------------------

/// Quark's getLastInLine: where a block replacing the scaffolding at `start` goes.
[[nodiscard]] BlockPos scaffoldingTarget(BlockSource& region, BlockPos const& start, int clickDir);

// ---- shared server hooks ------------------------------------------------------------------------

/// Runs `fn` on the server thread after `ticks` server ticks (after the shapes' own deferred work
/// of that tick).
void later(int ticks, std::function<void()> fn);
/// Plays the place sound of `block` at `pos` for everyone around (`by`: the placing actor).
void playPlaceSound(BlockSource& region, BlockPos const& pos, Block const& block, Actor const& by);
/// Survival: one item less in the selected slot (when it is still `itemId`).
void consumeSelected(Player& player, std::string const& itemId);

// ---- LongRangePickBlock -------------------------------------------------------------------------

/// Server: vanilla-like pick block of `item` into the hotbar (creative creates it, survival moves
/// it from the inventory). True when the hotbar changed or a slot was selected.
bool pickItem(Player& player, std::string const& itemId);

// ---- messages -----------------------------------------------------------------------------------

/// client -> server: the rotation lock (LockRotation).
struct LockMsg {
    static constexpr std::string_view kId = "placement:lock";
    bool        locked = false;
    int         facing = 0;
    int         half   = -1;
    std::string key; ///< the bound key's name, shown in the one-time message
};
/// client -> server: the selected variant.
struct VariantMsg {
    static constexpr std::string_view kId = "placement:variant";
    std::string variant;
};
/// server -> client: the saved variant on join.
struct VariantRestoreMsg {
    static constexpr std::string_view kId = "placement:variant_restore";
    std::string variant;
};
/// client -> server: pick block of a position (long range) or of an item (variant pick).
struct PickMsg {
    static constexpr std::string_view kId = "placement:pick";
    int         x = 0, y = 0, z = 0;
    std::string item; ///< the item to pick instead of the block's own ("" = the block's)
};

} // namespace lp::feature::placement
