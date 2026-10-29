// U10 redstone: the piston structure (ChainsConnectBlocks, IronRod, PistonsMoveTileEntities).
//
// A port of Zeta's ZetaPistonStructureResolver on Bedrock's piston rules, run in
// PistonBlockActor::_checkAttachedBlocks on the server. When the resolved structure uses none of
// Quark's features (chains, iron rods, block entity rules, chests together) and the push limit is 12,
// the game's own result is kept.

#include "feature/redstone/Redstone.h"

#include <algorithm>
#include <map>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"
#include "mc/world/level/block/actor/PistonBlockActor.h"
#include "mc/world/level/block/actor/PistonState.h"
#include "mc/world/level/block/components/BlockMovableComponent.h"

#include "core/GlobalPos.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace lp::feature::redstone {

namespace bs = game::blockstates;

namespace {

testing::PistonTrace gTrace;
bool                 gHooked = false;

enum class Move { Move, Break, Skip, Prevent };

/// Bedrock 1.26.51 marks these immovable; on Java they only stop pistons through their block entity,
/// so Quark moves them.
constexpr char const* kJavaMovable[] = {"minecraft:beacon",        "minecraft:jukebox",         "minecraft:sculk_sensor",
                                        "minecraft:calibrated_sculk_sensor", "minecraft:sculk_catalyst",
                                        "minecraft:sculk_shrieker"};

std::string const& nameOf(Block const& b) { return b.getTypeName(); }

bool isChain(Block const& b) {
    auto const& n = nameOf(b);
    return n.starts_with("minecraft:") && n.ends_with("chain") && bs::getString(b, "pillar_axis").has_value();
}
int chainAxis(Block const& b) {
    auto a = bs::getString(b, "pillar_axis").value_or("y");
    return a == "x" ? 0 : a == "z" ? 2 : 1;
}
int axisOf(int face) { return face <= 1 ? 1 : face <= 3 ? 2 : 0; }
bool isSlime(Block const& b) { return nameOf(b) == "minecraft:slime"; }
bool isHoney(Block const& b) { return nameOf(b) == "minecraft:honey_block"; }
bool isIronRod(Block const& b) { return nameOf(b) == "lepton:iron_rod"; }
int  rodFacing(Block const& b) { return faceFromName(bs::getString(b, "minecraft:block_face").value_or("up")); }
bool rodImmune(Block const& b) { return b.hasTag(HashedString("lepton:iron_rod_immune")); }

MovementType movementOf(Block const& b) {
    if (auto const* m = b.tryGetComponent<BlockMovableComponent>()) return m->mMovementType;
    return MovementType::PushPull;
}
bool hasBlockEntity(Block const& b) { return b.getBlockType().mBlockEntityType != BlockActorType::Undefined; }

/// Java ids of the default blacklist that are named differently on Bedrock.
std::string_view bedrockId(std::string_view javaId) {
    if (javaId == "minecraft:spawner") return "minecraft:mob_spawner";
    return javaId;
}

bool inMovementBlacklist(BlockSource& region, BlockPos const& pos, Block const& b) {
    auto const& c    = cfg();
    auto const& name = nameOf(b);
    for (auto const& e : c.movementBlacklist) {
        if (e.empty()) continue;
        if (e[0] == '#') {
            std::string_view tag(e);
            tag.remove_prefix(1);
            if (tag == "minecraft:ceiling_hanging_signs") {
                if (name.ends_with("_hanging_sign") && bs::getBool(b, "hanging").value_or(false)) return true;
            } else if (tag == "c:chests/trapped") {
                if (name == "minecraft:trapped_chest") return true;
            } else if (b.hasTag(HashedString(std::string(tag)))) return true;
        } else if (name == bedrockId(e) || name == e) return true;
    }
    for (auto const& ns : c.namespaceMovementBlacklist) {
        if (!ns.empty() && name.size() > ns.size() && name.starts_with(ns) && name[ns.size()] == ':') return true;
    }
    // jukeboxes that are playing can't be moved so the music can be stopped
    if (name == "minecraft:jukebox") {
        if (auto* be = region.getBlockEntity(pos); be && be->getType() == BlockActorType::Jukebox) {
            auto* jb = static_cast<JukeboxBlockActor*>(be);
            if (!jb->mRecord->isNull()) return true;
        }
    }
    return false;
}

std::optional<BlockPos> chestPartner(BlockSource& region, BlockPos const& pos) {
    auto* be = region.getBlockEntity(pos);
    if (!be || be->getType() != BlockActorType::Chest) return std::nullopt;
    auto* chest = static_cast<ChestBlockActor*>(be);
    if (!chest->mLargeChestPaired) return std::nullopt;
    return chest->mLargeChestPairedPosition.get();
}

struct Resolver {
    BlockSource&          r;
    BlockPos              piston;
    BlockPos              start;
    int                   dir;          ///< move direction
    int                   pistonFacing; ///< the piston's facing
    bool                  extending;
    size_t                limit;
    std::vector<BlockPos> push;
    std::vector<BlockPos> destroy;
    bool                  usedLepton = false;

