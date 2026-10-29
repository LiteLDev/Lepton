#include "game/VirtualContainer.h"

#include <cmath>
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
#include "mc/world/containers/managers/models/ChestContainerManagerModel.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/BlockActorDataPacket.h"
#include "mc/network/packet/ContainerOpenPacket.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/ContainerID.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"
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
    int                      readyTick = 0; ///< first tick the helper is known to clients
    std::set<std::string>    pending;       ///< viewers whose screen opens in a few ticks
    std::map<std::string, int> requested;   ///< viewer -> tick its screen was last requested
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

/// Sends `player` the block at `at` as it is in the world (ends a fake dispenser, see showDispenser).
void resendBlock(Player& player, GlobalPos const& at) {
    auto* region = regionOf(at.dim);
    if (!region) return;
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::UpdateBlock);
    if (!pkt) return;
    auto& upd        = static_cast<UpdateBlockPacket&>(*pkt);
    upd.mPos         = blockPos(at);
    upd.mLayer       = 0;
    upd.mUpdateFlags = 3;
    upd.mRuntimeId   = region->getBlock(blockPos(at)).mNetworkId;
    upd.sendTo(player);
}

bool                  fakeBlockScreen(Spec const& spec);
std::vector<BlockPos> fakeBlockPositions(Spec const& spec, GlobalPos const& pos);

/// A viewer left a session: undo what only that client was shown.
void viewerLeft(Spec const& spec, GlobalPos const& pos, std::string const& uuid) {
    if (!fakeBlockScreen(spec)) return;
    auto* p = playerByUuid(uuid);
    if (!p) return;
    for (auto const& b : fakeBlockPositions(spec, pos)) resendBlock(*p, GlobalPos{pos.dim, b.x, b.y, b.z});
}

/// The player is not looking at the screen of the session it views (it used a block or asked for
/// another container: a client with a screen open does neither); the client dropped the screen
/// or never showed it. Close it on the server too and drop the viewer, so the session can end.
void dropStaleViewer(Player& player);

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
    for (auto const& v : session.viewers) viewerLeft(session.spec, pos, v);
    if (auto* helper = entities::find(session.entity)) {
        if (auto* c = containerOf(*helper)) session.last = inv::snapshot(*c, inv::all(*c));
        emptyAndRemove(*helper);
    }
    writeCache(pos, std::move(session.last));
}

bool sameStack(ItemStack const& a, ItemStack const& b) {
    if (isEmpty(a) || isEmpty(b)) return isEmpty(a) && isEmpty(b);
    if (a.mCount != b.mCount || a.getId() != b.getId() || a.getAuxValue() != b.getAuxValue()) return false;
    if (a.isStackable(b)) return true; // same item, data and user data
    return a.getMaxStackSize() == 1 && itemToSnbt(a) == itemToSnbt(b); // unstackables (tools, ...)
}

/// Pushes rejected items back to a viewer (or drops them at the container).
void rejectItems(GlobalPos const& pos, Session& s, Container& c) {
    if (!s.spec.accepts) return;
    for (int i = 0; i < c.getContainerSize(); ++i) {
        ItemStack stack = c.getItem(i);
        if (isEmpty(stack) || s.spec.accepts(stack)) continue;
        c.setItem(i, ItemStack{});
        // Who put it in is unknown with several viewers: then it drops at the container.
        Player* to = s.viewers.size() == 1 ? playerByUuid(*s.viewers.begin()) : nullptr;
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
                viewerLeft(s.spec, pos, *vit);
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
    case Screen::Dispenser: // the helper's own container: see showDispenser
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

/// Ticks between spawning the helper and opening its screen. The container-open packet names the
/// helper entity, so the client must have received the entity first (it is sent with the next
/// entity sync); a client that does not know the entity ignores the packet and shows nothing.
constexpr int kSpawnToOpenTicks = 3;

/// Ticks for a changed name tag (the screen title) to reach the clients before the screen opens.
constexpr int kNameSyncTicks = 2;

/// Screens the client shows only for a block entity: a dispenser (3x3) and a double chest (54).
bool fakeBlockScreen(Spec const& spec) {
    return spec.screen == Screen::Dispenser || (spec.screen == Screen::Chest && sizeOf(spec) > 27);
}

/// Positions of the client-only blocks of `fakeBlockScreen` (the second half of a double chest
/// stands east of the container).
std::vector<BlockPos> fakeBlockPositions(Spec const& spec, GlobalPos const& pos) {
    auto at = blockPos(pos);
    if (spec.screen == Screen::Dispenser) return {at};
    return {at, BlockPos{at.x + 1, at.y, at.z}};
}

void sendBlock(Player& player, BlockPos const& at, Block const& block, CompoundTag tag) {
    if (auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::UpdateBlock)) {
        auto& upd        = static_cast<UpdateBlockPacket&>(*pkt);
        upd.mPos         = at;
        upd.mLayer       = 0;
        upd.mUpdateFlags = 3;
        upd.mRuntimeId   = block.mNetworkId;
        upd.sendTo(player);
    }
    if (auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::BlockActorData)) {
        auto& data = static_cast<BlockActorDataPacket&>(*pkt);
        data.mPos  = at;
        data.mData = std::move(tag);
        data.sendTo(player);
    }
}

