// Variant furnaces: the block types carry the vanilla FurnaceBlockActor (BlockPatch.cpp), so the
// game smelts, burns fuel, stores XP, saves the furnace and serves hoppers by itself. This file:
//   - keeps our block when the furnace lights up / goes out (FurnaceBlock::setLit would swap in the
//     vanilla furnace): the `lepton:lit` state is set instead,
//   - opens the vanilla furnace screen (vanilla clients briefly see a vanilla furnace block while
//     the screen is open: their client needs a furnace block actor at the position),
//   - the blackstone furnace's soul state (soul sand / soul soil below),
//   - lit particles and crackle sounds, comparator signal, drops on removal.

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/packet/BlockActorDataPacket.h"
#include "mc/network/packet/ContainerOpenPacket.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/ContainerID.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/managers/models/FurnaceContainerManagerModel.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/FurnaceBlock.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/variantcontainers/VariantContainers.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::feature::variantcontainers {

namespace bs = game::blockstates;

/// Client mod -> server after the handshake: its block registry gives our furnaces the furnace block
/// actor (then the furnace screen needs no stand-in block).
struct FurnaceSupport {
    static constexpr std::string_view kId = "variantcontainers:furnaces";
    bool blockActors = false;
};
LP_REGISTER_MESSAGE(FurnaceSupport);

namespace {

std::set<std::string>& patchedClients() {
    static std::set<std::string> s;
    return s;
}

#ifdef LP_CLIENT
bool registryHasFurnaceActors() {
    auto const* b = bs::of("lepton:deepslate_furnace");
    return b && b->getBlockType().getBlockEntityType() == BlockActorType::Furnace;
}
#endif

uint64_t gTick = 0;

std::mt19937& rng() {
    static std::mt19937 r{0xf0e7aceu};
    return r;
}
double nextDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

FurnaceBlockActor* furnaceAt(BlockSource& region, BlockPos const& pos) {
    auto* actor = region.getBlockEntity(pos);
    if (!actor || actor->mType != BlockActorType::Furnace) return nullptr;
    return static_cast<FurnaceBlockActor*>(actor);
}

bool isSoulBase(Block const& b) {
    auto const& n = b.getTypeName();
    return n == "minecraft:soul_sand" || n == "minecraft:soul_soil";
}

bool standInAt(BlockSource& region, BlockPos const& pos);

/// Changes our furnace's state without the furnace block actor dropping its items. Not while a
/// player looks at the furnace screen through a stand-in block: the block update would replace
/// the stand-in and close that screen (the tick hook applies the state once the screen closed).
void setFurnaceState(BlockSource& region, BlockPos const& pos, bs::Changes const& changes) {
    if (standInAt(region, pos)) return;
    auto const& b    = region.getBlock(pos);
    auto const* next = bs::with(b, changes);
    if (!next || *next == b) return;
    auto* f = furnaceAt(region, pos);
    if (f) f->mNoDrop = true;
    game::setBlock(region, pos, *next);
    if (auto* g = furnaceAt(region, pos)) g->mNoDrop = false;
}

void updateSoul(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (!isVariantFurnace(b) || !bs::getBool(b, "lepton:soul").has_value()) return;
    bool soul = isSoulBase(region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z}));
    if (bs::getBool(b, "lepton:soul") != soul) setFurnaceState(region, pos, {{"lepton:soul", soul}});
}

int frontOf(Block const& b) {
    auto look = bs::getString(b, "minecraft:cardinal_direction");
    return look ? opp(dirIndex(*look)) : 0;
}

/// Java AbstractFurnaceBlock#animateTick (+ SoulFurnaceBlock): smoke and flame at the front.
void animate(BlockSource& region, BlockPos const& pos, Block const& b) {
    auto level = ll::service::getLevel();
    if (!level) return;
    bool soul = bs::getBool(b, "lepton:soul").value_or(false);
    double x = pos.x + 0.5, y = pos.y, z = pos.z + 0.5;
    if (nextDouble() < 0.1) playSoundAt(region, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), "block.furnace.lit", 1.f, 1.f);
    int    f  = frontOf(b);
    auto [sx, sz] = game::facingOffset(f);
    double r  = nextDouble() * 0.6 - 0.3;
    double ox = sx != 0 ? sx * 0.52 : r;
    double oz = sz != 0 ? sz * 0.52 : r;
    double oy = nextDouble() * 6.0 / 16.0;
    Vec3   at{static_cast<float>(x + ox), static_cast<float>(y + oy), static_cast<float>(z + oz)};
    level->spawnParticleEffect("minecraft:basic_smoke_particle", at, &region.getDimension());
    level->spawnParticleEffect(soul ? "minecraft:blue_flame_particle" : "minecraft:basic_flame_particle", at, &region.getDimension());
}

