#pragma once

// The matrix enchanter (server side): table contents in the world DB, bookshelf power and
// candle influence, the matrix operations, the screen sessions (mod clients) and the form
// fallback (vanilla clients).

#include <map>
#include <optional>
#include <string>

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"
#include "feature/enchanting/Matrix.h"
#include "mc/world/item/ItemStack.h"

class BlockSource;
class BlockPos;
class Block;
class Player;

namespace lp::feature::enchanting::table {

inline constexpr char const* kMatrixTag = "lepton:matrix";
inline constexpr char const* kBlockId   = "lepton:matrix_enchanter";

struct Power {
    int                power = 0;
    std::map<int, int> influences; ///< enchant type -> influence
};

struct Data {
    ItemStack item;
    ItemStack lapis;
    int       charge = 0;
};

/// Is a block with this id a matrix enchanter (with the current config)?
[[nodiscard]] bool isTableId(std::string const& id);
/// Bookshelf power and candle influence around a table (Quark's updateEnchantPower).
[[nodiscard]] Power computePower(BlockSource& region, BlockPos const& pos);
/// Is the block "short" (<= 3 px from the bottom or the top, or without shape)?
[[nodiscard]] bool isShortBlock(Block const& block);

/// The table contents (loaded from the world DB on first use).
[[nodiscard]] Data& data(core::GlobalPos const& pos);
void                markDirty(core::GlobalPos const& pos);
/// Burns lapis into charge when the charge is empty (Quark's tick).
void normalize(core::GlobalPos const& pos);

/// The matrix of a target item (from its tag), nullopt when the item is not a matrix target.
[[nodiscard]] std::optional<Matrix> matrixOf(ItemStack const& item);
void                                writeMatrix(ItemStack& item, Matrix const& m);
[[nodiscard]] MatrixEnv             envFor(ItemStack const& item, Power const& power);
/// The output for the table (empty when nothing placed / clashing).
[[nodiscard]] ItemStack outputOf(core::GlobalPos const& pos);
[[nodiscard]] ItemStack outputFor(ItemStack const& item, Power const& power);

// ---- sessions ----------------------------------------------------------------------------------

/// Opens the table at `pos` for `player` (native screen for mod clients, forms otherwise).
bool                                   open(Player& player, core::GlobalPos const& pos);
void                                   close(Player& player, bool notifyClient = true);
[[nodiscard]] std::optional<core::GlobalPos> openBy(Player const& player);
/// Matrix operation (Quark's onOperation). Returns true when something changed.
bool operate(Player& player, int op, int arg0, int arg1, int arg2);
/// Slot move ("put_item", "put_lapis", "take_item", "take_lapis", "take_output").
bool slotAction(Player& player, std::string const& action, int slot);
/// The state JSON sent to mod clients.
[[nodiscard]] nlohmann::json stateJson(Player const& player, core::GlobalPos const& pos);
/// Sends the state to every viewer of `pos` (and re-shows forms).
void broadcast(core::GlobalPos const& pos);
/// Drops the item and the lapis of a removed table.
void dropContents(BlockSource& region, core::GlobalPos const& pos);

// ---- module lifecycle (MatrixModule.cpp) ----
void resetCaches();
void flush();
void reparseInfluences();
void tickSessions();

} // namespace lp::feature::enchanting::table
