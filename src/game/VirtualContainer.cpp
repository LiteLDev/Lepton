#include "game/VirtualContainer.h"

#include <map>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::game::vc {

namespace {

using core::GlobalPos;

std::string dbKey(GlobalPos const& pos) { return "lp:vc:" + pos.key(); }

struct Session {
    int64_t                  entity = 0;
    Spec                     spec;
    std::vector<ItemStack>   last;
    std::set<std::string>    viewers; ///< player UUIDs
    std::map<std::string, int> grace; ///< viewer -> ticks before the screen must be seen open
};

std::map<GlobalPos, std::vector<ItemStack>>& cache() {
    static std::map<GlobalPos, std::vector<ItemStack>> m;
    return m;
}
std::set<GlobalPos>& dirty() {
    static std::set<GlobalPos> s;
    return s;
}
std::map<GlobalPos, Session>& sessions() {
    static std::map<GlobalPos, Session> m;
    return m;
}
std::unordered_map<std::string, Spec>& bindings() {
    static std::unordered_map<std::string, Spec> m;
    return m;
}

std::vector<ItemStack>& cached(GlobalPos const& pos) {
    auto it = cache().find(pos);
    if (it != cache().end()) return it->second;
    std::vector<ItemStack> items;
    if (auto j = db::load(dbKey(pos)); j && j->is_object()) {
        int size = j->value("size", 0);
        items.resize(static_cast<size_t>(std::clamp(size, 0, 256)));
        for (auto const& e : j->value("items", nlohmann::json::array())) {
            int slot = e.value("slot", -1);
            if (slot < 0 || slot >= static_cast<int>(items.size())) continue;
            items[static_cast<size_t>(slot)] = itemFromSnbt(e.value("item", std::string{}));
        }
    }
    return cache()[pos] = std::move(items);
}

void writeCache(GlobalPos const& pos, std::vector<ItemStack> items) {
    cache()[pos] = std::move(items);
    dirty().insert(pos);
}

void flushOne(GlobalPos const& pos) {
    auto it = cache().find(pos);
    if (it == cache().end()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < it->second.size(); ++i) {
        if (isEmpty(it->second[i])) continue;
        arr.push_back({
            {"slot", i                            },
            {"item", itemToSnbt(it->second[i])},
        });
    }
    if (arr.empty()) db::erase(dbKey(pos));
    else
        db::save(
            dbKey(pos),
            {
                {"size",  it->second.size()},
                {"items", arr              },
            }
        );
}

ContainerComponent* componentOf(Actor& actor) {
    auto comp = actor.getEntityContext().tryGetComponent<ContainerComponent>();
    return comp ? comp.as_ptr() : nullptr;
}
Container* containerOf(Actor& actor) {
    auto* comp = componentOf(actor);
    return comp ? &comp->mContainer.get() : nullptr;
}

BlockSource* regionOf(int dim) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto d = level->getDimension(dim).lock();
    return d ? &d->getBlockSourceFromMainChunkSource() : nullptr;
}

/// The player has the helper `entity`'s container screen open.
bool hasOpen(Player const& player, int64_t entity) {
    auto const& manager = player.mContainerManager.get();
    if (!manager) return false;
    auto const& owner = manager->mScreenContext->mOwner.get();
    auto const* id    = std::get_if<ActorUniqueID>(&owner);
    return id && id->rawID == entity;
}

Player* playerByUuid(std::string const& uuid) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    return level->getPlayer(mce::UUID::fromString(uuid));
}

void emptyAndRemove(Actor& helper) {
    if (auto* c = containerOf(helper)) c->removeAllItems();
    entities::despawn(helper);
}

/// Ends a session: the items go back to the cache, the helper is removed.
void finish(GlobalPos const& pos) {
    auto it = sessions().find(pos);
    if (it == sessions().end()) return;
    auto session = std::move(it->second);
    sessions().erase(it);
    if (auto* helper = entities::find(session.entity)) {
        if (auto* c = containerOf(*helper)) session.last = inv::snapshot(*c, inv::all(*c));
        emptyAndRemove(*helper);
    }
    writeCache(pos, std::move(session.last));
}

