// Selftests of U8 storage (docs/spec/storage.md §8). U8 owns dx 800..899.

#include <cmath>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/ComposterBlock.h"
#include "mc/world/level/block/DispenserBlock.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/level/block/actor/DispenserBlockActor.h"
#include "mc/world/level/block/actor/BeaconBlockActor.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/storage/Storage.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
namespace bs = lp::game::blockstates;
namespace st = lp::feature::storage;
using Facing = ScriptModuleMinecraft::ScriptFacing;
using GP     = lp::core::GlobalPos;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

GP off(GP p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}
BlockPos bp(GP const& p) { return lp::game::blockPos(p); }

bool put(Context& ctx, GP const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), bp(p), *b);
}
Block const& at(Context& ctx, GP const& p) { return regionOf(ctx).getBlock(bp(p)); }
std::string  idAt(Context& ctx, GP const& p) { return at(ctx, p).getTypeName(); }
bool         flag(Block const& b, char const* s) { return bs::getBool(b, s).value_or(false); }

/// Clears a box of blocks (and remembers them for the cleanup).
void clearBox(Context& ctx, GP const& a, int w, int h, int d) {
    for (int x = 0; x < w; ++x)
        for (int y = 0; y < h; ++y)
            for (int z = 0; z < d; ++z) {
                auto p = off(a, x, y, z);
                ctx.cleanupBlock(p);
                lp::game::clearBlock(regionOf(ctx), bp(p));
            }
}

void stepAside(Context& ctx, GP const& p) {
    ctx.player().teleport(Vec3{p.x + 0.5f, static_cast<float>(p.y), p.z + 5.5f}, DimensionType(p.dim));
}

bool hasProperty(std::string_view id, BlockProperty prop) {
    auto const* b = bs::of(id);
    return b && (static_cast<uint64_t>(b->getBlockType().mProperties) & static_cast<uint64_t>(prop)) != 0;
}

Item const* itemOf(std::string_view id) {
    static std::vector<ItemStack> keep;
    keep.push_back(lp::game::makeItem(id));
    return keep.back().isNull() ? nullptr : keep.back().mItem.get();
}

std::set<std::string> loadedRecipes() {
    std::set<std::string> ids;
    auto                  level = ll::service::getLevel();
    if (!level) return ids;
    for (auto const& [tag, byId] : level->getRecipes().mRecipes.get())
        for (auto const& [id, r] : byId) ids.insert(id);
    return ids;
}

int countItems(Context& ctx, GP const& p, std::string_view id) {
    AABB box{
        Vec3{p.x - 1.5f, p.y - 1.5f, p.z - 1.5f},
        Vec3{p.x + 2.5f, p.y + 2.5f, p.z + 2.5f}
    };
    int n = 0;
    for (auto* a : lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item")) {
        auto const& stack = static_cast<ItemActor*>(a)->mItem.get();
        if (lp::game::itemId(stack) == id) n += stack.mCount;
    }
    return n;
}
void killItems(Context& ctx, GP const& p) {
    AABB box{
        Vec3{p.x - 3.f, p.y - 3.f, p.z - 3.f},
        Vec3{p.x + 4.f, p.y + 4.f, p.z + 4.f}
    };
    for (auto* a : lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item")) lp::game::entities::despawn(*a);
}
} // namespace

// ---- content ------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("storage.content", [](Context& ctx) {
    int missing = 0;
    for (auto id : st::contentIds()) {
        bool ok = bs::of(id) != nullptr || !lp::game::makeItem(id).isNull();
        if (!ok) {
            ++missing;
            ctx.check(false, "registered: " + std::string(id));
        }
    }
    ctx.check(st::contentIds().size() == 82, "82 blocks/items, got " + std::to_string(st::contentIds().size()));
    ctx.check(missing == 0, std::to_string(missing) + " missing");
    for (auto id : {"lepton:rope", "lepton:clear_shard", "lepton:red_shard", "lepton:dirty_shard"})
        ctx.check(!lp::game::makeItem(id).isNull(), std::string("item ") + id);
    auto const* shard = itemOf("lepton:magenta_shard");
    ctx.check(shard && shard->hasTag(HashedString("lepton:shards")), "shards carry lepton:shards");
    auto const* log = itemOf("minecraft:oak_log");
    ctx.check(log && log->hasTag(HashedString("minecraft:logs")), "oak log is #minecraft:logs");
    auto const* slab = itemOf("minecraft:spruce_slab");
    ctx.check(slab && slab->hasTag(HashedString("minecraft:wooden_slabs")), "spruce slab is #minecraft:wooden_slabs");
    ctx.check(bs::of("lepton:white_stool", {{"lepton:big", true}, {"lepton:sat_in", true}}) != nullptr, "stool states");
    ctx.check(bs::of("lepton:cut_vine", {{"lepton:up", true}, {"lepton:north", true}}) != nullptr, "cut vine states");
    ctx.check(st::plants::pottedFor("minecraft:wheat_seeds") == "lepton:potted_wheat", "wheat seeds pot");
    ctx.check(st::plants::plantOf("lepton:potted_cave_vines") == "minecraft:glow_berries", "cave vines plant");
});

LP_REGISTER_SELFTEST("storage.recipes", [](Context& ctx) {
    auto loaded = loadedRecipes();
    ctx.require(!loaded.empty(), "the level has recipes");
    bool u3 = !lp::game::makeItem("lepton:limestone").isNull();
    int  missing = 0;
    for (auto id : st::recipeIds()) {
        std::string s(id);
        bool        other = s.ends_with("_from_limestone") || s.ends_with("_from_jasper") || s.ends_with("_from_shale")
                    || s.ends_with("_from_permafrost");
        if (other && !u3) {
            ctx.check(!loaded.contains(s), "no U3 stone tool recipe without U3: " + s);
            continue;
        }
        if (!loaded.contains(s)) {
            ++missing;
            ctx.check(false, "recipe loaded: " + s);
        }
    }
    ctx.check(missing == 0, std::to_string(missing) + " recipes missing");
    ctx.check(!loaded.contains("minecraft:nether_wart_block"), "the 3x3 nether wart block recipe gives way to the sack");
    ctx.check(loaded.contains("lepton:storage_nether_wart_block_4x4"), "2x2 nether wart block");
});