// ---- hooks --------------------------------------------------------------------------------------------

LL_TYPE_STATIC_HOOK(
    FurnaceSetLitHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlock,
    &FurnaceBlock::setLit,
    void,
    bool              lit,
    ::BlockSource&    region,
    ::BlockPos const& pos,
    ::BlockActorType  blockActorType,
    ::Block const&    unlitFurnace,
    ::Block const&    litFurnace
) {
    if (isVariantFurnace(region.getBlock(pos))) {
        setFurnaceState(region, pos, {{"lepton:lit", lit}});
        return;
    }
    origin(lit, region, pos, blockActorType, unlitFurnace, litFurnace);
}

LL_TYPE_INSTANCE_HOOK(FurnaceTickHook, ll::memory::HookPriority::Normal, FurnaceBlockActor, &FurnaceBlockActor::$tick, void, ::BlockSource& region) {
    origin(region);
    if (region.getLevel().isClientSide()) return;
    auto const& b = region.getBlock(mPosition);
    if (!isVariantFurnace(b)) return;
    // keep the visible state in line with the furnace (lit time > 0), e.g. after a reload
    bool burning = mLitTime > 0;
    if (bs::getBool(b, "lepton:lit") != burning) {
        setFurnaceState(region, mPosition, {{"lepton:lit", burning}});
        return;
    }
    if (burning && nextDouble() < 0.08) animate(region, mPosition, b);
}

// ---- vanilla clients: a vanilla furnace block while the screen is open --------------------------------

struct Fake {
    int          dim;
    BlockPos     pos;
    uint64_t     since;
    Block const* shown  = nullptr; ///< our block state the stand-in was made for
    int          resend = 0;       ///< ticks left to send it again after a state change
};
std::unordered_map<int64_t, Fake>& fakes() { ///< player unique id -> faked furnace
    static std::unordered_map<int64_t, Fake> m;
    return m;
}

void sendBlock(Player& player, BlockPos const& pos, Block const& block) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::UpdateBlock);
    if (!pkt) return;
    auto& p        = static_cast<UpdateBlockPacket&>(*pkt);
    p.mPos         = pos;
    p.mLayer       = 0;
    p.mUpdateFlags = 3;
    p.mRuntimeId   = block.mNetworkId;
    player.sendNetworkPacket(p);
}

/// Block entity data of a furnace at `pos`, so the client creates the stand-in's block actor.
void sendStandInData(Player& player, BlockPos const& pos) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::BlockActorData);
    if (!pkt) return;
    auto&       data = static_cast<BlockActorDataPacket&>(*pkt);
    CompoundTag tag;
    tag["id"]        = std::string("Furnace");
    tag["x"]         = pos.x;
    tag["y"]         = pos.y;
    tag["z"]         = pos.z;
    tag["isMovable"] = static_cast<uchar>(1);
    data.mPos        = pos;
    data.mData       = std::move(tag);
    player.sendNetworkPacket(data);
}

Block const* vanillaFurnaceFor(Block const& ours) {
    bool lit  = bs::getBool(ours, "lepton:lit").value_or(false);
    auto look = bs::getString(ours, "minecraft:cardinal_direction").value_or("north");
    return bs::of(lit ? "minecraft:lit_furnace" : "minecraft:furnace", {{"minecraft:cardinal_direction", look}});
}

bool hasFurnaceOpen(Player const& player, BlockPos const& pos) {
    auto const& manager = player.mContainerManager.get();
    if (!manager) return false;
    auto const& owner = manager->mScreenContext->mOwner.get();
    auto const* at    = std::get_if<BlockPos>(&owner);
    return at && *at == pos;
}

void tickFakes() {
    for (auto it = fakes().begin(); it != fakes().end();) {
        ActorUniqueID uid;
        uid.rawID   = it->first;
        auto level  = ll::service::getLevel();
        auto* p     = level ? level->getPlayer(uid) : nullptr;
        bool  open  = p && hasFurnaceOpen(*p, it->second.pos);
        if (open || (p && gTick - it->second.since < 10)) {
            // a state change (lit / soul) sent our block to the client and replaced the stand-in:
            // send the stand-in again (twice: the game's own update may go out after ours)
            if (auto* r = game::regionOf(it->second.dim); p && r) {
                auto const& cur = r->getBlock(it->second.pos);
                if (&cur != it->second.shown) {
                    it->second.shown  = &cur;
                    it->second.resend = 2;
                }
                if (it->second.resend > 0) {
                    --it->second.resend;
                    if (auto const* v = vanillaFurnaceFor(cur)) sendBlock(*p, it->second.pos, *v);
                }
            }
            ++it;
            continue;
        }
        if (p) {
            if (auto* r = game::regionOf(it->second.dim)) sendBlock(*p, it->second.pos, r->getBlock(it->second.pos));
        }
        it = fakes().erase(it);
    }
}

