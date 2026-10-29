// U7 newwoods saplings (docs/spec/newwoods.md §3.3): random ticks (block ticks with a random delay),
// growth, bone meal from players and dispensers.

#include <cmath>
#include <mutex>
#include <set>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/common/Brightness.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/FertilizerItem.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/BlockTickingQueue.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"

#include "core/GlobalPos.h"
#include "feature/newwoods/NewWoods.h"
#include "feature/newwoods/Runtime.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "mod/Log.h"

namespace lp::feature::newwoods {

namespace bs = game::blockstates;

bool isOurSapling(Block const& b) { return b.getTypeName().starts_with("lepton:") && hasTag(b, "lepton:nw_sapling"); }

int maxRandomTickDelay() {
    int  speed = 1;
    auto level = ll::service::getLevel();
    if (level) {
        auto const& rules = level->getGameRules().mGameRules.get();
        auto        idx   = static_cast<size_t>(GameRules::GameRulesIndex::RandomTickSpeed);
        if (idx < rules.size())
            if (auto const* v = std::get_if<int>(&rules[idx].mValue.get())) speed = *v;
    }
    if (speed <= 0) return 0;
    // Bedrock's speed 1 is Java's 3: 3 * speed random ticks per 4096 blocks per tick; a uniform delay
    // 1..max has the same mean (4096 / (3 * speed))
    return std::max(1, static_cast<int>(std::lround(2.0 * 4096.0 / (3.0 * speed))) - 1);
}

void scheduleTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    int maxDelay = maxRandomTickDelay();
    if (maxDelay <= 0) return;
    if (auto* chunk = region.getChunkAt(pos)) {
        Tick next{};
        if (chunk->getTickQueue().getNextUpdateForPos(pos, next)) return; // already scheduled
    }
    region.addToTickingQueue(pos, block, 1 + runtimeRandom().nextInt(maxDelay));
}

namespace {

bool growTree(BlockSource& region, BlockPos const& pos, Block const& sapling, JavaRandom& random) {
    auto const* tree = treeOfSapling(sapling.getTypeName());
    auto const* air  = bs::of("minecraft:air");
    if (!tree || !air || !moduleEnabled(tree->module)) return false;
    // Java TreeGrower#growTree: the sapling makes room, the tree is placed, or the sapling comes back
    game::setBlock(region, pos, *air);
    if (placeTree(region, pos, *tree, random, false)) {
        log().info("newwoods: {} tree grown at {} {} {}", tree->name, pos.x, pos.y, pos.z);
        return true;
    }
    game::setBlock(region, pos, sapling);
    return false;
}

void growthParticles(BlockSource& region, BlockPos const& pos) {
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    region.getLevel().spawnParticleEffect("minecraft:crop_growth_emitter", at, &region.getDimension());
}

} // namespace

bool advanceSapling(BlockSource& region, BlockPos const& pos, JavaRandom& random) {
    auto const& b = region.getBlock(pos);
    if (!isOurSapling(b)) return false;
    if (bs::getInt(b, "lepton:stage").value_or(0) == 0) {
        auto const* next = bs::with(b, {{"lepton:stage", 1}});
        return next && game::setBlock(region, pos, *next);
    }
    return growTree(region, pos, b, random);
}

void saplingRandomTick(BlockSource& region, BlockPos const& pos, JavaRandom& random) {
    auto const& b = region.getBlock(pos);
    if (!isOurSapling(b)) return;
    auto const* tree = treeOfSapling(b.getTypeName());
    if (!tree || !moduleEnabled(tree->module)) return;
    if (!region.hasChunksAt(pos, 1, false)) return; // Java: isAreaLoaded(pos, 1)
    BlockPos above{pos.x, pos.y + 1, pos.z};
    int      light = region._getRawBrightness(above, region.getDimension().mSkyDarken, true).mValue;
    if (light >= 9 && random.nextInt(7) == 0) advanceSapling(region, pos, random);
}

