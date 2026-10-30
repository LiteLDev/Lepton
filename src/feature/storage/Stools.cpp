// Stools (Quark StoolBlock / Stool entity / StoolsModule).

#include <cmath>
#include <cstring>
#include <set>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/MovingBlockActor.h"
#include "mc/world/level/block/block_events/BlockEntityFallOnEvent.h"
#include "mc/world/phys/AABB.h"

#include "feature/storage/Storage.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace lp::feature::storage::stools {

namespace {

namespace bs                     = game::blockstates;
constexpr std::string_view kSeat = "lepton:stool";

bool isStool(Block const& b) { return b.getTypeName().ends_with("_stool") && hasTag(b, "lepton:stool"); }

std::set<int64_t>& seats() {
    static std::set<int64_t> s;
    return s;
}

struct PendingFix {
    int      dim;
    BlockPos pos;
};
std::vector<PendingFix>& pendingFixes() {
    static std::vector<PendingFix> v;
    return v;
}

BlockPos above(BlockPos p) { return BlockPos{p.x, p.y + 1, p.z}; }

void setStates(BlockSource& region, BlockPos const& pos, bool big, bool satIn) {
    auto const& b = region.getBlock(pos);
    if (!isStool(b)) return;
    auto const* n = bs::with(b, {{"lepton:big", big}, {"lepton:sat_in", satIn}});
    if (n && n != &b) game::setBlock(region, pos, *n);
}

/// Java: the block above has an outline shape starting at its bottom (`shape.min(Y) == 0`).
bool flushOnTop(BlockSource& region, BlockPos const& top) {
    auto const& b = region.getBlock(top);
    if (b.isAir()) return false;
    auto const& n = b.getTypeName();
    if (n == "minecraft:water" || n == "minecraft:flowing_water" || n == "minecraft:lava" || n == "minecraft:flowing_lava") return false;
    AABB        buf;
    AABB const& shape = b.getBlockType().getOutline(b, region, top, buf);
    if (shape.max.y <= shape.min.y && shape.max.x <= shape.min.x) return false; // empty shape
    float minY = shape.min.y;
    if (minY >= top.y - 0.5f) minY -= static_cast<float>(top.y); // world coordinates
    return std::abs(minY) < 1e-3f;
}

bool seatAt(BlockSource& region, BlockPos const& pos) {
    AABB box{
        Vec3{pos.x - 0.4f, pos.y - 0.4f, pos.z - 0.4f},
        Vec3{pos.x + 1.4f, pos.y + 1.4f, pos.z + 1.4f}
    };
    for (auto* a : game::entities::inBox(region, box, kSeat)) {
        if (game::blockPosOf(a->getPosition()) == pos) return true;
    }
    return false;
}

/// A moving block (piston) at `p` that carries a stool.
bool movingStoolAt(BlockSource& region, BlockPos const& p) {
    auto const& b = region.getBlock(p);
    if (b.getTypeName() != "minecraft:moving_block") return false;
    auto* actor = region.getBlockEntity(p);
    if (!actor) return false;
    auto* moving = static_cast<MovingBlockActor*>(actor);
    auto const* wrapped = moving->mWrappedBlock;
    return wrapped && isStool(*wrapped);
}

void queueFix(BlockSource& region, BlockPos const& pos) {
    pendingFixes().push_back({static_cast<int>(region.getDimensionId()), pos});
}

// ---- events -----------------------------------------------------------------------------------

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    if (!cfg().enableStools) return;
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.blockPos();
    auto const& b = region.getBlock(pos);
    if (!isStool(b)) return;
    auto const& item = ev.item();
    if (player.isSneaking() && !item.isNull()) {
        // Quark StoolsModule#itemUsed: a block placed on the top makes the stool a table
        bool blockItem = item.mBlock != nullptr || (item.mItem.get() && item.mItem->getBlockTypeForRendering());
        if (static_cast<int>(ev.face()) == 1 && blockItem && !bs::getBool(b, "lepton:big").value_or(false)) {
            setStates(region, pos, true, bs::getBool(b, "lepton:sat_in").value_or(false));
            queueFix(region, pos);
        }
        return;
    }
    // StoolBlock#useWithoutItem
    if (bs::getBool(b, "lepton:sat_in").value_or(false) || !region.getBlock(above(pos)).isAir() || player.getVehicle()) return;
    ev.cancel();
    auto id  = player.getOrCreateUniqueID().rawID;
    int  dim = static_cast<int>(region.getDimensionId());
    defer([id, dim, pos] {
        auto* a = game::entities::find(id);
        auto* r = game::regionOf(dim);
        if (a && r && a->isPlayer()) sit(static_cast<Player&>(*a), *r, pos);
    });
}

