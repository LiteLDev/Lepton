#include "feature/worldbeyond/Chorus.h"

#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <random>
#include <set>
#include <tuple>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/BlockTickingQueue.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "core/GlobalPos.h"
#include "feature/worldbeyond/Blocks.h"
#include "feature/worldbeyond/Config.h"
#include "feature/worldbeyond/JavaMath.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"

namespace lp::feature::worldbeyond {

namespace {

constexpr int kMaxTickDelay = 2730; ///< uniform 1..2730: mean 1365.5 ticks = 4096 / 3 (Java random ticks)

/// Java's Direction.values() order: DOWN, UP, NORTH, SOUTH, WEST, EAST.
constexpr int kDirs[6][3] = {
    {0,  -1, 0 },
    {0,  1,  0 },
    {0,  0,  -1},
    {0,  0,  1 },
    {-1, 0,  0 },
    {1,  0,  0 }
};

JavaRandom& threadRandom() {
    thread_local JavaRandom r(static_cast<int64_t>(std::random_device{}()) << 16 ^ std::random_device{}());
    return r;
}

Vec3 center(BlockPos const& p, float dy = 0.5f) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + dy, static_cast<float>(p.z) + 0.5f};
}

void particles(BlockSource& region, BlockPos const& pos, char const* effect, int count) {
    auto& level = region.getLevel();
    auto& dim   = region.getDimension();
    auto& rand  = serverRandom();
    for (int i = 0; i < count; ++i) {
        Vec3 at = center(pos, -0.25f + 0.5f);
        at.x += static_cast<float>(rand.nextDouble() - 0.5) * 0.5f;
        at.y += static_cast<float>(rand.nextDouble() - 0.5) * 0.5f;
        at.z += static_cast<float>(rand.nextDouble() - 0.5) * 0.5f;
        level.spawnParticleEffect(effect, at, &dim);
    }
}

void teleportSound(BlockSource& region, BlockPos const& pos, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt  = static_cast<PlaySoundPacket&>(*packet);
    pkt.mName  = "mob.endermen.portal";
    pkt.mPos   = BlockPos{(pos.x * 8) + 4, (pos.y * 8) + 4, (pos.z * 8) + 4}; // 1/8 block units
    pkt.mVolume = 0.1f;
    pkt.mPitch  = pitch;
    region.getDimension().sendPacketForPosition(pos, pkt, nullptr);
}

// ---- deferred work (server thread) ------------------------------------------------------------

struct Pending {
    std::mutex                      mutex;
    std::set<core::GlobalPos>       schedule;  ///< chorus placed: make sure it ticks
    std::set<core::GlobalPos>       water;     ///< water appeared next to chorus
    std::vector<std::tuple<int64_t, core::GlobalPos>> bonemeal; ///< (player id, pos)
};
Pending& pending() {
    static Pending p;
    return p;
}

std::vector<ll::event::ListenerPtr> gListeners;
std::atomic<std::thread::id>        gServerThread{};

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    auto const& b = blocks();
    if (!b.ready()) return;
    auto const& nb = ev.newBlock();
    if (b.isChorus(nb)) {
        std::lock_guard lock(pending().mutex);
        pending().schedule.insert(game::globalPos(region, ev.pos()));
    } else if (b.isWater(nb)) {
        std::lock_guard lock(pending().mutex);
        pending().water.insert(game::globalPos(region, ev.pos()));
    }
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    if (game::itemId(ev.item()) != "minecraft:bone_meal") return;
    auto&       region = player.getDimensionBlockSource();
    auto const& b      = blocks();
    if (!b.ready() || !b.isChorus(region.getBlock(ev.blockPos()))) return;
    std::lock_guard lock(pending().mutex);
    auto            key = std::make_tuple(game::entities::idOf(player), game::globalPos(region, ev.blockPos()));
    for (auto const& e : pending().bonemeal)
        if (e == key) return; // the client may send the use twice in one tick
    pending().bonemeal.push_back(key);
}

void consumeBoneMeal(Player& player) {
    if (player.getPlayerGameType() == GameType::Creative) return;
    auto& inv  = game::inv::playerInventory(player);
    int   slot = player.getSelectedItemSlot();
    auto  item = inv.getItem(slot);
    if (game::itemId(item) != "minecraft:bone_meal") return;
    item.remove(1);
    inv.setItem(slot, item);
    game::inv::sync(player);
}

