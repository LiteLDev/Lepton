// U17 placement, EnhancedLadders (server side): free-standing vanilla ladders (LadderBlock hooks)
// and dropping ladders down a column. Lepton ladders (`lepton:ladder`, U6/U5) get the
// free-standing rule from U6, which reads this unit's config. Sliding and inventory sneak are
// client-side (src/client/placement).

#include "feature/placement/Placement.h"

#include <algorithm>

#include "ll/api/memory/Hook.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/LadderBlock.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::placement {

namespace bs = game::blockstates;

bool isLadder(Block const& b) { return b.getTypeName() == "minecraft:ladder" || hasTag(b, "lepton:ladder"); }

int ladderFacing(Block const& b) {
    if (b.getTypeName() == "minecraft:ladder") return bs::getInt(b, "facing_direction").value_or(North);
    auto name = bs::getString(b, "minecraft:block_face").value_or("north");
    for (int d = 0; d < 6; ++d)
        if (dirName(d) == name) return d;
    return North;
}

Block const* withLadderFacing(Block const& b, int facing) {
    if (b.getTypeName() == "minecraft:ladder") return bs::with(b, {{"facing_direction", facing}});
    return bs::with(b, {{"minecraft:block_face", std::string(dirName(facing))}});
}

bool freestandingOn() { return cfg().enableEnhancedLadders && cfg().allowFreestanding; }

/// Java LadderBlock#canSurvive: a sturdy wall behind (facing = away from the wall).
bool ladderWallOk(BlockSource& region, BlockPos const& pos, int facing) {
    if (!isHorizontal(facing)) return false;
    auto const& wall = region.getBlock(step(pos, oppositeDir(facing)));
    if (wall.isAir() || isLadder(wall)) return false;
    return wall.canProvideFullSupport(static_cast<uchar>(facing));
}

/// Quark's canSurviveTweak: the wall, or (free-standing) a ladder of the same facing above.
bool ladderSurvives(BlockSource& region, BlockPos const& pos, int facing) {
    if (ladderWallOk(region, pos, facing)) return true;
    if (!freestandingOn() || !isHorizontal(facing)) return false;
    auto const& above = region.getBlock(step(pos, Up));
    return isLadder(above) && ladderFacing(above) == facing;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    LadderCanSurviveHook,
    ll::memory::HookPriority::Normal,
    LadderBlock,
    &LadderBlock::$canSurvive,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    if (origin(region, pos)) return true;
    if (!freestandingOn()) return false;
    auto const& b = region.getBlock(pos);
    int         f = b.getTypeName() == "minecraft:ladder" ? ladderFacing(b) : -1;
    if (f < 0) return false;
    return ladderSurvives(region, pos, f);
}

/// Java LadderBlock#getStateForPlacement with the free-standing rule: the clicked face first,
/// then the nearest looking directions (horizontal), facing = opposite.
std::optional<int> placementFacing(BlockSource& region, BlockPos const& pos, int face, float pitch, float yaw) {
    if (isHorizontal(face)) {
        auto const& clicked = region.getBlock(step(pos, oppositeDir(face)));
        if (isLadder(clicked) && ladderFacing(clicked) == face) return std::nullopt; // in front of a ladder
    }
    auto dirs = nearestLookingDirections(pitch, yaw);
    // BlockPlaceContext#getNearestLookingDirections: the clicked face's opposite comes first
    auto it = std::find(dirs.begin(), dirs.end(), oppositeDir(face));
    if (it != dirs.end()) std::rotate(dirs.begin(), it, it + 1);
    for (int d : dirs) {
        if (!isHorizontal(d)) continue;
        if (ladderSurvives(region, pos, oppositeDir(d))) return oppositeDir(d);
    }
    return std::nullopt;
}

LL_TYPE_INSTANCE_HOOK(
    LadderMayPlaceHook,
    ll::memory::HookPriority::Normal,
    LadderBlock,
    &LadderBlock::$mayPlace,
    bool,
    BlockSource&    region,
    BlockPos const& pos,
    uchar           face
) {
    bool orig = origin(region, pos, face);
    if (orig || !freestandingOn()) return orig;
    if (isHorizontal(face)) {
        auto const& clicked = region.getBlock(step(pos, oppositeDir(face)));
        if (isLadder(clicked) && ladderFacing(clicked) == face) return false; // in front of a ladder
    }
    // Without the placer's view: any horizontal facing that would survive.
    for (int d = North; d <= East; ++d)
        if (ladderSurvives(region, pos, d)) return true;
    return false;
}

LL_TYPE_INSTANCE_HOOK(
    LadderPlacementHook,
    ll::memory::HookPriority::Normal,
    LadderBlock,
    &LadderBlock::$getPlacementBlock,
    Block const&,
    Actor const&    by,
    BlockPos const& pos,
    uchar           face,
    Vec3 const&     clickPos,
    int             itemValue
) {
    Block const& orig = origin(by, pos, face, clickPos, itemValue);
    if (!freestandingOn() || by.isClientSide()) return orig;
    auto& region = by.getDimensionBlockSource();
    if (ladderWallOk(region, pos, ladderFacing(orig))) return orig;
    auto f = placementFacing(region, pos, face, by.getRotation().x, by.getRotation().y);
    if (!f) return orig;
    if (auto const* b = withLadderFacing(orig, *f)) return *b;
    return orig;
}

LL_TYPE_INSTANCE_HOOK(
    LadderNeighborHook,
    ll::memory::HookPriority::Normal,
    LadderBlock,
    &LadderBlock::$neighborChanged,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    BlockPos const& neighborPos
) {
    if (!freestandingOn() || region.getLevel().isClientSide()) return origin(region, pos, neighborPos);
    auto const& b = region.getBlock(pos);
    if (b.getTypeName() != "minecraft:ladder") return origin(region, pos, neighborPos);
    int facing = ladderFacing(b);
    if (ladderSurvives(region, pos, facing)) return; // free-standing: keep it
    // Quark: an update from above schedules a tick, so the column falls like bamboo.
    if (neighborPos == step(pos, Up) && !ladderWallOk(region, pos, facing)) {
        int dim = static_cast<int>(region.getDimensionId());
        later(1, [dim, pos] {
            auto* r = game::regionOf(dim);
            if (!r) return;
            auto const& cur = r->getBlock(pos);
            if (cur.getTypeName() != "minecraft:ladder" || ladderSurvives(*r, pos, ladderFacing(cur))) return;
            BlockChangeContext ctx{};
            r->getLevel().destroyBlock(*r, pos, true, ctx);
        });
        return;
    }
    origin(region, pos, neighborPos);
}

} // namespace

class EnhancedLaddersModule {
public:
    void install() {
        LadderCanSurviveHook::hook();
        LadderMayPlaceHook::hook();
        LadderPlacementHook::hook();
        LadderNeighborHook::hook();
    }
    void uninstall() {
        LadderCanSurviveHook::unhook();
        LadderMayPlaceHook::unhook();
        LadderPlacementHook::unhook();
        LadderNeighborHook::unhook();
    }
};

} // namespace lp::feature::placement

LP_REGISTER_MODULE(lp::feature::placement::EnhancedLaddersModule);
