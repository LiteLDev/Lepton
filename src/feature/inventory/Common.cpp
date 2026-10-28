#include "feature/inventory/Common.h"

#include <algorithm>
#include <variant>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/ByteTag.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/Container.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/EnderChestContainer.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/DataDrivenBlockContainer.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/VanillaBlockActor.h"
#include "mc/world/level/block/actor/component/ContainerBlockActorComponent.h"

#include "feature/inventory/Config.h"
#include "game/Entities.h"
#include "game/Items.h"

namespace lp::feature::inventory {

namespace {
bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}
Item const* itemOf(ItemStackBase const& s) { return s.mItem.get(); }
} // namespace

// ---- items -------------------------------------------------------------------------------------

bool isShulkerBox(ItemStackBase const& stack) {
    if (game::isEmpty(stack)) return false;
    return game::itemId(stack).find("shulker_box") != std::string::npos && stack.getMaxStackSize() == 1;
}

std::vector<ItemStack> shulkerContents(ItemStackBase const& box) {
    std::vector<ItemStack> items(kShulkerSlots);
    if (game::isEmpty(box)) return items;
    auto ctx = SaveContextFactory::createCloneSaveContext();
    auto tag = box.save(*ctx);
    if (!tag || !tag->contains("tag")) return items;
    auto& extra = (*tag)["tag"];
    if (!extra.is_object() || !extra.contains("Items")) return items;
    auto& list = extra["Items"];
    if (!list.is_array()) return items;
    for (auto& entry : list.get<ListTag>()) {
        if (!entry || entry->getId() != Tag::Type::Compound) continue;
        auto& c = entry.get<CompoundTag>();
        int   slot = 0;
        if (c.contains("Slot") && c["Slot"].hold<ByteTag>()) slot = static_cast<uchar>(c["Slot"].get<ByteTag>().data);
        if (slot < 0 || slot >= kShulkerSlots) continue;
        items[static_cast<size_t>(slot)] = ItemStack::fromTag(c);
    }
    return items;
}

ItemStack withShulkerContents(ItemStackBase const& box, std::vector<ItemStack> const& items) {
    auto ctx = SaveContextFactory::createCloneSaveContext();
    auto tag = box.save(*ctx);
    if (!tag) return ItemStack{};
    if (!tag->contains("tag") || !(*tag)["tag"].is_object()) tag->putCompound("tag", CompoundTag{});
    auto& extra = (*tag)["tag"].get<CompoundTag>();
    ListTag list;
    for (size_t i = 0; i < items.size() && i < static_cast<size_t>(kShulkerSlots); ++i) {
        if (game::isEmpty(items[i])) continue;
        auto itag = items[i].save(*ctx);
        if (!itag) continue;
        itag->putByte("Slot", static_cast<uchar>(i));
        list.add(std::move(itag));
    }
    extra.remove("Items");
    if (!list.empty()) extra.put("Items", std::move(list));
    if (extra.empty()) tag->remove("tag");
    auto out = ItemStack::fromTag(*tag);
    out.set(box.mCount);
    return out;
}

int insertIntoShulker(ItemStack& box, ItemStack& stack, bool simulate) {
    if (!isShulkerBox(box) || game::isEmpty(stack) || isShulkerBox(stack)) return 0;
    auto items = shulkerContents(box);
    int  want  = stack.mCount;
    int  left  = want;
    int  max   = std::max<int>(1, stack.getMaxStackSize());
    for (int pass = 0; pass < 2 && left > 0; ++pass) {
        for (auto& s : items) {
            if (left == 0) break;
            if (pass == 0) {
                if (game::isEmpty(s) || !game::canMerge(s, stack) || s.mCount >= max) continue;
                int n = std::min(left, max - static_cast<int>(s.mCount));
                s.set(s.mCount + n);
                left -= n;
            } else if (game::isEmpty(s)) {
                int n = std::min(left, max);
                s     = stack;
                s.set(n);
                left -= n;
            }
        }
    }
    int moved = want - left;
    if (moved > 0 && !simulate) {
        box = withShulkerContents(box, items);
        if (left == 0) stack = ItemStack{};
        else stack.set(left);
    }
    return moved;
}

