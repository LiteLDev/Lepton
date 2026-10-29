// U12 crystals / Corundum: growth (emulated random ticks), particles, waxing, cluster support and
// collateral movement with pistons. World generation lives in CorundumGen.cpp.

#include <cmath>
#include <fstream>
#include <random>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/PistonBlockActor.h"
#include "mc/world/level/block/actor/PistonState.h"

#include "feature/crystals/Common.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::crystals {

namespace bs = game::blockstates;
using SoundEvent = SharedTypes::Legacy::LevelSoundEvent;

namespace {

bool gInstalled = false;
bool corundumOn() { return gInstalled && cfg().enableCorundum; }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
double rand01() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }
int    randInt(int bound) { return bound <= 1 ? 0 : std::uniform_int_distribution<int>(0, bound - 1)(rng()); }

Vec3 center(BlockPos const& p) { return {p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

/// SpiralSpires' renewableMyalite (U29), read from its config file; the block must exist too.
bool renewableMyalite() {
    static uint64_t checkedAt = ~0ULL;
    static bool     value     = false;
    if (checkedAt != currentTick() / 200) {
        checkedAt = currentTick() / 200;
        value     = false;
        if (bs::of("lepton:myalite_crystal")) {
            bool enabled = true, renewable = true;
            std::ifstream in(config::configDir() / "worldbeyond.json");
            if (in) {
                auto j = nlohmann::json::parse(in, nullptr, false);
                if (j.is_object()) {
                    enabled   = j.value("enableSpiralSpires", true);
                    renewable = j.value("renewableMyalite", true);
                }
            }
            value = enabled && renewable;
        }
    }
    return value;
}

// ---- growth (Java CorundumBlock#canGrow / #randomTick) ----------------------------------------------

bool canGrow(BlockSource& region, BlockPos const& pos, Block const& self) {
    if (!isCorundum(self) || cfg().caveCrystalGrowthChance < 1 || pos.y >= 24) return false;
    if (!region.getBlock(offset(pos, 1)).isAir()) return false;
    int i = 1;
    while (i < 8 && &region.getBlock(offset(pos, 0, i)).getBlockType() == &self.getBlockType()) ++i;
    return i < 4;
}

void randomTick(BlockSource& region, BlockPos const& pos) {
    auto const& self = region.getBlock(pos);
    if (!canGrow(region, pos, self) || randInt(cfg().caveCrystalGrowthChance) != 0) return;
    auto const& down = region.getBlock(offset(pos, 0));
    auto        up   = offset(pos, 1);
    setBlock(region, up, self);
    if (down.getTypeName() == "lepton:myalite_crystal" && renewableMyalite()) {
        if (auto const* m = bs::of("lepton:myalite_crystal")) setBlock(region, pos, *m);
        return;
    }
    int c = colorOf(self);
    for (int d = 0; d < 6; ++d) {
        auto off = offset(up, d);
        if (region.getBlock(off).isAir() && randInt(3) == 0) {
            if (auto const* cl = bs::of(clusterId(c), {{"minecraft:block_face", std::string(faceToName(d))}}))
                setBlock(region, off, *cl);
        }
    }
}

// ---- particles (Java CorundumBlock#animateTick, sent from the server) -----------------------------

void animateTick(Player const& viewer, BlockSource& region, BlockPos const& pos, Block const& self) {
    int c = colorOf(self);
    if (c < 0) return;
    uint32_t col = kColors[c].beacon;
    float    r = ((col >> 16) & 255) / 255.f, g = ((col >> 8) & 255) / 255.f, b = (col & 255) / 255.f;
    if (canGrow(region, pos, self)) {
        Vec3 at{static_cast<float>(pos.x + rand01()), static_cast<float>(pos.y + rand01()), static_cast<float>(pos.z + rand01())};
        spawnParticleFor(viewer, region, "lepton:corundum_grow", at,
                      {{"r", r}, {"g", g}, {"b", b}, {"dx", static_cast<float>(rand01())}, {"dy", static_cast<float>(rand01())},
                       {"dz", static_cast<float>(rand01())}});
    }
    for (int i = 0; i < 4; ++i) {
        double range = 5;
        double ox = rand01() * range - range / 2, oy = rand01() * range - range / 2, oz = rand01() * range - range / 2;
        Vec3   at{static_cast<float>(pos.x + 0.5 + ox), static_cast<float>(pos.y + 0.5 + oy), static_cast<float>(pos.z + 0.5 + oz)};
        float  size = 0.4f + static_cast<float>(rand01()) * 0.5f;
        if (rand01() < 0.1) {
            double ol = (ox * ox + oy * oy + oz * oz) * -2;
            if (ol == 0) ol = 0.0001;
            spawnParticleFor(viewer, region, "lepton:corundum_spark", at,
                          {{"dx", static_cast<float>(ox / ol)}, {"dy", static_cast<float>(oy / ol)}, {"dz", static_cast<float>(oz / ol)}});
        }
        spawnParticleFor(viewer, region, "lepton:corundum_dust", at, {{"r", r}, {"g", g}, {"b", b}, {"size", size}});
    }
}

void particleTick() {
    // Java samples 667 random positions within 16 and 667 within 32 blocks of the player each tick
    constexpr double kNear = 667.0 / (33.0 * 33.0 * 33.0);
    constexpr double kFar  = 667.0 / (65.0 * 65.0 * 65.0);
    auto             level = ll::service::getLevel();
    if (!level) return;
    auto const& set = tracked(Kind::Corundum);
    if (set.empty()) return;
    level->forEachPlayer([&](Player& p) {
        int  dim = static_cast<int>(p.getDimensionId());
        auto fp  = p.getFeetBlockPos();
        auto lo  = set.lower_bound({dim, fp.x - 32, -100000, -100000});
        auto& region = p.getDimensionBlockSource();
        for (auto it = lo; it != set.end() && it->dim == dim && it->x <= fp.x + 32; ++it) {
            int dx = std::abs(it->x - fp.x), dy = std::abs(it->y - fp.y), dz = std::abs(it->z - fp.z);
            if (dy > 32 || dz > 32) continue;
            double chance = kFar + ((dx <= 16 && dy <= 16 && dz <= 16) ? kNear : 0.0);
            if (rand01() >= chance) continue;
            BlockPos bp{it->x, it->y, it->z};
            if (!region.hasChunksAt(bp, 0, false)) continue;
            auto const& b = region.getBlock(bp);
            if (isCorundum(b)) animateTick(p, region, bp, b); // each player samples its own, like Java clients
        }
        return true;
    });
}

void growthTick() {
    if (!corundumOn() || cfg().caveCrystalGrowthChance < 1) return;
    double p = 3.0 * randomTickSpeed() / 4096.0;
    if (p <= 0) return;
    std::vector<core::GlobalPos> hits;
    for (auto const& gp : tracked(Kind::Corundum))
        if (gp.y < 24 && rand01() < p) hits.push_back(gp);
    for (auto const& gp : hits) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos bp{gp.x, gp.y, gp.z};
        if (!region->hasChunksAt(bp, 0, false)) continue;
        if (!isCorundum(region->getBlock(bp))) {
            untrack(Kind::Corundum, gp);
            continue;
        }
        randomTick(*region, bp);
    }
}

// ---- deferred work ----------------------------------------------------------------------------------

struct Task {
    uint64_t              at;
    std::function<void()> fn;
};
std::vector<Task>& tasks() {
    static std::vector<Task> t;
    return t;
}

void runTasks() {
    auto now = currentTick();
    std::vector<Task> due;
    auto& all = tasks();
    for (auto it = all.begin(); it != all.end();) {
        if (it->at <= now) {
            due.push_back(std::move(*it));
            it = all.erase(it);
        } else ++it;
    }
    for (auto& t : due) t.fn();
}

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

// ---- cluster support -----------------------------------------------------------------------------

std::set<core::GlobalPos>& supportQueue() {
    static std::set<core::GlobalPos> q;
    return q;
}

bool clusterSupported(BlockSource& region, BlockPos const& pos, Block const& b) {
    int f = clusterFacing(b);
    if (f < 0) return true;
    auto const& support = region.getBlock(offset(pos, oppositeFace(f)));
    if (support.isAir()) return false;
    return support.canProvideFullSupport(static_cast<uchar>(f));
}

void supportTick() {
    if (supportQueue().empty()) return;
    auto q = std::move(supportQueue());
    supportQueue().clear();
    for (auto const& gp : q) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos bp{gp.x, gp.y, gp.z};
        auto const& b = region->getBlock(bp);
        if (!isCluster(b) || clusterSupported(*region, bp, b)) continue;
        // Java Level#destroyBlock(pos, true) drops without a tool; the pack's loot table needs a
        // pickaxe (requiresCorrectToolForDrops for players), so the drop is spawned here.
        auto drop = game::makeItem(game::blockId(b));
        destroyBlock(*region, bp, false);
        region->getLevel().getSpawner().spawnItem(*region, drop, nullptr, center(bp), 10);
    }
}

} // namespace

