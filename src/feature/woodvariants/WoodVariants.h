#pragma once

// U6 woodvariants: VerticalPlanks, VariantBookshelves, VariantLadders, WoodenPosts, Hedges,
// LeafCarpet, HollowLogs (docs/spec/woodvariants.md). The behaviour is keyed by block tags, so the
// variants other units add (U7's woods) get it too:
//
//   lepton:bookshelf    enchanting power 1
//   lepton:ladder       climbable, wall placement (minecraft:block_face = facing), pops without support
//   lepton:post         axis from minecraft:block_face, lepton:side_0..3 none|chain|other_post (postSides)
//   lepton:hedge        lepton:<dir> connections to hedges, lepton:extend
//   lepton:leaf_carpet  pops over air, composts (20%)
//   lepton:hollow_log   axis from minecraft:block_face, climbable, auto crawl
//   lepton:wv_<module>  the config module the block belongs to

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Block;
class BlockPos;
class BlockSource;
class Player;

namespace lp::feature::woodvariants {

struct WoodVariantsConfig {
    int  version                       = 1;
    bool enableVerticalPlanks          = true;
    bool enableVariantBookshelves      = true;
    bool variantBookshelvesChangeNames = true;
    bool enableVariantLadders          = true;
    bool variantLaddersChangeNames     = true;
    bool enableWoodenPosts             = true;
    bool enableHedges                  = true;
    bool enableLeafCarpet              = true;
    bool enableHollowLogs              = true;
    bool enableAutoCrawl               = true;
};

[[nodiscard]] WoodVariantsConfig const& config();

[[nodiscard]] bool hasTag(Block const& block, std::string_view tag);
/// False when the block belongs to a module of this unit that is disabled (lepton:wv_<module>).
[[nodiscard]] bool moduleEnabled(Block const& block);
/// Module name of a lepton:wv_<module> tag ("hedges", ...), empty for other blocks.
[[nodiscard]] std::string moduleOf(Block const& block);
[[nodiscard]] bool moduleEnabled(std::string_view module);

/// "x", "y" or "z" from the minecraft:block_face state of a post / hollow log.
[[nodiscard]] std::string axisOf(Block const& block);

/// Recomputes the derived states of a post / hedge, or pops a ladder / leaf carpet without support.
/// Returns true if the world changed.
bool updateBlock(BlockSource& region, BlockPos const& pos);

/// The directions off a post's axis ("x", "y", "z"), in the order of its lepton:side_0..3 states.
[[nodiscard]] std::array<std::string_view, 4> const& postSides(std::string_view axis);
/// The connection a post shows towards `dir` (none|chain|other_post; "none" along its axis).
[[nodiscard]] std::string postConnection(Block const& post, std::string_view dir);

/// The post connection type of side `dir` ("down", "up", "north", ...) at `pos` (Java PostSideType.get).
[[nodiscard]] std::string postSideType(BlockSource& region, BlockPos const& pos, std::string_view dir);

/// Gives the climbable / hanging-support properties to the block types of tagged blocks (idempotent).
void applyBlockProperties(Block const& block);
void applyKnownBlockProperties();

/// Number of lepton:bookshelf blocks around an enchanting table (Java rule, capped by the caller).
[[nodiscard]] int variantBookshelfCount(BlockSource& region, BlockPos const& table);
[[nodiscard]] std::vector<BlockPos> variantBookshelfPositions(BlockSource& region, BlockPos const& table);

/// The auto crawl check of one player for this tick (Quark HollowLogsModule.playerTick).
void autoCrawlTick(Player& player);

/// U7's AzaleaWood is enabled (config/newwoods.json enableAzaleaWood; false when missing).
[[nodiscard]] bool azaleaWoodEnabled();
/// A recipe of this unit whose module is disabled (docs §3.8); `result` is the result item id.
[[nodiscard]] bool recipeDisabled(std::string const& recipeId, std::string const& result);
/// A vanilla `minecraft:bookshelf_from_<wood>_planks` recipe that the variant bookshelf replaces.
[[nodiscard]] bool vanillaRecipeReplaced(std::string const& recipeId);

/// Every block id of this unit (for the selftests).
[[nodiscard]] std::vector<std::string> const& allBlockIds();

} // namespace lp::feature::woodvariants
