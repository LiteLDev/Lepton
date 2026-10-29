// U15 inventory, server side: sorting, transferring, hotbar changing, tool restock, quick armour
// swapping, expanded item interactions (incl. held shulker boxes) and item sharing.
// docs/spec/inventory.md

#include "feature/inventory/Server.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/containers/IContainerRegistryAccess.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/inventory/FillingContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/components/StorageItemComponentTags.h"
#include "mc/world/item/components/storage_item_utility/StorageItemUtility.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"

#include "feature/inventory/Common.h"
#include "feature/inventory/Config.h"
#include "feature/inventory/Messages.h"
#include "feature/inventory/Sorting.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Net.h"

namespace lp::feature::inventory {

namespace inv = game::inv;
using game::isEmpty;

namespace {

uint64_t serverTick() {
    auto level = ll::service::getLevel();
    return level ? level->getCurrentServerTick().tickID : 0;
}

std::string uuidOf(Player const& p) { return p.getUuid().asString(); }

Player* playerByUuid(std::string const& uuid) {
    auto level = ll::service::getLevel();
    return level ? level->getPlayer(mce::UUID::fromString(uuid)) : nullptr;
}

bool isServerPlayer(Actor const& a) { return a.isPlayer() && !a.getLevel().isClientSide(); }

// ---- held shulker boxes ----------------------------------------------------------------------------

struct HeldSession {
    int             slot = 0;
    core::GlobalPos pos;
    ItemStack       box;          ///< the box (contents as of the last change)
    bool            seenOpen = false;
    int             wait     = 0; ///< ticks left to get the screen open
    bool            opening  = true;
};
std::map<std::string, HeldSession>& heldSessions() {
    static std::map<std::string, HeldSession> m;
    return m;
}
std::string heldKey(std::string const& uuid) { return "lp:inventory:held:" + uuid; }

void saveHeld(std::string const& uuid, HeldSession const& s) {
    game::db::save(heldKey(uuid), {{"slot", s.slot},
                                   {"item", game::itemToSnbt(s.box)},
                                   {"pos", {s.pos.dim, s.pos.x, s.pos.y, s.pos.z}}});
}

bool heldPosInUse(core::GlobalPos const& pos) {
    for (auto const& [uuid, s] : heldSessions())
        if (s.pos == pos) return true;
    return false;
}

/// Gives a box back to the player: its old slot, else any free slot, else dropped.
void returnBox(Player& player, int slot, ItemStack box) {
    auto& pinv = player.getInventory();
    if (slot >= 0 && slot < inv::kPlayerSlots && isEmpty(pinv.getItem(slot))) pinv.setItem(slot, box);
    else if (inv::insert(pinv, box, inv::kPlayerAll); !isEmpty(box)) player.drop(box, false);
    syncPlayer(player);
}

std::vector<int> lockedPlayerSlots(Player const& player) {
    auto it = heldSessions().find(uuidOf(player));
    if (it == heldSessions().end()) return {};
    return {it->second.slot};
}

// ---- share cooldowns -----------------------------------------------------------------------------

std::map<std::string, uint64_t>& lastShares() {
    static std::map<std::string, uint64_t> m;
    return m;
}

// ---- restock queue --------------------------------------------------------------------------------

struct PendingRestock {
    std::string uuid;
    int         slot = 0;
    ItemStack   original;
    uint64_t    tick = 0;
};
std::vector<PendingRestock>& pendingRestocks() {
    static std::vector<PendingRestock> v;
    return v;
}

} // namespace

// ==== InventorySorting ==============================================================================

bool sortPlayerInventory(Player& player) {
    if (!cfg().enableInventorySorting) return false;
    sortRange(player.getInventory(), 9, inv::kPlayerSlots, lockedPlayerSlots(player));
    syncPlayer(player);
    log().info("inventory: sorted the inventory of {}", player.getRealName());
    return true;
}

bool handleSort(Player& player, bool forcePlayer) {
    auto const& c = cfg();
    if (!c.enableInventorySorting) return false;
    Container* container = openContainer(player, true);
    if (forcePlayer || !container) {
        if (!(container ? c.enablePlayerInventoryInChests : c.enablePlayerInventory)) return false;
        return sortPlayerInventory(player);
    }
    if (!c.enableChests) return false;
    sortRange(*container, 0, container->getContainerSize());
    log().info("inventory: sorted a container for {}", player.getRealName());
    return true;
}

// ==== EasyTransferring ==============================================================================

namespace {

bool mayPlace(Container const& c, ItemStack const& stack, bool shulkerContainer) {
    if (shulkerContainer && isShulkerBox(stack)) return false;
    (void)c;
    return true;
}

/// Quark smart-transfer predicate: the target already holds this item.
bool targetHas(Container const& target, inv::Range range, ItemStack const& stack) {
    for (int i = range.begin; i < range.end && i < target.getContainerSize(); ++i) {
        auto const& at = target.getItem(i);
        if (isEmpty(at) || at.getId() != stack.getId()) continue;
        if (sameItem(at, stack)) return true;
        if (isDamageable(stack) && stack.getMaxStackSize() == 1 && at.hasSameUserData(stack)) return true;
        if (isDamageable(stack) && stack.getMaxStackSize() == 1 && !hasExtraData(at) && !hasExtraData(stack)) return true;
    }
    return false;
}

/// The open screen shows a shulker box (a placed one, or a held one in its virtual chest screen).
bool isShulkerContainer(Player& player) {
    if (heldShulkerPos(player)) return true;
    auto const& manager = player.mContainerManager.get();
    if (!manager) return false;
    auto const* pos = std::get_if<BlockPos>(&manager->mScreenContext->mOwner.get());
    return pos && game::blockIdAt(player.getDimensionBlockSource(), *pos).find("shulker_box") != std::string::npos;
}

} // namespace

int transferItems(Player& player, Container& container, bool restock, bool smart, std::optional<bool> shulkerTarget) {
    auto& pinv    = player.getInventory();
    bool  shulker = shulkerTarget ? *shulkerTarget : isShulkerContainer(player);
    int   moved   = 0;
    if (!restock) {
        for (int i = 9; i < inv::kPlayerSlots; ++i) {
            ItemStack stack = pinv.getItem(i);
            if (isEmpty(stack) || !mayPlace(container, stack, shulker)) continue;
            if (smart && !targetHas(container, inv::all(container), stack)) continue;
            int n = inv::insert(container, stack, inv::all(container));
            if (n > 0) {
                pinv.setItem(i, stack);
                moved += n;
            }
        }
    } else {
        for (int i = container.getContainerSize() - 1; i >= 0; --i) {
            ItemStack stack = container.getItem(i);
            if (isEmpty(stack)) continue;
            if (smart && !targetHas(pinv, inv::kPlayerAll, stack)) continue;
            int n = inv::insert(pinv, stack, inv::kPlayerAll);
            if (n > 0) {
                container.setItem(i, stack);
                moved += n;
            }
        }
    }
    container.setContainerChanged(0);
    syncPlayer(player);
    return moved;
}

int handleTransfer(Player& player, bool restock, bool smart) {
    if (!cfg().enableEasyTransferring || player.isSpectator()) return -1;
    Container* container = openContainer(player, true);
    if (!container) return -1;
    int moved = transferItems(player, *container, restock, smart);
    log().info("inventory: {} {} items{} for {}", restock ? "extracted" : "inserted", moved, smart ? " (filtered)" : "",
               player.getRealName());
    return moved;
}

int quickMove(Player& player, std::string const& collection, int slot) {
    if (!cfg().enableEasyTransferring || !cfg().enableShiftLock || player.isSpectator()) return 0;
    auto ref = resolveSlot(player, collection, slot);
    if (!ref.valid()) return 0;
    auto locked = lockedPlayerSlots(player);
    if (ref.player && std::find(locked.begin(), locked.end(), ref.slot) != locked.end()) return 0;
    auto& pinv  = player.getInventory();
    int   moved = 0;
    if (!ref.player) {
        // Container -> player: like Java's chest quick move, hotbar from the right first.
        ItemStack stack = ref.container->getItem(ref.slot);
        if (isEmpty(stack)) return 0;
        int total = stack.mCount;
        inv::insert(pinv, stack, inv::kPlayerAll, true);
        for (int i = 8; i >= 0 && !isEmpty(stack); --i)
            if (isEmpty(pinv.getItem(i))) inv::insert(pinv, stack, {i, i + 1});
        for (int i = 35; i >= 9 && !isEmpty(stack); --i)
            if (isEmpty(pinv.getItem(i))) inv::insert(pinv, stack, {i, i + 1});
        moved = total - (isEmpty(stack) ? 0 : stack.mCount);
        ref.container->setItem(ref.slot, stack);
        ref.container->setContainerChanged(ref.slot);
    } else if (auto* container = openContainer(player, false)) {
        if (isShulkerContainer(player) && isShulkerBox(pinv.getItem(ref.slot))) return 0;
        moved = inv::transferSlot(pinv, ref.slot, *container, inv::all(*container));
        container->setContainerChanged(0);
    } else {
        inv::Range to = ref.slot < 9 ? inv::kPlayerMain : inv::kHotbar;
        moved         = inv::transferSlot(pinv, ref.slot, pinv, to);
    }
    syncPlayer(player);
    if (moved > 0) log().info("inventory: quick move of {} items for {}", moved, player.getRealName());
    return moved;
}

// ==== HotbarChanger =================================================================================

bool changeHotbar(Player& player, int bar) {
    if (!cfg().enableHotbarChanger || bar < 1 || bar > 3 || player.isSpectator()) return false;
    auto& pinv = player.getInventory();
    for (int i = 0; i < 9; ++i) {
        ItemStack a = pinv.getItem(i);
        ItemStack b = pinv.getItem(i + bar * 9);
        pinv.setItem(i, b);
        pinv.setItem(i + bar * 9, a);
    }
    syncPlayer(player);
    log().info("inventory: {} swapped the hotbar with row {}", player.getRealName(), bar);
    return true;
}

// ==== AutomaticToolRestock ==========================================================================

namespace {

std::string bareId(std::string const& id) {
    auto p = id.find(':');
    return p == std::string::npos ? id : id.substr(p + 1);
}

std::vector<std::string> importantEnchants(ItemStack const& stack) {
    std::vector<std::string> out;
    auto                     on = enchantments(stack);
    for (auto const& name : cfg().enchantNames)
        for (auto const& [id, lvl] : on)
            if (lvl > 0 && bareId(id) == bareId(name)) out.push_back(bareId(name));
    return out;
}

std::set<std::string> toolClasses(ItemStack const& s) {
    std::set<std::string> out;
    auto                  id = game::itemId(s);
    if (id == "minecraft:bow") out.insert("bow");
    else if (id == "minecraft:crossbow") out.insert("crossbow");
    if (itemHasTag(s, "minecraft:is_axe")) out.insert("axe");
    if (itemHasTag(s, "minecraft:is_hoe")) out.insert("hoe");
    if (itemHasTag(s, "minecraft:is_shovel")) out.insert("shovel");
    if (itemHasTag(s, "minecraft:is_pickaxe")) out.insert("pickaxe");
    if (itemHasTag(s, "minecraft:is_sword")) out.insert("sword");
    if (id == "minecraft:shears") out.insert("shears");
    if (id == "minecraft:fishing_rod") out.insert("fishing_rod");
    return out;
}

bool ignored(ItemStack const& s) {
    if (isEmpty(s)) return false;
    auto id = game::itemId(s);
    for (auto const& name : cfg().ignoredItems) {
        if (name == id) return true;
        if (name.starts_with("quark:") && "lepton:" + name.substr(6) == id) return true;
    }
    return false;
}

} // namespace

bool restockTool(Player& player, int handSlot, ItemStack const& original) {
    auto const& c = cfg();
    if (!c.enableAutomaticToolRestock || isEmpty(original) || isArmorItem(original)) return false;
    if (c.unstackablesOnly && original.getMaxStackSize() > 1) return false;

    auto   enchOnStack = importantEnchants(original);
    auto   classes     = toolClasses(original);
    bool   damageable  = isDamageable(original);
    auto   itemPred    = [&](ItemStack const& o) {
        return o.getId() == original.getId() && (damageable || o.getAuxValue() == original.getAuxValue());
    };
    // Quark: !copy(enchantsOnStack).retainAll(important(other)): every important enchantment of the
    // old item is on the other one.
    auto enchPred = [&](ItemStack const& o) {
        auto other = importantEnchants(o);
        return std::all_of(enchOnStack.begin(), enchOnStack.end(), [&](std::string const& e) {
            return std::find(other.begin(), other.end(), e) != other.end();
        });
    };
    // Quark: !otherClasses.retainAll(classes): the other item has classes, all of them shared.
    auto toolPred = [&](ItemStack const& o) {
        auto other = toolClasses(o);
        return !other.empty() && std::all_of(other.begin(), other.end(), [&](std::string const& k) { return classes.contains(k); });
    };

    auto& pinv  = player.getInventory();
    int   lower = c.checkHotbar ? 0 : 9;
    auto  find  = [&](auto const& pred) -> int {
        for (int i = lower; i < inv::kPlayerSlots; ++i) {
            if (i == handSlot) continue;
            auto const& at = pinv.getItem(i);
            if (!isEmpty(at) && pred(at)) return i;
        }
        return -1;
    };

    int found = -1;
    if (c.enableEnchantMatching) found = find([&](ItemStack const& o) { return itemPred(o) && enchPred(o); });
    if (found < 0) found = find(itemPred);
    if (found < 0 && c.enableLooseMatching && !classes.empty()) {
        if (c.enableEnchantMatching && !enchOnStack.empty())
            found = find([&](ItemStack const& o) { return toolPred(o) && enchPred(o); });
        if (found < 0) found = find(toolPred);
    }
    if (found < 0) return false;

    ItemStack inHand    = handSlot < 0 ? player.getOffhandSlot() : pinv.getItem(handSlot);
    ItemStack providing = pinv.getItem(found);
    if (ignored(inHand) || ignored(providing)) return false;
    pinv.setItem(found, inHand);
    if (handSlot < 0) player.setOffhandSlot(providing);
    else pinv.setItem(handSlot, providing);
    syncPlayer(player);
    log().info("inventory: restocked {} for {} from slot {}", game::itemId(providing), player.getRealName(), found);
    return true;
}

void queueRestock(Player& player, int handSlot, ItemStack const& original) {
    if (!cfg().enableAutomaticToolRestock || isEmpty(original)) return;
    auto uuid = uuidOf(player);
    for (auto const& p : pendingRestocks())
        if (p.uuid == uuid && p.slot == handSlot) return;
    pendingRestocks().push_back({uuid, handSlot, original, serverTick()});
}

// ==== QuickArmorSwapping ============================================================================

bool swapArmorWithStand(Player& player, Actor& stand) {
    if (!cfg().enableQuickArmorSwapping || player.isSpectator() || player.isCreative()) return false;
    using AS = SharedTypes::Legacy::ArmorSlot;
    for (int s = 0; s < 4; ++s) {
        auto      slot       = static_cast<AS>(s);
        ItemStack playerItem = player.getArmor(slot);
        ItemStack standItem  = stand.getArmor(slot);
        if (hasBindingCurse(playerItem)) continue;
        ItemStack held = player.getSelectedItem();
        if (isEmpty(standItem) && !isEmpty(held) && armorSlotFor(held) == s) {
            standItem = held;
            player.getInventory().setItem(player.getSelectedItemSlot(), ItemStack{});
        }
        player.setArmor(slot, standItem);
        stand.setArmor(slot, playerItem);
    }
    if (cfg().swapOffHand) {
        ItemStack playerItem = player.getOffhandSlot();
        ItemStack standItem  = stand.getCarriedItem();
        if (!hasBindingCurse(playerItem)) {
            player.setOffhandSlot(standItem);
            stand.setCarriedItem(playerItem);
        }
    }
    syncPlayer(player);
    log().info("inventory: {} swapped armour with an armour stand", player.getRealName());
    return true;
}

// ==== ExpandedItemInteractions ======================================================================

namespace {

bool canTrash(Player& player, ItemStack const& slotItem, ItemStack const& cursor) {
    return game::itemId(slotItem) == "minecraft:lava_bucket" && !isEmpty(cursor) && !player.isCreative()
        && !isFireResistant(cursor) && !isShulkerBox(cursor);
}

} // namespace

std::string slotInteract(Player& player, std::string const& collection, int slot) {
    auto const& c = cfg();
    if (!c.enableExpandedItemInteractions || player.isSpectator()) return {};
    auto ref = resolveSlot(player, collection, slot);
    if (!ref.valid()) return {};
    ItemStack cursor = cursorItem(player);
    ItemStack at     = ref.container->getItem(ref.slot);
    std::string done;

    // The carried item's perspective first (Quark overrideStackedOnOther): a shulker box on the
    // cursor picks the slot's item up.
    if (c.enableShulkerBoxInteraction && isShulkerBox(cursor) && cursor.mCount == 1 && !isEmpty(at) && !isShulkerBox(at)) {
        if (insertIntoShulker(cursor, at) > 0) {
            ref.container->setItem(ref.slot, at);
            setCursorItem(player, cursor);
            done = "pick_into";
        }
    }
    // The slot item's perspective (overrideOtherStackedOnMe): lava, armour, shulker box.
    if (done.empty() && c.enableLavaInteraction && canTrash(player, at, cursor)) {
        setCursorItem(player, ItemStack{});
        player.playSound(player.getDimensionBlockSource(), SharedTypes::Legacy::LevelSoundEvent::Fizz, player.getPosition(), -1);
        done = "trash";
    }
    if (done.empty() && c.enableArmorInteraction && isEmpty(cursor) && !isEmpty(at) && at.mCount == 1) {
        int s = armorSlotFor(at);
        if (s >= 0) {
            auto      armorSlot = static_cast<SharedTypes::Legacy::ArmorSlot>(s);
            ItemStack worn      = player.getArmor(armorSlot);
            if (isEmpty(worn) || !hasBindingCurse(worn)) {
                player.setArmor(armorSlot, at);
                ref.container->setItem(ref.slot, worn);
                done = "equip";
            }
        }
    }
    if (done.empty() && c.enableShulkerBoxInteraction && isShulkerBox(at) && at.mCount == 1) {
        if (isEmpty(cursor)) {
            if (c.allowOpeningShulkerBoxes && ref.player && onlyInventoryOpen(player) && openHeldShulker(player, ref.slot))
                done = "open";
        } else if (isShulkerBox(cursor)) {
            if (dumpShulker(at, cursor) > 0) {
                ref.container->setItem(ref.slot, at);
                setCursorItem(player, cursor);
                done = "dump";
            }
        } else if (insertIntoShulker(at, cursor) > 0) {
            ref.container->setItem(ref.slot, at);
            setCursorItem(player, cursor);
            done = "insert";
        }
    }
    if (!done.empty()) {
        if (!ref.player) ref.container->setContainerChanged(ref.slot);
        syncPlayer(player);
        log().info("inventory: item interaction '{}' for {}", done, player.getRealName());
    }
    return done;
}

bool scrollBundle(Player& player, std::string const& collection, int slot, double delta, int dynamicId) {
    auto const& c = cfg();
    if (!c.enableExpandedItemInteractions || !c.allowRotatingBundles || player.isSpectator()) return false;
    if (delta >= -0.1 && delta <= 0.1) return false;
    auto ref = resolveSlot(player, collection, slot);
    if (!ref.valid()) return false;
    auto const& stack = ref.container->getItem(ref.slot);
    if (game::itemId(stack).find("bundle") == std::string::npos) return false;
    // The storage comes from the server's own copy of the item; the id the client saw only has to
    // agree with it (a client may lie).
    // (StorageItemUtility::getStorageItemID only exists in the client, bundleIDTag only in BDS.)
#ifdef LL_PLAT_C
    auto storage = StorageItemUtility::getStorageItemID(stack);
    if (!storage || !storage->mDynamicId->has_value()) return false;
    auto bundleId = static_cast<int>(**storage->mDynamicId);
#else
    std::string_view idTag = StorageItemComponentTags::bundleIDTag();
    if (!stack.mUserData || !stack.mUserData->contains(idTag)) return false;
    auto const& idValue = (*stack.mUserData)[idTag];
    if (!idValue.is_number_integer()) return false;
    auto bundleId = static_cast<int>(idValue);
#endif
    if (dynamicId >= 0 && dynamicId != bundleId) return false;
    FullContainerName name{};
    name.mName      = ContainerEnumName::DynamicContainer;
    name.mDynamicId = static_cast<uint>(bundleId);
    auto access  = player.getLevel().getContainerRegistryAccess();
    auto backing = access->getBackingContainer(name);
    if (!backing) return false;
    std::vector<ItemStack> items;
    for (int i = 0; i < backing->getContainerSize(); ++i)
        if (!isEmpty(backing->getItem(i))) items.push_back(backing->getItem(i));
    if (items.size() < 2) return false;
    if (delta < 0) std::rotate(items.rbegin(), items.rbegin() + 1, items.rend());
    else std::rotate(items.begin(), items.begin() + 1, items.end());
    size_t k = 0;
    for (int i = 0; i < backing->getContainerSize(); ++i)
        if (!isEmpty(backing->getItem(i))) backing->setItem(i, items[k++]);
    backing->setContainerChanged(0);
    syncPlayer(player);
    return true;
}

bool openHeldShulker(Player& player, int slot) {
    auto const& c = cfg();
    if (!c.enableExpandedItemInteractions || !c.enableShulkerBoxInteraction || !c.allowOpeningShulkerBoxes) return false;
    if (slot < 0 || slot >= inv::kPlayerSlots) return false;
    auto uuid = uuidOf(player);
    if (heldSessions().contains(uuid)) return false;
    auto& pinv = player.getInventory();
    ItemStack box = pinv.getItem(slot);
    if (!isShulkerBox(box) || box.mCount != 1) return false;

    auto head = player.getFeetBlockPos();
    head.y += 1;
    auto pos = game::globalPos(player.getDimensionBlockSource(), head);
    while (game::vc::viewers(pos) > 0 || heldPosInUse(pos)) ++pos.y;

    HeldSession s;
    s.slot = slot;
    s.pos  = pos;
    s.box  = box;
    s.wait = 20;
    // Right-clicked in the inventory screen: close it, the chest screen replaces it (tickHeld
    // retries vc::open until the client has closed the inventory).
    if (player.mContainerManager.get()) static_cast<ServerPlayer&>(player).initiateContainerClose();
    pinv.setItem(slot, ItemStack{});
    syncPlayer(player);
    saveHeld(uuid, s);
    game::vc::takeAll(pos);
    game::vc::setItems(pos, shulkerContents(box));
    heldSessions()[uuid] = s;
    player.playSound(player.getDimensionBlockSource(), SharedTypes::Legacy::LevelSoundEvent::ShulkerBoxOpen, player.getPosition(), -1);
    log().info("inventory: {} opened a held shulker box", player.getRealName());
    return true;
}

std::optional<core::GlobalPos> heldShulkerPos(Player const& player) {
    auto it = heldSessions().find(uuidOf(player));
    if (it == heldSessions().end()) return std::nullopt;
    return it->second.pos;
}

namespace {

game::vc::Spec heldSpec(std::string const& uuid, Player const& player, ItemStack const& box) {
    game::vc::Spec spec;
    spec.size          = kShulkerSlots;
    spec.screen        = game::vc::Screen::Chest;
    spec.titleFallback = displayName(box);
    spec.titleKey      = "lp.inventory.held_shulker"; // not defined: the screen shows the box name
    (void)player;
    spec.accepts   = [](ItemStack const& s) { return !isShulkerBox(s); };
    spec.onChanged = [uuid](core::GlobalPos const& pos) {
        auto it = heldSessions().find(uuid);
        if (it == heldSessions().end()) return;
        it->second.box = withShulkerContents(it->second.box, game::vc::items(pos, kShulkerSlots));
        saveHeld(uuid, it->second);
    };
    return spec;
}

void finishHeld(std::string const& uuid) {
    auto it = heldSessions().find(uuid);
    if (it == heldSessions().end()) return;
    auto s    = it->second;
    auto items = game::vc::items(s.pos, kShulkerSlots);
    game::vc::takeAll(s.pos);
    s.box = withShulkerContents(s.box, items);
    heldSessions().erase(it);
    if (auto* p = playerByUuid(uuid)) {
        returnBox(*p, s.slot, s.box);
        game::db::erase(heldKey(uuid));
        p->playSound(p->getDimensionBlockSource(), SharedTypes::Legacy::LevelSoundEvent::ShulkerBoxClosed, p->getPosition(), -1);
        log().info("inventory: {} closed a held shulker box", p->getRealName());
    } else {
        saveHeld(uuid, s); // given back when the player joins again
    }
}

void tickHeld() {
    std::vector<std::string> done;
    for (auto& [uuid, s] : heldSessions()) {
        auto* p = playerByUuid(uuid);
        if (!p) {
            done.push_back(uuid);
            continue;
        }
        if (s.opening) {
            if (game::vc::open(*p, s.pos, heldSpec(uuid, *p, s.box))) {
                s.opening = false;
            } else if (--s.wait <= 0) {
                done.push_back(uuid);
            }
            continue;
        }
        int viewers = game::vc::viewers(s.pos);
        if (viewers > 0) s.seenOpen = true;
        else if (s.seenOpen || --s.wait <= -40) done.push_back(uuid);
    }
    for (auto const& uuid : done) finishHeld(uuid);
}

void restoreHeldOnJoin(Player& player) {
    auto uuid = uuidOf(player);
    if (heldSessions().contains(uuid)) return;
    auto j = game::db::load(heldKey(uuid));
    if (!j || !j->is_object()) return;
    auto box = game::itemFromSnbt(j->value("item", std::string{}));
    game::db::erase(heldKey(uuid));
    // The record holds the latest contents: drop what a crash left in the virtual container.
    if (auto p = j->find("pos"); p != j->end() && p->is_array() && p->size() == 4)
        game::vc::takeAll(core::GlobalPos{(*p)[0].get<int>(), (*p)[1].get<int>(), (*p)[2].get<int>(), (*p)[3].get<int>()});
    if (isEmpty(box)) return;
    returnBox(player, j->value("slot", -1), box);
    log().info("inventory: gave {} back the shulker box that was open", player.getRealName());
}

} // namespace

// ==== ItemSharing ===================================================================================

std::string shareText(Player const& sender, ItemStack const& item, Player const* viewer) {
    std::string name  = displayName(item);
    auto        ench  = enchantments(item);
    std::string color = ench.empty() ? "§f" : "§b";
    std::string text  = "[" + color + name + "§r]";
    if (item.mCount > 1) text += " §7x" + std::to_string(static_cast<int>(item.mCount)) + "§r";
    if (!ench.empty()) {
        std::string list;
        for (auto const& [id, lvl] : ench) list += (list.empty() ? "" : ", ") + enchantDisplay(id, lvl);
        text += " §7(" + list + ")§r";
    }
    auto who = sender.getRealName();
    return viewer ? trFor(*viewer, "lp.inventory.shared", "<{}> {}", who, text) : tr("lp.inventory.shared", "<{}> {}", who, text);
}

bool shareItem(Player& player, ItemStack const& item) {
    auto const& c = cfg();
    if (!c.enableItemSharing || isEmpty(item)) return false;
    auto     uuid = uuidOf(player);
    uint64_t now  = serverTick();
    auto     it   = lastShares().find(uuid);
    if (it != lastShares().end() && now - it->second < static_cast<uint64_t>(std::max(0, c.cooldown))) return false;
    lastShares()[uuid] = now;

    auto level = ll::service::getLevel();
    if (!level) return false;
    auto snbt = game::itemToSnbt(item);
    level->forEachPlayer([&](Player& viewer) {
        auto text = shareText(player, item, &viewer);
        viewer.sendMessage(text);
        if (net::hasClientMod(viewer)) net::sendToClient(viewer, SharedItemNotice{player.getRealName(), uuid, snbt, text});
        return true;
    });
    log().info("inventory: {} shared {}", player.getRealName(), game::itemId(item));
    return true;
}

void resetShareCooldowns() { lastShares().clear(); }

// ==== hooks and module ==============================================================================

namespace {

LL_TYPE_INSTANCE_HOOK(
    ToolBreakHook,
    ll::memory::HookPriority::Normal,
    ItemStackBase,
    &ItemStackBase::hurtAndBreak,
    bool,
    int     deltaDamage,
    ::Actor* owner
) {
    if (!owner || isEmpty(static_cast<ItemStackBase const&>(*this)) || !cfg().enableAutomaticToolRestock || !isServerPlayer(*owner)) return origin(deltaDamage, owner);
    auto const& self = static_cast<ItemStackBase const&>(*this);
    // Only a hit that can break the item needs a snapshot (armour takes damage on every hit).
    if (deltaDamage <= 0 || !self.mItem || self.getDamageValue() + deltaDamage < self.mItem->getMaxDamage() - 1)
        return origin(deltaDamage, owner);
    ItemStack before = game::itemFromSnbt(game::itemToSnbt(self));
    bool      broke  = origin(deltaDamage, owner);
    if (broke || isEmpty(self)) {
        auto& p = static_cast<Player&>(*owner);
        int   hand = &self == static_cast<ItemStackBase const*>(&p.getOffhandSlot()) ? -1 : p.getSelectedItemSlot();
        queueRestock(p, hand, before);
    }
    return broke;
}

LL_TYPE_INSTANCE_HOOK(
    ArmorStandInteractHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    InteractionResult,
    ::Actor&      actor,
    ::Vec3 const& location
) {
    if (cfg().enableQuickArmorSwapping && !getLevel().isClientSide() && isSneaking() && !isSpectator() && !isCreative()
        && actor.getTypeName() == "minecraft:armor_stand") {
        swapArmorWithStand(*this, actor);
        InteractionResult r{};
        r.mSuccess = true;
        r.mSwing   = true;
        return r;
    }
    return origin(actor, location);
}

void processRestocks() {
    auto now = serverTick();
    auto& v  = pendingRestocks();
    std::vector<PendingRestock> due;
    std::erase_if(v, [&](PendingRestock const& p) {
        if (p.tick >= now) return false;
        due.push_back(p);
        return true;
    });
    for (auto const& p : due) {
        auto* player = playerByUuid(p.uuid);
        if (!player) continue;
        ItemStack const& now = p.slot < 0 ? player->getOffhandSlot() : player->getInventory().getItem(p.slot);
        if (isEmpty(now)) restockTool(*player, p.slot, p.original);
    }
}

} // namespace

class InventoryModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            if (!pendingRestocks().empty()) processRestocks();
            if (!heldSessions().empty()) tickHeld();
        }));
        auto rememberHand = [](Player& p) {
            if (p.getLevel().isClientSide()) return;
            auto const& held = p.getSelectedItem();
            if (!isEmpty(held) && held.mCount == 1) queueRestock(p, p.getSelectedItemSlot(), held);
        };
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(
            [rememberHand](ll::event::PlayerPlacingBlockEvent& ev) { rememberHand(ev.self()); }
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>(
            [rememberHand](ll::event::PlayerUseItemEvent& ev) { rememberHand(ev.self()); }
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [rememberHand](ll::event::PlayerInteractBlockEvent& ev) { rememberHand(ev.self()); }
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
            restoreHeldOnJoin(ev.self());
        }));
        ToolBreakHook::hook();
        ArmorStandInteractHook::hook();
        // The client loads many worlds in one process: nothing may leak into the next world. An
        // open held box stays recorded in the closed world's DB and is given back on rejoin.
        static bool worldHooks = false;
        if (!worldHooks) {
            worldHooks = true;
            game::db::onWorldClosed([] {
                heldSessions().clear();
                pendingRestocks().clear();
                lastShares().clear();
            });
        }

        mHandlers.push_back(net::onServer<SortRequest>([](Player& p, SortRequest const& m) { handleSort(p, m.forcePlayer); }));
        mHandlers.push_back(net::onServer<TransferRequest>([](Player& p, TransferRequest const& m) {
            handleTransfer(p, m.restock, m.smart);
        }));
        mHandlers.push_back(net::onServer<QuickMoveRequest>([](Player& p, QuickMoveRequest const& m) {
            quickMove(p, m.collection, m.slot);
        }));
        mHandlers.push_back(net::onServer<ChangeHotbarRequest>([](Player& p, ChangeHotbarRequest const& m) {
            changeHotbar(p, m.bar);
        }));
        mHandlers.push_back(net::onServer<SlotInteractRequest>([](Player& p, SlotInteractRequest const& m) {
            slotInteract(p, m.collection, m.slot);
        }));
        mHandlers.push_back(net::onServer<ScrollBundleRequest>([](Player& p, ScrollBundleRequest const& m) {
            scrollBundle(p, m.collection, m.slot, m.delta, m.dynamicId);
        }));
        mHandlers.push_back(net::onServer<ShareItemRequest>([](Player& p, ShareItemRequest const& m) {
            auto ref = resolveSlot(p, m.collection, m.slot);
            if (ref.valid()) shareItem(p, ref.container->getItem(ref.slot));
        }));
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        for (auto id : mHandlers) net::removeHandler(id);
        mHandlers.clear();
        ToolBreakHook::unhook();
        ArmorStandInteractHook::unhook();
        std::vector<std::string> open;
        for (auto const& [uuid, s] : heldSessions()) open.push_back(uuid);
        for (auto const& uuid : open) finishHeld(uuid);
        pendingRestocks().clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    std::vector<net::HandlerId>         mHandlers;
};

} // namespace lp::feature::inventory

#include "mod/Module.h"
LP_REGISTER_MODULE(lp::feature::inventory::InventoryModule);
