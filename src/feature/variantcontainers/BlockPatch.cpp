// Native behaviour of the module's block types.
//
// Data-driven blocks have no way to declare a comparator signal or a block actor. Each of our block
// types gets a private copy of its vtable with these entries replaced (the other blocks keep the
// shared vtable, so nothing else changes):
//   - chests:   hasComparatorSignal / getComparatorSignal (fill level of the virtual container)
//   - furnaces: the same (fill level of the furnace's own container), isContainerBlock,
//               isCraftingBlock and onRemove (drop items + experience like FurnaceBlock#onRemove).
// The furnace block types also get the vanilla furnace block actor (`BlockType::mBlockEntityType`),
// so the game itself smelts, saves, serves hoppers and opens the furnace screen.
//
// The patch runs when the game registers the block from its definition (server pack load, and on a
// client when it joins a world) and again when a world is ready (idempotent).

#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/definition/BlockDefinition.h"
#include "mc/world/level/block/definition/BlockDefinitionGroup.h"
#include "mc/world/level/block/definition/BlockDescription.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

#include "feature/variantcontainers/VariantContainers.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::variantcontainers {
namespace {

enum class Kind { Chest, Furnace };

struct Slots {
    uint hasComparator = 0, getComparator = 0, onRemove = 0, isContainer = 0, isCrafting = 0, count = 0;
    bool ok = false;
};

Slots const& slots() {
    static Slots const s = [] {
        Slots r;
        auto  idx = [](auto fn) { return ll::memory::getVtableIndex(fn); };
        auto  a   = idx(&BlockType::hasComparatorSignal);
        auto  b   = idx(&BlockType::getComparatorSignal);
        auto  c   = idx(&BlockType::onRemove);
        auto  d   = idx(&BlockType::isContainerBlock);
        auto  e   = idx(&BlockType::isCraftingBlock);
        auto  z   = idx(&BlockType::entityInside); // the last virtual declared by BlockType
        if (!a || !b || !c || !d || !e || !z) return r;
        r.hasComparator = *a;
        r.getComparator = *b;
        r.onRemove      = *c;
        r.isContainer   = *d;
        r.isCrafting    = *e;
        r.count         = *z + 1 + 48; // room for subclass virtuals
        r.ok            = true;
        return r;
    }();
    return s;
}

std::mutex& mutex() {
    static std::mutex m;
    return m;
}
/// Cloned vtable (pointing past the RTTI slot) -> original vtable, per kind.
struct Clone {
    void** original;
    Kind   kind;
};
std::unordered_map<void**, Clone>& clones() {
    static std::unordered_map<void**, Clone> m;
    return m;
}
/// (original vtable, kind) -> cloned vtable.
std::unordered_map<void**, void**>& cloneOf(Kind k) {
    static std::unordered_map<void**, void**> chest, furnace;
    return k == Kind::Chest ? chest : furnace;
}
bool gFurnacesPatched = false;

void** vtableOf(BlockType const& t) { return *reinterpret_cast<void** const*>(&t); }

void** originalOf(BlockType const& self) {
    auto it = clones().find(vtableOf(self));
    return it == clones().end() ? vtableOf(self) : it->second.original;
}

// ---- replacements (called through the cloned vtable; `self` is the BlockType) ------------------------

bool hasComparatorSignalImpl(BlockType const*) { return true; }
bool trueImpl(BlockType const*) { return true; }

int getComparatorSignalImpl(BlockType const* self, BlockSource& region, BlockPos const& pos, Block const&, uchar) {
    try {
        auto it = clones().find(vtableOf(*self));
        if (it != clones().end() && it->second.kind == Kind::Furnace) return furnaceComparatorSignal(region, pos);
        return chestComparatorSignal(region, pos);
    } catch (...) {
        return 0;
    }
}

void onRemoveImpl(BlockType const* self, BlockSource& region, BlockPos const& pos) {
    try {
        onFurnaceRemoved(region, pos);
    } catch (std::exception const& e) {
        log().error("variantcontainers: furnace removal failed: {}", e.what());
    }
    using Fn = void (*)(BlockType const*, BlockSource&, BlockPos const&);
    reinterpret_cast<Fn>(originalOf(*self)[slots().onRemove])(self, region, pos);
}

void** makeClone(void** original, Kind kind) {
    auto& s = slots();
    auto* mem = new void*[s.count + 1];
    std::memcpy(mem, original - 1, (s.count + 1) * sizeof(void*)); // [-1] is the RTTI locator
    void** table              = mem + 1;
    table[s.hasComparator]    = reinterpret_cast<void*>(&hasComparatorSignalImpl);
    table[s.getComparator]    = reinterpret_cast<void*>(&getComparatorSignalImpl);
    if (kind == Kind::Furnace) {
        table[s.onRemove]    = reinterpret_cast<void*>(&onRemoveImpl);
        table[s.isContainer] = reinterpret_cast<void*>(&trueImpl);
        table[s.isCrafting]  = reinterpret_cast<void*>(&trueImpl);
    }
    return table;
}

void patchOne(BlockType& type, Kind kind, std::string const& id) {
    if (!slots().ok) return;
    void** current = vtableOf(type);
    if (clones().contains(current)) {
        if (kind == Kind::Furnace) type.mBlockEntityType = BlockActorType::Furnace;
        return; // already ours
    }
    auto& byOriginal = cloneOf(kind);
    auto  it         = byOriginal.find(current);
    if (it == byOriginal.end()) {
        void** table = makeClone(current, kind);
        clones().emplace(table, Clone{current, kind});
        it = byOriginal.emplace(current, table).first;
    }
    *reinterpret_cast<void***>(&type) = it->second;
    if (kind == Kind::Furnace) {
        auto before           = type.mBlockEntityType;
        type.mBlockEntityType = BlockActorType::Furnace;
        log().debug("variantcontainers: {} gets the furnace block actor (was {})", id, static_cast<int>(before));
    }
}

/// What a block type is to this module: by id (ours, known before the tags are applied) or by tag
/// (chests other units add with lepton:variant_chest / lepton:variant_trapped_chest).
std::optional<Kind> kindOf(BlockType const& type, std::string const& id) {
    for (auto const& f : furnaceIds())
        if (f == id) return Kind::Furnace;
    for (auto const& c : chestIds())
        if (c == id) return Kind::Chest;
    if (type.hasTag(HashedString("lepton:variant_chest")) || type.hasTag(HashedString("lepton:variant_trapped_chest"))) return Kind::Chest;
    return std::nullopt;
}

void patchType(BlockType& type, std::string const& id) {
    auto kind = kindOf(type, id);
    if (!kind) return;
    std::lock_guard lock(mutex());
    patchOne(type, *kind, id);
    if (*kind == Kind::Furnace) gFurnacesPatched = true;
}

void patchById(std::string const& id) {
    if (auto* type = const_cast<BlockType*>(BlockTypeRegistry::get().lookupByName(HashedString(id), false))) patchType(*type, id);
}

LL_TYPE_INSTANCE_HOOK(
    RegisterBlockHook,
    ll::memory::HookPriority::Normal,
    BlockDefinitionGroup,
    &BlockDefinitionGroup::registerBlockFromDefinition,
    void,
    ::BlockDefinition const& definition,
    bool                     assertIfAlreadyExists
) {
    origin(definition, assertIfAlreadyExists);
    try {
        patchById(definition.mDescription->mIdentifier);
    } catch (...) {
    }
}

} // namespace

