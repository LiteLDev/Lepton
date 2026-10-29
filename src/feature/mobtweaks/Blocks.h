#pragma once

// Block tweak helpers shared with the selftests (server thread).

#include <optional>
#include <string>
#include <vector>

#include "mc/world/level/BlockPos.h"

class BlockSource;
class ItemStack;
class Player;

namespace lp::feature::mobtweaks {

inline constexpr char const* kChestTag         = "lepton:chest";
inline constexpr char const* kNonDoubleDoorTag = "lepton:non_double_door";

// ---- ShulkerPacking ------------------------------------------------------------------------------

/// Packs the chest at `pos` into a shulker box for `player` (shells in hand, rules of Quark).
bool packChest(Player& player, BlockSource& region, BlockPos const& pos);
/// An undyed shulker box item holding `items` (slot = index), optionally named.
[[nodiscard]] ItemStack shulkerBoxWith(std::vector<ItemStack> const& items, std::string const& customName);

// ---- ImprovedSponges -----------------------------------------------------------------------------

/// Java's sponge breadth-first drain from `start`. Returns the number of blocks absorbed.
int absorbWater(BlockSource& region, BlockPos const& start, int maxDrain, int maxDepth);
/// The first water source block the player looks at within reach (nothing else in between).
[[nodiscard]] std::optional<BlockPos> waterSourceInView(Player& player);
/// Puts a sponge into the water source at `pos` (sound, absorption). False if not water.
bool placeSpongeOnWater(Player& player, BlockSource& region, BlockPos const& pos);

// ---- DoubleDoorOpening ---------------------------------------------------------------------------

struct DoorState {
    std::string block;           ///< block id
    BlockPos    lower;           ///< door: the lower half; gate: its position
    bool        door       = false;
    bool        open       = false;
    bool        hingeRight = false;
    int         facing     = -1; ///< vanilla door: Direction::Type
    std::string facingName;      ///< Lepton doors, gates: "north", ...
};
/// The state of the door / fence gate at `pos` (nullopt for other blocks).
[[nodiscard]] std::optional<DoorState> doorState(BlockSource& region, BlockPos const& pos);
/// The lower half of the mirror door of `self` (Java DoubleDoorOpeningModule#openDoor).
[[nodiscard]] std::optional<BlockPos> doorPartner(BlockSource& region, DoorState const& self);
/// After the door/gate at `pos` changed from `before` to `open`, makes its partner follow.
bool followPartner(BlockSource& region, BlockPos const& pos, DoorState const& before, bool open, Player* by);

} // namespace lp::feature::mobtweaks