bool bonemealSapling(BlockSource& region, BlockPos const& pos, JavaRandom& random) {
    auto const& b = region.getBlock(pos);
    if (!isOurSapling(b)) return false;
    auto const* tree = treeOfSapling(b.getTypeName());
    if (!tree || !moduleEnabled(tree->module)) return false;
    growthParticles(region, pos);
    if (random.nextFloat() < 0.45f) advanceSapling(region, pos, random); // SaplingBlock#isBonemealSuccess
    return true;
}

// ---- block ticks -----------------------------------------------------------------------------------

namespace {

struct Deferred {
    std::mutex                mutex;
    std::set<core::GlobalPos> ticks;
};
Deferred& deferred() {
    static Deferred d;
    return d;
}

void blockTick(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (isOurSapling(b)) {
        saplingRandomTick(region, pos, runtimeRandom());
        auto const& now = region.getBlock(pos);
        if (isOurSapling(now)) scheduleTick(region, pos, now);
    } else if (isOurLeaves(b)) {
        leafTick(region, pos);
    }
}

LL_TYPE_INSTANCE_HOOK(
    QueuedTickHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::queuedTick,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    Random&         random
) {
    origin(region, pos, random);
    if (!isOurSapling(*this) && !isOurLeaves(*this)) return;
    if (region.getLevel().isClientSide()) return;
    if (!onServerThread()) {
        // ticks that run while chunks load come from worker threads: redo them on the server thread
        std::lock_guard lock(deferred().mutex);
        deferred().ticks.insert(game::globalPos(region, pos));
        return;
    }
    try {
        blockTick(region, pos);
    } catch (std::exception const& e) {
        log().error("newwoods: block tick failed: {}", e.what());
    }
}

/// Bone meal used by a player on a block.
LL_TYPE_INSTANCE_HOOK(
    BonemealUseHook,
    ll::memory::HookPriority::Normal,
    FertilizerItem,
    &FertilizerItem::$_useOn,
    InteractionResult,
    ItemStack&    instance,
    Actor&        entity,
    BlockPos      pos,
    uchar         face,
    HandSlot      handSlot,
    Vec3 const&   clickPos
) {
    if (!entity.isClientSide()) {
        auto& region = entity.getDimensionBlockSource();
        if (isOurSapling(region.getBlock(pos))) {
            if (bonemealSapling(region, pos, runtimeRandom())) {
                if (!entity.isCreative()) instance.remove(1);
                InteractionResult r{};
                r.mSuccess = true;
                r.mSwing   = true;
                return r;
            }
        }
    }
    return origin(instance, entity, pos, face, handSlot, clickPos);
}

/// Bone meal shot by a dispenser at the block in front of it.
LL_TYPE_INSTANCE_HOOK(
    BonemealDispenseHook,
    ll::memory::HookPriority::Normal,
    FertilizerItem,
    &FertilizerItem::$dispense,
    bool,
    BlockSource& region,
    Container&   container,
    int          slot,
    Vec3 const&  pos,
    uchar        face
) {
    if (!region.getLevel().isClientSide()) {
        BlockPos target = game::blockPosOf(pos);
        if (isOurSapling(region.getBlock(target)) && bonemealSapling(region, target, runtimeRandom())) {
            container.removeItem(slot, 1);
            return true;
        }
    }
    return origin(region, container, slot, pos, face);
}

} // namespace

void runDeferredTicks() {
    std::set<core::GlobalPos> ticks;
    {
        std::lock_guard lock(deferred().mutex);
        ticks.swap(deferred().ticks);
    }
    for (auto const& g : ticks) {
        auto* region = game::regionOf(g.dim);
        auto  pos    = game::blockPos(g);
        if (!region || !region->hasChunksAt(pos, 0, false)) continue;
        try {
            blockTick(*region, pos);
        } catch (std::exception const& e) {
            log().error("newwoods: block tick failed: {}", e.what());
        }
    }
}

void installSaplingHooks() {
    QueuedTickHook::hook();
    BonemealUseHook::hook();
    BonemealDispenseHook::hook();
}
void uninstallSaplingHooks() {
    QueuedTickHook::unhook();
    BonemealUseHook::unhook();
    BonemealDispenseHook::unhook();
}

} // namespace lp::feature::newwoods