LP_REGISTER_SELFTEST("storage.gates", [](Context& ctx) {
    auto& c     = lp::config::mut<st::StorageConfig>();
    auto  saved = c;
    ctx.check(st::contentEnabled("lepton:apple_crate") && st::recipeEnabled("lepton:storage_apple_crate"), "on by default");
    c.enableAppleCrate = false;
    ctx.check(!st::contentEnabled("lepton:apple_crate"), "apple crate off");
    ctx.check(!st::recipeEnabled("lepton:storage_apple_crate") && !st::recipeEnabled("lepton:storage_apple_crate_uncompress"), "its recipes off");
    ctx.check(st::contentEnabled("lepton:potato_crate"), "other crates stay");
    c.enableCompressedBlocks = false;
    ctx.check(!st::contentEnabled("lepton:potato_crate"), "module off");
    ctx.check(st::recipeEnabled("minecraft:nether_wart_block"), "vanilla nether wart block back with the module off");
    c                      = saved;
    c.enableUtilityRecipes = false;
    ctx.check(!st::recipeEnabled("lepton:storage_easy_sticks"), "utility module off");
    c                      = saved;
    c.easySticks           = false;
    ctx.check(!st::recipeEnabled("lepton:storage_easy_sticks") && st::recipeEnabled("lepton:storage_easy_hopper"), "per-option gate");
    c                  = saved;
    c.enableGlassShard = false;
    ctx.check(!st::contentEnabled("lepton:clear_shard") && !st::recipeEnabled("lepton:storage_mixed_dirty_glass"), "glass shards off");
    ctx.check(st::shardFor("minecraft:glass").empty(), "no shards while off");
    c = saved;
    ctx.check(st::shardFor("minecraft:light_blue_stained_glass") == "lepton:light_blue_shard", "stained glass shard");
    ctx.check(st::contentEnabled("minecraft:stone"), "foreign ids untouched");
});

// ---- CompressedBlocks / UtilityRecipes ------------------------------------------------------------

LP_REGISTER_SELFTEST("storage.fuel", [](Context& ctx) {
    float ref = FurnaceBlockActor::getBurnDuration(lp::game::makeItem("minecraft:coal"), 200.f);
    ctx.require(ref > 0.f, "coal burns");
    auto ratio = [&](char const* id) { return FurnaceBlockActor::getBurnDuration(lp::game::makeItem(id), 200.f) / ref; };
    ctx.check(std::abs(ratio("lepton:charcoal_block") - 10.f) < 0.01f, "charcoal block = 16000 ticks: " + std::to_string(ratio("lepton:charcoal_block")));
    ctx.check(std::abs(ratio("lepton:blaze_lantern") - 15.f) < 0.01f, "blaze lantern = 24000 ticks");
    ctx.check(std::abs(ratio("lepton:stick_block") - 900.f / 1600.f) < 0.01f, "stick stack = 900 ticks");
    ctx.check(std::abs(ratio("lepton:apple_crate") - 300.f / 1600.f) < 0.01f, "crates = 300 ticks");
    ctx.check(std::abs(ratio("lepton:red_stool") - 300.f / 1600.f) < 0.01f, "stools = 300 ticks");
    ctx.check(std::abs(ratio("minecraft:torch") - 400.f / 1600.f) < 0.01f, "torches = 400 ticks");
    ctx.check(ratio("lepton:gunpowder_sack") <= 0.f, "sacks are no fuel");
    float blast = FurnaceBlockActor::getBurnDuration(lp::game::makeItem("lepton:charcoal_block"), 100.f);
    ctx.check(std::abs(blast / FurnaceBlockActor::getBurnDuration(lp::game::makeItem("minecraft:coal"), 100.f) - 10.f) < 0.01f, "same ratio in a blast furnace");    auto block = lp::game::makeItem("lepton:charcoal_block");
    ctx.check(FurnaceBlockActor::isItemAllowedInFuelSlot(1, block, 1), "charcoal block fits the fuel slot");
    auto const* bi = block.mItem.get();
    ctx.check(bi && FurnaceBlockActor::getItemBurnDuration(*bi, 200.f) / ref > 9.9f, "getItemBurnDuration agrees");
    auto& c     = lp::config::mut<st::StorageConfig>();
    auto  saved = c;
    c.charcoalBlockFuelTime = 3200;
    ctx.check(std::abs(ratio("lepton:charcoal_block") - 2.f) < 0.01f, "configurable fuel time");
    c.torchesBurn = false;
    ctx.check(ratio("minecraft:torch") <= 0.01f, "torches off");
    c = saved;
});

LP_REGISTER_SELFTEST("storage.compost", [](Context& ctx) {
    auto chance = [](char const* id) { return static_cast<int>(ComposterBlock::getFillChance(lp::game::makeItem(id))); };
    ctx.check(chance("lepton:apple_crate") == 100, "apple crate 100%: " + std::to_string(chance("lepton:apple_crate")));
    ctx.check(chance("lepton:glowberry_sack") == 100, "glow berry sack 100%");
    ctx.check(chance("lepton:sugar_cane_block") == 100, "sugar cane bundle 100%");
    ctx.check(chance("lepton:gunpowder_sack") <= 0, "gunpowder sack not compostable");
    ctx.check(chance("lepton:golden_apple_crate") <= 0, "golden apple crate not compostable");
    ctx.check(chance("minecraft:poisonous_potato") == 85, "poisonous potato 85%");
    ctx.check(chance("minecraft:rotten_flesh") == 30, "rotten flesh 30%");
});