void scanEntities(Level& level) {
    auto const& b = blocks();
    for (auto* a : level.getRuntimeActorList()) {
        if (!a || a->isClientSide() || !a->isAlive() || !a->hasCategory(ActorCategory::Mob)) continue;
        if (a->isType(ActorType::EnderMan) || a->isType(ActorType::Endermite)) continue;
        auto const& box = a->getAABB();
        int         x0 = static_cast<int>(std::floor(box.min.x + 1e-5f)), x1 = static_cast<int>(std::floor(box.max.x - 1e-5f));
        int         y0 = static_cast<int>(std::floor(box.min.y + 1e-5f)), y1 = static_cast<int>(std::floor(box.max.y - 1e-5f));
        int         z0 = static_cast<int>(std::floor(box.min.z + 1e-5f)), z1 = static_cast<int>(std::floor(box.max.z - 1e-5f));
        if ((x1 - x0) > 4 || (y1 - y0) > 4 || (z1 - z0) > 4) continue; // huge mobs: skip (never stand in plants)
        auto& region = a->getDimensionBlockSource();
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) {
                    BlockPos p{x, y, z};
                    if (!region.hasChunksAt(p, 0, false)) continue;
                    auto const& blk = region.getBlock(p);
                    if (&blk.getBlockType() == b.weedsType) chorusEntityInside(region, p, *a);
                }
    }
}

/// Client-side `animateTick` of Quark (a portal particle per plant now and then), spawned by the
/// server around players since custom blocks have no ambient particles.
void ambientParticles(Level& level) {
    auto const& b    = blocks();
    auto&       rand = serverRandom();
    level.forEachPlayer([&](Player& p) {
        auto& region = p.getDimensionBlockSource();
        auto  feet   = p.getFeetPos();
        int   px = static_cast<int>(std::floor(feet.x)), py = static_cast<int>(std::floor(feet.y)), pz = static_cast<int>(std::floor(feet.z));
        for (int i = 0; i < 200; ++i) {
            BlockPos q{px + rand.nextInt(16) - rand.nextInt(16), py + rand.nextInt(16) - rand.nextInt(16), pz + rand.nextInt(16) - rand.nextInt(16)};
            if (q.y < region.getMinHeight() || q.y >= region.getMaxHeight() || !region.hasChunksAt(q, 0, false)) continue;
            if (!b.isChorus(region.getBlock(q))) continue;
            Vec3 at{
                static_cast<float>(q.x) + 0.2f + static_cast<float>(rand.nextDouble()) * 0.6f,
                static_cast<float>(q.y) + 0.3f,
                static_cast<float>(q.z) + 0.2f + static_cast<float>(rand.nextDouble()) * 0.6f
            };
            level.spawnParticleEffect("minecraft:portal_directional", at, &region.getDimension());
        }
        return true;
    });
}

void onTick(ll::event::ServerLevelTickEvent& ev) {
    gServerThread.store(std::this_thread::get_id());
    auto& level = ev.level();
    auto  cfg   = config();
    if (!blocks().ready()) return;
    std::set<core::GlobalPos>                         schedule, water;
    std::vector<std::tuple<int64_t, core::GlobalPos>> bonemeal;
    {
        std::lock_guard lock(pending().mutex);
        schedule.swap(pending().schedule);
        water.swap(pending().water);
        bonemeal.swap(pending().bonemeal);
    }
    auto const& b = blocks();
    for (auto const& g : schedule) {
        auto* region = game::regionOf(g.dim);
        auto  pos    = game::blockPos(g);
        if (!region || !region->hasChunksAt(pos, 0, false)) continue;
        auto const& blk = region->getBlock(pos);
        if (b.isChorus(blk)) scheduleChorusTick(*region, pos, blk);
    }
    if (cfg.enableChorusVegetation) {
        for (auto const& g : water) {
            auto* region = game::regionOf(g.dim);
            if (!region) continue;
            auto w = game::blockPos(g);
            for (auto const& d : kDirs) {
                BlockPos p{w.x + d[0], w.y + d[1], w.z + d[2]};
                if (region->hasChunksAt(p, 0, false) && b.isChorus(region->getBlock(p))) chorusRunAwayFromWater(*region, p);
            }
        }
        for (auto const& [id, g] : bonemeal) {
            auto* region = game::regionOf(g.dim);
            auto  pos    = game::blockPos(g);
            if (!region || !region->hasChunksAt(pos, 0, false) || !b.isChorus(region->getBlock(pos))) continue;
            chorusBonemeal(*region, pos);
            level.spawnParticleEffect("minecraft:crop_growth_emitter", center(pos), &region->getDimension());
            if (auto* a = game::entities::find(id); a && a->isPlayer()) consumeBoneMeal(static_cast<Player&>(*a));
        }
        scanEntities(level);
        ambientParticles(level);
    }
}