    RedstoneConfig const& c = cfg();

    Block const& at(BlockPos const& p) const { return r.getBlock(p); }
    static bool  has(std::vector<BlockPos> const& v, BlockPos const& p) { return std::find(v.begin(), v.end(), p) != v.end(); }

    bool chainsOn() const { return c.enableChainsConnectBlocks; }
    bool chestsOn() const { return c.enablePistonsMoveTileEntities && c.enableChestsMovingTogether; }
    bool rodsOn() const { return c.enableIronRod; }

    bool extendedPiston(BlockPos const& p, Block const&) const {
        auto* be = r.getBlockEntity(p);
        if (!be || be->getType() != BlockActorType::PistonArm) return false;
        return static_cast<PistonState>(static_cast<PistonBlockActor*>(be)->mState) != PistonState::Retracted;
    }

    /// Java PistonBaseBlock#isPushable on Bedrock's rules.
    bool pushable(BlockPos const& p, int face, bool allowDestroy) {
        auto const& b = at(p);
        if (p.y < r.getMinHeight() || p.y >= r.getMaxHeight()) return false;
        if (b.isAir()) return true;
        if (dir == 0 && p.y == r.getMinHeight()) return false;
        if (dir == 1 && p.y == r.getMaxHeight() - 1) return false;
        auto const& name = nameOf(b);
        if (name == "minecraft:piston" || name == "minecraft:sticky_piston") {
            if (extendedPiston(p, b)) return false;
        }
        if (name == "minecraft:moving_block" || name.ends_with("piston_arm_collision")) return false;
        auto mt = movementOf(b);
        if (c.enablePistonsMoveTileEntities && hasBlockEntity(b)) {
            if (inMovementBlacklist(r, p, b)) {
                if (mt != MovementType::Immovable) usedLepton = true;
                return false;
            }
            if (mt == MovementType::Immovable && b.getBlockType().getDestroySpeed() >= 0.0f && javaMovable(name)) {
                mt         = MovementType::PushPull;
                usedLepton = true;
            }
        }
        switch (mt) {
        case MovementType::Immovable: return false;
        case MovementType::Popped: return allowDestroy;
        case MovementType::Push: return face == dir;
        default: return true;
        }
    }
    /// Bedrock-immovable block entity blocks that Java Quark moves (breakable, Java push reaction normal).
    static bool javaMovable(std::string const& name) {
        return std::find(std::begin(kJavaMovable), std::end(kJavaMovable), name) != std::end(kJavaMovable);
    }
    bool popped(Block const& b) const { return !b.isAir() && movementOf(b) == MovementType::Popped; }

