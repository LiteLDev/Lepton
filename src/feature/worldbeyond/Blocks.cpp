#include "feature/worldbeyond/Blocks.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "feature/worldbeyond/MyaliteColor.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"

namespace lp::feature::worldbeyond {

namespace {

/// The current set is published through an atomic pointer (lock-free reads on every thread).
/// Replaced sets are kept alive for the whole process: callers hold plain references across
/// nested calls and generation threads may still use the previous world's set (one small
/// struct per world load).
struct Cache {
    std::mutex                                   mutex;
    std::atomic<BlockSet const*>                 current{nullptr};
    std::vector<std::shared_ptr<BlockSet const>> all;
};
Cache& cache() {
    static Cache c;
    return c;
}
std::atomic<BlockType const*> gFastWeeds{nullptr}, gFastTwist{nullptr};

BlockType const* typeOf(char const* id) {
    auto t = BlockType::tryGetFromRegistry(HashedString(std::string(id)));
    return t ? t.as_ptr() : nullptr;
}

std::shared_ptr<BlockSet const> resolve() {
    auto s            = std::make_shared<BlockSet>();
    s->air            = game::blockWith("minecraft:air");
    s->obsidian       = game::blockWith("minecraft:obsidian");
    s->cryingObsidian = game::blockWith("minecraft:crying_obsidian");
    s->glowstone      = game::blockWith("minecraft:glowstone");
    s->spawner        = game::blockWith("minecraft:mob_spawner");
    s->chest          = game::blockWith("minecraft:chest");
    s->endStone       = game::blockWith("minecraft:end_stone");
    s->weeds          = game::blockWith(kChorusWeeds);
    s->twist          = game::blockWith(kChorusTwist);
    s->dusky          = game::blockWith(kDuskyMyalite);
    s->myalite        = game::blockWith(kMyalite);
    s->blazeLantern   = game::blockWith(kBlazeLantern);
    for (int i = 0; i < kHueSteps; ++i) s->crystal[i] = game::blockstates::of(kMyaliteCrystal, {{kHueState, i}});
    s->lavaType         = typeOf("minecraft:lava");
    s->flowingLavaType  = typeOf("minecraft:flowing_lava");
    s->waterType        = typeOf("minecraft:water");
    s->flowingWaterType = typeOf("minecraft:flowing_water");
    s->endStoneType     = typeOf("minecraft:end_stone");
    s->obsidianType     = typeOf("minecraft:obsidian");
    s->cryingObsType    = typeOf("minecraft:crying_obsidian");
    s->weedsType        = typeOf(kChorusWeeds);
    s->twistType        = typeOf(kChorusTwist);
    s->crystalType      = typeOf(kMyaliteCrystal);
    return s;
}

} // namespace

bool BlockSet::isLava(Block const& b) const {
    auto const* t = &b.getBlockType();
    return t == lavaType || t == flowingLavaType;
}
bool BlockSet::isWater(Block const& b) const {
    auto const* t = &b.getBlockType();
    return t == waterType || t == flowingWaterType;
}
bool BlockSet::isEndStone(Block const& b) const { return &b.getBlockType() == endStoneType; }
bool BlockSet::isChorus(Block const& b) const {
    auto const* t = &b.getBlockType();
    return t && (t == weedsType || t == twistType);
}
bool BlockSet::isCrystal(Block const& b) const { return crystalType && &b.getBlockType() == crystalType; }

BlockSet const& blocks() {
    auto& c = cache();
    if (auto const* s = c.current.load(std::memory_order_acquire)) return *s;
    std::lock_guard lock(c.mutex);
    if (auto const* s = c.current.load(std::memory_order_acquire)) return *s;
    auto set = resolve(); // once per world (resetBlocks), even when our pack is missing
    c.all.push_back(set);
    gFastWeeds.store(set->weedsType);
    gFastTwist.store(set->twistType);
    c.current.store(set.get(), std::memory_order_release);
    return *set;
}

bool isChorusType(BlockType const* type) {
    return type && (type == gFastWeeds.load(std::memory_order_relaxed) || type == gFastTwist.load(std::memory_order_relaxed));
}

void resetBlocks() {
    auto&           c = cache();
    std::lock_guard lock(c.mutex);
    c.current.store(nullptr, std::memory_order_release); // the old set stays alive in `all`
    gFastWeeds.store(nullptr);
    gFastTwist.store(nullptr);
}

Block const* crystalFor(BlockPos const& pos) {
    return blocks().crystal[myaliteHueStep(pos.x, pos.y, pos.z)];
}

bool hasWater(BlockSource& region, BlockPos const& pos) {
    auto const& b = blocks();
    return b.isWater(region.getBlock(pos)) || b.isWater(region.getExtraBlock(pos));
}

} // namespace lp::feature::worldbeyond