LP_REGISTER_SELFTEST("storage.properties", [](Context& ctx) {
    st::applyBlockProperties();
    ctx.check(hasProperty("lepton:rope", BlockProperty::Climbable), "rope climbable");
    ctx.check(hasProperty("lepton:cut_vine", BlockProperty::Climbable), "cut vine climbable");
    ctx.check(hasProperty("lepton:charcoal_block", BlockProperty::InfiniBurn), "charcoal block burns forever");
    ctx.check(hasProperty("lepton:blaze_lantern", BlockProperty::InfiniBurn), "blaze lantern burns forever");
    ctx.check(!hasProperty("lepton:apple_crate", BlockProperty::InfiniBurn), "crates don't");
    auto const* charcoal = bs::of("lepton:charcoal_block");
    ctx.check(charcoal && charcoal->getBlockType().mRequiresCorrectToolForDrops, "charcoal block needs a pickaxe");
    ctx.check(st::stools::typesPatched(), "stools have their bounce / comparator");
    auto& c = lp::config::mut<st::StorageConfig>();
    c.burnsForever = false;
    st::applyBlockProperties();
    ctx.check(!hasProperty("lepton:charcoal_block", BlockProperty::InfiniBurn), "burnsForever off");
    c.burnsForever = true;
    st::applyBlockProperties();
});

LP_REGISTER_SELFTEST("storage.charcoal_tool", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(804);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    killItems(ctx, p);
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    sp->simulateSetItem(ItemStack{}, true, 0);
    ctx.require(put(ctx, p, "lepton:charcoal_block"), "charcoal block");
    at(ctx, p).playerDestroy(ctx.player(), bp(p));
    lp::game::clearBlock(regionOf(ctx), bp(p));
    co_await ctx.ticks(3);
    ctx.check(countItems(ctx, p, "lepton:charcoal_block") == 0, "no drop without a pickaxe");
    killItems(ctx, p);
    auto pick = lp::game::makeItem("minecraft:stone_pickaxe");
    sp->simulateSetItem(pick, true, 0);
    ctx.require(put(ctx, p, "lepton:charcoal_block"), "charcoal block again");
    at(ctx, p).playerDestroy(ctx.player(), bp(p));
    lp::game::clearBlock(regionOf(ctx), bp(p));
    co_await ctx.ticks(3);
    ctx.check(countItems(ctx, p, "lepton:charcoal_block") == 1, "a pickaxe gets it");
    killItems(ctx, p);
    ctx.require(put(ctx, p, "lepton:apple_crate"), "crate");
    sp->simulateSetItem(ItemStack{}, true, 0);
    at(ctx, p).playerDestroy(ctx.player(), bp(p));
    lp::game::clearBlock(regionOf(ctx), bp(p));
    co_await ctx.ticks(3);
    ctx.check(countItems(ctx, p, "lepton:apple_crate") == 1, "crates drop by hand");
    killItems(ctx, p);
});

LP_REGISTER_SELFTEST("storage.beacon", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(801); // the 3x3 base spans dx 800..802
    base.y += 4;
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    clearBox(ctx, off(base, -1, 0, -1), 3, 3, 3);
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) ctx.require(put(ctx, off(base, x, 0, z), x == 0 && z == 0 ? "minecraft:iron_block" : "lepton:golden_apple_crate"), "base");
    ctx.require(put(ctx, off(base, 0, 1, 0), "minecraft:beacon"), "beacon");
    ctx.check(st::beaconLevels(regionOf(ctx), bp(off(base, 0, 1, 0))) == 1, "one level of crates");
    co_await ctx.ticks(90);
    auto* be = regionOf(ctx).getBlockEntity(bp(off(base, 0, 1, 0)));
    ctx.require(be != nullptr, "beacon block actor");
    ctx.check(static_cast<BeaconBlockActor*>(be)->mNumLevels >= 1, "the beacon is active on golden apple crates");
    lp::game::setBlock(regionOf(ctx), bp(off(base, 1, 0, 1)), *bs::of("minecraft:stone"));
    ctx.check(st::beaconLevels(regionOf(ctx), bp(off(base, 0, 1, 0))) == 0, "a stone breaks the pyramid");
});

LP_REGISTER_SELFTEST("storage.utility", [](Context& ctx) {
    st::applyDragonBreath();
    auto const* breath = itemOf("minecraft:dragon_breath");
    ctx.require(breath != nullptr, "dragon's breath");
    ctx.check(breath->mCraftingRemainingItem == nullptr, "effective dragon's breath leaves no bottle");
    auto& c = lp::config::mut<st::StorageConfig>();
    c.effectiveDragonBreath = false;
    st::applyDragonBreath();
    ctx.check(breath->mCraftingRemainingItem && breath->mCraftingRemainingItem->mFullName->getString() == "minecraft:glass_bottle",
        "off: a glass bottle remains");
    c.effectiveDragonBreath = true;
    st::applyDragonBreath();
    auto xp = [](char const* id) { return FurnaceBlockActor::_getXPRewardMultiplier(lp::game::makeItem(id)); };
    ctx.check(std::abs(xp("minecraft:iron_block") - 6.3f) < 0.01f, "iron block XP 6.3");
    ctx.check(std::abs(xp("minecraft:gold_block") - 9.f) < 0.01f, "gold block XP 9");
    ctx.check(std::abs(xp("minecraft:bone_meal") - 0.05f) < 0.001f, "bone meal XP 0.05");
});

