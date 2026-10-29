// U10 redstone: Gravisand. The block falls (2 ticks after it gets a redstone signal) down, or up when
// it can't fall down, taking adjacent gravisand along; the falling entity is lepton:gravisand, moved
// natively like Java's FallingBlockEntity. Comparators read 15 (patched virtual table).

#include "feature/redstone/Redstone.h"

#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/ConsumerComponent.h"

#include "core/GlobalPos.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "mod/Log.h"

namespace lp::feature::redstone {

namespace bs = game::blockstates;

namespace {

constexpr std::string_view kBlock  = "lepton:gravisand";
constexpr std::string_view kEntity = "lepton:gravisand";

struct Falling {
    Vec3 velocity{0, 0, 0};
    int  time = 0;
};
struct State {
    std::set<core::GlobalPos>                     scheduled; ///< pending fall ticks
    std::set<core::GlobalPos>                     checks;    ///< power checks for the next tick
    std::vector<std::pair<CircuitSystem*, BlockPos>> powered; ///< consumers the circuit evaluated powered
    std::unordered_map<int64_t, Falling>          falling;
    int                                           lastTicked = 0;
    bool                                          hooked     = false;
    bool                                          listening  = false;
};
State& st() {
    static State s;
    return s;
}

bool isGravisand(Block const& b) { return b.getTypeName() == kBlock; }

std::optional<AABB> collisionBox(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (b.isAir()) return std::nullopt;
    auto box = b.getBlockType().getCollisionShape(b, region, pos, nullptr);
    if (!(box.max.x > box.min.x && box.max.y > box.min.y && box.max.z > box.min.z)) return std::nullopt;
    return box;
}

// ---- block ------------------------------------------------------------------------------------------

void fallTick(core::GlobalPos const& gp);

void scheduleFall(int dim, BlockPos const& pos, uint64_t delay) {
    auto gp = game::globalPos(dim, pos);
    if (!st().scheduled.insert(gp).second) return;
    later(delay, [gp] {
        st().scheduled.erase(gp);
        fallTick(gp);
    });
}

bool tryFall(BlockSource& region, BlockPos const& pos, int face) {
    BlockPos target = offset(pos, face);
    if (target.y < region.getMinHeight() || target.y >= region.getMaxHeight()) return false;
    if (!canFallThrough(region, target)) return false;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.01f, static_cast<float>(pos.z) + 0.5f};
    game::clearBlock(region, pos);
    auto* e = game::entities::spawn(region, kEntity, at);
    if (!e) {
        if (auto const* b = bs::of(kBlock)) game::setBlock(region, pos, *b);
        return false;
    }
    if (face == 1) game::entities::triggerEvent(*e, "lepton:fall_up");
    st().falling[game::entities::idOf(*e)] = Falling{};
    log().info("redstone: gravisand at {} {} {} falls {}", pos.x, pos.y, pos.z, face == 1 ? "up" : "down");
    return true;
}

void fallTick(core::GlobalPos const& gp) {
    if (!cfg().enableGravisand) return;
    auto* region = game::regionOf(gp.dim);
    if (!region) return;
    BlockPos pos = game::blockPos(gp);
    if (!region->hasChunksAt(pos, 1, false) || !isGravisand(region->getBlock(pos))) return;
    if (!tryFall(*region, pos, 0) && !tryFall(*region, pos, 1)) return;
    for (int f = 0; f < 6; ++f) {
        BlockPos n = offset(pos, f);
        if (isGravisand(region->getBlock(n))) scheduleFall(gp.dim, n, 2);
    }
}

bool powered(BlockSource& region, BlockPos const& pos) {
    if (auto s = componentStrength(region, pos); s && *s > 0) return true;
    return hasNeighborSignal(region, pos);
}

int dimOfCircuit(CircuitSystem* sys) {
    auto level = ll::service::getLevel();
    if (!level) return -1;
    for (int d = 0; d < 3; ++d) {
        auto dim = level->getDimension(d).lock();
        if (dim && dim->mCircuitSystem.get() == sys) return d;
    }
    return -1;
}

void processChecks() {
    auto& s = st();
    if (!s.powered.empty()) {
        auto list = std::move(s.powered);
        s.powered.clear();
        for (auto const& [sys, pos] : list) {
            int d = dimOfCircuit(sys);
            if (d >= 0) s.checks.insert(game::globalPos(d, pos));
        }
    }
    if (s.checks.empty()) return;
    auto checks = std::move(s.checks);
    s.checks.clear();
    for (auto const& gp : checks) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos pos = game::blockPos(gp);
        if (!region->hasChunksAt(pos, 0, false) || !isGravisand(region->getBlock(pos))) continue;
        if (powered(*region, pos)) scheduleFall(gp.dim, pos, 2);
    }
}

// ---- entity ---------------------------------------------------------------------------------------

void land(Actor& e, BlockSource& region, BlockPos const& at, bool up) {
    bool replaceable = canFallThrough(region, at); // Java BlockState#canBeReplaced
    BlockPos support = offset(at, up ? 1 : 0);
    bool     free    = canFallThrough(region, support);
    auto const* b    = bs::of(kBlock);
    if (b && replaceable && !free) {
        game::setBlock(region, at, *b);
        log().info("redstone: gravisand landed at {} {} {}", at.x, at.y, at.z);
    } else {
        dropItem(region, at, kBlock);
    }
    st().falling.erase(game::entities::idOf(e));
    game::entities::despawn(e);
}