int dumpShulker(ItemStack& into, ItemStack& from, bool simulate) {
    if (!isShulkerBox(into) || !isShulkerBox(from)) return 0;
    ItemStack target  = into;
    auto      source  = shulkerContents(from);
    int       moved   = 0;
    for (auto& s : source) {
        if (game::isEmpty(s)) continue;
        moved += insertIntoShulker(target, s, false);
    }
    if (moved > 0 && !simulate) {
        into = target;
        from = withShulkerContents(from, source);
    }
    return moved;
}

bool isArmorItem(ItemStackBase const& stack) {
    if (game::isEmpty(stack) || !stack.isArmorItem() || stack.isHorseArmorItem()) return false;
    if (game::itemId(stack) == "minecraft:elytra") return false;
    int slot = static_cast<int>(stack.getArmorSlot());
    return slot >= 0 && slot < 4;
}

int armorSlotFor(ItemStackBase const& stack) {
    if (game::isEmpty(stack)) return -1;
    auto id = game::itemId(stack);
    if (id == "minecraft:elytra") return 1;
    if (id == "minecraft:carved_pumpkin" || id == "minecraft:skull" || endsWith(id, "_head") || endsWith(id, "_skull"))
        return 0;
    if (!isArmorItem(stack)) return -1;
    return static_cast<int>(stack.getArmorSlot());
}

bool hasBindingCurse(ItemStackBase const& stack) {
    return !game::isEmpty(stack) && EnchantUtils::hasEnchant(Enchant::Type::CurseOfBinding, stack);
}

bool isFireResistant(ItemStackBase const& stack) {
    auto const* item = itemOf(stack);
    return item && item->mFireResistant;
}

std::vector<std::pair<std::string, int>> enchantments(ItemStackBase const& stack) {
    std::vector<std::pair<std::string, int>> out;
    if (game::isEmpty(stack) || !stack.mUserData) return out;
    for (auto const& e : stack.constructItemEnchantsFromUserData().getAllEnchants()) {
        auto const& all = Enchant::mEnchants();
        auto        idx = static_cast<size_t>(e.mEnchantType);
        if (idx >= all.size() || !all[idx]) continue;
        auto id = all[idx]->mStringId->getString();
        if (id.find(':') == std::string::npos) id = "minecraft:" + id;
        out.emplace_back(std::move(id), e.mLevel);
    }
    return out;
}

std::string enchantDisplay(std::string const& id, int level) {
    auto bare = id.substr(id.find(':') == std::string::npos ? 0 : id.find(':') + 1);
    auto type = EnchantUtils::getEnchantmentId(HashedString(bare));
    return EnchantUtils::getEnchantNameAndLevel(type, level);
}

int enchantPower(ItemStackBase const& stack) {
    int total = 0;
    for (auto const& [id, level] : enchantments(stack)) total += level;
    return total;
}

bool sameItem(ItemStackBase const& a, ItemStackBase const& b) {
    if (game::isEmpty(a) || game::isEmpty(b)) return false;
    return a.getId() == b.getId() && a.getAuxValue() == b.getAuxValue() && a.getDamageValue() == b.getDamageValue()
        && a.hasSameUserData(b);
}

bool hasExtraData(ItemStackBase const& stack) { return stack.mUserData && !stack.mUserData->empty(); }

bool isDamageable(ItemStackBase const& stack) { return !game::isEmpty(stack) && stack.isDamageableItem(); }

int damageOf(ItemStackBase const& stack) {
    return isDamageable(stack) ? stack.getDamageValue() : stack.getAuxValue();
}