/// The game opens an entity's container only as a chest (27) or hopper screen: its dispenser and
/// double chest screens need block entities, on the server and on the client. Those screens use
/// the chest model of the helper on the server (same server-authoritative `LevelEntity` slots,
/// any size), and on the client a dispenser / a pair of chests at the container's position,
/// shown to this player only (block + block entity data with the title) and replaced by the real
/// blocks when the viewer leaves.
void showFakeBlockScreen(ServerPlayer& player, Actor& helper, Spec const& spec, GlobalPos const& pos, std::string const& title) {
    bool dispenser = spec.screen == Screen::Dispenser;
    auto block     = Block::tryGetFromRegistry(HashedString(dispenser ? "minecraft:dispenser" : "minecraft:chest"));
    if (!block) return;
    auto at = fakeBlockPositions(spec, pos);
    for (size_t i = 0; i < at.size(); ++i) {
        CompoundTag tag;
        tag["id"]         = std::string(dispenser ? "Dispenser" : "Chest");
        tag["x"]          = at[i].x;
        tag["y"]          = at[i].y;
        tag["z"]          = at[i].z;
        tag["isMovable"]  = static_cast<uchar>(1);
        tag["CustomName"] = title;
        if (!dispenser) {
            auto const& other = at[1 - i];
            tag["pairx"]    = other.x;
            tag["pairz"]    = other.z;
            tag["pairlead"] = static_cast<uchar>(i == 0 ? 1 : 0);
        }
        sendBlock(player, at[i], *block, std::move(tag));
    }
    ContainerID& counter = player.mContainerCounter;
    auto         next    = static_cast<int>(counter) + 1;
    if (next >= static_cast<int>(ContainerID::Last)) next = static_cast<int>(ContainerID::First);
    counter    = static_cast<ContainerID>(next);
    auto type  = dispenser ? SharedTypes::Legacy::ContainerType::Dispenser : SharedTypes::Legacy::ContainerType::Container;
    auto model = std::make_shared<ChestContainerManagerModel>(counter, player, helper.getOrCreateUniqueID());
    model->postInit();
    model->setContainerType(type);
    player._setContainerManagerModel(model);
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerOpen);
    if (!pkt) return;
    auto& open           = static_cast<ContainerOpenPacket&>(*pkt);
    open.mContainerId    = counter;
    open.mType           = type;
    open.mPos            = at[0];
    open.mEntityUniqueID = ActorUniqueID{-1}; // a block screen for the client
    open.sendTo(player);
}

/// Sends the open screen's contents (the client shows an empty window otherwise: the contents of
/// an entity container only reach it with changes).
void sendContents(Player& player, int64_t entity) {
    if (!hasOpen(player, entity)) return;
    if (auto const& manager = player.mContainerManager.get()) static_cast<ServerPlayer&>(player).refreshContainer(*manager);
}

