// U17 placement: right-click rules on blocks: ReplaceScaffolding and EnhancedLadders' dropping
// down. Both decide during the interaction (so the normal use goes on when they don't apply) and
// edit the world on the next tick (a cancelled interaction rolls back block changes).

#include "feature/placement/Placement.h"

#include <cmath>
#include <optional>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActorType.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::placement {

namespace {

constexpr std::string_view kScaffolding = "minecraft:scaffolding";

bool isScaffolding(Block const& b) { return b.getTypeName() == kScaffolding; }

bool loaded(BlockSource& region, BlockPos const& p) {
    return p.y >= region.getMinHeight() && p.y < region.getMaxHeight() && region.hasChunksAt(p, 0, false);
}

/// Quark's `quark:cannot_replace_scaffolding` block tag.
bool cannotReplaceScaffolding(Block const& b) {
    auto const& n = b.getTypeName();
    if (n.ends_with("_door") || n == "minecraft:wooden_door" || game::shapes::kindOf(b) == game::shapes::Kind::Door) return true;
    static constexpr std::string_view kIds[] = {
        "minecraft:bed",
        "minecraft:sunflower",
        "minecraft:lilac",
        "minecraft:rose_bush",
        "minecraft:peony",
        "minecraft:pitcher_plant",
        "minecraft:double_plant",
        "minecraft:tall_grass",
        "minecraft:large_fern",
        "minecraft:barrier",
    };
    for (auto id : kIds)
        if (n == id) return true;
    return false;
}

// ---- ReplaceScaffolding ------------------------------------------------------------------------

BlockPos lastInLineRecursive(BlockSource& region, BlockPos const& start, int dir, int bounces) {
    BlockPos    curr      = start;
    std::string currBlock = region.getBlock(start).getTypeName();
    while (true) {
        BlockPos test = step(curr, dir);
        if (!loaded(region, test)) break;
        if (region.getBlock(test).getTypeName() == currBlock) curr = test;
        else break;
    }
    if (!(curr == start) && bounces > 0) {
        std::optional<BlockPos> maxDist;
        double                  maxDistVal = -1;
        for (int d2 = 0; d2 < 6; ++d2) {
            if ((d2 >> 1) == (dir >> 1)) continue; // same axis
            BlockPos bounceStart = step(curr, d2);
            if (loaded(region, bounceStart) && region.getBlock(bounceStart).getTypeName() == currBlock) {
                BlockPos test = lastInLineRecursive(region, bounceStart, d2, bounces - 1);
                double   val  = std::abs(test.x - curr.x) + std::abs(test.y - curr.y) + std::abs(test.z - curr.z);
                // Quark never updates maxDistVal, so the last matching direction wins.
                if (val > maxDistVal) maxDist = test;
            }
        }
        if (maxDist) curr = *maxDist;
    }
    return curr;
}

std::optional<BlockPos> lastInLineOrNull(BlockSource& region, BlockPos const& start, int dir) {
    BlockPos last = lastInLineRecursive(region, start, dir, std::max(cfg().maxBounces, 0));
    if (last == start) return std::nullopt;
    return last;
}

} // namespace

BlockPos scaffoldingTarget(BlockSource& region, BlockPos const& start, int clickDir) {
    if (auto r = lastInLineOrNull(region, start, clickDir)) return *r;
    if (clickDir != Up)
        if (auto r = lastInLineOrNull(region, start, Up)) return *r;
    for (int h : {North, South, West, East}) // Quark's MiscUtil.HORIZONTALS
        if (h != clickDir)
            if (auto r = lastInLineOrNull(region, start, h)) return *r;
    if (clickDir != Down)
        if (auto r = lastInLineOrNull(region, start, Down)) return *r;
    return start;
}

