// Hoppers, hopper minecarts and droppers see a variant chest as a container: while someone has the
// chest open, the live screen container; otherwise a container over the stored items (writes go
// straight back to the virtual container). Containers handed out are freed on the next tick.

#include <memory>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"

#include "feature/variantcontainers/ItemsContainer.h"
#include "feature/variantcontainers/VariantContainers.h"
#include "game/Blocks.h"
#include "game/VirtualContainer.h"
#include "mod/Module.h"

namespace lp::feature::variantcontainers {

namespace {

std::vector<std::unique_ptr<ItemsContainer>>& live() {
    static std::vector<std::unique_ptr<ItemsContainer>> v;
    return v;
}

LL_TYPE_INSTANCE_HOOK(
    ChestHopperHook,
    ll::memory::HookPriority::Normal,
    Hopper,
    &Hopper::_getContainerInBlock,
    ::Container*,
    ::BlockSource& region,
    ::Vec3 const&  pos
) {
    if (auto* c = chestContainerAt(region, game::blockPosOf(pos))) return c;
    return origin(region, pos);
}

LL_TYPE_STATIC_HOOK(
    ChestDropperHook,
    ll::memory::HookPriority::Normal,
    DropperBlockActor,
    &DropperBlockActor::getContainerAt,
    ::Container*,
    ::BlockSource& region,
    ::Vec3 const&  pos
) {
    if (auto* c = chestContainerAt(region, game::blockPosOf(pos))) return c;
    return origin(region, pos);
}

} // namespace

Container* chestContainerAt(BlockSource& region, BlockPos const& pos) {
    if (region.getLevel().isClientSide()) return nullptr;
    if (!isVariantChest(region.getBlock(pos))) return nullptr;
    auto st = chestStorage(region, pos);
    if (!st) return nullptr;
    ensureLoot(region, pos);
    if (auto* c = game::vc::liveContainer(st->key)) return c;
    auto key = st->key;
    int  dim = key.dim;
    auto c   = std::make_unique<ItemsContainer>(game::vc::items(key, st->size), [key, dim](int, std::vector<ItemStack> const& all) {
        game::vc::setItems(key, all);
        if (auto* r = game::regionOf(dim)) notifyChestChanged(*r, game::blockPos(key));
    });
    auto* raw = c.get();
    live().push_back(std::move(c));
    return raw;
}

class ChestPortsModule {
public:
    void install() {
        ChestHopperHook::hook();
        ChestDropperHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            if (!live().empty()) live().clear();
        });
    }
    void uninstall() {
        ChestHopperHook::unhook();
        ChestDropperHook::unhook();
        ll::event::EventBus::getInstance().removeListener(mTick);
        live().clear();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace lp::feature::variantcontainers

LP_REGISTER_MODULE(lp::feature::variantcontainers::ChestPortsModule);
