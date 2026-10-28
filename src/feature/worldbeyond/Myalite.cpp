#include "feature/worldbeyond/Myalite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/LevelEventPacket.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/GlobalPos.h"
#include "feature/worldbeyond/Blocks.h"
#include "feature/worldbeyond/Chorus.h"
#include "feature/worldbeyond/Config.h"
#include "feature/worldbeyond/JavaMath.h"
#include "feature/worldbeyond/MyaliteColor.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace lp::feature::worldbeyond {

namespace {

/// Java's Direction.values() order: DOWN, UP, NORTH, SOUTH, WEST, EAST.
constexpr std::array<std::array<int, 3>, 6> kDirs{
    {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}
};

BlockPos off(BlockPos const& p, std::array<int, 3> const& d) { return BlockPos{p.x + d[0], p.y + d[1], p.z + d[2]}; }

bool isCrystalAt(BlockSource& region, BlockPos const& p) {
    if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight() || !region.hasChunksAt(p, 0, false)) return false;
    return blocks().isCrystal(region.getBlock(p));
}

bool isAirAt(BlockSource& region, BlockPos const& p) {
    if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight()) return true;
    if (!region.hasChunksAt(p, 0, false)) return false;
    return region.getBlock(p).isAir();
}

/// Quark's getAdjacentMyalite: unvisited crystals around `pos`; nullopt when the walk ran into a
/// loop (no new crystal but two or more visited ones).
std::optional<std::vector<BlockPos>> adjacentCrystals(BlockSource& region, BlockPos const& pos, std::vector<BlockPos> const* found) {
    std::vector<BlockPos> ret;
    int                   collisions = 0;
    for (auto const& d : kDirs) {
        BlockPos o = off(pos, d);
        if (!isCrystalAt(region, o)) continue;
        if (found && std::find(found->begin(), found->end(), o) != found->end()) ++collisions;
        else ret.push_back(o);
    }
    if (ret.empty() && collisions > 1) return std::nullopt;
    return ret;
}

void crystalParticles(BlockSource& region, BlockPos const& p) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::LevelEvent);
    if (!packet) return;
    auto& pkt    = static_cast<LevelEventPacket&>(*packet);
    pkt.mEventId = static_cast<int>(SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock);
    pkt.mPos     = Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f};
    pkt.mData    = static_cast<int>(region.getBlock(p).mNetworkId);
    region.getDimension().sendPacketForPosition(p, pkt, nullptr);
}

// ---- teleport hooks ------------------------------------------------------------------------------

thread_local bool gInTeleport = false;

std::optional<Vec3> redirect(Actor& actor, Vec3 const& pos) {
    if (gInTeleport || actor.isClientSide()) return std::nullopt;
    auto cfg = config();
    if (!cfg.enableSpiralSpires || !cfg.enableMyaliteViaducts) return std::nullopt;
    if (!blocks().ready()) return std::nullopt;
    // Player positions are at eye height; Quark works with feet positions.
    float    eye = actor.getPosition().y - actor.getFeetPos().y;
    BlockPos target{
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y - eye + 0.001f)),
        static_cast<int>(std::floor(pos.z))
    };
    auto& region = actor.getDimensionBlockSource();
    auto  feet   = viaductTarget(region, target, actor);
    if (!feet) return std::nullopt;
    return Vec3{feet->x, feet->y + eye, feet->z};
}

#define LP_WB_TELEPORT_HOOK(NAME, TYPE)                                                                                \
    LL_TYPE_INSTANCE_HOOK(                                                                                             \
        NAME,                                                                                                          \
        ll::memory::HookPriority::Normal,                                                                              \
        TYPE,                                                                                                          \
        &TYPE::$teleportTo,                                                                                            \
        void,                                                                                                          \
        Vec3 const& pos,                                                                                               \
        bool        shouldStopRiding,                                                                                  \
        int         cause,                                                                                             \
        int         sourceEntityType,                                                                                  \
        bool        keepVelocity                                                                                       \
    ) {                                                                                                                \
        if (gInTeleport) return origin(pos, shouldStopRiding, cause, sourceEntityType, keepVelocity);                  \
        auto np     = redirect(*this, pos);                                                                            \
        gInTeleport = true;                                                                                            \
        origin(np ? *np : pos, shouldStopRiding, cause, sourceEntityType, keepVelocity);                               \
        gInTeleport = false;                                                                                           \
    }

LP_WB_TELEPORT_HOOK(ActorTeleportHook, Actor)
LP_WB_TELEPORT_HOOK(MobTeleportHook, Mob)
LP_WB_TELEPORT_HOOK(PlayerTeleportHook, Player)
LP_WB_TELEPORT_HOOK(SimulatedPlayerTeleportHook, SimulatedPlayer)

#undef LP_WB_TELEPORT_HOOK

// ---- crystal colours and renewable myalite ------------------------------------------------------

struct Pending {
    std::mutex                 mutex;
    std::set<core::GlobalPos>  hue;       ///< crystals whose colour state may be wrong
    std::set<core::GlobalPos>  corundum;  ///< corundum that appeared (growth candidates)
    std::set<core::GlobalPos>  placedNow; ///< blocks placed by players this tick
};
Pending& pending() {
    static Pending p;
    return p;
}

std::vector<ll::event::ListenerPtr> gListeners;

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    auto const& b  = blocks();
    auto const& nb = ev.newBlock();
    if (b.isCrystal(nb)) {
        auto const& p = ev.pos();
        int         want = myaliteHueStep(p.x, p.y, p.z);
        if (game::blockstates::getInt(nb, kHueState).value_or(-1) != want) {
            std::lock_guard lock(pending().mutex);
            pending().hue.insert(game::globalPos(region, p));
        }
    } else if (isGrowingCorundum(nb.getTypeName())) {
        std::lock_guard lock(pending().mutex);
        pending().corundum.insert(game::globalPos(region, ev.pos()));
    }
}