void onTick(ll::event::ServerLevelTickEvent&) {
    if (!pendingFixes().empty()) {
        auto work = std::move(pendingFixes());
        pendingFixes().clear();
        for (auto const& f : work)
            if (auto* r = game::regionOf(f.dim)) fixState(*r, f.pos);
    }
    tickSeats();
}

// ---- fall damage --------------------------------------------------------------------------------

// Quark StoolBlock#fallOn: half the fall distance.
LL_TYPE_INSTANCE_HOOK(
    StoolFallHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::onFallOnBase,
    void,
    BlockEvents::BlockEntityFallOnEvent& event
) {
    if (cfg().enableStools && getTypeName().ends_with("_stool") && getTypeName().starts_with("lepton:")) {
        event.mFallDistance = event.mFallDistance * 0.5f;
    }
    origin(event);
}

// ---- bounce and comparator: a patched vtable for the stool block types ----------------------------
// Custom blocks share one C++ class; the stools get a copy of its vtable with getBounciness (like a
// bed) and the comparator signal (15 while sat in) replaced. Other blocks are not touched.

constexpr size_t kVtableCopy = 512;

float stoolBounciness(BlockType const*, IConstBlockSource const&, BlockPos const&) { return cfg().enableStools ? 0.66f : 0.f; }
bool  stoolHasComparator(BlockType const*) { return cfg().enableStools; }
int   stoolComparator(BlockType const*, BlockSource&, BlockPos const&, Block const& block, uchar) {
    return cfg().enableStools && bs::getBool(block, "lepton:sat_in").value_or(false) ? 15 : 0;
}

void** gOriginalVtable = nullptr;
void*  gPatched[kVtableCopy + 1]{};
bool   gPatchFailed = false;

void patchStoolTypes(bool install) {
    for (auto const& id : contentIds()) {
        if (!id.ends_with("_stool")) continue;
        auto type = BlockType::tryGetFromRegistry(HashedString(std::string(id)));
        if (!type) continue;
        auto*  t    = const_cast<BlockType*>(type.as_ptr());
        auto** vptr = reinterpret_cast<void***>(t);
        if (!install) {
            if (gOriginalVtable && *vptr == &gPatched[1]) *vptr = gOriginalVtable;
            continue;
        }
        if (gPatchFailed || *vptr == &gPatched[1]) continue;
        if (!gOriginalVtable) {
            auto bounce = ll::memory::getVtableIndex(&BlockType::getBounciness);
            auto hasCmp = ll::memory::getVtableIndex(&BlockType::hasComparatorSignal);
            auto getCmp = ll::memory::getVtableIndex(&BlockType::getComparatorSignal);
            if (!bounce || !hasCmp || !getCmp || *bounce >= kVtableCopy || *getCmp >= kVtableCopy) {
                gPatchFailed = true;
                log().warn("storage: stool bounce/comparator unavailable (vtable slots not found)");
                return;
            }
            gOriginalVtable = *vptr;
            std::memcpy(gPatched, gOriginalVtable - 1, sizeof(gPatched)); // RTTI locator + slots
            gPatched[1 + *bounce] = reinterpret_cast<void*>(&stoolBounciness);
            gPatched[1 + *hasCmp] = reinterpret_cast<void*>(&stoolHasComparator);
            gPatched[1 + *getCmp] = reinterpret_cast<void*>(&stoolComparator);
        }
        if (*vptr == gOriginalVtable) *vptr = &gPatched[1];
    }
}

} // namespace

// ---- public -----------------------------------------------------------------------------------