void tickEntity(Actor& e) {
    auto& f  = st().falling[game::entities::idOf(e)];
    bool  up = e.getVariant() == 1;
    ++f.time;
    // external impulses (U12 magnets add to the motion) join the velocity
    Vec3& delta = e.mBuiltInComponents->mStateVectorComponent->mPosDelta.get();
    f.velocity.x += delta.x;
    f.velocity.y += delta.y;
    f.velocity.z += delta.z;
    delta = Vec3{0, 0, 0};
    f.velocity.y -= 0.04f;
    float scale = up ? -1.0f : 1.0f;
    auto& region = e.getDimensionBlockSource();
    Vec3  pos    = e.getPosition();
    Vec3  next{pos.x + f.velocity.x * scale, pos.y + f.velocity.y * scale, pos.z + f.velocity.z * scale};
    int   bx = static_cast<int>(std::floor(next.x)), bz = static_cast<int>(std::floor(next.z));
    bool  landed = false;
    if (!up) {
        int from = static_cast<int>(std::floor(pos.y - 0.001f)), to = static_cast<int>(std::floor(next.y));
        for (int y = from; y >= to; --y) {
            BlockPos p{bx, y, bz};
            if (!region.hasChunksAt(p, 0, false)) break;
            if (auto box = collisionBox(region, p); box && box->max.y > next.y && box->max.y <= pos.y + 0.001f) {
                next.y = box->max.y;
                landed = true;
                break;
            }
        }
    } else {
        int from = static_cast<int>(std::floor(pos.y + 0.98f + 0.001f)), to = static_cast<int>(std::floor(next.y + 0.98f));
        for (int y = from; y <= to; ++y) {
            BlockPos p{bx, y, bz};
            if (!region.hasChunksAt(p, 0, false)) break;
            if (auto box = collisionBox(region, p); box && box->min.y < next.y + 0.98f && box->min.y >= pos.y + 0.98f - 0.001f) {
                next.y = box->min.y - 0.98f;
                landed = true;
                break;
            }
        }
    }
    f.velocity.x *= 0.98f;
    f.velocity.y *= 0.98f;
    f.velocity.z *= 0.98f;
    e.teleport(next, e.getDimensionId());
    if (landed) {
        BlockPos at{bx, static_cast<int>(std::floor(next.y + 0.001f)), bz};
        land(e, region, at, up);
        return;
    }
    bool outside = next.y < region.getMinHeight() || next.y > region.getMaxHeight();
    if (f.time > 600 || (f.time > 100 && outside)) {
        BlockPos at{bx, static_cast<int>(std::floor(next.y)), bz};
        if (!outside) dropItem(region, at, kBlock);
        st().falling.erase(game::entities::idOf(e));
        game::entities::despawn(e);
    }
}

void tick() {
    processChecks();
    auto list = game::entities::ofType(kEntity);
    st().lastTicked = static_cast<int>(list.size());
    for (auto* e : list) {
        if (!e || e->isClientSide()) continue;
        tickEntity(*e);
    }
    if (list.empty() && !st().falling.empty()) st().falling.clear();
}

// ---- comparator output (Quark: hasAnalogOutputSignal / getAnalogOutputSignal = 15) ----------------

bool hasComparatorSignal(BlockType const*) { return true; }
int  comparatorSignal(BlockType const*, BlockSource&, BlockPos const&, Block const&, uchar) { return 15; }

template <class M>
void* thunkOf(M member) {
    static_assert(sizeof(M) == sizeof(void*), "single-inheritance member function pointer");
    void* p;
    std::memcpy(&p, &member, sizeof(p));
    return p;
}

LL_TYPE_INSTANCE_HOOK(
    GravisandConsumerHook,
    ll::memory::HookPriority::Normal,
    ConsumerComponent,
    &ConsumerComponent::$evaluate,
    bool,
    CircuitSystem&  system,
    BlockPos const& pos
) {
    bool r = origin(system, pos);
    if (installed() && getStrength() > 0 && onServerThread()) st().powered.emplace_back(&system, pos);
    return r;
}

} // namespace

void gravisandNeighborChanged(BlockSource& region, BlockPos const& pos) {
    if (!cfg().enableGravisand) return;
    st().checks.insert(game::globalPos(region, pos));
}

void onWorldReadyGravisand() {
    auto const* b = bs::of(kBlock);
    if (!b) {
        log().warn("redstone: {} is not registered (behavior pack missing?)", kBlock);
        return;
    }
    auto& type = const_cast<BlockType&>(b->getBlockType());
    bool  ok   = patchVirtual(type, thunkOf(&BlockType::hasComparatorSignal), reinterpret_cast<void*>(&hasComparatorSignal))
           && patchVirtual(type, thunkOf(&BlockType::getComparatorSignal), reinterpret_cast<void*>(&comparatorSignal));
    if (ok) log().info("redstone: gravisand comparator output installed");
    else log().error("redstone: gravisand comparator output could not be installed");
}

void installGravisand() {
    auto& s = st();
    if (!s.hooked) {
        GravisandConsumerHook::hook();
        s.hooked = true;
    }
    if (!s.listening) {
        s.listening = true;
        onTick([] {
            if (installed()) tick();
        });
    }
}

void uninstallGravisand() {
    auto& s = st();
    if (s.hooked) {
        GravisandConsumerHook::unhook();
        s.hooked = false;
    }
    s.scheduled.clear();
    s.checks.clear();
    s.powered.clear();
}

int testing::fallingGravisand() { return st().lastTicked; }

} // namespace lp::feature::redstone