/// Opens the helper's screen for the player now (the helper and its name are known to the client).
void showScreen(Player& player, Actor& helper, Spec const& spec, GlobalPos const& pos) {
    auto* comp = componentOf(helper);
    if (!comp) return;
    auto title = trFor(player, spec.titleKey, spec.titleFallback);
    comp->setCustomName(title);
    if (fakeBlockScreen(spec)) showFakeBlockScreen(static_cast<ServerPlayer&>(player), helper, spec, pos, title);
    else helper.openContainerComponent(player);
    log().info("virtual container {} opened by {}", pos.key(), player.getRealName());
    // Contents now and again once the client surely shows the screen.
    int64_t entity = entities::idOf(helper);
    sendContents(player, entity);
    auto uuid = player.getUuid().asString();
    ll::coro::keepThis([uuid, entity]() -> ll::coro::CoroTask<> {
        for (int wait : {4, 16}) {
            co_await ll::chrono::ticks(wait);
            if (auto* p = playerByUuid(uuid)) sendContents(*p, entity);
        }
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}
/// Opens the screen once the client can show it: the helper spawned at least kSpawnToOpenTicks ago
/// and its name tag carries the title. Clients title an entity's container screen with the
/// entity's name tag (synced actor data, shared by all viewers: the last opener's language wins).
void requestShow(Player& player, Session& session, Actor& helper, Spec const& spec, GlobalPos const& pos) {
    auto uuid = player.getUuid().asString();
    // Already on its way. (A screen the server thinks open is sent again: the client may have
    // dropped it, and the server cannot tell.)
    if (session.pending.contains(uuid)) return;
    auto title = trFor(player, spec.titleKey, spec.titleFallback);
    int  wait  = std::max(0, session.readyTick - gTick);
    // The title changes only while nobody looks at the screen (it would change for them too).
    bool watched = false;
    for (auto const& v : session.viewers)
        if (auto* p = playerByUuid(v); p && v != uuid && hasOpen(*p, session.entity)) watched = true;
    if (!watched && helper.getNameTag() != title) {
        helper.setNameTag(title);
        wait = std::max(wait, kNameSyncTicks);
    }
    session.grace[uuid]     = wait + 10;
    session.requested[uuid] = gTick;
    if (wait == 0) {
        showScreen(player, helper, spec, pos);
        return;
    }
    session.pending.insert(uuid);
    int64_t entity = session.entity;
    ll::coro::keepThis([uuid, pos, spec, entity, wait]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(wait);
        auto it = sessions().find(pos);
        if (it == sessions().end() || it->second.entity != entity) co_return;
        it->second.pending.erase(uuid);
        if (!it->second.viewers.contains(uuid)) co_return;
        auto* p      = playerByUuid(uuid);
        auto* helper = entities::find(entity);
        if (!p || !helper || (p->mContainerManager.get() && !hasOpen(*p, entity))) {
            it->second.viewers.erase(uuid); // left, or another screen opened meanwhile
            it->second.grace.erase(uuid);
            co_return;
        }
        showScreen(*p, *helper, spec, pos);
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

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
    auto uuid = player.getUuid().asString();
    if (auto open = openBy(player)) {
        if (*open != pos) {
            dropStaleViewer(player);
            return lp::game::vc::open(player, pos, spec);
        }
        // Asked again: the client may not have shown the screen (it rejects a container opened
        // while another of its screens, e.g. the chat, is still closing). Send it again, unless
        // another container screen took its place meanwhile.
        auto it = sessions().find(pos);
        if (player.mContainerManager.get() && !hasOpen(player, it->second.entity)) return false;
        if (auto* helper = entities::find(it->second.entity)) requestShow(player, it->second, *helper, spec, pos);
        return true;
    }
    if (player.mContainerManager.get()) return false; // another screen is open

    auto it = sessions().find(pos);
    Actor* helper = it != sessions().end() ? entities::find(it->second.entity) : nullptr;
    if (!helper) {
        if (it != sessions().end()) finish(pos);
        // A container entity inside a solid block (a full-cube container block such as a crate)
        // loses its contents once its screen is open: the helper stands in the block only when the
        // block is not solid, else on top of it, else at the viewer's feet.
        Vec3 at{pos.x + 0.5f, pos.y + 0.25f, pos.z + 0.5f};
        auto solidAt = [&](Vec3 const& v) {
            BlockPos    b{static_cast<int>(std::floor(v.x)), static_cast<int>(std::floor(v.y)), static_cast<int>(std::floor(v.z))};
            auto const& blk = region->getBlock(b);
            AABB        box = blk.getBlockType().getCollisionShape(blk, *region, b, nullptr);
            return box.max.y > box.min.y && box.min.x <= v.x && v.x <= box.max.x && box.min.y <= v.y && v.y <= box.max.y
                && box.min.z <= v.z && v.z <= box.max.z;
        };
        if (solidAt(at)) {
            Vec3 above{pos.x + 0.5f, pos.y + 1.05f, pos.z + 0.5f};
            if (!solidAt(above)) at = above;
            else if (static_cast<int>(player.getDimensionId()) == pos.dim) at = player.getFeetPos();
        }
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
        s.entity    = entities::idOf(*helper);
        s.spec      = spec;
        s.readyTick = gTick + kSpawnToOpenTicks;
        s.last      = inv::snapshot(c, inv::all(c));
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
    // The player is a viewer from now on, also while the screen waits for the client.
    it->second.viewers.insert(uuid);
    requestShow(player, it->second, *helper, spec, pos);
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

namespace {
void dropStaleViewer(Player& player) {
    auto at = openBy(player);
    if (!at) return;
    auto it   = sessions().find(*at);
    auto uuid = player.getUuid().asString();
    // Its screen is still on its way, or just sent (a click sends several use packets).
    if (it->second.pending.contains(uuid)) return;
    if (auto r = it->second.requested.find(uuid); r != it->second.requested.end() && gTick - r->second < 20) return;
    if (hasOpen(player, it->second.entity)) static_cast<ServerPlayer&>(player).initiateContainerClose();
    it->second.grace.erase(uuid);
    it->second.viewers.erase(uuid);
    viewerLeft(it->second.spec, *at, uuid);
    log().info("virtual container {}: {} no longer views it (screen dropped by the client)", at->key(), player.getRealName());
}
} // namespace

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
            dropStaleViewer(player);
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
                auto* r = regionOf(pos.dim);
                if (!r || !bindings().contains(blockIdAt(*r, blockPos(pos)))) co_return; // broken meanwhile
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