// ---- glass shards -------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("storage.shard_count", [](Context& ctx) {
    std::mt19937 rng(8);
    auto         next = [](void* r) { return static_cast<std::uint32_t>((*static_cast<std::mt19937*>(r))()); };
    int          lo = 99, hi = 0, loF = 99, hiF = 0, decayed = 0;
    for (int i = 0; i < 2000; ++i) {
        int n = st::shardCount(next, &rng, 0, 0.f);
        lo    = std::min(lo, n);
        hi    = std::max(hi, n);
        int f = st::shardCount(next, &rng, 3, 0.f);
        loF   = std::min(loF, f);
        hiF   = std::max(hiF, f);
        decayed += st::shardCount(next, &rng, 0, 4.f);
    }
    ctx.check(lo == 2 && hi == 4, "2..4 shards, got " + std::to_string(lo) + ".." + std::to_string(hi));
    ctx.check(loF == 2 && hiF == 4, "fortune is capped at 4");
    ctx.check(decayed > 1000 && decayed < 2000, "explosions keep about 1/radius: " + std::to_string(decayed));
});

LP_REGISTER_SELFTEST("storage.glass_shards", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(806);
    p.y += 2;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    killItems(ctx, p);
    ctx.require(put(ctx, p, "minecraft:glass"), "glass");
    BlockChangeContext cc{};
    regionOf(ctx).getLevel().destroyBlock(regionOf(ctx), bp(p), true, cc);
    co_await ctx.ticks(3);
    int n = countItems(ctx, p, "lepton:clear_shard");
    ctx.check(n >= 2 && n <= 4, "broken glass drops 2-4 glass shards, got " + std::to_string(n));
    killItems(ctx, p);
    ctx.require(put(ctx, p, "lepton:dirty_glass"), "dirty glass");
    regionOf(ctx).getLevel().destroyBlock(regionOf(ctx), bp(p), true, cc);
    co_await ctx.ticks(3);
    n = countItems(ctx, p, "lepton:dirty_shard");
    ctx.check(n >= 2 && n <= 4, "dirty glass drops dirty shards, got " + std::to_string(n));
    ctx.check(countItems(ctx, p, "lepton:dirty_glass") == 0, "not itself without silk touch");
    killItems(ctx, p);
    ctx.require(put(ctx, p, "lepton:dirty_glass_pane"), "dirty pane");
    regionOf(ctx).getLevel().destroyBlock(regionOf(ctx), bp(p), true, cc);
    co_await ctx.ticks(3);
    ctx.check(countItems(ctx, p, "lepton:dirty_glass_pane") == 0, "the pane drops nothing without silk touch");
    killItems(ctx, p);
});

// ---- stools -------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("storage.stool_sit", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(810);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, 0, 0, 0), 1, 3, 1);
    ctx.require(put(ctx, p, "lepton:red_stool"), "stool placed");
    stepAside(ctx, p);
    co_await ctx.ticks(2);
    auto& player = ctx.player();
    ctx.require(st::stools::sit(player, regionOf(ctx), bp(p)), "sat down");
    co_await ctx.ticks(2);
    auto* seat = st::stools::seatOf(player);
    ctx.require(seat != nullptr, "the player rides the seat");
    ctx.check(flag(at(ctx, p), "lepton:sat_in"), "sat_in");
    auto feet = player.getFeetPos();
    ctx.info("rider feet at stool y + " + std::to_string(feet.y - p.y));
    auto const& b = at(ctx, p);
    ctx.check(b.getBlockType().hasComparatorSignal(), "the stool has a comparator signal");
    ctx.check(b.getBlockType().getComparatorSignal(regionOf(ctx), bp(p), b, 0) == 15, "comparator 15 while sat in");
    ctx.check(!st::stools::sit(ctx.spawnPlayer("storage_stool2"), regionOf(ctx), bp(p)), "one seat per stool");
    auto seatId = lp::game::entities::idOf(*seat);
    player.stopRiding(true, false, false, false);
    co_await ctx.ticks(3);
    ctx.check(!flag(at(ctx, p), "lepton:sat_in"), "sat_in cleared after getting up");
    ctx.check(at(ctx, p).getBlockType().getComparatorSignal(regionOf(ctx), bp(p), at(ctx, p), 0) == 0, "comparator 0 when empty");
    ctx.check(lp::game::entities::find(seatId) == nullptr, "the seat is gone");
    // a block above blocks sitting
    ctx.require(put(ctx, off(p, 0, 1, 0), "minecraft:stone"), "stone above");
    ctx.check(!st::stools::sit(player, regionOf(ctx), bp(p)), "no sitting under a block");
});

LP_REGISTER_SELFTEST("storage.stool_table", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(814);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, p, 1, 3, 1);
    ctx.require(put(ctx, p, "lepton:blue_stool"), "stool placed");
    co_await ctx.ticks(2);
    ctx.check(!flag(at(ctx, p), "lepton:big"), "small without a block on top");
    ctx.require(put(ctx, off(p, 0, 1, 0), "minecraft:oak_planks"), "planks on top");
    co_await ctx.ticks(2);
    ctx.check(flag(at(ctx, p), "lepton:big"), "a block on top makes it a table");
    auto const& b = at(ctx, p);
    ctx.check(b.getBlockType().getBounciness(regionOf(ctx), bp(p)) > 0.6f, "stools bounce like beds");
    lp::game::clearBlock(regionOf(ctx), bp(off(p, 0, 1, 0)));
    co_await ctx.ticks(2);
    ctx.check(!flag(at(ctx, p), "lepton:big"), "small again");
    ctx.require(put(ctx, off(p, 0, 1, 0), "minecraft:oak_slab", {{"minecraft:vertical_half", "top"}}), "top slab");
    co_await ctx.ticks(2);
    ctx.check(!flag(at(ctx, p), "lepton:big"), "a top slab doesn't sit on it");
});