namespace {

/// Decides now, edits next tick. True when the click is taken.
bool replaceScaffolding(Player& player, BlockSource& region, BlockPos const& pos, int face, ItemStack const& stack) {
    if (!cfg().enableReplaceScaffolding || player.isSneaking()) return false;
    if (!isScaffolding(region.getBlock(pos))) return false;
    auto const* block = blockOfItem(stack);
    if (!block || cannotReplaceScaffolding(*block) || isScaffolding(*block)) return false;
    if (block->getBlockType().getBlockEntityType() != BlockActorType::Undefined) return false;

    BlockPos     last  = scaffoldingTarget(region, pos, face);
    // Quark: a hit at (0.5, 1, 0.5) in world space, i.e. the bottom half of the target
    Block const* state = placementState(region, last, player, block->getTypeName(), face, Vec3{0.5f, 0.0f, 0.5f});
    if (!state) return false;
    state = &lockedState(region, last, player, *state, 0);
    if (!state->canSurvive(region, last)) return false;
    if (isScaffolding(region.getBlock(step(last, Up))) && !state->canProvideFullSupport(Up)) return false;

    std::string itemId = game::itemId(stack);
    int         dim    = static_cast<int>(region.getDimensionId());
    auto        uid    = player.getOrCreateUniqueID();
    Block const* place = state;
    later(1, [dim, last, uid, place, itemId] {
        auto* r     = game::regionOf(dim);
        auto  level = ll::service::getLevel();
        if (!r || !level) return;
        auto* p = level->getPlayer(uid);
        if (!p || !isScaffolding(r->getBlock(last))) return;
        if (!consumeSelected(*p, itemId)) return; // the item left the hand meanwhile
        if (!game::setBlock(*r, last, *place)) return;
        game::shapes::updateShape(*r, last);
        playPlaceSound(*r, last, *place, *p);
        if (!p->isCreative()) {
            ItemStack give = game::makeItem(kScaffolding);
            if (!p->addAndRefresh(give)) p->drop(give, false);
        }
        log().info("placement: replaced scaffolding at {} {} {} with {}", last.x, last.y, last.z, place->getTypeName());
    });
    return true;
}

// ---- EnhancedLadders: dropping down -------------------------------------------------------------

bool dropLadder(Player& player, BlockSource& region, BlockPos const& clicked, ItemStack const& stack) {
    if (!cfg().enableEnhancedLadders || !cfg().allowDroppingDown) return false;
    auto const* block = blockOfItem(stack);
    if (!block || !isLadder(*block)) return false;
    std::string type = block->getTypeName();
    if (region.getBlock(clicked).getTypeName() != type) return false;

    // Quark cancels the click whenever the clicked block is that ladder.
    BlockPos pos = clicked;
    while (true) {
        BlockPos down = step(pos, Down);
        if (down.y < region.getMinHeight() || !loaded(region, down)) return true;
        auto const& below = region.getBlock(down);
        if (below.getTypeName() == type) {
            pos = down;
            continue;
        }
        bool water = below.getTypeName() == "minecraft:water" || below.getTypeName() == "minecraft:flowing_water";
        if (!water && !below.isAir()) return true;
        auto const& copy = region.getBlock(pos);
        if (!ladderSurvives(region, down, ladderFacing(copy))) return true;
        int  dim    = static_cast<int>(region.getDimensionId());
        auto uid    = player.getOrCreateUniqueID();
        auto itemId = game::itemId(stack);
        Block const* place = &copy;
        later(1, [dim, down, uid, place, water, itemId, type] {
            auto* r     = game::regionOf(dim);
            auto  level = ll::service::getLevel();
            if (!r || !level) return;
            auto*       p   = level->getPlayer(uid);
            auto const& cur = r->getBlock(down);
            bool        ok  = cur.isAir() || cur.getTypeName() == "minecraft:water" || cur.getTypeName() == "minecraft:flowing_water";
            if (!p || !ok) return;
            if (!consumeSelected(*p, itemId)) return; // the item left the hand meanwhile
            Block const* water = cur.isAir() ? nullptr : &cur;
            if (!game::setBlock(*r, down, *place)) return;
            if (water) r->setExtraBlock(down, *water, 3);
            playPlaceSound(*r, down, *place, *p);
            log().info("placement: dropped {} down to {} {} {}", type, down.x, down.y, down.z);
        });
        (void)water;
        return true;
    }
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& stack = ev.item();
    if (game::isEmpty(stack)) return;
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.blockPos();
    int   face   = static_cast<int>(ev.face());
    if (dropLadder(player, region, pos, stack) || replaceScaffolding(player, region, pos, face, stack)) ev.cancel();
}

} // namespace

class PlacementInteractModule {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract);
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace lp::feature::placement

LP_REGISTER_MODULE(lp::feature::placement::PlacementInteractModule);
