#pragma once

// Pathfinder Maps internals shared with the selftests.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class BlockPos;
class Dimension;
class EconomyTradeableComponent;
class ItemStack;
class Player;

namespace lp::feature::gadgets::pathfinder {

/// Quark's TradeInfo (built-in or custom).
struct TradeInfo {
    std::string biome; ///< Java id ("minecraft:jungle")
    bool        enabled  = true;
    int         level    = 1;
    int         minPrice = 1;
    int         maxPrice = 1;
    int         color    = -1; ///< ARGB
};

/// The spiral cursor Quark keeps on the quill.
struct Spiral {
    int sourceX = 0, sourceZ = 0;
    int x = 0, z = 0, leg = 0, legIndex = 0;
};

/// Bedrock biome name for a Java biome id ("minecraft:snowy_plains" -> "ice_plains").
[[nodiscard]] std::string bedrockBiome(std::string_view javaId);
/// "minecraft:old_growth_pine_taiga" -> "old_growth_pine_taiga".
[[nodiscard]] std::string javaPath(std::string_view javaId);
/// Translated biome name ("Jungle"), in the player's locale when given.
[[nodiscard]] std::string biomeName(Player const* player, std::string_view javaId);

/// One custom map line "<id>,<level>,<min>,<max>,<ARGB hex>[,<name>]".
[[nodiscard]] std::optional<TradeInfo> parseCustom(std::string const& line);
/// Built-ins (when applyDefaultTrades) + customs, as configured.
[[nodiscard]] std::vector<TradeInfo> tradeList();

/// An inscribed quill.
[[nodiscard]] ItemStack makeQuill(std::string const& biome, int color, Player const* player);

/// Quark's nextPos: advances the cursor and returns the next column (x, z), nullopt at the end.
std::optional<std::pair<int, int>> nextPos(Spiral& sp, int searchRadius);
/// Java Mth.outFromOrigin(y, min, max, 64).
[[nodiscard]] std::vector<int> searchHeights(int y, int minY, int maxY);
/// Bedrock biome name at a position from the dimension's biome source (no chunk needed).
[[nodiscard]] std::string biomeAt(Dimension& dim, int x, int y, int z);
/// Up to `steps` spiral columns; the found position, or nullopt (`failed` = end of the spiral).
std::optional<BlockPos> searchSteps(
    Dimension&              dim,
    std::string const&      bedrockName,
    Spiral&                 sp,
    std::vector<int> const& heights,
    int                     steps,
    bool&                   failed
);

/// The filled map Quark's createMap makes.
[[nodiscard]] ItemStack createMap(Player& player, BlockPos const& target, std::string const& javaBiome, int color);

/// Test seams.
[[nodiscard]] std::vector<int> spiralForTest(int steps, int searchRadius);

} // namespace lp::feature::gadgets::pathfinder
