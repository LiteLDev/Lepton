// U7 newwoods leaves (docs/spec/newwoods.md §3.1, §3.2, §3.9): decay, Java loot with fortune, and
// the falling leaves of the trumpet trees.

#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacketPayload.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/ResourceDropsContext.h"

#include "core/GlobalPos.h"
#include "feature/newwoods/NewWoods.h"
#include "feature/newwoods/Palette.h"
#include "feature/newwoods/Runtime.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::newwoods {

namespace bs = game::blockstates;

namespace {

constexpr int kDirs[6][3] = {
    {0,  -1, 0 },
    {0,  1,  0 },
    {0,  0,  -1},
    {0,  0,  1 },
    {-1, 0,  0 },
    {1,  0,  0 }
};

BlockPos step(BlockPos const& p, int d) { return BlockPos{p.x + kDirs[d][0], p.y + kDirs[d][1], p.z + kDirs[d][2]}; }

bool inWorld(BlockSource& r, BlockPos const& p) { return p.y >= r.getMinHeight() && p.y < r.getMaxHeight(); }

bool persistent(Block const& b) { return bs::getBool(b, "lepton:persistent").value_or(true); }

// Java loot tables (fortune 0..4: table_bonus clamps to the last entry)
constexpr double kAncientSapling[] = {0.05, 0.0625, 0.083333336, 0.1};
constexpr double kBlossomSapling[] = {0.0046875, 0.0052083335, 0.005859375, 0.0078125, 0.01875};
constexpr double kSticks[]         = {0.02, 0.022222223, 0.025, 0.033333335, 0.1};
constexpr double kFruit[]          = {0.005, 0.0055555557, 0.00625, 0.008333334, 0.025};

template <size_t N>
double bonus(double const (&table)[N], int fortune) {
    return table[std::min<size_t>(static_cast<size_t>(std::max(0, fortune)), N - 1)];
}

struct CheckQueue {
    std::mutex                mutex;
    std::set<core::GlobalPos> changed; ///< positions where a log or leaf changed
};
CheckQueue& checks() {
    static CheckQueue q;
    return q;
}

} // namespace

bool leafSupported(BlockSource& region, BlockPos const& pos) {
    // BFS through leaves: a log next to a leaf at most 5 leaf-steps away (Java distance <= 6)
    std::deque<std::pair<BlockPos, int>> todo{{pos, 0}};
    std::set<std::tuple<int, int, int>>  seen{{pos.x, pos.y, pos.z}};
    while (!todo.empty()) {
        auto [p, depth] = todo.front();
        todo.pop_front();
        for (int d = 0; d < 6; ++d) {
            BlockPos n = step(p, d);
            if (!inWorld(region, n)) continue;
            if (!region.hasChunksAt(n, 0, false)) return true; // unknown: keep the leaf
            auto const& b = region.getBlock(n);
            if (isLogLike(b)) return true;
            if (depth + 1 <= 5 && isAnyLeaves(b) && seen.insert({n.x, n.y, n.z}).second) todo.emplace_back(n, depth + 1);
        }
    }
    return false;
}

std::vector<std::pair<std::string, int>>
leafDrops(std::string_view leavesId, bool shearsOrSilk, int fortune, float explosionRadius, JavaRandom& random) {
    std::vector<std::pair<std::string, int>> out;
    auto const*                              tree = treeOfLeaves(leavesId);
    if (!tree) return out;
    if (shearsOrSilk) {
        out.emplace_back(std::string(leavesId), 1);
        return out;
    }
    auto survives = [&] { return explosionRadius <= 0 || random.nextFloat() <= 1.0f / explosionRadius; };
    double sapling = tree->ancient ? bonus(kAncientSapling, fortune) : bonus(kBlossomSapling, fortune);
    if (survives() && random.nextFloat() < sapling) out.emplace_back(std::string(tree->sapling), 1);
    if (random.nextFloat() < bonus(kSticks, fortune)) {
        int n = 1 + random.nextInt(2), kept = 0;
        for (int i = 0; i < n; ++i)
            if (survives()) ++kept; // explosion_decay: each item survives with 1 / radius
        if (kept) out.emplace_back("minecraft:stick", kept);
    }
    if (tree->ancient && survives() && random.nextFloat() < bonus(kFruit, fortune)) out.emplace_back("lepton:ancient_fruit", 1);
    return out;
}

void queueLeafCheck(BlockSource& region, BlockPos const& pos) {
    auto& q = checks();
    std::lock_guard lock(q.mutex);
    q.changed.insert(game::globalPos(region, pos));
}