void later(uint64_t delay, std::function<void()> fn) { tasks().push_back({currentTick() + std::max<uint64_t>(delay, 1), std::move(fn)}); }

bool clusterCanSurvive(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    return !isCluster(b) || clusterSupported(region, pos, b);
}

void corundumRandomTick(BlockSource& region, BlockPos const& pos) { randomTick(region, pos); }
bool corundumCanGrow(BlockSource& region, BlockPos const& pos) { return canGrow(region, pos, region.getBlock(pos)); }

namespace {

LL_TYPE_INSTANCE_HOOK(
    ClusterNeighborHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::neighborChanged,
    void,
    BlockSource&      region,
    BlockPos const&   pos,
    BlockPos const&   neighborPos
) {
    origin(region, pos, neighborPos);
    if (!gInstalled || region.getLevel().isClientSide()) return;
    if (isCluster(*this)) supportQueue().insert(game::globalPos(region, pos));
    for (auto& fn : neighborListeners()) fn(region, pos, *this);
}

// Collateral movement: clusters ride along with the corundum they hang on (Quark ClusterConnection).
LL_TYPE_INSTANCE_HOOK(
    PistonAttachedHook,
    ll::memory::HookPriority::Normal,
    PistonBlockActor,
    &PistonBlockActor::_checkAttachedBlocks,
    bool,
    BlockSource& region
) {
    bool ok = origin(region);
    if (!ok || !corundumOn() || !cfg().enableCollateralMovement || region.getLevel().isClientSide()) return ok;
    auto& attached = mAttachedBlocks.get();
    if (attached.empty()) return ok;
    auto const& step = getFacingDir(region);
    int  sign   = static_cast<PistonState>(mNewState) == PistonState::Retracting ? -1 : 1;
    BlockPos move{step.x * sign, step.y * sign, step.z * sign};
    auto const& breaking = mBreakBlocks.get();
    auto contains = [&](BlockPos const& p) {
        return std::find(attached.begin(), attached.end(), p) != attached.end()
            || std::find(breaking.begin(), breaking.end(), p) != breaking.end();
    };
    std::vector<BlockPos> extra;
    for (auto const& p : attached) {
        auto const& base = region.getBlock(p);
        if (!isCorundum(base)) continue;
        int c = colorOf(base);
        for (int f = 0; f < 6; ++f) {
            BlockPos cp = offset(p, f);
            auto const& cb = region.getBlock(cp);
            if (!isCluster(cb) || colorOf(cb) != c || clusterFacing(cb) != f) continue;
            if (contains(cp) || std::find(extra.begin(), extra.end(), cp) != extra.end()) continue;
            BlockPos dest{cp.x + move.x, cp.y + move.y, cp.z + move.z};
            if (!region.getBlock(dest).isAir() && !contains(dest) && !(dest == p)) continue;
            extra.push_back(cp);
        }
    }
    for (auto const& e : extra) attached.push_back(e);
    return ok;
}

class CorundumModule {
public:
    void install() {
        gInstalled = true;
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldClosed([] {
                tasks().clear();
                supportQueue().clear();
            });
            tickListeners().push_back([] {
                if (!gInstalled) return;
                runTasks();
                supportTick();
                growthTick();
                if (corundumOn()) particleTick();
            });
        }
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
        ClusterNeighborHook::hook();
        PistonAttachedHook::hook();
    }
    void uninstall() {
        gInstalled = false;
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        ClusterNeighborHook::unhook();
        PistonAttachedHook::unhook();
        tasks().clear();
        supportQueue().clear();
    }

private:
    static void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
        auto& player = ev.self();
        if (player.isClientSide()) return;
        auto&       region = player.getDimensionBlockSource();
        auto        pos    = ev.blockPos();
        auto const& b      = region.getBlock(pos);
        auto const& item   = ev.item();
        if (item.isNull()) return;
        auto itemId = item.getTypeName();
        bool honey  = itemId == "minecraft:honeycomb";
        bool axe    = itemId.ends_with("_axe");
        if (!honey && !axe) return;