LP_REGISTER_SELFTEST("storage.stool_piston", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(818);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, -1, 0, 0), 5, 3, 1);
    // piston facing east (facing_direction 5) at p, stool east of it
    ctx.require(put(ctx, p, "minecraft:piston", {{"facing_direction", 4}}), "piston");
    ctx.require(put(ctx, off(p, 1, 0, 0), "lepton:green_stool"), "stool");
    stepAside(ctx, p);
    co_await ctx.ticks(2);
    ctx.require(st::stools::sit(ctx.player(), regionOf(ctx), bp(off(p, 1, 0, 0))), "sat down");
    co_await ctx.ticks(2);
    ctx.require(put(ctx, off(p, -1, 0, 0), "minecraft:redstone_block"), "power the piston");
    co_await ctx.ticks(12);
    ctx.info("after the push: x-1 " + idAt(ctx, off(p, -1, 0, 0)) + " x+0 " + idAt(ctx, p) + " x+1 " + idAt(ctx, off(p, 1, 0, 0)) + " x+2 " + idAt(ctx, off(p, 2, 0, 0)) + " x+3 " + idAt(ctx, off(p, 3, 0, 0)));
    ctx.check(idAt(ctx, off(p, 2, 0, 0)).ends_with("_stool"), "the stool moved: " + idAt(ctx, off(p, 2, 0, 0)));
    auto* seat = st::stools::seatOf(ctx.player());
    ctx.check(seat != nullptr, "still seated after the push");
    if (seat) ctx.check(lp::game::blockPosOf(seat->getPosition()) == bp(off(p, 2, 0, 0)), "the seat moved with the stool");
    ctx.check(flag(at(ctx, off(p, 2, 0, 0)), "lepton:sat_in"), "sat_in after the move");
    ctx.player().stopRiding(true, false, false, false);
    ctx.cleanupBlock(off(p, 1, 0, 0));
    co_await ctx.ticks(2);
});

LP_REGISTER_SELFTEST("storage.stool_fall", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(824);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, p, 1, 16, 1);
    clearBox(ctx, off(p, 3, 0, 0), 1, 16, 1);
    ctx.require(put(ctx, p, "lepton:orange_stool"), "stool");
    auto& onStool = ctx.player();
    auto& onFloor = ctx.spawnPlayer("storage_fall_ref");
    co_await ctx.ticks(5);
    int h0 = onStool.getHealth(), r0 = onFloor.getHealth();
    onStool.teleport(Vec3{p.x + 0.5f, p.y + 13.f, p.z + 0.5f}, DimensionType(p.dim));
    onFloor.teleport(Vec3{p.x + 3.5f, p.y + 13.f, p.z + 0.5f}, DimensionType(p.dim));
    co_await ctx.ticks(80);
    int lost = h0 - onStool.getHealth(), ref = r0 - onFloor.getHealth();
    ctx.info("fall damage: on the stool " + std::to_string(lost) + ", on the ground " + std::to_string(ref));
    ctx.check(ref >= 5, "the reference fall hurts");
    ctx.check(lost < ref && lost <= (ref + 1) / 2, "a stool halves the fall distance");
});

LP_REGISTER_SELFTEST("storage.stool_interact", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(822);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, p, 1, 3, 1);
    ctx.require(put(ctx, p, "lepton:yellow_stool"), "stool");
    stepAside(ctx, p);
    co_await ctx.ticks(2);
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  s  = lp::game::makeItem("minecraft:stick", 1);
    sp->simulateUseItemOnBlock(s, bp(p), Facing::Up, Vec3{0.5f, 0.56f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(st::stools::seatOf(ctx.player()) != nullptr, "using a stool sits down");
    ctx.player().stopRiding(true, false, false, false);
    co_await ctx.ticks(3);
});

// ---- rope ---------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("storage.rope_down_up", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(830);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, 0, 1, 0), 1, 7, 1);
    auto& r = regionOf(ctx);
    ctx.require(put(ctx, off(p, 0, 7, 0), "minecraft:stone"), "ceiling");
    ctx.require(put(ctx, off(p, 0, 6, 0), "lepton:rope"), "rope");
    ctx.require(put(ctx, off(p, 0, 4, 0), "minecraft:oak_planks"), "planks below the end");
    ctx.check(st::rope::canHang(r, bp(off(p, 0, 6, 0))), "rope hangs from stone");
    ctx.check(st::rope::pullDown(r, bp(off(p, 0, 6, 0))), "pull down into the air");
    ctx.check(idAt(ctx, off(p, 0, 5, 0)) == "lepton:rope", "rope extended");
    ctx.check(st::rope::pullDown(r, bp(off(p, 0, 6, 0))), "pull down onto the planks");
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "lepton:rope", "rope took the planks' place");
    ctx.check(idAt(ctx, off(p, 0, 3, 0)) == "minecraft:oak_planks", "the planks moved down");
    ctx.check(st::rope::pullUp(r, bp(off(p, 0, 6, 0))), "pull up");
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "minecraft:oak_planks", "the planks came up");
    ctx.check(idAt(ctx, off(p, 0, 3, 0)) == "minecraft:air", "and left air");
    ctx.check(!st::rope::pullUp(r, bp(off(p, 0, 5, 0))), "the bottom rope can't pull itself up");
    // obsidian is never moved
    ctx.require(put(ctx, off(p, 0, 4, 0), "minecraft:obsidian"), "obsidian");
    ctx.check(!st::rope::pullDown(r, bp(off(p, 0, 6, 0))), "obsidian stops the rope");
    co_await ctx.ticks(1);
});

