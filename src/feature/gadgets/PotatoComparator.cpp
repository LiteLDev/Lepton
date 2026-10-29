// Tiny potato comparator output (Quark: hasAnalogOutputSignal = true, getAnalogOutputSignal = the
// fill level of its 6 one-item slots). Data-driven blocks can't declare an analog output and
// BlockType::hasComparatorSignal is folded with many other `return false` functions, so it can't be
// hooked. Instead the tiny potato's BlockType gets its own copy of its virtual function table with
// the two comparator slots replaced; vanilla comparators then read the potato like a container.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <windows.h>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "core/GlobalPos.h"
#include "feature/gadgets/Gadgets.h"
#include "feature/gadgets/Potato.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace lp::feature::gadgets::potato {

namespace {

struct Patch {
    BlockType*               type     = nullptr;
    void**                   original = nullptr;
    std::unique_ptr<void*[]> table; ///< [0] = RTTI complete object locator, [1..] = the slots
};
Patch                                 gPatch;
std::vector<std::unique_ptr<void*[]>> gRetired; ///< old copies stay alive: a thread may still be in them

/// The vtable slot a member-function vcall thunk dispatches through (`mov rax,[rcx]; jmp [rax+d]`).
int slotOfThunk(void* thunk) {
    auto const* p = static_cast<uint8_t const*>(thunk);
    for (int i = 0; i < 4 && p[0] == 0xE9; ++i) p = p + 5 + *reinterpret_cast<int32_t const*>(p + 1); // jmp rel32
    if (p[0] != 0x48 || p[1] != 0x8B || p[2] != 0x01) return -1; // mov rax, [rcx]
    p += 3;
    if (p[0] == 0x48 && p[1] == 0x8B) { // mov rax, [rax + d]; jmp rax
        if (p[2] == 0x00) return 0;
        if (p[2] == 0x40) return p[3] / 8;
        if (p[2] == 0x80) return *reinterpret_cast<int32_t const*>(p + 3) / 8;
        return -1;
    }
    if (p[0] == 0x48) ++p; // rex.W
    if (p[0] != 0xFF) return -1; // jmp [rax + d]
    if (p[1] == 0x20) return 0;
    if (p[1] == 0x60) return p[2] / 8;
    if (p[1] == 0xA0) return *reinterpret_cast<int32_t const*>(p + 2) / 8;
    return -1;
}

size_t readablePointers(void* const* p, size_t max) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
    auto end = static_cast<uint8_t const*>(mbi.BaseAddress) + mbi.RegionSize;
    return std::min(static_cast<size_t>(end - reinterpret_cast<uint8_t const*>(p)) / sizeof(void*), max);
}

template <class M>
void* thunkOf(M member) {
    static_assert(sizeof(M) == sizeof(void*), "single-inheritance member function pointer");
    void* p;
    std::memcpy(&p, &member, sizeof(p));
    return p;
}

bool patchSlot(BlockType& type, void* thunk, void* fn) {
    int slot = slotOfThunk(thunk);
    if (slot < 0) return false;
    auto*& vptr = *reinterpret_cast<void***>(&type);
    if (gPatch.type == &type && vptr != gPatch.table.get() + 1) { // re-registered: start over
        gRetired.push_back(std::move(gPatch.table));
        gPatch = {};
    }
    if (gPatch.type && gPatch.type != &type) { // another world's block type: keep its table alive
        gRetired.push_back(std::move(gPatch.table));
        gPatch = {};
    }
    if (gPatch.type != &type) {
        size_t n = readablePointers(vptr - 1, 600);
        if (n < static_cast<size_t>(slot) + 2) return false;
        Patch p{&type, vptr, std::make_unique<void*[]>(n)};
        std::memcpy(p.table.get(), vptr - 1, n * sizeof(void*));
        gPatch = std::move(p);
        vptr   = gPatch.table.get() + 1;
    }
    gPatch.table[static_cast<size_t>(slot) + 1] = fn;
    return true;
}

// The replacements (MSVC x64 member functions: `this` first).
bool hasSignal(BlockType const*) { return true; }
int  signal(BlockType const*, BlockSource& region, BlockPos const& pos, Block const&, uchar) {
    if (region.getLevel().isClientSide()) return 0; // the potato data lives on the server thread
    if (!cfg().enableTinyPotato) return 0;
    auto data = dataAt(game::globalPos(region, pos));
    return data ? comparatorSignal(*data) : 0;
}

} // namespace

bool installComparator() {
    auto const* b = game::blockstates::of(kPotato);
    if (!b) return false;
    auto& type = const_cast<BlockType&>(b->getBlockType());
    bool  ok   = patchSlot(type, thunkOf(&BlockType::hasComparatorSignal), reinterpret_cast<void*>(&hasSignal))
           && patchSlot(type, thunkOf(&BlockType::getComparatorSignal), reinterpret_cast<void*>(&signal));
    if (ok) log().info("gadgets: tiny potato comparator output installed");
    else log().error("gadgets: tiny potato comparator output could not be installed");
    return ok;
}

void uninstallComparator() {
    if (!gPatch.type) return;
    auto*& vptr = *reinterpret_cast<void***>(gPatch.type);
    if (vptr == gPatch.table.get() + 1) vptr = gPatch.original;
    gRetired.push_back(std::move(gPatch.table));
    gPatch = {};
}

} // namespace lp::feature::gadgets::potato
