#pragma once

// Container / player inventory primitives (server thread): slot access, insert with merging,
// transfer, count/remove, compact and sort. Every function works on any `Container` (player
// inventory, chest, a VirtualContainer, ...). Ranges are half-open slot ranges [begin, end).
//
// Player inventory layout (Bedrock): slots 0..8 hotbar, 9..35 main; armour and off-hand are
// separate (`Player::getArmor/setArmor`, `getOffhandSlot/setOffhandSlot`).
//
// Changing a player's container on the server does not resend it by itself: call
// `inv::sync(player)` after a batch of edits of the player's own inventory.
//
//     namespace inv = lp::game::inv;
//     auto& pinv = inv::playerInventory(player);
//     auto stack = lp::game::makeItem("minecraft:stone", 100);
//     int moved  = inv::insert(pinv, stack, inv::kPlayerMain);   // stack keeps what didn't fit
//     inv::sort(chest, inv::all(chest));
//     inv::sync(player);

#include <functional>
#include <vector>

class Container;
class ItemStack;
class Player;

namespace lp::game::inv {

struct Range {
    int begin = 0;
    int end   = 0;
    [[nodiscard]] constexpr bool contains(int slot) const { return slot >= begin && slot < end; }
    [[nodiscard]] constexpr int  size() const { return end > begin ? end - begin : 0; }
};

inline constexpr Range kHotbar{0, 9};
inline constexpr Range kPlayerMain{9, 36};   ///< the 27 slots above the hotbar
inline constexpr Range kPlayerAll{0, 36};    ///< hotbar + main
inline constexpr int   kPlayerSlots = 36;

/// The whole container.
[[nodiscard]] Range all(Container const& c);

/// The player's 36-slot inventory (hotbar + main).
[[nodiscard]] Container& playerInventory(Player& player);

using Predicate  = std::function<bool(ItemStack const&)>;
using Comparator = std::function<bool(ItemStack const&, ItemStack const&)>; ///< strict weak "a before b"

/// Max count of `stack` in a slot of `c` (min of the item's and the container's limit).
[[nodiscard]] int maxStackIn(Container const& c, ItemStack const& stack);

/// How many items of `stack` (up to its count) fit into `range` (merging first, then empty slots).
[[nodiscard]] int roomFor(Container const& c, ItemStack const& stack, Range range);

/// Inserts `stack` into `range`: first merges into matching partial stacks, then fills empty
/// slots (unless `mergeOnly`). `stack`'s count is reduced by the amount moved, which is returned.
int insert(Container& c, ItemStack& stack, Range range, bool mergeOnly = false);

/// Moves up to `maxCount` (-1 = all) items from `from[slot]` into `to` (range `toRange`).
/// Returns the amount moved.
int transferSlot(Container& from, int slot, Container& to, Range toRange, int maxCount = -1);

/// Moves every stack of `fromRange` matching `filter` (null = all) into `to`. Returns the amount moved.
int transferAll(Container& from, Range fromRange, Container& to, Range toRange, Predicate const& filter = {});

/// Total count of matching items in `range`.
[[nodiscard]] int count(Container const& c, Predicate const& filter, Range range);

/// Removes up to `amount` matching items from `range` (last slots first). Returns the amount removed.
int remove(Container& c, Predicate const& filter, int amount, Range range);

/// Merges partial stacks of the same item in `range` (earlier slots fill first).
void compact(Container& c, Range range);

/// Compacts `range`, then orders the stacks by `less` (default `defaultOrder`), empty slots last.
void sort(Container& c, Range range, Comparator const& less = {});

/// Default order: item id (numeric, i.e. the game's registration order), then data value, then
/// name, then larger stacks first.
[[nodiscard]] bool defaultOrder(ItemStack const& a, ItemStack const& b);

/// Copies of the stacks in `range` (empty slots as empty stacks).
[[nodiscard]] std::vector<ItemStack> snapshot(Container const& c, Range range);
/// Writes `items` back to `range` (extra slots are cleared).
void restore(Container& c, Range range, std::vector<ItemStack> const& items);

/// Resends the player's inventory to their client.
void sync(Player& player);

} // namespace lp::game::inv