LP_REGISTER_SELFTEST("storage.rope_player", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(834);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, 0, 1, 0), 1, 6, 1);
    ctx.require(put(ctx, off(p, 0, 6, 0), "minecraft:stone"), "ceiling");
    ctx.require(put(ctx, off(p, 0, 5, 0), "lepton:rope"), "rope");
    stepAside(ctx, off(p, 0, 3, 0));
    co_await ctx.ticks(2);
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  s  = lp::game::makeItem("lepton:rope", 4);
    sp->simulateSetItem(s, true, 0);
    sp->simulateUseItemOnBlock(s, bp(off(p, 0, 5, 0)), Facing::South, Vec3{0.5f, 0.5f, 1.f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "lepton:rope", "using rope on rope lowers it");
    co_await ctx.ticks(10);
    auto stick = lp::game::makeItem("minecraft:stick", 1);
    sp->simulateUseItemOnBlock(stick, bp(off(p, 0, 5, 0)), Facing::South, Vec3{0.5f, 0.5f, 1.f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "minecraft:air", "using another item pulls it up");
    co_await ctx.ticks(10);
    // rope can't stand on the ground: placing it on a floor is refused
    ctx.require(put(ctx, off(p, 1, 0, 0), "minecraft:stone"), "floor");
    ctx.cleanupBlock(off(p, 1, 1, 0));
    sp->simulateSetItem(s, true, 0);
    sp->simulateUseItemOnBlock(s, bp(off(p, 1, 0, 0)), Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 1, 1, 0)) != "lepton:rope", "no rope on the floor");
});

LP_REGISTER_SELFTEST("storage.rope_move_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(838);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, 0, 1, 0), 1, 5, 1);
    auto& r = regionOf(ctx);
    ctx.require(put(ctx, off(p, 0, 5, 0), "minecraft:stone"), "ceiling");
    ctx.require(put(ctx, off(p, 0, 4, 0), "lepton:rope"), "rope");
    ctx.require(put(ctx, off(p, 0, 3, 0), "minecraft:chest"), "chest");
    auto* be = r.getBlockEntity(bp(off(p, 0, 3, 0)));
    ctx.require(be != nullptr, "chest actor");
    static_cast<Container*>(static_cast<ChestBlockActor*>(be))->setItem(0, lp::game::makeItem("minecraft:diamond", 5));
    ctx.check(st::rope::pullDown(r, bp(off(p, 0, 4, 0))), "pull down onto the chest");
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "minecraft:chest", "the chest moved down");
    auto* moved = r.getBlockEntity(bp(off(p, 0, 2, 0)));
    ctx.require(moved != nullptr, "moved chest actor");
    auto const& item = static_cast<Container*>(static_cast<ChestBlockActor*>(moved))->getItem(0);
    ctx.check(lp::game::itemId(item) == "minecraft:diamond" && item.mCount == 5, "the contents moved with it");
    co_await ctx.ticks(1);
    killItems(ctx, off(p, 0, 2, 0));
    ctx.check(countItems(ctx, off(p, 0, 3, 0), "minecraft:diamond") == 0, "nothing dropped");
    static_cast<Container*>(static_cast<ChestBlockActor*>(moved))->removeAllItems();
});

LP_REGISTER_SELFTEST("storage.rope_survive", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(842);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, 0, 1, 0), 1, 5, 1);
    auto& r = regionOf(ctx);
    ctx.require(put(ctx, off(p, 0, 5, 0), "minecraft:stone"), "ceiling");
    ctx.require(put(ctx, off(p, 0, 4, 0), "lepton:rope"), "rope");
    ctx.require(put(ctx, off(p, 0, 3, 0), "lepton:rope"), "rope 2");
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, off(p, 0, 3, 0)) == "lepton:rope", "a rope column stays");
    lp::game::clearBlock(r, bp(off(p, 0, 5, 0)));
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) != "lepton:rope", "without the ceiling the rope pops");
    ctx.check(idAt(ctx, off(p, 0, 3, 0)) != "lepton:rope", "and the one below");
    killItems(ctx, off(p, 0, 3, 0));
    auto sup = [&](char const* what) {
        auto const& b = at(ctx, off(p, 0, 5, 0));
        auto const& t = b.getBlockType();
        ctx.info(std::string(what) + ": center " + std::to_string(t.canProvideSupport(b, 0, BlockSupportType::Center)) + " edge "
            + std::to_string(t.canProvideSupport(b, 0, BlockSupportType::Edge)) + " any " + std::to_string(t.canProvideSupport(b, 0, BlockSupportType::Any)));
    };
    put(ctx, off(p, 0, 5, 0), "minecraft:stone");
    sup("stone");
    ctx.require(put(ctx, off(p, 0, 5, 0), "minecraft:oak_fence"), "fence");
    sup("fence");
    ctx.check(!st::rope::canHang(r, bp(off(p, 0, 4, 0))), "a fence's bottom is not sturdy");
    ctx.require(put(ctx, off(p, 0, 5, 0), "minecraft:oak_slab", {{"minecraft:vertical_half", "top"}}), "top slab");
    sup("top slab");
    put(ctx, off(p, 0, 5, 0), "minecraft:glass");
    sup("glass");
    ctx.require(put(ctx, off(p, 0, 5, 0), "minecraft:oak_slab", {{"minecraft:vertical_half", "top"}}), "top slab");
    ctx.check(!st::rope::canHang(r, bp(off(p, 0, 4, 0))), "a top slab's bottom face is not full (Java: no face at the block's bottom)");
    put(ctx, off(p, 0, 5, 0), "minecraft:glass");
    ctx.check(st::rope::canHang(r, bp(off(p, 0, 4, 0))), "glass holds rope");
});