bool itemHasTag(ItemStackBase const& stack, std::string_view tag) {
    auto const* item = itemOf(stack);
    return item && item->hasTag(HashedString(tag));
}

std::string displayName(ItemStackBase const& stack) {
    if (game::isEmpty(stack)) return {};
    auto custom = stack.getCustomName();
    return custom.empty() ? stack.getName() : custom;
}

// ---- containers --------------------------------------------------------------------------------

Container* blockActorContainer(BlockActor* ba) {
    if (!ba) return nullptr;
    if (auto* comp = ba->mContainer.get(); comp && comp->mContainer.get()) return comp->mContainer.get();
    switch (ba->getType()) {
    case BlockActorType::Chest:
    case BlockActorType::ShulkerBox:
    case BlockActorType::BarrelBlock:
    case BlockActorType::Dispenser:
    case BlockActorType::Dropper:
    case BlockActorType::Hopper:
    case BlockActorType::Crafter: return static_cast<VanillaBlockActor*>(ba)->getContainer();
    default: return nullptr;
    }
}

namespace {
Container* containerOfActor(Actor& actor) {
    auto comp = actor.getEntityContext().tryGetComponent<ContainerComponent>();
    return comp ? &comp->mContainer.get() : nullptr;
}
} // namespace

Container* openContainer(Player& player, bool accepted) {
    auto const& manager = player.mContainerManager.get();
    if (!manager) return nullptr;
    auto type = manager->getContainerType();
    using CT  = SharedTypes::Legacy::ContainerType;
    if (type == CT::Inventory || type == CT::None) return nullptr;
    if (accepted && (type == CT::Workbench || type == CT::Crafter)) return nullptr;
    auto const& owner = manager->mScreenContext->mOwner.get();
    Container*  c     = nullptr;
    if (auto const* pos = std::get_if<BlockPos>(&owner)) {
        auto& region = player.getDimensionBlockSource();
        if (region.getBlock(*pos).getTypeName() == "minecraft:ender_chest") {
            if (auto ender = player.getEnderChestContainer()) c = ender.as_ptr();
        } else {
            c = blockActorContainer(region.getBlockEntity(*pos));
        }
    } else if (auto const* id = std::get_if<ActorUniqueID>(&owner)) {
        if (auto* actor = game::entities::find(id->rawID)) c = containerOfActor(*actor);
    }
    if (!c) return nullptr;
    if (accepted && c->getContainerSize() < cfg().chestButtonSlotTarget) return nullptr;
    return c;
}

bool onlyInventoryOpen(Player const& player) {
    auto const& manager = player.mContainerManager.get();
    if (!manager) return true;
    auto type = manager->getContainerType();
    return type == SharedTypes::Legacy::ContainerType::Inventory || type == SharedTypes::Legacy::ContainerType::None;
}

SlotRef resolveSlot(Player& player, std::string const& collection, int index) {
    SlotRef ref;
    if (index < 0) return ref;
    auto& pinv = player.getInventory();
    if (collection == "hotbar_items") {
        if (index < 9) ref = {&pinv, index, true};
    } else if (collection == "inventory_items") {
        if (index < 27) ref = {&pinv, index + 9, true};
    } else if (collection == "combined_hotbar_and_inventory_items") {
        if (index < 36) ref = {&pinv, index, true};
    } else if (collection.find("container") != std::string::npos || collection.find("shulker") != std::string::npos
               || collection.find("chest") != std::string::npos) {
        if (auto* c = openContainer(player, false); c && index < c->getContainerSize()) ref = {c, index, false};
    }
    return ref;
}

ItemStack cursorItem(Player& player) { return player.mPlayerUIContainer->getItem(0); }

void setCursorItem(Player& player, ItemStack const& item) {
    player.setPlayerUIItem(PlayerUISlot::CursorSelected, item, false);
}

void syncPlayer(Player& player) { player.sendInventory(false); }

} // namespace lp::feature::inventory