    static bool canStickTo(Block const& a, Block const& b) {
        if (isHoney(a) && isSlime(b)) return false;
        if (isSlime(a) && isHoney(b)) return false;
        return true;
    }
    bool sticky(Block const& b) const { return isSlime(b) || isHoney(b); }
    bool pairedChest(BlockPos const& p, Block const& b) const {
        auto const& n = nameOf(b);
        return (n == "minecraft:chest" || n == "minecraft:trapped_chest") && chestPartner(r, p).has_value();
    }
    /// An enabled IIndirectConnector (chain, paired chest).
    bool indirect(BlockPos const& p, Block const& b) const {
        if (chainsOn() && isChain(b)) return true;
        if (chestsOn() && pairedChest(p, b)) return true;
        return false;
    }
    /// IIndirectConnector#canConnectIndirectly(world, ourPos, sourcePos, ourState, sourceState).
    bool canConnectIndirectly(BlockPos const& ourPos, BlockPos const& sourcePos, Block const& our, Block const& source) const {
        if (isChain(our)) {
            int axis = chainAxis(our);
            if (axis == 0 && ourPos.x == sourcePos.x) return false;
            if (axis == 1 && ourPos.y == sourcePos.y) return false;
            if (axis == 2 && ourPos.z == sourcePos.z) return false;
            if (isChain(source)) return chainAxis(source) == axis;
            return true;
        }
        // chests: Quark's ChestConnection
        if (sticky(source)) return true;
        auto partner = chestPartner(r, ourPos);
        return partner && *partner == sourcePos;
    }
    /// The stick condition of `b` at `p` towards (`other`, `ob`): nullopt when the block has none.
    std::optional<bool> stickCondition(BlockPos const& p, Block const& b, BlockPos const& other, Block const& ob) const {
        if (indirect(p, b)) return canConnectIndirectly(p, other, b, ob);
        if (sticky(b)) return canStickTo(ob, b) && canStickTo(b, ob);
        return std::nullopt;
    }
    Move stickCompat(Block const& s1, Block const& s2, BlockPos const& p1, BlockPos const& p2) {
        if (auto ok = stickCondition(p1, s1, p2, s2); ok && !*ok) return Move::Skip;
        if (auto ok = stickCondition(p2, s2, p1, s1); ok && !*ok) return Move::Skip;
        return Move::Move;
    }
    bool collateral(BlockPos const& p, Block const& b) const {
        return rodsOn() && isIronRod(b) && rodFacing(b) == dir && !rodImmune(at(offset(p, dir)));
    }
    bool blockSticky(BlockPos const& p, Block const& b) const { return sticky(b) || indirect(p, b); }
    bool branching(BlockPos const& p) const {
        auto const& b = at(p);
        if (rodsOn() && isIronRod(b)) return collateral(p, b);
        return blockSticky(p, b);
    }
    /// ICollateralMover#getCollateralMovement for the iron rod.
    Move rodMovement(BlockPos const& p, int side) const {
        return side == dir && !rodImmune(at(offset(p, side))) ? Move::Break : Move::Skip;
    }
    Move branchResult(BlockPos const& p) const {
        auto const& b = at(p);
        if (rodsOn() && isIronRod(b)) return rodMovement(p, dir);
        return Move::Move;
    }

    Move addBranching(BlockPos const& from, bool sourceBranching) {
        auto const& state = at(from);
        int         opp   = opposite(dir);
        Move        ret   = Move::Skip;
        for (int face = 0; face < 6; ++face) {
            BlockPos    target = offset(from, face);
            auto const& ts     = at(target);
            Move        res;
            if (!sourceBranching) {
                if (indirect(target, ts) && canConnectIndirectly(target, from, ts, state)) {
                    res = stickCompat(state, ts, from, target);
                    if (res == Move::Move && !ts.isAir() && !has(push, target)) usedLepton = true;
                } else res = Move::Skip;
            } else if (rodsOn() && isIronRod(state)) {
                res = rodMovement(from, face);
            } else {
                res = stickCompat(state, ts, from, target);
                if (res == Move::Move && indirect(from, state) && !ts.isAir() && !has(push, target)) usedLepton = true;
            }
            switch (res) {
            case Move::Prevent: return Move::Prevent;
            case Move::Move:
                if (!addLine(target, face)) return Move::Prevent;
                break;
            case Move::Break:
                usedLepton = true;
                if (pushable(target, dir, true)) {
                    if (!at(target).isAir() && !has(destroy, target)) destroy.push_back(target);
                    return Move::Break;
                }
                return Move::Prevent;
            default: break;
            }
            if (face == opp) ret = res;
        }
        return ret;
    }

    void reorder(size_t collisionEnd, size_t collisionStart) {
        std::vector<BlockPos> before(push.begin(), push.begin() + collisionStart);
        std::vector<BlockPos> collision(push.end() - collisionEnd, push.end());
        std::vector<BlockPos> after(push.begin() + collisionStart, push.end() - collisionEnd);
        push.clear();
        push.insert(push.end(), before.begin(), before.end());
        push.insert(push.end(), collision.begin(), collision.end());
        push.insert(push.end(), after.begin(), after.end());
    }

