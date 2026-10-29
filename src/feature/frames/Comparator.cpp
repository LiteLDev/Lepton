// U24 GlassItemFrame comparator output. Java comparators read an item frame (rotation + 1, 0 when
// empty) through the block it hangs on. Custom blocks cannot declare comparator output, and
// BlockType::hasComparatorSignal is folded code (a hook would hit every block). Instead each glass
// frame block type gets its own copy of its virtual function table with hasComparatorSignal /
// getComparatorSignal replaced (the technique of U10's gravisand, unit-local here).

#include "feature/frames/Frames.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>
#include <windows.h>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace lp::feature::frames::glass {

namespace {

struct Patched {
    BlockType*               type;
    void**                   original;
    std::unique_ptr<void*[]> table; ///< [0] = RTTI locator, [1..] = slots
};
std::vector<Patched>& patched() {
    static std::vector<Patched> p;
    return p;
}
/// Tables that may still be referenced by a block type we no longer track: never freed.
std::vector<std::unique_ptr<void*[]>>& oldTables() {
    static std::vector<std::unique_ptr<void*[]>> t;
    return t;
}

template <class M>
void* thunkOf(M member) {
    static_assert(sizeof(M) == sizeof(void*), "single-inheritance member function pointer");
    void* p;
    std::memcpy(&p, &member, sizeof(p));
    return p;
}

/// The vtable slot a vcall thunk (`mov rax,[rcx]; jmp [rax+disp]`) dispatches through; -1 if unknown.
int slotOfThunk(void* thunk) {
    auto* p = static_cast<uint8_t const*>(thunk);
    for (int i = 0; i < 4 && p[0] == 0xE9; ++i) p = p + 5 + *reinterpret_cast<int32_t const*>(p + 1);
    if (p[0] != 0x48 || p[1] != 0x8B || p[2] != 0x01) return -1;
    p += 3;
    if (p[0] == 0x48 && p[1] == 0x8B) { // mov rax, [rax + disp]; jmp rax
        if (p[2] == 0x00) return 0;
        if (p[2] == 0x40) return p[3] / 8;
        if (p[2] == 0x80) return *reinterpret_cast<int32_t const*>(p + 3) / 8;
        return -1;
    }
    if (p[0] == 0x48) ++p; // rex.W jmp [rax + disp]
    if (p[0] != 0xFF) return -1;
    if (p[1] == 0x20) return 0;
    if (p[1] == 0x60) return p[2] / 8;
    if (p[1] == 0xA0) return *reinterpret_cast<int32_t const*>(p + 2) / 8;
    return -1;
}

size_t readablePointers(void* const* p, size_t max) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
    auto end   = static_cast<uint8_t const*>(mbi.BaseAddress) + mbi.RegionSize;
    auto avail = static_cast<size_t>(end - reinterpret_cast<uint8_t const*>(p)) / sizeof(void*);
    return std::min(avail, max);
}

bool patchVirtual(BlockType& type, void* memberThunk, void* fn) {
    int slot = slotOfThunk(memberThunk);
    if (slot < 0) return false;
    auto*& vptr = *reinterpret_cast<void***>(&type);
    auto   it   = std::find_if(patched().begin(), patched().end(), [&](Patched const& p) { return p.type == &type; });
    if (it != patched().end() && vptr != it->table.get() + 1) { // re-registered: start over
        oldTables().push_back(std::move(it->table));
        patched().erase(it);
        it = patched().end();
    }
    if (it == patched().end()) {
        size_t n = readablePointers(vptr - 1, 600);
        if (n < static_cast<size_t>(slot) + 2) return false;
        Patched p{&type, vptr, std::make_unique<void*[]>(n)};
        std::memcpy(p.table.get(), vptr - 1, n * sizeof(void*));
        patched().push_back(std::move(p));
        it   = std::prev(patched().end());
        vptr = it->table.get() + 1;
    }
    it->table[static_cast<size_t>(slot) + 1] = fn;
    return true;
}

bool hasSignal(BlockType const*) { return true; }
int  signal(BlockType const*, BlockSource& region, BlockPos const& pos, Block const&, uchar) {
    return comparatorSignal(game::globalPos(region, pos));
}

} // namespace

void installComparators() {
    for (auto id : {kGlassFrame, kGlowingGlassFrame}) {
        auto const* b = game::blockstates::of(id);
        if (!b) {
            log().warn("frames: {} is not registered (behavior pack missing?)", id);
            continue;
        }
        auto& type = const_cast<BlockType&>(b->getBlockType());
        bool  ok   = patchVirtual(type, thunkOf(&BlockType::hasComparatorSignal), reinterpret_cast<void*>(&hasSignal))
                && patchVirtual(type, thunkOf(&BlockType::getComparatorSignal), reinterpret_cast<void*>(&signal));
        if (ok) log().info("frames: comparator output installed for {}", id);
        else log().error("frames: comparator output could not be installed for {}", id);
    }
}

void uninstallComparators() {
    for (auto& p : patched()) {
        auto*& vptr = *reinterpret_cast<void***>(p.type);
        if (vptr == p.table.get() + 1) vptr = p.original;
        oldTables().push_back(std::move(p.table));
    }
    patched().clear();
}

void forgetComparators() {
    // the client destroys a closed world's block types: forget them without touching them
    for (auto& p : patched()) oldTables().push_back(std::move(p.table));
    patched().clear();
}

} // namespace lp::feature::frames::glass