bool sameStack(ItemStack const& a, ItemStack const& b) {
    if (isEmpty(a) || isEmpty(b)) return isEmpty(a) && isEmpty(b);
    return a.mCount == b.mCount && itemToSnbt(a) == itemToSnbt(b);
}

/// Pushes rejected items back to a viewer (or drops them at the container).
void rejectItems(GlobalPos const& pos, Session& s, Container& c) {
    if (!s.spec.accepts) return;
    for (int i = 0; i < c.getContainerSize(); ++i) {
        ItemStack stack = c.getItem(i);
        if (isEmpty(stack) || s.spec.accepts(stack)) continue;
        c.setItem(i, ItemStack{});
        Player* to = s.viewers.empty() ? nullptr : playerByUuid(*s.viewers.begin());
        if (to) {
            inv::insert(inv::playerInventory(*to), stack, inv::kPlayerAll);
            inv::sync(*to);
        }
        if (!isEmpty(stack)) {
            if (auto* region = regionOf(pos.dim)) {
                Vec3 at{pos.x + 0.5f, pos.y + 1.1f, pos.z + 0.5f};
                ll::service::getLevel()->getSpawner().spawnItem(*region, stack, nullptr, at, 0);
            }
        }
    }
}

void tickSessions() {
    std::vector<GlobalPos> ended;
    for (auto& [pos, s] : sessions()) {
        auto* helper = entities::find(s.entity);
        auto* c      = helper ? containerOf(*helper) : nullptr;
        if (!c) {
            ended.push_back(pos); // helper unloaded / killed: keep the last known contents
            continue;
        }
        rejectItems(pos, s, *c);
        auto now     = inv::snapshot(*c, inv::all(*c));
        bool changed = now.size() != s.last.size();
        for (size_t i = 0; !changed && i < now.size(); ++i) changed = !sameStack(now[i], s.last[i]);
        if (changed) {
            s.last = now;
            writeCache(pos, std::move(now));
            if (s.spec.onChanged) {
                try {
                    s.spec.onChanged(pos);
                } catch (std::exception const& e) {
                    log().error("virtual container onChanged failed: {}", e.what());
                }
            }
        }
        for (auto vit = s.viewers.begin(); vit != s.viewers.end();) {
            auto* p    = playerByUuid(*vit);
            auto& left = s.grace[*vit];
            bool  open = p && hasOpen(*p, s.entity);
            if (open) left = 0;
            if (open || (p && left-- > 0)) {
                ++vit;
            } else {
                s.grace.erase(*vit);
                vit = s.viewers.erase(vit);
            }
        }
        if (s.viewers.empty()) ended.push_back(pos);
    }
    for (auto const& pos : ended) finish(pos);
}

/// Empties and removes helpers that no session owns (left over by a crash or a saved chunk).
void sweepStrays() {
    std::set<int64_t> owned;
    for (auto const& [pos, s] : sessions()) owned.insert(s.entity);
    for (auto* a : entities::ofType(kHelperEntity)) {
        if (owned.contains(entities::idOf(*a))) continue;
        emptyAndRemove(*a);
        log().debug("removed a stray virtual container helper");
    }
}

SharedTypes::Legacy::ContainerType typeOf(Screen s) {
    switch (s) {
    case Screen::Hopper:
        return SharedTypes::Legacy::ContainerType::Hopper;
    case Screen::Dispenser:
        return SharedTypes::Legacy::ContainerType::Dispenser;
    default:
        return SharedTypes::Legacy::ContainerType::Container;
    }
}
int sizeOf(Spec const& spec) {
    switch (spec.screen) {
    case Screen::Hopper:
        return 5;
    case Screen::Dispenser:
        return 9;
    default:
        return std::clamp(spec.size, 1, 54);
    }
}

std::map<std::string, int>& recentOpen() { ///< player uuid -> tick of the last bound open
    static std::map<std::string, int> m;
    return m;
}
int gTick = 0;

} // namespace