    bool addLine(BlockPos const& origin, int face) {
        auto const& first = at(origin);
        if (first.isAir() || !pushable(origin, face, false) || origin == piston || has(push, origin)) return true;
        size_t lineLen = 1;
        if (lineLen + push.size() > limit) return false;
        BlockPos     oldPos   = origin;
        Block const* oldState = &first;
        BlockPos     target   = origin;
        bool         skipping = false;
        while (true) {
            if (!branching(target)) break;
            Move res = branchResult(target);
            if (res == Move::Prevent) return false;
            if (res != Move::Move) {
                skipping = true;
                break;
            }
            target          = offset(origin, opposite(dir), static_cast<int>(lineLen));
            auto const& st  = at(target);
            if (st.isAir() || !(canStickTo(*oldState, st) && canStickTo(st, *oldState)) || !pushable(target, opposite(dir), false)
                || target == piston)
                break;
            if (stickCompat(st, *oldState, target, oldPos) != Move::Move) break;
            if (isChain(st) || isChain(*oldState)) usedLepton = usedLepton || chainsOn();
            oldState = &st;
            oldPos   = target;
            ++lineLen;
            if (lineLen + push.size() > limit) return false;
        }
        size_t collisionEnd = 0;
        for (size_t j = lineLen; j-- > 0;) {
            BlockPos movePos = offset(origin, opposite(dir), static_cast<int>(j));
            if (has(destroy, movePos)) break;
            push.push_back(movePos);
            ++collisionEnd;
        }
        if (skipping) return true;
        int off = 1;
        while (true) {
            BlockPos current = offset(origin, dir, off);
            auto     it      = std::find(push.begin(), push.end(), current);
            if (it != push.end()) {
                auto collisionStart = static_cast<size_t>(it - push.begin());
                reorder(collisionEnd, collisionStart);
                for (size_t i = 0; i <= collisionStart + collisionEnd && i < push.size(); ++i) {
                    BlockPos p = push[i];
                    if (addBranching(p, branching(p)) == Move::Prevent) return false;
                }
                return true;
            }
            auto const& st = at(current);
            if (st.isAir()) return true;
            if (!pushable(current, dir, true) || current == piston) return false;
            if (popped(st)) {
                if (!has(destroy, current)) destroy.push_back(current);
                return true;
            }
            bool done = false;
            if (branching(current)) {
                Move res = branchResult(current);
                if (res == Move::Prevent) return false;
                if (res != Move::Move) done = true;
            }
            if (push.size() >= limit) return false;
            push.push_back(current);
            ++collisionEnd;
            ++off;
            if (done) return true;
        }
    }