void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    std::lock_guard lock(pending().mutex);
    pending().placedNow.insert(game::globalPos(player.getDimensionBlockSource(), ev.pos()));
}

void onTick(ll::event::ServerLevelTickEvent&) {
    std::set<core::GlobalPos> hue, corundum, placed;
    {
        std::lock_guard lock(pending().mutex);
        hue.swap(pending().hue);
        corundum.swap(pending().corundum);
        placed.swap(pending().placedNow);
    }
    for (auto const& g : hue) {
        if (auto* region = game::regionOf(g.dim)) fixCrystalHue(*region, game::blockPos(g));
    }
    auto cfg = config();
    if (!cfg.enableSpiralSpires || !cfg.renewableMyalite) return;
    auto const& b = blocks();
    for (auto const& g : corundum) {
        if (placed.contains(g)) continue; // a player stacked it: not growth
        auto* region = game::regionOf(g.dim);
        if (!region) continue;
        BlockPos up = game::blockPos(g);
        BlockPos mid{up.x, up.y - 1, up.z};
        BlockPos down{up.x, up.y - 2, up.z};
        if (!region->hasChunksAt(down, 0, false)) continue;
        auto const& top = region->getBlock(up);
        if (!isGrowingCorundum(top.getTypeName())) continue;
        if (&region->getBlock(mid).getBlockType() != &top.getBlockType()) continue;
        if (!b.isCrystal(region->getBlock(down))) continue;
        // Quark's CorundumBlock#randomTick with renewableMyalite: the growing block turns into crystal
        if (auto const* c = crystalFor(mid)) {
            game::setBlock(*region, mid, *c);
            log().info("worldbeyond: myalite crystal grew at {} {} {}", mid.x, mid.y, mid.z);
        }
    }
}

} // namespace

std::optional<Vec3> viaductTarget(BlockSource& region, BlockPos target, Actor const& entity) {
    auto cfg = config();
    if (!region.hasChunksAt(target, 0, false)) return std::nullopt; // Quark: !world.isLoaded(pos)
    BlockPos pos     = target;
    auto     myalite = adjacentCrystals(region, pos, nullptr);
    if (!myalite || myalite->empty()) {
        pos.y -= 1;
        myalite = adjacentCrystals(region, pos, nullptr);
    }
    if (!myalite || myalite->empty()) {
        auto [dx, dz] = game::facingOffset(game::playerFacing(entity));
        pos           = BlockPos{pos.x + dx, pos.y, pos.z + dz};
        myalite       = adjacentCrystals(region, pos, nullptr);
    }
    if (!myalite || myalite->empty()) return std::nullopt;

    auto&                 rand = serverRandom();
    int                   maxMoves = std::clamp(cfg.myaliteConduitDistance, 2, 1024);
    BlockPos              prev, cond = pos;
    std::vector<BlockPos> found;
    int                   moves = 0;
    do {
        prev = cond;
        cond = (*myalite)[rand.nextInt(static_cast<int>(myalite->size()))];
        found.push_back(cond);
        myalite = adjacentCrystals(region, cond, &found);
        ++moves;
        if (!myalite || moves > maxMoves) return std::nullopt;
    } while (!myalite->empty());

    BlockPos test{cond.x + (cond.x - prev.x), cond.y + (cond.y - prev.y), cond.z + (cond.z - prev.z)};
    if (!isAirAt(region, test)) {
        bool ok = false;
        for (auto const& d : kDirs) {
            test = off(cond, d);
            if (isAirAt(region, test)) {
                if (d[1] != 0) test = off(test, d);
                ok = true;
                break;
            }
        }
        if (!ok) return std::nullopt;
    }
    for (auto const& f : found) crystalParticles(region, f);
    log().info(
        "worldbeyond: viaduct of {} crystals took {} to {} {} {}",
        found.size(),
        entity.getTypeName(),
        test.x,
        test.y,
        test.z
    );
    return Vec3{static_cast<float>(test.x) + 0.5f, static_cast<float>(test.y) + 0.5f, static_cast<float>(test.z) + 0.5f};
}

bool fixCrystalHue(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) return false;
    auto const& now = region.getBlock(pos);
    if (!blocks().isCrystal(now)) return false;
    auto const* want = crystalFor(pos);
    if (!want || want == &now) return false;
    return game::setBlock(region, pos, *want);
}

bool isGrowingCorundum(std::string_view id) {
    static constexpr std::array<std::string_view, 9> kColours{"red", "orange", "yellow", "green", "blue", "indigo", "violet", "white", "black"};
    constexpr std::string_view                       kPrefix = "lepton:";
    constexpr std::string_view                       kSuffix = "_corundum";
    if (!id.starts_with(kPrefix) || !id.ends_with(kSuffix)) return false;
    auto colour = id.substr(kPrefix.size(), id.size() - kPrefix.size() - kSuffix.size());
    return std::find(kColours.begin(), kColours.end(), colour) != kColours.end();
}

void installMyalite() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced));
    gListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
    ActorTeleportHook::hook();
    MobTeleportHook::hook();
    PlayerTeleportHook::hook();
    SimulatedPlayerTeleportHook::hook();
}

void uninstallMyalite() {
    SimulatedPlayerTeleportHook::unhook();
    PlayerTeleportHook::unhook();
    MobTeleportHook::unhook();
    ActorTeleportHook::unhook();
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
    std::lock_guard lock(pending().mutex);
    pending().hue.clear();
    pending().corundum.clear();
    pending().placedNow.clear();
}

} // namespace lp::feature::worldbeyond
