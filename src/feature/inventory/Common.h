#pragma once

// U15 inventory: item and container helpers shared by the server features, the client overlay
// and the selftests (both builds; game types only).

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mc/world/item/ItemStack.h"

class BlockActor;
class Container;
class ItemStackBase;
class Player;

namespace lp::feature::inventory {

// ---- items -------------------------------------------------------------------------------------

/// Quark `SimilarBlockTypeHandler.isShulkerBox`: a shulker box id (or any id containing
/// "shulker_box") with max stack size 1.
[[nodiscard]] bool isShulkerBox(ItemStackBase const& stack);
/// Slots of a shulker box.
inline constexpr int kShulkerSlots = 27;
/// The items stored in a shulker box item (`kShulkerSlots` entries).
[[nodiscard]] std::vector<ItemStack> shulkerContents(ItemStackBase const& box);
/// A copy of `box` holding `items`.
[[nodiscard]] ItemStack withShulkerContents(ItemStackBase const& box, std::vector<ItemStack> const& items);
/// Inserts `stack` into the shulker box `box` (merge, then empty slots; shulker boxes are
/// refused). Returns how many items fit; `box` and `stack` are updated unless `simulate`.
int insertIntoShulker(ItemStack& box, ItemStack& stack, bool simulate = false);
/// Moves as much as fits of `from`'s contents into `into` (both shulker boxes). Returns the number
/// of items moved.
int dumpShulker(ItemStack& into, ItemStack& from, bool simulate = false);

/// Humanoid armour slot of an item (0 head, 1 chest, 2 legs, 3 feet; -1 none). Includes Quark's
/// "counts as armour" items (elytra: chest; carved pumpkin and heads/skulls: head).
[[nodiscard]] int armorSlotFor(ItemStackBase const& stack);
/// Is a real armour item (Quark `ArmorItem`): humanoid armour, not elytra or heads.
[[nodiscard]] bool isArmorItem(ItemStackBase const& stack);
[[nodiscard]] bool hasBindingCurse(ItemStackBase const& stack);
[[nodiscard]] bool isFireResistant(ItemStackBase const& stack);

/// Enchantments of a stack (also stored ones of books): ("minecraft:sharpness", level).
[[nodiscard]] std::vector<std::pair<std::string, int>> enchantments(ItemStackBase const& stack);
/// Display name of an enchantment with its level ("Sharpness V").
[[nodiscard]] std::string enchantDisplay(std::string const& id, int level);
/// Sum of enchantment levels.
[[nodiscard]] int enchantPower(ItemStackBase const& stack);

/// Same item, data value and extra data (counts ignored).
[[nodiscard]] bool sameItem(ItemStackBase const& a, ItemStackBase const& b);
/// The stack has extra data (enchantments, name, contents, ...).
[[nodiscard]] bool hasExtraData(ItemStackBase const& stack);
[[nodiscard]] bool isDamageable(ItemStackBase const& stack);
/// Damage for damageable items, data value otherwise.
[[nodiscard]] int damageOf(ItemStackBase const& stack);
[[nodiscard]] bool itemHasTag(ItemStackBase const& stack, std::string_view tag);
/// The name shown for the stack (custom name, else the item name).
[[nodiscard]] std::string displayName(ItemStackBase const& stack);

// ---- containers --------------------------------------------------------------------------------

/// The container the player has open in a container screen (chest, barrel, ender chest, entity
/// inventory, Lepton virtual container), or nullptr. With `accepted`, only containers Quark puts
/// buttons on (>= `chestButtonSlotTarget` slots, not a crafting screen).
[[nodiscard]] Container* openContainer(Player& player, bool accepted = true);
/// The container of a block entity (chests, barrels, shulker boxes, data-driven containers, ...).
[[nodiscard]] Container* blockActorContainer(BlockActor* ba);
/// The player has no container open (or only the inventory screen).
[[nodiscard]] bool onlyInventoryOpen(Player const& player);

/// A slot named by a UI collection + index, resolved on the server.
struct SlotRef {
    Container* container = nullptr;
    int        slot      = -1;
    bool       player    = false; ///< the player's 36-slot inventory
    [[nodiscard]] bool valid() const { return container != nullptr && slot >= 0; }
};
[[nodiscard]] SlotRef resolveSlot(Player& player, std::string const& collection, int index);

/// The item held on the inventory cursor.
[[nodiscard]] ItemStack cursorItem(Player& player);
void                    setCursorItem(Player& player, ItemStack const& item);

/// Resends the player's inventory (and cursor).
void syncPlayer(Player& player);

} // namespace lp::feature::inventory