    bool resolve() {
        push.clear();
        destroy.clear();
        auto const& st = at(start);
        if (!pushable(start, pistonFacing, false)) {
            if (extending && popped(st)) {
                destroy.push_back(start);
                return true;
            }
            return false;
        }
        if (!addLine(start, dir)) return false;
        for (size_t i = 0; i < push.size(); ++i) {
            BlockPos p = push[i];
            if (addBranching(p, branching(p)) == Move::Prevent) return false;
        }
        return true;
    }
};

int faceOfStep(BlockPos const& s) {
    if (s.y < 0) return 0;
    if (s.y > 0) return 1;
    if (s.z < 0) return 2;
    if (s.z > 0) return 3;
    if (s.x < 0) return 4;
    return 5;
}

std::map<core::GlobalPos, uint64_t>& delayed() {
    static std::map<core::GlobalPos, uint64_t> d;
    return d;
}

/// Dispensers / droppers (delayedUpdateList) get a neighbour + comparator update a tick after landing.
void scheduleDelayedUpdates(BlockSource& region, std::vector<BlockPos> const& moved, int dir) {
    auto const& list = cfg().delayedUpdateList;
    if (list.empty()) return;
    int dim = dimOf(region);
    for (auto const& p : moved) {
        auto const& name = nameOf(region.getBlock(p));
        if (std::find(list.begin(), list.end(), name) == list.end()) continue;
        BlockPos dest = offset(p, dir);
        auto     key  = game::globalPos(dim, dest);
        auto&    due  = delayed()[key];
        if (due > now()) continue;
        due = now() + 4;
        later(4, [dim, dest, key] {
            delayed().erase(key);
            if (auto* reg = game::regionOf(dim); reg && reg->hasChunksAt(dest, 0, false)) reg->updateNeighborsAt(dest);
        });
    }
}

/// Bedrock refuses a few block-entity blocks in its own piston code too: while PistonsMoveTileEntities
/// is on they get the push_pull movement type (restored when it is turned off / uninstalled).
struct MovableOverride {
    bool                                                         applied = false;
    std::vector<std::pair<BlockMovableComponent*, MovementType>> saved;
};
MovableOverride& movableOverride() {
    static MovableOverride m;
    return m;
}
void restoreMovable() {
    auto& m = movableOverride();
    for (auto& [c, t] : m.saved) c->mMovementType = t;
    m.saved.clear();
    m.applied = false;
}
void syncMovable() {
    auto& m    = movableOverride();
    bool  want = cfg().enablePistonsMoveTileEntities && installed();
    if (want == m.applied) return;
    if (!want) {
        restoreMovable();
        return;
    }
    for (auto const* id : kJavaMovable) {
        auto const* b = bs::of(id);
        if (!b) continue;
        b->getBlockType().forEachBlockPermutation([&](Block const& perm) {
            if (auto* c = const_cast<BlockMovableComponent*>(perm.tryGetComponent<BlockMovableComponent>());
                c && c->mMovementType == MovementType::Immovable) {
                m.saved.emplace_back(c, c->mMovementType);
                c->mMovementType = MovementType::PushPull;
            }
            return true;
        });
    }
    m.applied = true;
}

LL_TYPE_INSTANCE_HOOK(
    LeptonPistonResolverHook,
    ll::memory::HookPriority::Low,
    PistonBlockActor,
    &PistonBlockActor::_checkAttachedBlocks,
    bool,
    BlockSource& region
) {
    if (!installed() || !isServerRegion(region) || !onServerThread()) return origin(region);
    syncMovable();
    auto const& c        = cfg();
    bool        anyRules = c.enableChainsConnectBlocks || c.enableIronRod || c.enablePistonsMoveTileEntities || c.pistonPushLimit != 12;
    bool        retracting = static_cast<PistonState>(mNewState) == PistonState::Retracting;
    if (!anyRules || (retracting && !mSticky)) return origin(region);

    BlockPos pistonPos = getPosition();
    int      facing    = faceOfStep(getFacingDir(region));
    Resolver res{
        region,
        pistonPos,
        retracting ? offset(pistonPos, facing, 2) : offset(pistonPos, facing),
        retracting ? opposite(facing) : facing,
        facing,
        !retracting,
        static_cast<size_t>(std::max(1, c.pistonPushLimit)),
    };
    bool ok  = res.resolve();
    bool use = res.usedLepton || c.pistonPushLimit != 12;
    if (!use) {
        bool v = origin(region);
        if (v && c.enablePistonsMoveTileEntities) scheduleDelayedUpdates(region, mAttachedBlocks.get(), res.dir);
        gTrace = {false, v, mAttachedBlocks.get(), mBreakBlocks.get()};
        return v;
    }
    auto& attached = mAttachedBlocks.get();
    auto& breaking = mBreakBlocks.get();
    attached.clear();
    breaking.clear();
    if (ok) {
        attached = res.push;
        breaking = res.destroy;
        if (c.enablePistonsMoveTileEntities) scheduleDelayedUpdates(region, attached, res.dir);
    }
    gTrace = {true, ok, attached, breaking};
    return ok;
}

// Java EndRodBlock placement: against an iron rod with the same facing, the new rod faces the other way.
void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    if (!installed()) return;
    auto& player = ev.self();
    int   dim    = static_cast<int>(player.getDimensionId());
    auto  pos    = ev.pos();
    later(1, [dim, pos] {
        auto* region = game::regionOf(dim);
        if (!region) return;
        auto const& b = region->getBlock(pos);
        if (!isIronRod(b)) return;
        int  f    = rodFacing(b);
        auto const& back = region->getBlock(offset(pos, opposite(f)));
        if (!isIronRod(back) || rodFacing(back) != f) return;
        if (auto const* flipped = bs::with(b, {{"minecraft:block_face", std::string(faceToName(opposite(f)))}}))
            game::setBlock(*region, pos, *flipped);
    });
}

ll::event::ListenerPtr gPlaced;

} // namespace

void onWorldClosedPistons() {
    // the client destroys the world's blocks: forget the overrides without touching them
    movableOverride().saved.clear();
    movableOverride().applied = false;
    delayed().clear();
}

void installPistons() {
    if (!gHooked) {
        LeptonPistonResolverHook::hook();
        gHooked = true;
    }
    gPlaced = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced);
}
void uninstallPistons() {
    if (gPlaced) ll::event::EventBus::getInstance().removeListener(gPlaced);
    gPlaced.reset();
    if (gHooked) {
        LeptonPistonResolverHook::unhook();
        gHooked = false;
    }
    delayed().clear();
    restoreMovable();
}

testing::PistonTrace const& testing::lastPiston() { return gTrace; }

int testing::movementTypeOf(std::string_view id, bool* blockEntity) {
    auto const* b = bs::of(id);
    if (!b) return -1;
    if (blockEntity) *blockEntity = hasBlockEntity(*b);
    return static_cast<int>(movementOf(*b));
}

std::optional<BlockPos> testing::chestPartnerOf(BlockSource& region, BlockPos const& pos) { return chestPartner(region, pos); }

} // namespace lp::feature::redstone
