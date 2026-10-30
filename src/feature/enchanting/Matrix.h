#pragma once

// The enchantment matrix of Quark's matrix enchanter (behaviour of Quark's EnchantmentMatrix),
// independent of the game: enchantments are described by `EnchantDef` (filled from Bedrock's
// enchantment registry by Enchants.cpp) so the rules can be tested in isolation.

#include <array>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace lp::feature::enchanting {

struct EnchantDef {
    int                     type = -1; ///< Bedrock Enchant::Type
    std::string             id;        ///< "minecraft:sharpness" (Bedrock string id)
    int                     minLevel = 1, maxLevel = 1;
    int                     weight   = 1; ///< Java weight (rarity)
    bool                    treasure = false, discoverable = true;
    std::vector<int>        minCost, maxCost; ///< index = level (0 unused)
    std::vector<int>        incompatible;     ///< types that cannot coexist (exclusive set)
    int                     primarySlots = 0, secondarySlots = 0;

    [[nodiscard]] bool compatibleWith(int other) const;
};

/// Quark's piece value rules.
[[nodiscard]] int maxXpFor(EnchantDef const& e, int level);
[[nodiscard]] int valueFor(EnchantDef const& e, int level);

struct Piece {
    int                             ench = -1; ///< Enchant::Type
    int                             level = 1, color = 0, type = 0, x = 0, y = 0, xp = 0;
    std::vector<std::array<int, 2>> blocks;
    bool                            marked    = false;
    int                             influence = 0;

    void rotate();
};

/// Everything a matrix needs from the table and the world when generating pieces.
struct MatrixEnv {
    std::vector<EnchantDef> const*   registry = nullptr;
    std::map<int, int>               influences; ///< enchant type -> candle influence
    int                              bookshelfPower      = 0;
    int                              enchantability      = 0;
    bool                             zeroButEnchantable  = false;
    /// Can enchantment `type` go on the target item (ignored for books)?
    std::function<bool(EnchantDef const&)> applicable;
    /// Config.
    int    maxBookshelves = 15, piecePriceScale = 9, bookEnchantability = 12, baseMaxPieceCount = 3,
           baseMaxPieceCountBook = 1, minLevelCutoff = 8, influenceMax = 4;
    double minLevelScaleFactor = 1.2, minLevelScaleFactorBook = 2.0, dupeMultiplier = 1.4,
           incompatibleMultiplier = 0.0, influencePower = 0.125;
    bool allowBooks = true, allowTreasures = false, allowUndiscoverable = false, allowZeroEnchantability = false,
         normalizeRarity = true;
    std::vector<std::string> treasureWhitelist, undiscoverableWhitelist, disallowed;

    [[nodiscard]] EnchantDef const* def(int type) const;
};

class Matrix {
public:
    static constexpr int kWidth = 5, kHeight = 5, kPieceVariants = 8;

    explicit Matrix(bool book = false);

    bool                    book = false;
    std::map<int, Piece>    pieces;
    std::vector<int>        benched, placed;
    int                     count = 0, typeCount = 0;
    bool                    influenced = false;
    std::array<std::array<int, kHeight>, kWidth> grid{};
    std::mt19937            rng;

    // ---- rules ----
    [[nodiscard]] bool canGeneratePiece(MatrixEnv const& env);
    [[nodiscard]] bool validateXp(int playerLevel, bool creative, int bookshelfPower, MatrixEnv const& env) const;
    [[nodiscard]] int  minXpLevel(int bookshelfPower, MatrixEnv const& env) const;
    [[nodiscard]] int  newPiecePrice(MatrixEnv const& env) const;
    /// Rolls a piece; with `simulate` only checks that one could be rolled.
    bool generatePiece(MatrixEnv const& env, bool simulate);

    // ---- operations ----
    [[nodiscard]] bool canPlace(Piece const& p, int x, int y) const;
    bool               place(int id, int x, int y);
    bool               remove(int id);
    bool               rotate(int id);
    bool               merge(int placedId, int benchedId, MatrixEnv const& env);

    /// The enchantments of the placed pieces (type -> level); empty with `ok = false` when two
    /// placed pieces clash (same or incompatible enchantment).
    [[nodiscard]] std::map<int, int> result(MatrixEnv const& env, bool& ok) const;

    [[nodiscard]] Piece const* piece(int id) const;
    [[nodiscard]] int          totalValue(int ench, MatrixEnv const& env) const;

    [[nodiscard]] nlohmann::json toJson() const;
    static Matrix                fromJson(nlohmann::json const& j, bool book);

    void computeGrid();

    /// Quark's piece shapes (12).
    static std::vector<std::vector<std::array<int, 2>>> const& shapes();
    /// Quark's piece colour for an enchantment (stable per enchantment id).
    static int colorFor(std::string const& enchantId);
};

} // namespace lp::feature::enchanting