// ---- hooks -------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    ChorusQueuedTickHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::queuedTick,
    void,
    BlockSource& region,
    BlockPos const& pos,
    Random&      random
) {
    origin(region, pos, random);
    if (!isChorusType(&this->getBlockType()) || region.getLevel().isClientSide()) return;
    if (std::this_thread::get_id() != gServerThread.load()) {
        // instaticks while chunks load run on worker threads: redo it on the server thread
        std::lock_guard lock(pending().mutex);
        pending().schedule.insert(game::globalPos(region, pos));
        return;
    }
    auto const& b = blocks();
    chorusRandomTick(region, pos);
    if (region.hasChunksAt(pos, 0, false)) {
        auto const& now = region.getBlock(pos);
        if (b.isChorus(now)) scheduleChorusTick(region, pos, now);
    }
}

} // namespace

JavaRandom& serverRandom() {
    static JavaRandom r(static_cast<int64_t>(std::random_device{}()) << 20 ^ std::random_device{}());
    return r;
}

void scheduleChorusTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto* chunk = region.getChunkAt(pos);
    if (!chunk) return;
    Tick next{};
    if (chunk->getTickQueue().getNextUpdateForPos(pos, next)) return; // already scheduled
    int delay = 1 + threadRandom().nextInt(kMaxTickDelay);
    region.addToTickingQueue(pos, block, delay);
}

std::optional<BlockPos> chorusTeleport(BlockSource& region, BlockPos const& pos, Block const& state, int range, double growthChance) {
    auto const& b    = blocks();
    auto&       rand = serverRandom();
    int         xOff, zOff;
    do {
        xOff = rand.nextInt(range + 1) - (range / 2);
        zOff = rand.nextInt(range + 1) - (range / 2);
    } while (xOff == 0 && zOff == 0);
    BlockPos np{pos.x + xOff, pos.y + 10, pos.z + zOff};
    auto     blockAt = [&](BlockPos const& p) -> Block const& {
        if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight() || !region.hasChunksAt(p, 0, false)) return *b.air;
        return region.getBlock(p);
    };
    for (int i = 0; i < 20; ++i) {
        if (b.isEndStone(blockAt(np))) break;
        np.y -= 1;
    }
    BlockPos up{np.x, np.y + 1, np.z};
    if (!(b.isEndStone(blockAt(np)) && blockAt(up).isAir())) return std::nullopt;
    if (!game::setBlock(region, up, state)) return std::nullopt;
    scheduleChorusTick(region, up, state);

    if (rand.nextDouble() < growthChance) {
        game::setBlock(region, pos, *b.air);
        particles(region, pos, "minecraft:portal_directional", 8);
        teleportSound(region, pos, 5.0f + rand.nextFloat());
    }
    particles(region, up, "minecraft:portal_reverse_particle", 8);
    return up;
}

void chorusRandomTick(BlockSource& region, BlockPos const& pos) {
    auto cfg = config();
    if (!cfg.enableChorusVegetation) return;
    if (serverRandom().nextDouble() < cfg.passiveTeleportChance) {
        Block const& state = region.getBlock(pos);
        chorusTeleport(region, pos, state, 4, 1.0 - cfg.teleportDuplicationChance);
    }
}

void chorusEntityInside(BlockSource& region, BlockPos const& pos, Actor& entity) {
    auto cfg = config();
    if (!cfg.enableChorusVegetation) return;
    Block const& state = region.getBlock(pos);
    if (&state.getBlockType() != blocks().weedsType) return; // only the "simple" plant reacts
    (void)entity;
    auto target = chorusTeleport(region, pos, state, 4, 1.0 - cfg.teleportDuplicationChance);
    if (target && serverRandom().nextDouble() < cfg.endermiteSpawnChance) {
        game::entities::spawn(region, "minecraft:endermite", Vec3{static_cast<float>(target->x), static_cast<float>(target->y), static_cast<float>(target->z)});
    }
}

void chorusRunAwayFromWater(BlockSource& region, BlockPos const& pos) {
    for (auto const& d : kDirs) {
        BlockPos t{pos.x + d[0], pos.y + d[1], pos.z + d[2]};
        if (region.hasChunksAt(t, 0, false) && hasWater(region, t)) {
            Block const& state = region.getBlock(pos);
            chorusTeleport(region, pos, state, 8, 1);
            return;
        }
    }
}

void chorusBonemeal(BlockSource& region, BlockPos const& pos) {
    Block const& state = region.getBlock(pos);
    int          n     = 3 + serverRandom().nextInt(3);
    for (int i = 0; i < n; ++i) chorusTeleport(region, pos, state, 10, 0);
    chorusTeleport(region, pos, state, 4, 1);
}

void installChorus() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
    gListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
    ChorusQueuedTickHook::hook();
}

void uninstallChorus() {
    ChorusQueuedTickHook::unhook();
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
    std::lock_guard lock(pending().mutex);
    pending().schedule.clear();
    pending().water.clear();
    pending().bonemeal.clear();
}

} // namespace lp::feature::worldbeyond
