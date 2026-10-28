#pragma once

// U15 inventory: the server-side operations (server thread). The net handlers, commands and
// selftests call these; every one validates its input and the module's config switch.

#include <optional>
#include <string>

#include "core/GlobalPos.h"

class Actor;
class Container;
class ItemStack;
class Player;

namespace lp::feature::inventory {

// ---- InventorySorting ---------------------------------------------------------------------------

/// Sorts the player's main inventory (slots 9..35; a held shulker box's slot stays empty).
bool sortPlayerInventory(Player& player);
/// Quark `SortInventoryMessage`: the player inventory (`forcePlayer`) or the open container.
bool handleSort(Player& player, bool forcePlayer);

// ---- EasyTransferring ---------------------------------------------------------------------------

/// Insert (restock = false) or extract (restock = true) between the player and `container`.
/// Returns the number of items moved.
int transferItems(Player& player, Container& container, bool restock, bool smart);
/// Quark `InventoryTransferMessage` on the open container (-1 when not allowed).
int handleTransfer(Player& player, bool restock, bool smart);
/// Shift-lock quick move of one slot. Returns the number of items moved.
int quickMove(Player& player, std::string const& collection, int slot);

// ---- HotbarChanger ------------------------------------------------------------------------------

bool changeHotbar(Player& player, int bar);

// ---- AutomaticToolRestock -----------------------------------------------------------------------

/// Looks for a replacement of `original` (destroyed in hand slot `handSlot`, -1 = off hand) and
/// swaps it in. Returns true when a replacement was found.
bool restockTool(Player& player, int handSlot, ItemStack const& original);
/// Queues a check at the end of the tick: when the hand slot is empty then, it is restocked.
void queueRestock(Player& player, int handSlot, ItemStack const& original);

// ---- QuickArmorSwapping -------------------------------------------------------------------------

bool swapArmorWithStand(Player& player, Actor& stand);

// ---- ExpandedItemInteractions -------------------------------------------------------------------

/// The interaction click on a slot (cursor contents against the slot). Returns the action done:
/// "trash", "equip", "insert", "dump", "pick_into", "open" or "" for none.
std::string slotInteract(Player& player, std::string const& collection, int slot);
/// Rotates a bundle's contents (`dynamicId`: the bundle's dynamic container id sent by the client).
bool scrollBundle(Player& player, std::string const& collection, int slot, double delta, int dynamicId = -1);
/// Opens the shulker box in player inventory slot `slot` (0..35) in a chest screen.
bool openHeldShulker(Player& player, int slot);
/// Position of the player's open held shulker box screen, if any.
[[nodiscard]] std::optional<core::GlobalPos> heldShulkerPos(Player const& player);

// ---- ItemSharing --------------------------------------------------------------------------------

/// The chat line for a shared item.
[[nodiscard]] std::string shareText(Player const& sender, ItemStack const& item, Player const* viewer = nullptr);
/// Shares `item` (cooldown checked). Returns false when throttled or disabled.
bool shareItem(Player& player, ItemStack const& item);
/// Testing: forget the share cooldowns.
void resetShareCooldowns();

} // namespace lp::feature::inventory
