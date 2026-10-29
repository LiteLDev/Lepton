// Chute (Quark automation/ChuteModule): items fed in by hoppers / droppers fall out below (or
// into a pipe below). A redstone signal disables it (state lepton:enabled).

#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/phys/AABB.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/logistics/Logistics.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::logistics {

namespace chute {

bool enabled(Block const& b) { return game::blockstates::getBool(b, "lepton:enabled").value_or(true); }

/// Quark's ChuteBlockEntity#canDropItem.
bool canDrop(BlockSource& region, BlockPos const& pos) {
    if (!cfg().enableChute) return false;
    auto const& self = region.getBlock(pos);
    if (game::blockId(self) != kChute || !enabled(self)) return false;
    BlockPos below = offset(pos, Down);
    auto const& b  = region.getBlock(below);
    if (b.isAir()) return true;
    auto id = game::blockId(b);
    if (id == kGrate || isPipe(id)) return true;
    AABB shape = b.getBlockType().getCollisionShape(b, region, below, nullptr);
    Vec3 lo    = shape.min - Vec3{static_cast<float>(below.x), static_cast<float>(below.y), static_cast<float>(below.z)};
    Vec3 hi    = shape.max - Vec3{static_cast<float>(below.x), static_cast<float>(below.y), static_cast<float>(below.z)};
    if (hi.x - lo.x <= 0 || hi.y - lo.y <= 0 || hi.z - lo.z <= 0) return true; // no collision
    if (hi.y > 1) return false;
    // clearance box: the block's space shrunk by 0.25 and moved up by 0.25
    bool hits = lo.x < 0.75f && hi.x > 0.25f && lo.y < 1.0f && hi.y > 0.5f && lo.z < 0.75f && hi.z > 0.25f;
    return !hits;
}

/// ChuteBlockEntity#setItem: the item drops below with no motion, or goes into a pipe below.
void drop(BlockSource& region, BlockPos const& pos, ItemStack const& stack) {
    auto below = offset(pos, Down);
    if (isPipe(game::blockIdAt(region, below)) && pipes::passIn(game::globalPos(region, below), stack, Up)) return;
    spawnItemAt(region, stack, Vec3{pos.x + 0.5f, pos.y - 0.5f, pos.z + 0.5f}, Vec3{0, 0, 0}, false);
}

class ChutePort final : public Port {
public:
    explicit ChutePort(core::GlobalPos pos) : mPos(pos) {}
    int              size() const override { return 1; }
    ItemStack const& get(int) const override { return ItemStack::EMPTY_ITEM(); }
    void             set(int, ItemStack const& item) override {
        auto* region = game::regionOf(mPos.dim);
        if (!region || item.isNull() || item.mCount == 0) return;
        auto bp = game::blockPos(mPos);
        if (!canDrop(*region, bp)) return;
        drop(*region, bp, item);
    }
    bool canPushIn(int, int face, ItemStack const&) const override {
        if (face == Down) return false;
        auto* region = game::regionOf(mPos.dim);
        return region && canDrop(*region, game::blockPos(mPos));
    }
    bool canPullOut(int, int, ItemStack const&) const override { return false; }

private:
    core::GlobalPos mPos;
};

/// Sets lepton:enabled from the redstone around it (ChuteBlock#neighborChanged).
void updatePower(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (game::blockId(b) != kChute) return;
    bool want = !powered(region, pos);
    if (enabled(b) == want) return;
    if (auto const* n = game::blockstates::with(b, {{"lepton:enabled", want}})) game::setBlock(region, pos, *n);
}

} // namespace chute

class ChuteModule {
public:
    void install() {
        registerPort(kChute, [](core::GlobalPos const& p) -> std::unique_ptr<Port> {
            if (!cfg().enableChute) return nullptr;
            return std::make_unique<chute::ChutePort>(p);
        });
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([this](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            auto const& pos = ev.pos();
            if (ev.newBlock().getTypeName() == kChute) mQueue.insert(game::globalPos(region, pos));
            for (int f : kFaces) {
                auto n = offset(pos, f);
                if (region.getBlock(n).getTypeName() == kChute) mQueue.insert(game::globalPos(region, n));
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            if (mQueue.empty()) return;
            auto queue = std::move(mQueue);
            mQueue.clear();
            for (auto const& p : queue) {
                auto* region = game::regionOf(p.dim);
                if (!region || !region->hasChunksAt(game::blockPos(p), 0, false)) continue;
                if (game::blockIdAt(*region, game::blockPos(p)) == kChute) chute::updatePower(*region, game::blockPos(p));
            }
        }));
    }
    void uninstall() {
        unregisterPort(kChute);
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        mQueue.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    std::set<core::GlobalPos>           mQueue;
};

} // namespace lp::feature::logistics

LP_REGISTER_MODULE(lp::feature::logistics::ChuteModule);
