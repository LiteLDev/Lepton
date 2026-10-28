#include "game/Items.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/SaveContextFactory.h"

namespace lp::game {

ItemStack makeItem(std::string_view id, int count, int aux) {
    if (id.empty() || count <= 0) return ItemStack{};
    CompoundTag tag;
    tag.putString("Name", std::string(id));
    tag.putByte("Count", static_cast<uchar>(std::min(count, 255)));
    tag.putShort("Damage", static_cast<short>(aux));
    auto stack = ItemStack::fromTag(tag);
    if (stack.isNull()) return ItemStack{};
    stack.set(count);
    return stack;
}

std::string itemId(ItemStackBase const& stack) { return isEmpty(stack) ? std::string{} : stack.getTypeName(); }

bool isEmpty(ItemStackBase const& stack) { return stack.isNull() || stack.mCount == 0; }

bool canMerge(ItemStackBase const& a, ItemStackBase const& b) {
    if (isEmpty(a) || isEmpty(b)) return false;
    return a.isStackable(b);
}

std::string itemToSnbt(ItemStackBase const& stack) {
    if (isEmpty(stack)) return "{}";
    auto ctx = SaveContextFactory::createCloneSaveContext();
    auto tag = stack.save(*ctx);
    if (!tag) return "{}";
    return tag->toSnbt(SnbtFormat::Minimize, 0);
}

ItemStack itemFromSnbt(std::string_view snbt) {
    auto tag = CompoundTag::fromSnbt(snbt);
    if (!tag || tag->empty()) return ItemStack{};
    return ItemStack::fromTag(*tag);
}

} // namespace lp::game