LP_REGISTER_SELFTEST("storage.rope_dispenser", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(846);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, 0, 1, 0), 1, 5, 1);
    auto& r = regionOf(ctx);
    ctx.require(put(ctx, off(p, 0, 5, 0), "minecraft:stone"), "ceiling");
    ctx.check(st::rope::dispenseAt(r, bp(off(p, 0, 4, 0))), "a dispenser places rope under a ceiling");
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "lepton:rope", "rope placed");
    ctx.check(st::rope::dispenseAt(r, bp(off(p, 0, 4, 0))), "and lowers an existing rope");
    ctx.check(idAt(ctx, off(p, 0, 3, 0)) == "lepton:rope", "rope lowered");
    ctx.cleanupBlock(off(p, 0, 3, 0));
    // the real dispenser, facing down: the rope hangs from the dispenser itself
    auto d = off(p, 1, 5, 0);
    clearBox(ctx, off(p, 1, 1, 0), 1, 5, 1);
    ctx.require(put(ctx, d, "minecraft:dispenser", {{"facing_direction", 0}}), "dispenser");
    auto* be = r.getBlockEntity(bp(d));
    ctx.require(be != nullptr, "dispenser actor");
    auto* cont = static_cast<Container*>(static_cast<DispenserBlockActor*>(be));
    cont->setItem(0, lp::game::makeItem("lepton:rope", 3));
    auto const& block = at(ctx, d);
    static_cast<DispenserBlock const&>(block.getBlockType()).dispenseFrom(r, bp(d));
    co_await ctx.ticks(1);
    ctx.check(idAt(ctx, off(p, 1, 4, 0)) == "lepton:rope", "the dispenser placed a rope: " + idAt(ctx, off(p, 1, 4, 0)));
    ctx.check(cont->getItem(0).mCount == 2, "one rope used");
    static_cast<DispenserBlock const&>(block.getBlockType()).dispenseFrom(r, bp(d));
    co_await ctx.ticks(1);
    ctx.check(idAt(ctx, off(p, 1, 3, 0)) == "lepton:rope", "the dispenser lowered the rope");
    ctx.cleanupBlock(off(p, 1, 3, 0));
    cont->removeAllItems();
    killItems(ctx, off(p, 1, 3, 0));
});
// ---- vines / pots -------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("storage.shear_vines", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(850);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, 0, 1, 0), 1, 5, 2);
    auto& r = regionOf(ctx);
    for (int y = 1; y <= 4; ++y) ctx.require(put(ctx, off(p, 0, y, 0), "minecraft:stone"), "wall");
    // vines on the wall's south side: vine_direction_bits 4 = attached to the north
    for (int y = 1; y <= 4; ++y) ctx.require(put(ctx, off(p, 0, y, 1), "minecraft:vine", {{"vine_direction_bits", 4}}), "vine");
    ctx.check(st::plants::shearVine(r, bp(off(p, 0, 4, 1))), "sheared");
    auto const& cut = at(ctx, off(p, 0, 4, 1));
    ctx.check(cut.getTypeName() == "lepton:cut_vine" && flag(cut, "lepton:north") && !flag(cut, "lepton:south"), "cut vine with the same face");
    for (int y = 1; y <= 3; ++y) ctx.check(idAt(ctx, off(p, 0, y, 1)) == "minecraft:air", "vines below removed");
    lp::game::clearBlock(r, bp(off(p, 0, 4, 0)));
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 4, 1)) == "minecraft:air", "an unsupported cut vine falls off");
});

LP_REGISTER_SELFTEST("storage.shear_vines_player", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(854);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, off(p, 0, 1, 0), 1, 4, 2);
    for (int y = 1; y <= 3; ++y) ctx.require(put(ctx, off(p, 0, y, 0), "minecraft:stone"), "wall");
    for (int y = 2; y <= 3; ++y) ctx.require(put(ctx, off(p, 0, y, 1), "minecraft:vine", {{"vine_direction_bits", 4}}), "vine");
    stepAside(ctx, off(p, 0, 1, 0));
    co_await ctx.ticks(2);
    auto* sp     = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  shears = lp::game::makeItem("minecraft:shears", 1);
    sp->simulateSetItem(shears, true, 0);
    sp->simulateUseItemOnBlock(shears, bp(off(p, 0, 3, 1)), Facing::South, Vec3{0.5f, 0.5f, 0.1f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 3, 1)) == "lepton:cut_vine", "shears cut the vine");
    ctx.check(idAt(ctx, off(p, 0, 2, 1)) == "minecraft:air", "the vine below is gone");
});

LP_REGISTER_SELFTEST("storage.potting", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(858);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    clearBox(ctx, p, 1, 2, 1);
    auto& r = regionOf(ctx);
    ctx.require(put(ctx, p, "minecraft:flower_pot"), "pot");
    ctx.check(st::plants::pot(r, bp(p), "minecraft:sweet_berries"), "berries into the pot");
    ctx.check(idAt(ctx, p) == "lepton:potted_berries", "potted berries");
    ctx.check(st::plants::unpot(r, bp(p)) == "minecraft:sweet_berries", "taken out");
    ctx.check(idAt(ctx, p) == "minecraft:flower_pot", "empty pot again");
    stepAside(ctx, p);
    co_await ctx.ticks(2);
    auto* sp    = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  seeds = lp::game::makeItem("minecraft:wheat_seeds", 4);
    sp->simulateSetItem(seeds, true, 0);
    sp->simulateUseItemOnBlock(seeds, bp(p), Facing::Up, Vec3{0.5f, 0.4f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, p) == "lepton:potted_wheat", "a player pots wheat seeds: " + idAt(ctx, p));
    for (auto id : {"lepton:potted_sea_pickle", "lepton:potted_cave_vines"}) {
        auto const* b = bs::of(id);
        ctx.check(b && b->mDirectData->mLightEmission->mValue > 0, std::string(id) + " gives light");
    }
});