// ---- contents ------------------------------------------------------------------------------------

std::vector<ItemStack> items(GlobalPos const& pos, int size) {
    if (auto it = sessions().find(pos); it != sessions().end()) {
        if (auto* helper = entities::find(it->second.entity)) {
            if (auto* c = containerOf(*helper)) {
                auto v = inv::snapshot(*c, inv::all(*c));
                v.resize(static_cast<size_t>(std::max(size, 0)));
                return v;
            }
        }
    }
    auto v = cached(pos);
    v.resize(static_cast<size_t>(std::max(size, 0)));
    return v;
}

void setItems(GlobalPos const& pos, std::vector<ItemStack> const& items) {
    if (auto it = sessions().find(pos); it != sessions().end()) {
        if (auto* helper = entities::find(it->second.entity)) {
            if (auto* c = containerOf(*helper)) {
                inv::restore(*c, inv::all(*c), items);
                it->second.last = inv::snapshot(*c, inv::all(*c));
            }
        }
    }
    writeCache(pos, items);
}

bool hasItems(GlobalPos const& pos) {
    for (auto const& s : items(pos, 256))
        if (!isEmpty(s)) return true;
    return false;
}

std::vector<ItemStack> takeAll(GlobalPos const& pos) {
    closeAll(pos);
    auto all = cached(pos);
    std::erase_if(all, [](ItemStack const& s) { return isEmpty(s); });
    writeCache(pos, {});
    return all;
}

void dropContents(BlockSource& region, GlobalPos const& pos) {
    auto level = ll::service::getLevel();
    if (!level) return;
    for (auto& stack : takeAll(pos)) {
        Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
        level->getSpawner().spawnItem(region, stack, nullptr, at, 10);
    }
}

void flush() {
    for (auto const& pos : dirty()) flushOne(pos);
    dirty().clear();
}

// ---- screens -------------------------------------------------------------------------------------

bool open(Player& player, GlobalPos const& pos, Spec const& spec) {
    if (player.isClientSide()) return false;
    auto* region = regionOf(pos.dim);
    if (!region || !region->hasChunksAt(blockPos(pos), 0, false)) return false;
    if (player.mContainerManager.get()) return false; // another screen is open
    auto uuid = player.getUuid().asString();
    if (auto open = openBy(player)) {
        if (*open == pos) return true;
        return false;
    }

    auto it = sessions().find(pos);
    Actor* helper = it != sessions().end() ? entities::find(it->second.entity) : nullptr;
    if (!helper) {
        if (it != sessions().end()) finish(pos);
        Vec3 at{pos.x + 0.5f, pos.y + 0.25f, pos.z + 0.5f};
        helper = entities::spawn(*region, kHelperEntity, at);
        if (!helper) {
            log().error("virtual container: could not spawn {} (behaviour pack missing?)", kHelperEntity);
            return false;
        }
        auto* comp = componentOf(*helper);
        if (!comp) {
            log().error("virtual container: {} has no inventory component", kHelperEntity);
            entities::despawn(*helper);
            return false;
        }
        int size = sizeOf(spec);
        comp->rebuildContainer(*helper, typeOf(spec.screen), size, false, 0, false);
        auto stored = cached(pos);
        stored.resize(static_cast<size_t>(std::max<size_t>(stored.size(), static_cast<size_t>(size))));
        auto& c = comp->mContainer.get();
        for (int i = 0; i < size; ++i) c.setItem(i, stored[static_cast<size_t>(i)]);
        Session s;
        s.entity = entities::idOf(*helper);
        s.spec   = spec;
        s.last   = inv::snapshot(c, inv::all(c));
        // A smaller screen than before (config change): drop what no longer fits.
        bool dropped = false;
        for (size_t i = static_cast<size_t>(size); i < stored.size(); ++i) {
            if (isEmpty(stored[i])) continue;
            Vec3 drop{pos.x + 0.5f, pos.y + 1.1f, pos.z + 0.5f};
            ll::service::getLevel()->getSpawner().spawnItem(*region, stored[i], nullptr, drop, 10);
            dropped = true;
        }
        if (dropped) {
            log().warn("virtual container {} had more slots than its screen; the extra items were dropped", pos.key());
            writeCache(pos, s.last);
        }
        it = sessions().insert_or_assign(pos, std::move(s)).first;
    }
    auto* comp = componentOf(*helper);
    comp->setCustomName(trFor(player, spec.titleKey, spec.titleFallback));
    helper->openContainerComponent(player);
    it->second.viewers.insert(uuid);
    it->second.grace[uuid] = 10;
    log().info("virtual container {} opened by {}", pos.key(), player.getRealName());
    return true;
}

