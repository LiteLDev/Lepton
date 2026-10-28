#include "game/Inventory.h"

#include <algorithm>

#include "mc/world/Container.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "game/Items.h"

namespace lp::game::inv {

namespace {
Range clampRange(Container const& c, Range r) {
    int size = c.getContainerSize();
    return {std::clamp(r.begin, 0, size), std::clamp(r.end, 0, size)};
}
int countOf(ItemStack const& s) { return isEmpty(s) ? 0 : s.mCount; }
} // namespace

Range all(Container const& c) { return {0, c.getContainerSize()}; }

Container& playerInventory(Player& player) { return player.getInventory(); }

int maxStackIn(Container const& c, ItemStack const& stack) {
    return std::max(1, std::min<int>(stack.getMaxStackSize(), c.getMaxStackSize()));
}

int roomFor(Container const& c, ItemStack const& stack, Range range) {
    int want = countOf(stack);
    if (want == 0) return 0;
    int max  = maxStackIn(c, stack);
    int room = 0;
    range    = clampRange(c, range);
    for (int i = range.begin; i < range.end && room < want; ++i) {
        auto const& s = c.getItem(i);
        if (isEmpty(s)) room += max;
        else if (canMerge(s, stack)) room += std::max(0, max - s.mCount);
    }
    return std::min(room, want);
}

int insert(Container& c, ItemStack& stack, Range range, bool mergeOnly) {
    int total = countOf(stack);
    if (total == 0) return 0;
    range     = clampRange(c, range);
    int max   = maxStackIn(c, stack);
    int left  = total;
    for (int pass = 0; pass < (mergeOnly ? 1 : 2) && left > 0; ++pass) {
        for (int i = range.begin; i < range.end && left > 0; ++i) {
            auto const& s = c.getItem(i);
            if (pass == 0) {
                if (!canMerge(s, stack) || s.mCount >= max) continue;
                int n      = std::min(left, max - static_cast<int>(s.mCount));
                ItemStack merged = s;
                merged.set(s.mCount + n);
                c.setItem(i, merged);
                left -= n;
            } else {
                if (!isEmpty(s)) continue;
                int       n = std::min(left, max);
                ItemStack placed = stack;
                placed.set(n);
                c.setItem(i, placed);
                left -= n;
            }
        }
    }
    int moved = total - left;
    if (left == 0) stack = ItemStack{};
    else stack.set(left);
    return moved;
}

int transferSlot(Container& from, int slot, Container& to, Range toRange, int maxCount) {
    if (slot < 0 || slot >= from.getContainerSize()) return 0;
    ItemStack source = from.getItem(slot);
    int       have   = countOf(source);
    if (have == 0) return 0;
    int       n      = maxCount < 0 ? have : std::min(have, maxCount);
    ItemStack moving = source;
    moving.set(n);
    int moved = insert(to, moving, toRange);
    if (moved == 0) return 0;
    if (moved >= have) from.setItem(slot, ItemStack{});
    else {
        source.set(have - moved);
        from.setItem(slot, source);
    }
    return moved;
}

int transferAll(Container& from, Range fromRange, Container& to, Range toRange, Predicate const& filter) {
    fromRange = clampRange(from, fromRange);
    int moved = 0;
    for (int i = fromRange.begin; i < fromRange.end; ++i) {
        auto const& s = from.getItem(i);
        if (isEmpty(s) || (filter && !filter(s))) continue;
        moved += transferSlot(from, i, to, toRange);
    }
    return moved;
}

int count(Container const& c, Predicate const& filter, Range range) {
    range = clampRange(c, range);
    int n = 0;
    for (int i = range.begin; i < range.end; ++i) {
        auto const& s = c.getItem(i);
        if (!isEmpty(s) && (!filter || filter(s))) n += s.mCount;
    }
    return n;
}

int remove(Container& c, Predicate const& filter, int amount, Range range) {
    range       = clampRange(c, range);
    int removed = 0;
    for (int i = range.end - 1; i >= range.begin && removed < amount; --i) {
        ItemStack s = c.getItem(i);
        if (isEmpty(s) || (filter && !filter(s))) continue;
        int n = std::min<int>(amount - removed, s.mCount);
        removed += n;
        if (n >= s.mCount) c.setItem(i, ItemStack{});
        else {
            s.set(s.mCount - n);
            c.setItem(i, s);
        }
    }
    return removed;
}

void compact(Container& c, Range range) {
    range = clampRange(c, range);
    for (int i = range.begin; i < range.end; ++i) {
        ItemStack target = c.getItem(i);
        if (isEmpty(target)) continue;
        int max = maxStackIn(c, target);
        for (int j = i + 1; j < range.end && target.mCount < max; ++j) {
            ItemStack s = c.getItem(j);
            if (!canMerge(s, target)) continue;
            int n = std::min<int>(max - target.mCount, s.mCount);
            target.set(target.mCount + n);
            if (n >= s.mCount) c.setItem(j, ItemStack{});
            else {
                s.set(s.mCount - n);
                c.setItem(j, s);
            }
        }
        c.setItem(i, target);
    }
}

bool defaultOrder(ItemStack const& a, ItemStack const& b) {
    if (a.getId() != b.getId()) return a.getId() < b.getId();
    if (a.getAuxValue() != b.getAuxValue()) return a.getAuxValue() < b.getAuxValue();
    auto na = a.getName(), nb = b.getName();
    if (na != nb) return na < nb;
    return a.mCount > b.mCount;
}

void sort(Container& c, Range range, Comparator const& less) {
    range = clampRange(c, range);
    compact(c, range);
    std::vector<ItemStack> items;
    for (int i = range.begin; i < range.end; ++i) {
        auto const& s = c.getItem(i);
        if (!isEmpty(s)) items.push_back(s);
    }
    auto const& cmp = less ? less : Comparator(defaultOrder);
    std::stable_sort(items.begin(), items.end(), [&](ItemStack const& a, ItemStack const& b) { return cmp(a, b); });
    restore(c, range, items);
}

std::vector<ItemStack> snapshot(Container const& c, Range range) {
    range = clampRange(c, range);
    std::vector<ItemStack> out;
    out.reserve(range.size());
    for (int i = range.begin; i < range.end; ++i) out.push_back(c.getItem(i));
    return out;
}

void restore(Container& c, Range range, std::vector<ItemStack> const& items) {
    range = clampRange(c, range);
    for (int i = range.begin; i < range.end; ++i) {
        size_t k = static_cast<size_t>(i - range.begin);
        c.setItem(i, k < items.size() ? items[k] : ItemStack{});
    }
}

void sync(Player& player) { player.sendInventory(false); }

} // namespace lp::game::inv