int runLeafChecks() {
    std::set<core::GlobalPos> changed;
    {
        auto&           q = checks();
        std::lock_guard lock(q.mutex);
        changed.swap(q.changed);
    }
    // Re-check our leaves around the changes, spreading through our leaves up to 7 steps (a leaf
    // whose distance rose to 7 is at most 7 leaf-steps from the change; Java's distance update).
    // The spread goes through our leaves only, so changes in vanilla forests cost nothing.
    int                                            unsupported = 0;
    std::map<int, std::set<std::tuple<int, int, int>>> seen; // per dimension, shared by all changes
    for (auto const& g : changed) {
        auto* region = game::regionOf(g.dim);
        if (!region) continue;
        auto&                                seenDim = seen[g.dim];
        std::deque<std::pair<BlockPos, int>> todo;
        BlockPos                             origin = game::blockPos(g);
        for (int d = 0; d < 6; ++d) todo.emplace_back(step(origin, d), 1);
        while (!todo.empty()) {
            auto [p, depth] = todo.front();
            todo.pop_front();
            if (!inWorld(*region, p) || !region->hasChunksAt(p, 0, false)) continue;
            auto const& b = region->getBlock(p);
            if (!isOurLeaves(b)) continue;
            if (!seenDim.insert({p.x, p.y, p.z}).second) continue;
            if (!persistent(b) && !leafSupported(*region, p)) {
                scheduleTick(*region, p, b);
                ++unsupported;
            }
            if (depth < 7)
                for (int d = 0; d < 6; ++d) todo.emplace_back(step(p, d), depth + 1);
        }
    }
    return unsupported;
}

void leafTick(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (!isOurLeaves(b) || persistent(b)) return;
    if (leafSupported(region, pos)) return;
    BlockChangeContext ctx{};
    region.getLevel().destroyBlock(region, pos, true, ctx);
}

// ---- falling leaves (Java BlossomLeavesBlock#animateTick) ----------------------------------------

namespace {

/// The particle computes Java's wind from the world time itself (tools/pack/newwoods.ps1).
void sendLeafParticle(Player& player, BlockPos const& pos, std::string const& effect) {
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    SpawnParticleEffectPacket pkt(
        SpawnParticleEffectPacketPayload(at, effect, static_cast<uchar>(static_cast<int>(player.getDimensionId())), std::nullopt)
    );
    pkt.sendTo(player);
}

} // namespace

void leafParticlesTick(Level& level) {
    auto const& c = config();
    if (!c.enableBlossomTrees || !c.dropLeafParticles) return;
    auto pal = palette();
    if (!pal || pal->blossomLeafTypes.empty()) return;
    // players with no trumpet leaves around are sampled at a tenth of the rate until one shows up
    // again (each block is still looked at every few seconds), which keeps the server thread cheap
    static std::unordered_map<int64_t, uint64_t> lastHit;
    static uint64_t                               tick = 0;
    ++tick;
    auto& r = runtimeRandom();
    level.forEachPlayer([&](Player& p) {
        if (p.isSimulated()) return true;
        auto&    region = p.getDimensionBlockSource();
        BlockPos base   = p.getFeetBlockPos();
        int      minY = region.getMinHeight(), maxY = region.getMaxHeight();
        auto     uid    = p.getOrCreateUniqueID().rawID;
        auto     it     = lastHit.try_emplace(uid, tick).first;
        int      count  = tick - it->second > 200 ? 67 : 667;
        for (int radius : {16, 32}) {
            for (int i = 0; i < count; ++i) {
                BlockPos q{
                    base.x + r.nextInt(radius) - r.nextInt(radius),
                    base.y + r.nextInt(radius) - r.nextInt(radius),
                    base.z + r.nextInt(radius) - r.nextInt(radius)};
                if (q.y <= minY || q.y >= maxY) continue;
                auto const& b     = region.getBlock(q);
                auto        found = pal->blossomLeafTypes.find(&b.getBlockType());
                if (found == pal->blossomLeafTypes.end()) continue;
                it->second = tick;
                if (r.nextInt(5) != 0) continue;
                if (!region.getBlock(BlockPos{q.x, q.y - 1, q.z}).isAir()) continue;
                sendLeafParticle(p, q, found->second);
            }
        }
        return true;
    });
    if (lastHit.size() > 256) lastHit.clear();
}

// ---- drops ---------------------------------------------------------------------------------------

namespace {

LL_TYPE_INSTANCE_HOOK(
    LeafDropsHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::spawnResources,
    void,
    BlockSource&                region,
    BlockPos const&             pos,
    Block const&                block,
    IRandom&                    random,
    ResourceDropsContext const& ctx,
    Actor const*                actor
) {
    if (!isOurLeaves(block) || region.getLevel().isClientSide()) {
        origin(region, pos, block, random, ctx, actor);
        return;
    }
    try {
        ItemStack const& tool = ctx.mUsedItem;
        bool        shear = !tool.isNull() && tool.getTypeName() == "minecraft:shears";
        bool        silk  = ctx.isUsingSilkTouch();
        int         fortune = ctx.getMiningLootBonusLevel();
        float       radius  = ctx.mCause == ResourceDropsCause::Explosion ? static_cast<float>(ctx.mExplosionRadius) : 0.0f;
        auto        drops   = leafDrops(block.getTypeName(), shear || silk, fortune, radius, runtimeRandom());
        Vec3        at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
        for (auto const& [id, count] : drops) {
            auto stack = game::makeItem(id, count);
            if (!stack.isNull()) region.getLevel().getSpawner().spawnItem(region, stack, nullptr, at, 10);
        }
    } catch (std::exception const& e) {
        log().error("newwoods: leaf drops failed: {}", e.what());
    }
}

} // namespace

void installLeafHooks() { LeafDropsHook::hook(); }
void uninstallLeafHooks() { LeafDropsHook::unhook(); }

} // namespace lp::feature::newwoods