bool sit(Player& player, BlockSource& region, BlockPos const& pos) {
    if (!cfg().enableStools) return false;
    auto const& b = region.getBlock(pos);
    if (!isStool(b) || bs::getBool(b, "lepton:sat_in").value_or(false)) return false;
    if (!region.getBlock(above(pos)).isAir() || player.getVehicle()) return false;
    Vec3  at{pos.x + 0.5f, pos.y + 0.6f, pos.z + 0.5f};
    auto* seat = game::entities::spawn(region, kSeat, at);
    if (!seat) return false;
    if (!player.startRiding(*seat, true)) {
        game::entities::despawn(*seat);
        return false;
    }
    seats().insert(game::entities::idOf(*seat));
    setStates(region, pos, bs::getBool(b, "lepton:big").value_or(false), true);
    log().info("storage: {} sat on a stool at {} {} {}", player.getRealName(), pos.x, pos.y, pos.z);
    return true;
}

bool fixState(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (!isStool(b)) return false;
    bool big   = flushOnTop(region, above(pos));
    bool satIn = seatAt(region, pos);
    if (bs::getBool(b, "lepton:big").value_or(false) == big && bs::getBool(b, "lepton:sat_in").value_or(false) == satIn) return false;
    setStates(region, pos, big, satIn);
    return true;
}

void blockChanged(BlockSource& region, BlockPos const& pos, Block const& newBlock, Block const& below) {
    if (isStool(newBlock)) queueFix(region, pos); // placed: compute its state
    if (isStool(below)) queueFix(region, BlockPos{pos.x, pos.y - 1, pos.z});
}

void applyTypes() {
    if (cfg().enableStools) patchStoolTypes(true);
}

bool typesPatched() {
    auto type = BlockType::tryGetFromRegistry(HashedString("lepton:white_stool"));
    return type && gOriginalVtable && *reinterpret_cast<void* const*>(type.as_ptr()) == &gPatched[1];
}

Actor* seatOf(Player& player) {
    auto* v = player.getVehicle();
    return v && game::entities::typeOf(*v) == kSeat ? v : nullptr;
}

void tickSeats() {
    auto level = ll::service::getLevel();
    if (!level) return;
    // seats of a reloaded world are found again
    if (level->getCurrentTick().tickID % 40 == 0)
        for (auto* a : game::entities::ofType(kSeat)) seats().insert(game::entities::idOf(*a));
    for (auto it = seats().begin(); it != seats().end();) {
        auto* seat = game::entities::find(*it);
        if (!seat) { // gone, or its chunk is not loaded
            it = seats().erase(it);
            continue;
        }
        auto& region = seat->getDimensionBlockSource();
        auto  cell   = game::blockPosOf(seat->getPosition());
        bool  alive  = seat->hasPassenger();
        if (alive && !isStool(region.getBlock(cell)) && !movingStoolAt(region, cell)) {
            // Quark: a stool pushed by a piston carries its seat along
            alive = false;
            for (auto [dx, dy, dz] : {std::tuple{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}) {
                BlockPos n{cell.x + dx, cell.y + dy, cell.z + dz};
                if (movingStoolAt(region, n)) {
                    Vec3 to{n.x + 0.5f, n.y + 0.6f, n.z + 0.5f};
                    seat->teleportTo(to, false, 0, 0, false);
                    alive = true;
                    break;
                }
            }
        }
        if (!alive) {
            auto const& b = region.getBlock(cell);
            if (isStool(b)) setStates(region, cell, bs::getBool(b, "lepton:big").value_or(false), false);
            game::entities::despawn(*seat);
            it = seats().erase(it);
            continue;
        }
        auto const& b = region.getBlock(cell);
        if (isStool(b) && !bs::getBool(b, "lepton:sat_in").value_or(false))
            setStates(region, cell, bs::getBool(b, "lepton:big").value_or(false), true);
        ++it;
    }
}

namespace {
std::vector<ll::event::ListenerPtr>& listeners() {
    static std::vector<ll::event::ListenerPtr> v;
    return v;
}
} // namespace

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    listeners().push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
    listeners().push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
    StoolFallHook::hook();
    static bool once = false;
    if (!once) {
        once = true;
        game::db::onWorldReady([] { applyTypes(); });
        game::db::onWorldClosed([] {
            seats().clear();
            pendingFixes().clear();
        });
    }
    if (ll::service::getLevel()) applyTypes();
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : listeners()) bus.removeListener(l);
    listeners().clear();
    StoolFallHook::unhook();
    patchStoolTypes(false);
    seats().clear();
    pendingFixes().clear();
}

} // namespace lp::feature::storage::stools
