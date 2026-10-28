// Item ports: custom blocks seen as containers by hoppers, droppers and pipes (see Logistics.h).

#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"

#include "feature/logistics/Logistics.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::logistics {

bool Port::canPushIn(int, int, ItemStack const&) const { return true; }
bool Port::canPullOut(int, int, ItemStack const&) const { return true; }
int  Port::room(int, ItemStack const&) const { return 1 << 30; }

namespace {

/// The `Container` the game sees for a port. Created per lookup, freed on the next tick.
class PortContainer final : public ::Container {
public:
    explicit PortContainer(std::unique_ptr<Port> port)
    : ::Container(SharedTypes::Legacy::ContainerType::Container),
      mPort(std::move(port)) {}

    void serverInitItemStackIds(int, int, std::function<void(int, ::ItemStack const&)>) override {}
    ::ItemStack const& getItem(int slot) const override {
        if (slot < 0 || slot >= mPort->size()) return ::ItemStack::EMPTY_ITEM();
        return mPort->get(slot);
    }
    void setItem(int slot, ::ItemStack const& item) override {
        if (slot < 0 || slot >= mPort->size()) return;
        mPort->set(slot, item);
    }
    int  getContainerSize() const override { return mPort->size(); }
    int  getMaxStackSize() const override { return mPort->maxStack(); }
    void startOpen(::Actor&) override {}
    void stopOpen(::Actor&) override {}
    bool canPushInItem(int slot, int face, ::ItemStack const& item) const override {
        return slot >= 0 && slot < mPort->size() && mPort->canPushIn(slot, face, item);
    }
    bool canPullOutItem(int slot, int face, ::ItemStack const& item) const override {
        return slot >= 0 && slot < mPort->size() && mPort->canPullOut(slot, face, item);
    }
    void setContainerChanged(int) override {}
    [[nodiscard]] Port const& port() const { return *mPort; }

private:
    std::unique_ptr<Port> mPort;
};

std::unordered_map<std::string, PortFactory>& factories() {
    static std::unordered_map<std::string, PortFactory> m;
    return m;
}
std::vector<std::unique_ptr<PortContainer>>& live() {
    static std::vector<std::unique_ptr<PortContainer>> v;
    return v;
}

LL_TYPE_INSTANCE_HOOK(
    HopperBlockContainerHook,
    ll::memory::HookPriority::Normal,
    Hopper,
    &Hopper::_getContainerInBlock,
    ::Container*,
    ::BlockSource& region,
    ::Vec3 const&  pos
) {
    if (auto* c = portAt(region, game::blockPosOf(pos))) return c;
    return origin(region, pos);
}

LL_TYPE_STATIC_HOOK(
    DropperContainerHook,
    ll::memory::HookPriority::Normal,
    DropperBlockActor,
    &DropperBlockActor::getContainerAt,
    ::Container*,
    ::BlockSource& region,
    ::Vec3 const&  pos
) {
    if (auto* c = portAt(region, game::blockPosOf(pos))) return c;
    return origin(region, pos);
}

} // namespace

void registerPort(std::string const& blockId, PortFactory factory) { factories()[blockId] = std::move(factory); }
void unregisterPort(std::string const& blockId) { factories().erase(blockId); }

Container* portAt(BlockSource& region, BlockPos const& pos) {
    if (factories().empty() || region.getLevel().isClientSide()) return nullptr;
    auto it = factories().find(game::blockIdAt(region, pos));
    if (it == factories().end()) return nullptr;
    auto port = it->second(game::globalPos(region, pos));
    if (!port) return nullptr;
    live().push_back(std::make_unique<PortContainer>(std::move(port)));
    return live().back().get();
}

Container* containerAt(BlockSource& region, BlockPos const& pos) {
    if (auto* c = portAt(region, pos)) return c;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    return DropperBlockActor::getContainerAt(region, at); // hooked: ports first, then the game's lookup
}

/// The port behind a container we created (nullptr for game containers).
Port const* portOf(Container const& c) {
    for (auto const& p : live())
        if (p.get() == &c) return &p->port();
    return nullptr;
}

int insertThroughFace(Container& c, ItemStack& stack, int face, bool simulate) {
    Port const* port = portOf(c);
    if (game::isEmpty(stack)) return 0;
    int const start = stack.mCount;
    int const size  = c.getContainerSize();
    // merge into matching stacks first, then fill empty slots (like a hopper)
    for (int pass = 0; pass < 2 && stack.mCount > 0; ++pass) {
        for (int slot = 0; slot < size && stack.mCount > 0; ++slot) {
            if (!c.canPushInItem(slot, face, stack)) continue;
            ItemStack const& existing = c.getItem(slot);
            int limit = std::min<int>(stack.getMaxStackSize(), c.getMaxStackSize());
            if (port) limit = std::min(limit, existing.mCount + port->room(slot, stack));
            if (pass == 0) {
                if (game::isEmpty(existing) || !game::canMerge(existing, stack) || existing.mCount >= limit) continue;
                int move = std::min<int>(limit - existing.mCount, stack.mCount);
                if (!simulate) {
                    ItemStack merged = existing;
                    merged.set(existing.mCount + move);
                    c.setItem(slot, merged);
                }
                stack.set(stack.mCount - move);
            } else {
                if (!game::isEmpty(existing)) continue;
                int       move = std::min<int>(limit, stack.mCount);
                if (move <= 0) continue;
                ItemStack part = stack;
                part.set(move);
                if (!simulate) c.setItem(slot, part);
                stack.set(stack.mCount - move);
            }
        }
    }
    int moved = start - stack.mCount;
    if (moved > 0 && !simulate) c.setContainerChanged(0);
    return moved;
}

class PortsModule {
public:
    void install() {
        HopperBlockContainerHook::hook();
        DropperContainerHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            if (!live().empty()) live().clear();
        });
    }
    void uninstall() {
        HopperBlockContainerHook::unhook();
        DropperContainerHook::unhook();
        ll::event::EventBus::getInstance().removeListener(mTick);
        live().clear();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace lp::feature::logistics

LP_REGISTER_MODULE_ORDERED(lp::feature::logistics::PortsModule, lp::ModuleOrder::Feature - 1);