bool standInAt(BlockSource& region, BlockPos const& pos) {
    int dim = static_cast<int>(region.getDimensionId());
    for (auto const& [id, f] : fakes())
        if (f.dim == dim && f.pos == pos) return true;
    return false;
}

std::unordered_map<std::string, uint64_t>& recentOpen() {
    static std::unordered_map<std::string, uint64_t> m;
    return m;
}

struct Pending {
    int64_t  player;
    int      dim;
    BlockPos pos;
    uint64_t due;
    bool     show = false; ///< the stand-in was sent: show the screen now
};
std::vector<std::pair<int, BlockPos>>& soulChecks() {
    static std::vector<std::pair<int, BlockPos>> v;
    return v;
}

std::vector<Pending>& pendingOpens() {
    static std::vector<Pending> v;
    return v;
}

} // namespace

// ---- public -----------------------------------------------------------------------------------------

int furnaceComparatorSignal(BlockSource& region, BlockPos const& pos) {
    auto* f = furnaceAt(region, pos);
    if (!f) return 0;
    auto* c = f->getContainer();
    return c ? c->getRedstoneSignalFromContainer(region) : 0;
}

void onFurnaceRemoved(BlockSource& region, BlockPos const& pos) {
    auto* f = furnaceAt(region, pos);
    if (!f || f->mNoDrop) return;
    auto* c = f->getContainer();
    if (c) {
        Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
        c->dropContents(region, at, true);
        c->removeAllItems();
    }
    int xp = f->withdrawStoredXPReward();
    if (xp > 0) {
        Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
        ExperienceOrb::spawnOrbs(region, at, xp, ExperienceOrb::DropType::FromBlock, nullptr);
    }
    log().info("variantcontainers: furnace at {} {} {} removed ({} xp)", pos.x, pos.y, pos.z, xp);
}

bool showFurnaceScreen(Player& player, BlockPos const& pos);

bool openFurnace(Player& player, BlockPos const& pos) {
    if (player.isClientSide()) return false;
    auto& region = player.getDimensionBlockSource();
    auto const& b = region.getBlock(pos);
    if (!isVariantFurnace(b) || !furnaceAt(region, pos)) return false;
    if (player.mContainerManager.get()) return false;
    auto& sp = static_cast<ServerPlayer&>(player);
    if (!player.isSimulated() && !patchedClients().contains(player.getUuid().asString())) {
        // the client needs a furnace block actor at the position to show the screen: a client
        // without our registry patch (vanilla, or an old client mod) gets a vanilla furnace there
        if (auto const* v = vanillaFurnaceFor(b)) {
            // a stand-in from a furnace this player just left goes back to the real block first
            if (auto old = fakes().find(player.getOrCreateUniqueID().rawID); old != fakes().end() && !(old->second.pos == pos)) {
                if (auto* r = game::regionOf(old->second.dim)) sendBlock(player, old->second.pos, r->getBlock(old->second.pos));
            }
            sendBlock(player, pos, *v);
            // and the furnace's block actor data, so the client has a furnace block actor there
            sendStandInData(player, pos);
            fakes()[player.getOrCreateUniqueID().rawID] = Fake{static_cast<int>(region.getDimensionId()), pos, gTick, &b, 0};
            // the client creates the furnace block actor when the stand-in arrives: open a few ticks later
            pendingOpens().push_back({player.getOrCreateUniqueID().rawID, static_cast<int>(region.getDimensionId()), pos, gTick + 4, true});
            log().info("variantcontainers: {} opens {} at {} {} {} (stand-in)", player.getRealName(), b.getTypeName(), pos.x, pos.y, pos.z);
            return true;
        }
    }
    return showFurnaceScreen(player, pos);
}