// ---- gallery (client e2e screenshots) -----------------------------------------------------------
// Every block of the unit in rows (x = dx 870..894). With a real player online, the player is moved
// in front of each part (views logged as "STORAGE GALLERY view <name>", 100 ticks each) while the
// test's simulated player sits on a stool.

LP_REGISTER_SELFTEST("storage.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto o = ctx.testPos(870);
    ctx.require(co_await ctx.ensureLoaded(o), "test area loaded");
    clearBox(ctx, off(o, 0, 0, 0), 25, 6, 6);
    int x = 0, placed = 0;
    auto place = [&](GP p, std::string_view id, bs::Changes const& ch = {}) {
        if (put(ctx, p, id, ch)) ++placed;
        else ctx.check(false, "placed " + std::string(id));
    };
    for (auto id : st::contentIds()) { // z 0: compressed blocks
        std::string s(id);
        if (s.ends_with("_stool") || s.ends_with("_shard") || s.starts_with("lepton:potted_") || s == "lepton:rope"
            || s == "lepton:cut_vine" || s.starts_with("lepton:dirty_glass"))
            continue;
        place(off(o, x++, 0, 0), s);
    }
    place(off(o, 20, 0, 0), "lepton:dirty_glass");
    place(off(o, 21, 0, 0), "lepton:dirty_glass_pane", {{"lepton:east", true}, {"lepton:west", true}});
    place(off(o, 22, 0, 0), "lepton:dirty_glass_pane", {{"lepton:west", true}});
    x = 0;
    for (auto id : st::contentIds()) // z 2: stools, the last one a table
        if (std::string_view(id).ends_with("_stool")) place(off(o, x++, 0, 2), id);
    place(off(o, 15, 1, 2), "minecraft:oak_planks");
    lp::feature::storage::stools::fixState(regionOf(ctx), bp(off(o, 15, 0, 2)));
    x = 0;
    for (auto id : st::contentIds()) // z 4: potted plants
        if (std::string_view(id).starts_with("lepton:potted_")) place(off(o, x++, 0, 4), id);
    // rope column under a stone beam (x 18..20, z 2) and cut vines on a stone wall (x 22)
    for (int bx = 18; bx <= 20; ++bx) place(off(o, bx, 4, 2), "minecraft:stone");
    for (int y = 1; y <= 3; ++y) place(off(o, 19, y, 2), "lepton:rope");
    for (int y = 1; y <= 4; ++y) place(off(o, 22, y, 2), "minecraft:stone");
    for (int y = 2; y <= 4; ++y) place(off(o, 22, y, 3), "lepton:cut_vine", {{"lepton:north", true}});
    ctx.info("STORAGE GALLERY placed " + std::to_string(placed));

    // the test player sits on a stool (x 5: the ids are sorted, so the green one)
    GP sitAt = off(o, 5, 0, 2);
    st::stools::sit(ctx.player(), regionOf(ctx), bp(sitAt));

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (viewer) {
        auto uid  = viewer->getOrCreateUniqueID();
        auto view = [&](std::string const& name, float dx, float dy, float dz, float pitch, float yaw) -> ll::coro::CoroTask<> {
            if (auto* v = ctx.player().getLevel().getPlayer(uid))
                v->teleport(Vec3{o.x + dx, o.y + dy, o.z + dz}, DimensionType(o.dim), Vec2{pitch, yaw});
            ctx.info("STORAGE GALLERY view " + name);
            co_await ctx.ticks(100);
        };
        co_await view("compressed-1", 5.5f, 1.6f, -3.5f, 22.f, 0.f);     // looking south at z 0
        co_await view("compressed-2", 16.5f, 1.6f, -3.5f, 22.f, 0.f);
        co_await view("stools", 7.5f, 4.5f, 3.5f, 55.f, 180.f);           // above the pots, down onto the stools
        co_await view("rope-vine", 20.5f, 2.5f, 7.5f, 10.f, 180.f);      // looking north at the rope / vines
        co_await view("potted-1", 5.5f, 2.f, 8.5f, 32.f, 180.f);         // the pots from the south
        co_await view("potted-2", 18.5f, 2.f, 8.5f, 32.f, 180.f);
    }
    ctx.player().stopRiding(true, false, false, false);
    co_await ctx.ticks(2);
    ctx.info("STORAGE GALLERY done");
});
// Client scenario `storage-furnace`: reports what the real player's furnace (within 4 blocks) holds,
// so the scenario can check that a client could put a Lepton fuel block into the fuel slot.
LP_REGISTER_SELFTEST("storage.furnace_report", [](Context& ctx) {
    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) return;
    auto& r    = viewer->getDimensionBlockSource();
    auto  feet = lp::game::blockPosOf(viewer->getFeetPos());
    for (int dx = -4; dx <= 4; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -4; dz <= 4; ++dz) {
                BlockPos q{feet.x + dx, feet.y + dy, feet.z + dz};
                if (r.getBlock(q).getTypeName() != "minecraft:furnace") continue;
                auto* be = r.getBlockEntity(q);
                if (!be) continue;
                auto* fa = static_cast<FurnaceBlockActor*>(be);
                auto const& in   = fa->getItem(0);
                auto const& fuel = fa->getItem(1);
                lp::log().info(
                    "STORAGE FURNACE input={} x{} fuel={} x{}",
                    lp::game::itemId(in),
                    lp::game::isEmpty(in) ? 0 : in.mCount,
                    lp::game::itemId(fuel),
                    lp::game::isEmpty(fuel) ? 0 : fuel.mCount
                );
                return;
            }
    lp::log().info("STORAGE FURNACE none");
});