void closeAll(GlobalPos const& pos) {
    auto it = sessions().find(pos);
    if (it == sessions().end()) return;
    for (auto const& uuid : it->second.viewers) {
        if (auto* p = playerByUuid(uuid); p && hasOpen(*p, it->second.entity)) static_cast<ServerPlayer*>(p)->initiateContainerClose();
    }
    finish(pos);
}

int viewers(GlobalPos const& pos) {
    auto it = sessions().find(pos);
    return it == sessions().end() ? 0 : static_cast<int>(it->second.viewers.size());
}

Container* liveContainer(GlobalPos const& pos) {
    auto it = sessions().find(pos);
    if (it == sessions().end()) return nullptr;
    auto* helper = entities::find(it->second.entity);
    return helper ? containerOf(*helper) : nullptr;
}

std::optional<GlobalPos> openBy(Player const& player) {
    auto uuid = player.getUuid().asString();
    for (auto const& [pos, s] : sessions())
        if (s.viewers.contains(uuid)) return pos;
    return std::nullopt;
}

// ---- block binding -------------------------------------------------------------------------------

void bindBlock(std::string const& blockId, Spec spec) { bindings()[blockId] = std::move(spec); }
void unbindBlock(std::string const& blockId) { bindings().erase(blockId); }

class VirtualContainerModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            ++gTick;
            if (!sessions().empty()) tickSessions();
            if (gTick % 100 == 0) sweepStrays();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide()) return;
            if (bindings().empty()) return;
            auto bit = bindings().find(blockIdAt(player.getDimensionBlockSource(), ev.blockPos()));
            if (bit == bindings().end()) return;
            if (player.isSneaking() && !isEmpty(ev.item())) return; // sneak + item: place / use the item
            ev.cancel();
            auto uuid = player.getUuid().asString();
            if (auto last = recentOpen().find(uuid); last != recentOpen().end() && gTick - last->second < 5) return;
            recentOpen()[uuid] = gTick;
            auto pos  = globalPos(player.getDimensionBlockSource(), ev.blockPos());
            auto spec = bit->second;
            // Defer: the cancelled interaction is rolled back after the event.
            ll::coro::keepThis([uuid, pos, spec]() -> ll::coro::CoroTask<> {
                co_await ll::chrono::ticks(1);
                auto* p = playerByUuid(uuid);
                if (!p) co_return;
                if (spec.canOpen && !spec.canOpen(*p, pos)) co_return;
                open(*p, pos, spec);
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            auto const& prev = ev.previousBlock().getTypeName();
            if (!bindings().contains(prev) || prev == ev.newBlock().getTypeName()) return;
            auto pos = globalPos(region, ev.pos());
            int  dim = pos.dim;
            ll::coro::keepThis([pos, dim]() -> ll::coro::CoroTask<> {
                co_await ll::chrono::ticks(1);
                if (auto* r = regionOf(dim)) dropContents(*r, pos);
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }));
        db::onWorldSave([] { flush(); });
        db::onWorldClosed([] {
            sessions().clear();
            cache().clear();
            dirty().clear();
            recentOpen().clear();
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        std::vector<GlobalPos> open;
        for (auto const& [pos, s] : sessions()) open.push_back(pos);
        for (auto const& pos : open) closeAll(pos);
        flush();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::game::vc

LP_REGISTER_MODULE_ORDERED(lp::game::vc::VirtualContainerModule, lp::ModuleOrder::Services);