void patchBlockTypes() {
    if (!slots().ok) {
        log().error("variantcontainers: could not read the block vtable layout; chests and furnaces lose comparators");
        return;
    }
    for (auto const& id : furnaceIds()) patchById(id);
    BlockTypeRegistry::get().forEachBlockType([](BlockType const& t) {
        patchType(const_cast<BlockType&>(t), t.getTypeName());
        return true;
    });
}

bool furnacesPatched() { return gFurnacesPatched; }

class BlockPatchModule {
public:
    // The hook goes in at mod load: on BDS the world (and its blocks) loads before mods are enabled.
    void load() {
        if (!mHooked) {
            RegisterBlockHook::hook();
            mHooked = true;
        }
    }
    void install() {
        load();
        patchBlockTypes();
        game::db::onWorldReady([] { patchBlockTypes(); });
    }
    // Our replacement functions live in this DLL: give every block type its own vtable back.
    void uninstall() {
        if (mHooked) {
            RegisterBlockHook::unhook();
            mHooked = false;
        }
        std::lock_guard lock(mutex());
        BlockTypeRegistry::get().forEachBlockType([](BlockType const& t) {
            auto it = clones().find(vtableOf(t));
            if (it != clones().end()) *reinterpret_cast<void***>(const_cast<BlockType*>(&t)) = it->second.original;
            return true;
        });
    }

private:
    bool mHooked = false;
};

} // namespace lp::feature::variantcontainers

LP_REGISTER_MODULE_ORDERED(lp::feature::variantcontainers::BlockPatchModule, lp::ModuleOrder::Feature - 10);