bool showFurnaceScreen(Player& player, BlockPos const& pos) {
    auto& region = player.getDimensionBlockSource();
    auto const& b = region.getBlock(pos);
    if (!isVariantFurnace(b) || !furnaceAt(region, pos) || player.mContainerManager.get()) return false;
    auto& sp = static_cast<ServerPlayer&>(player);
    // what the game does for its own furnaces: a furnace container manager on the server, then the
    // open packet that names the block position
    int next = static_cast<int>(sp.mContainerCounter) + 1;
    if (next >= static_cast<int>(ContainerID::Last)) next = static_cast<int>(ContainerID::First);
    sp.mContainerCounter = static_cast<ContainerID>(next);
    auto id    = static_cast<ContainerID>(next);
    auto model = std::make_shared<FurnaceContainerManagerModel>(HashedString("furnace"), SharedTypes::Legacy::ContainerType::Furnace,
        BlockActorType::Furnace, id, player, pos);
    model->postInit();
    sp._setContainerManagerModel(model);
    if (auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::ContainerOpen)) {
        auto& open           = static_cast<ContainerOpenPacket&>(*pkt);
        open.mContainerId    = id;
        open.mType           = SharedTypes::Legacy::ContainerType::Furnace;
        open.mPos            = pos;
        open.mEntityUniqueID = ActorUniqueID{-1};
        player.sendNetworkPacket(open);
    }
    sp.refreshContainer(*model); // the slots go to the client now

    bool ok = hasFurnaceOpen(player, pos);
    log().info("variantcontainers: {} opened {} at {} {} {}{}", player.getRealName(), b.getTypeName(), pos.x, pos.y, pos.z, ok ? "" : " (failed)");
    return ok;
}

class FurnacesModule {
public:
    void install() {
        FurnaceSetLitHook::hook();
        FurnaceTickHook::hook();
        mSupport = net::onServer<FurnaceSupport>([](Player& p, FurnaceSupport const& m) {
            if (m.blockActors) patchedClients().insert(p.getUuid().asString());
            else patchedClients().erase(p.getUuid().asString());
        });
#ifdef LP_CLIENT
        net::onServerModReady([] { net::sendToServer(FurnaceSupport{registryHasFurnaceActors()}); });
#endif
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            ++gTick;
            if (!fakes().empty()) tickFakes();
            if (!soulChecks().empty()) {
                auto list = std::move(soulChecks());
                soulChecks().clear();
                for (auto const& [dim, pos] : list)
                    if (auto* r = game::regionOf(dim)) updateSoul(*r, pos);
            }
            if (pendingOpens().empty()) return;
            auto list = std::move(pendingOpens());
            pendingOpens().clear();
            for (auto& o : list) {
                if (o.due > gTick) {
                    pendingOpens().push_back(o);
                    continue;
                }
                ActorUniqueID uid;
                uid.rawID = o.player;
                auto level = ll::service::getLevel();
                auto* p    = level ? level->getPlayer(uid) : nullptr;
                auto* r    = game::regionOf(o.dim);
                if (!p || !r || static_cast<int>(p->getDimensionId()) != o.dim) continue;
                if (o.show) {
                    // the block actor data again: the first copy may have reached the client before
                    // the stand-in block existed there
                    sendStandInData(*p, o.pos);
                    showFurnaceScreen(*p, o.pos);
                }
                else openFurnace(*p, o.pos);
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide()) return;
            auto& region = player.getDimensionBlockSource();
            if (!isVariantFurnace(region.getBlock(ev.blockPos()))) return;
            if (player.isSneaking() && !ev.item().isNull()) return;
            ev.cancel();
            auto uuid = player.getUuid().asString();
            if (auto it = recentOpen().find(uuid); it != recentOpen().end() && gTick - it->second < 5) return;
            recentOpen()[uuid] = gTick;
            pendingOpens().push_back({player.getOrCreateUniqueID().rawID, static_cast<int>(region.getDimensionId()), ev.blockPos(), gTick + 1});
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            auto pos = ev.pos();
            // a blackstone furnace placed, or the block below one changed (next tick: no world edits
            // from inside a block change)
            int      dim = static_cast<int>(region.getDimensionId());
            BlockPos above{pos.x, pos.y + 1, pos.z};
            if (isVariantFurnace(ev.newBlock()) && !isVariantFurnace(ev.previousBlock())) soulChecks().push_back({dim, pos});
            if (isVariantFurnace(region.getBlock(above))) soulChecks().push_back({dim, above});
        }));
        game::db::onWorldClosed([] {
            fakes().clear();
            recentOpen().clear();
            patchedClients().clear();
            pendingOpens().clear();
            soulChecks().clear();
        });
    }
    void uninstall() {
        FurnaceSetLitHook::unhook();
        FurnaceTickHook::unhook();
        net::removeHandler(mSupport);
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    net::HandlerId                      mSupport = 0;
};

} // namespace lp::feature::variantcontainers

LP_REGISTER_MODULE(lp::feature::variantcontainers::FurnacesModule);
