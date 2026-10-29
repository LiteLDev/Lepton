#pragma once

// A plain `Container` over a vector of stacks: what hoppers / droppers see for a variant chest when
// nobody has it open, and the target the loot tables fill. Writes go to `onWrite`.

#include <functional>
#include <vector>

#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/Container.h"
#include "mc/world/item/ItemStack.h"

namespace lp::feature::variantcontainers {

class ItemsContainer final : public ::Container {
public:
    using WriteFn = std::function<void(int slot, std::vector<::ItemStack> const& all)>;

    ItemsContainer(std::vector<::ItemStack> items, WriteFn onWrite = {})
    : ::Container(SharedTypes::Legacy::ContainerType::Container),
      mItems(std::move(items)),
      mOnWrite(std::move(onWrite)) {}

    void serverInitItemStackIds(int, int, std::function<void(int, ::ItemStack const&)>) override {}
    ::ItemStack const& getItem(int slot) const override {
        if (slot < 0 || slot >= static_cast<int>(mItems.size())) return ::ItemStack::EMPTY_ITEM();
        return mItems[static_cast<size_t>(slot)];
    }
    void setItem(int slot, ::ItemStack const& item) override {
        if (slot < 0 || slot >= static_cast<int>(mItems.size())) return;
        mItems[static_cast<size_t>(slot)] = item;
        if (mOnWrite) mOnWrite(slot, mItems);
    }
    int  getContainerSize() const override { return static_cast<int>(mItems.size()); }
    int  getMaxStackSize() const override { return 64; }
    void startOpen(::Actor&) override {}
    void stopOpen(::Actor&) override {}
    bool canPushInItem(int slot, int, ::ItemStack const&) const override {
        return slot >= 0 && slot < static_cast<int>(mItems.size());
    }
    bool canPullOutItem(int slot, int, ::ItemStack const&) const override {
        return slot >= 0 && slot < static_cast<int>(mItems.size());
    }
    void setContainerChanged(int) override {}

    [[nodiscard]] std::vector<::ItemStack> const& items() const { return mItems; }

private:
    std::vector<::ItemStack> mItems;
    WriteFn                  mOnWrite;
};

} // namespace lp::feature::variantcontainers
