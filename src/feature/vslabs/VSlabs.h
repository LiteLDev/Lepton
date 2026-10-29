#pragma once

// U1 vslabs: Quark's VerticalSlabs and SlabsToBlocks modules (docs/spec/vslabs.md).
//
// The vertical slab blocks themselves (placement, merging into a double, collision) come from the
// pack generator + game/Shapes (tag lepton:vertical_slab). This unit adds what is specific to
// Quark's module: wall/pane side connections (allowSideConnections), waterlogging, the copper
// weathering / waxing chain, fuel, disabling the module, and the SlabsToBlocks recipes.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Block;
class BlockPos;
class BlockSource;
class Recipes;

namespace lp::feature::vslabs {

/// config/vslabs.json (SPEC §5.1, U1).
struct Config {
    int  version              = 1;
    bool enableVerticalSlabs  = true; ///< VerticalSlabs module
    bool allowSideConnections = true; ///< Should Walls and Panes attempt to connect to the side of Vertical Slabs?
    bool enableSlabsToBlocks  = true; ///< SlabsToBlocks module
    std::vector<std::string> bannedSlabs{}; ///< Slabs that should NOT be convertible back into full-blocks
};

[[nodiscard]] Config const& config();

// ---- vertical slabs ---------------------------------------------------------------------------

/// A vertical slab (any unit's: the generator's lepton:vertical_slab tag).
[[nodiscard]] bool isVerticalSlab(Block const& block);
/// An item / block id that is a vertical slab ("lepton:oak_vertical_slab").
[[nodiscard]] bool isVerticalSlabId(std::string_view id);
/// The lepton:type of a vertical slab ("north", ..., "double"); empty for other blocks.
[[nodiscard]] std::string typeOf(Block const& block);
/// The slab a vanilla-material vertical slab is made from ("minecraft:oak_slab"); nullopt if the
/// id is not one of U1's vertical slabs.
[[nodiscard]] std::optional<std::string> parentSlabOf(std::string_view vslabId);

/// Quark's VerticalSlabsModule#shouldWallConnect plus the sturdy-face rule: does a wall/pane/fence
/// (`wallOrPane` false = fence) connect to `other`'s `face` ("north", ...)? nullopt: `other` is not
/// a vertical slab.
[[nodiscard]] std::optional<bool> connectsTo(Block const& other, std::string_view face, bool wallOrPane);

// ---- copper ------------------------------------------------------------------------------------

/// Weathering age (0 unaffected .. 3 oxidized) of a non-waxed weathering copper block, vanilla or
/// Lepton; nullopt for anything else (waxed copper included).
[[nodiscard]] std::optional<int> copperAge(std::string_view id);
/// The vertical slab id of a copper stage: (age 0..3, waxed).
[[nodiscard]] std::string copperSlabId(int age, bool waxed);
/// (age, waxed) of a copper vertical slab id; nullopt for other ids.
[[nodiscard]] std::optional<std::pair<int, bool>> copperSlabStage(std::string_view id);
/// One attempt of Java's ChangeOverTimeBlock#changeOverTime at `pos` (the 0.05688889 roll
/// included unless `force`). Returns true if the block aged.
bool weatherOnce(BlockSource& region, BlockPos const& pos, bool force = false);

/// Java LightningBolt#clearCopperOnLightningStrike for Lepton's copper slabs: a strike at `strike`
/// (a lightning rod or the block itself) resets the attached slab and random-walks scraping others.
void clearCopperOnLightning(BlockSource& region, BlockPos const& strike);

namespace testing {
/// Queued ticks seen on weathering copper vertical slabs since the module was installed.
[[nodiscard]] int copperTicks();
/// Upper bound of the weathering tick delay (default 8191; tests shorten it).
void setTickDelayMax(int ticks);
} // namespace testing

// ---- recipes -----------------------------------------------------------------------------------

/// Applies both modules to `recipes` (idempotent): removes every recipe that makes or uses a
/// vertical slab while VerticalSlabs is off, adds/removes the SlabsToBlocks recipes.
void applyRecipes(Recipes& recipes);
/// applyRecipes on the current level and resends the recipes to every player.
void reapplyRecipes();
/// Ids of the SlabsToBlocks recipes currently active ("lepton:slabs_to_blocks_<slab path>").
[[nodiscard]] std::vector<std::string> slabsToBlocksRecipes();
/// The SlabsToBlocks table: slab item id -> full block (id, aux).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> slabsToBlocksTable();
/// True if `recipes` currently holds a recipe with this id.
[[nodiscard]] bool hasRecipe(Recipes const& recipes, std::string_view id);
/// Forgets per-world recipe state (world closed).
void resetRecipes();
/// Puts every recipe taken out by applyRecipes back (module uninstalled).
void restoreRecipes();

} // namespace lp::feature::vslabs