        std::string target;
        bs::Changes changes;
        if (isCorundum(b) && honey && cfg().enableCorundum) target = waxedCorundumId(colorOf(b));
        else if (isWaxedCorundum(b) && axe && cfg().enableCorundum) target = corundumId(colorOf(b));
        else if (isMagnet(b) && cfg().enableMagnets) {
            bool waxed = bs::getBool(b, "lepton:waxed").value_or(false);
            if (honey && !waxed) changes = {{"lepton:waxed", true}};
            else if (axe && waxed) changes = {{"lepton:waxed", false}};
            else return;
        } else return;

        ev.cancel();
        int     dim = static_cast<int>(region.getDimensionId());
        int64_t pid = player.getOrCreateUniqueID().rawID;
        auto    was = b.getTypeName();
        later(1, [dim, pos, pid, target, changes, honey, was] {
            auto* r = game::regionOf(dim);
            if (!r) return;
            auto const& cur = r->getBlock(pos);
            if (cur.getTypeName() != was) return;
            Block const* next = target.empty() ? bs::with(cur, changes) : bs::of(target);
            if (!next || next == &cur) return;
            setBlock(*r, pos, *next);
            playSound(*r, pos, static_cast<int>(honey ? SoundEvent::CopperWaxOn : SoundEvent::CopperWaxOff));
            auto* p = playerById(pid);
            if (!p || p->isCreative()) return;
            ItemStack held = p->getSelectedItem();
            if (held.isNull()) return;
            if (honey && held.getTypeName() == "minecraft:honeycomb") held.remove(1);
            else if (!honey && held.getTypeName().ends_with("_axe")) held.hurtAndBreak(1, p);
            else return;
            p->setSelectedItem(held);
            p->sendInventory(false);
        });
    }

    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

std::vector<NeighborFn>& neighborListeners() {
    static std::vector<NeighborFn> v;
    return v;
}

} // namespace lp::feature::crystals

LP_REGISTER_MODULE(lp::feature::crystals::CorundumModule);
