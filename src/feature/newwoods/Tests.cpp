// Selftests of U7 newwoods (docs/spec/newwoods.md §8). U7 owns dx 700..799; the tree tests grow
// their trees far away in z (dz 120+) so the canopies stay clear of the other units' test rows.

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/Item.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/BlockTickingQueue.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/level/storage/LevelData.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/ComposterBlock.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/storage/loot/LootPool.h"
#include "mc/world/level/storage/loot/LootTable.h"
#include "mc/world/level/storage/loot/LootTables.h"
#include "mc/world/level/storage/loot/entries/LootPoolEntry.h"

#include "ll/api/service/Bedrock.h"

#include "feature/newwoods/NewWoods.h"
#include "feature/newwoods/Palette.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
namespace bs = lp::game::blockstates;
namespace nw = lp::feature::newwoods;
using Facing = ScriptModuleMinecraft::ScriptFacing;
using GP     = lp::core::GlobalPos;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

bool put(Context& ctx, GP const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}

Block const& at(Context& ctx, GP const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }
std::string  idAt(Context& ctx, GP const& p) { return at(ctx, p).getTypeName(); }

GP off(GP p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

bool use(Context& ctx, std::string_view id, GP const& p, Facing face, Vec3 facePos) {
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  s  = lp::game::makeItem(id, 16);
    return sp->simulateUseItemOnBlock(s, lp::game::blockPos(p), face, facePos);
}

void stepAside(Context& ctx, GP const& p, int dz = 6) {
    ctx.player().teleport(
        Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 1, static_cast<float>(p.z + dz) + 0.5f},
        DimensionType(p.dim)
    );
}

/// Every log / leaf of this unit (and oak / azalea logs) in the box around `p` is cleared after the
/// test; returns the number of logs and leaves by id.
std::map<std::string, int> collectTree(Context& ctx, GP const& p, int r, int h) {
    std::map<std::string, int> out;
    auto&                      region = regionOf(ctx);
    for (int dx = -r; dx <= r; ++dx)
        for (int dy = -1; dy <= h; ++dy)
            for (int dz = -r; dz <= r; ++dz) {
                auto        q = off(p, dx, dy, dz);
                auto const& b = region.getBlock(lp::game::blockPos(q));
                auto const& n = b.getTypeName();
                if (b.isAir() || !(n.starts_with("lepton:") || n.find("leaves") != std::string::npos || n.ends_with("_log")
                                   || n == "minecraft:dirt"))
                    continue;
                ++out[n];
                ctx.cleanupBlock(q);
            }
    return out;
}

/// Grass at `p`, the sapling of `tree` on it at stage 1.
bool plantSapling(Context& ctx, GP const& p, nw::TreeType const& tree) {
    return put(ctx, p, "minecraft:grass_block") && put(ctx, off(p, 0, 1, 0), tree.sapling, {{"lepton:stage", 1}});
}

} // namespace

LP_REGISTER_SELFTEST("newwoods.blocks", [](Context& ctx) {
    int missing = 0;
    for (auto const& id : nw::allBlockIds()) {
        if (!bs::of(id)) {
            ++missing;
            ctx.check(false, "block registered: " + id);
        }
    }
    ctx.check(nw::allBlockIds().size() == 90, "90 blocks, got " + std::to_string(nw::allBlockIds().size()));
    ctx.check(missing == 0, std::to_string(missing) + " blocks missing");
    ctx.check(!lp::game::makeItem("lepton:ancient_fruit").isNull(), "Enchanted Fruit item");
    for (auto const* id : {"lepton:azalea_door", "lepton:blossom_door", "lepton:ancient_door", "lepton:red_blossom_sapling"})
        ctx.check(!lp::game::makeItem(id).isNull(), std::string("item ") + id);
    ctx.check(nw::moduleOfId("lepton:azalea_planks_slab") == nw::Module::AzaleaWood, "azalea slab module");
    ctx.check(nw::moduleOfId("lepton:hollow_blossom_log") == nw::Module::BlossomTrees, "trumpet hollow log module");
    ctx.check(nw::moduleOfId("lepton:ancient_fruit") == nw::Module::AncientWood, "fruit module");
    ctx.check(nw::moduleOfId("lepton:azalea_hedge") == nw::Module::None, "azalea hedge is U6's");
    auto const* leaves = bs::of("lepton:ancient_leaves");
    ctx.require(leaves != nullptr, "ancient leaves");
    ctx.check(bs::getBool(*leaves, "lepton:persistent").value_or(false), "placed leaves are persistent by default");
    ctx.check(nw::isOurLeaves(*leaves) && nw::isAnyLeaves(*leaves), "leaves tags");
    ctx.check(nw::isLogLike(*bs::of("lepton:stripped_blossom_wood")), "stripped wood is a log");
    ctx.check(nw::isLogLike(*bs::of("minecraft:oak_log")) && !nw::isLogLike(*bs::of("minecraft:oak_planks")), "vanilla logs");
    ctx.check(nw::isOurSapling(*bs::of("lepton:ancient_sapling")), "sapling tag");
    auto pal = nw::palette();
    ctx.require(pal != nullptr, "palette resolved");
    ctx.check(pal->log("lepton:ancient_log", 1) && pal->generatedLeaves("lepton:red_blossom_leaves") && pal->azaleaLog[0], "palette blocks");
});

// Java's item tags: the vanilla recipes taking any planks / logs / wooden slabs accept ours.
LP_REGISTER_SELFTEST("newwoods.item_tags", [](Context& ctx) {
    auto tagged = [](char const* id, char const* tag) {
        auto s = lp::game::makeItem(id);
        return !s.isNull() && s.mItem && s.mItem->hasTag(HashedString(tag));
    };
    for (auto w : {"azalea", "blossom", "ancient"}) {
        std::string s(w);
        ctx.check(tagged(("lepton:" + s + "_planks").c_str(), "minecraft:planks"), s + " planks are planks");
        ctx.check(tagged(("lepton:vertical_" + s + "_planks").c_str(), "minecraft:planks"), "vertical " + s + " planks are planks");
        ctx.check(tagged(("lepton:stripped_" + s + "_wood").c_str(), "minecraft:logs"), s + " stripped wood is a log");
        ctx.check(tagged(("lepton:" + s + "_planks_slab").c_str(), "minecraft:wooden_slabs"), s + " slab is a wooden slab");
    }
});

LP_REGISTER_SELFTEST("newwoods.recipes", [](Context& ctx) {
    auto& c     = lp::config::mut<nw::NewWoodsConfig>();
    auto  saved = c;
    ctx.check(!nw::recipeDisabled("lepton:azalea_planks_from_azalea_log", "lepton:azalea_planks"), "enabled module keeps recipes");
    bool hedges = bs::of("lepton:azalea_hedge") != nullptr; // U6's block
    ctx.check(nw::recipeDisabled("lepton:azalea_hedge_from_azalea_log", "lepton:azalea_hedge") == !hedges,
        "azalea hedge from azalea logs (when U6's hedge exists)");
    c.enableAzaleaWood = false;
    ctx.check(nw::recipeDisabled("lepton:azalea_planks_from_azalea_log", "lepton:azalea_planks"), "AzaleaWood off: no planks");
    ctx.check(nw::recipeDisabled("lepton:azalea_hedge_from_azalea_log", "lepton:azalea_hedge"), "AzaleaWood off: no azalea-log hedge");
    ctx.check(!nw::recipeDisabled("lepton:blossom_planks_from_blossom_log", "lepton:blossom_planks"), "other modules keep theirs");
    c.enableBlossomTrees = false;
    ctx.check(nw::recipeDisabled("lepton:red_blossom_hedge_from_blossom_log", "lepton:red_blossom_hedge"), "BlossomTrees off");
    ctx.check(!nw::recipeDisabled("minecraft:stick", "minecraft:stick"), "vanilla recipes untouched");
    ctx.check(!nw::recipeDisabled("lepton:oak_post", "lepton:oak_post"), "other units' recipes untouched");
    c = saved;
});

LP_REGISTER_SELFTEST("newwoods.strip", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(700);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "lepton:azalea_log", {{"minecraft:block_face", "east"}}), "azalea log along x");
    stepAside(ctx, p, 3);
    co_await ctx.ticks(2);
    ctx.check(use(ctx, "minecraft:iron_axe", p, Facing::Up, Vec3{0.5f, 1.f, 0.5f}), "used an axe");
    co_await ctx.ticks(3);
    auto const& b = at(ctx, p);
    ctx.check(b.getTypeName() == "lepton:stripped_azalea_log", "stripped, got " + b.getTypeName());
    ctx.check(bs::getString(b, "minecraft:block_face").value_or("") == "east", "axis kept");
});

LP_REGISTER_SELFTEST("newwoods.leaf_drops", [](Context& ctx) {
    nw::JavaRandom r(7);
    auto           one = nw::leafDrops("lepton:ancient_leaves", true, 0, 0, r);
    ctx.check(one.size() == 1 && one[0].first == "lepton:ancient_leaves", "shears / silk touch drop the leaves");
    auto rate = [&](std::string_view leaves, int fortune, float radius, std::string const& item) {
        nw::JavaRandom rr(1234);
        int            n = 200000, hits = 0;
        for (int i = 0; i < n; ++i)
            for (auto const& [id, c] : nw::leafDrops(leaves, false, fortune, radius, rr))
                if (id == item) ++hits;
        return static_cast<double>(hits) / n;
    };
    auto near = [](double a, double b) { return std::abs(a - b) <= std::max(0.0012, b * 0.08); };
    double s0 = rate("lepton:ancient_leaves", 0, 0, "lepton:ancient_sapling");
    double s3 = rate("lepton:ancient_leaves", 3, 0, "lepton:ancient_sapling");
    double f0 = rate("lepton:ancient_leaves", 0, 0, "lepton:ancient_fruit");
    double b0 = rate("lepton:red_blossom_leaves", 0, 0, "lepton:red_blossom_sapling");
    double b4 = rate("lepton:red_blossom_leaves", 4, 0, "lepton:red_blossom_sapling");
    double bf = rate("lepton:red_blossom_leaves", 0, 0, "lepton:ancient_fruit");
    double ex = rate("lepton:ancient_leaves", 0, 4.0f, "lepton:ancient_sapling");
    ctx.check(near(s0, 0.05), "ashen sapling 5%: " + std::to_string(s0));
    ctx.check(near(s3, 0.1), "ashen sapling fortune III 10%: " + std::to_string(s3));
    ctx.check(near(f0, 0.005), "fruit 0.5%: " + std::to_string(f0));
    ctx.check(near(b0, 0.0046875), "trumpet sapling 0.47%: " + std::to_string(b0));
    ctx.check(near(b4, 0.01875), "trumpet sapling fortune IV: " + std::to_string(b4));
    ctx.check(bf == 0, "trumpet leaves drop no fruit");
    ctx.check(near(ex, 0.05 / 4), "explosion radius 4 keeps a quarter: " + std::to_string(ex));
});

LP_REGISTER_SELFTEST("newwoods.leaf_decay", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(704);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    // log + a chain of 3 generated leaves up, a persistent leaf on top, a leaf touching another log
    ctx.require(put(ctx, p, "lepton:ancient_log"), "log");
    for (int i = 1; i <= 3; ++i) ctx.require(put(ctx, off(p, 0, i, 0), "lepton:ancient_leaves", {{"lepton:persistent", false}}), "leaf");
    ctx.require(put(ctx, off(p, 0, 4, 0), "lepton:red_blossom_leaves", {{"lepton:persistent", true}}), "persistent leaf");
    ctx.require(put(ctx, off(p, 2, 0, 0), "minecraft:oak_log"), "other log");
    ctx.require(put(ctx, off(p, 2, 1, 0), "lepton:ancient_leaves", {{"lepton:persistent", false}}), "leaf on the other log");
    auto& region = regionOf(ctx);
    ctx.check(nw::leafSupported(region, lp::game::blockPos(off(p, 0, 3, 0))), "3 above the log: supported");
    nw::runLeafChecks();
    lp::game::clearBlock(region, lp::game::blockPos(p)); // the change is queued synchronously
    int unsupported = nw::runLeafChecks();
    ctx.check(unsupported == 3, "3 leaves lost their log, got " + std::to_string(unsupported));
    for (int i = 1; i <= 4; ++i) nw::leafTick(region, lp::game::blockPos(off(p, 0, i, 0)));
    co_await ctx.ticks(2);
    for (int i = 1; i <= 3; ++i) ctx.check(at(ctx, off(p, 0, i, 0)).isAir(), "leaf " + std::to_string(i) + " decayed");
    ctx.check(idAt(ctx, off(p, 0, 4, 0)) == "lepton:red_blossom_leaves", "persistent leaf stays");
    ctx.check(idAt(ctx, off(p, 2, 1, 0)) == "lepton:ancient_leaves", "supported leaf stays");
    // a distant leaf: 6 steps through leaves -> unsupported
    ctx.require(put(ctx, off(p, 4, 0, 0), "minecraft:oak_log"), "log far");
    for (int i = 1; i <= 6; ++i) ctx.require(put(ctx, off(p, 4, i, 0), "lepton:ancient_leaves", {{"lepton:persistent", false}}), "chain leaf");
    ctx.check(nw::leafSupported(region, lp::game::blockPos(off(p, 4, 6, 0))), "distance 6 supported");
    ctx.require(put(ctx, off(p, 4, 7, 0), "lepton:ancient_leaves", {{"lepton:persistent", false}}), "7th leaf");
    ctx.check(!nw::leafSupported(region, lp::game::blockPos(off(p, 4, 7, 0))), "distance 7 decays");
});

LP_REGISTER_SELFTEST("newwoods.sapling_place", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(712);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "minecraft:dirt"), "dirt");
    ctx.require(put(ctx, off(p, 2, 0, 0), "minecraft:stone"), "stone");
    ctx.cleanupBlock(off(p, 0, 1, 0));
    ctx.cleanupBlock(off(p, 2, 1, 0));
    stepAside(ctx, p, 4);
    co_await ctx.ticks(2);
    use(ctx, "lepton:blue_blossom_sapling", p, Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(10);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "lepton:blue_blossom_sapling", "planted on dirt, got " + idAt(ctx, off(p, 0, 1, 0)));
    use(ctx, "lepton:blue_blossom_sapling", off(p, 2, 0, 0), Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(10);
    ctx.check(at(ctx, off(p, 2, 1, 0)).isAir(), "not on stone, got " + idAt(ctx, off(p, 2, 1, 0)));
    // the dirt goes: the sapling pops off
    put(ctx, p, "minecraft:stone");
    co_await ctx.ticks(5);
    ctx.check(at(ctx, off(p, 0, 1, 0)).isAir(), "sapling popped off, got " + idAt(ctx, off(p, 0, 1, 0)));
    // potted: a sapling on an empty flower pot makes the potted sapling; using it again gives it back
    auto pot = off(p, 4, 1, 0);
    ctx.require(put(ctx, off(p, 4, 0, 0), "minecraft:stone") && put(ctx, pot, "minecraft:flower_pot"), "flower pot");
    co_await ctx.ticks(10);
    use(ctx, "lepton:blue_blossom_sapling", pot, Facing::Up, Vec3{0.5f, 0.4f, 0.5f});
    co_await ctx.ticks(5);
    ctx.check(idAt(ctx, pot) == "lepton:potted_blue_blossom_sapling", "potted, got " + idAt(ctx, pot));
    auto* be = regionOf(ctx).getBlockEntity(lp::game::blockPos(pot));
    Block const* plant = be ? static_cast<FlowerPotBlockActor*>(be)->mPlant : nullptr;
    ctx.check(plant == nullptr, "no vanilla pot holds the custom sapling (the client would crash)");
    co_await ctx.ticks(10);
    use(ctx, "minecraft:stick", pot, Facing::Up, Vec3{0.5f, 0.4f, 0.5f});
    co_await ctx.ticks(5);
    ctx.check(idAt(ctx, pot) == "minecraft:flower_pot", "the sapling came out, got " + idAt(ctx, pot));
    // the API and the drops
    auto& region = regionOf(ctx);
    auto  bp     = lp::game::blockPos(pot);
    ctx.check(nw::potSapling(region, bp, "lepton:ancient_sapling"), "pot an ashen sapling");
    ctx.check(!nw::potSapling(region, bp, "lepton:red_blossom_sapling"), "a full pot takes nothing");
    ctx.check(nw::unpotSapling(region, bp) == "lepton:ancient_sapling", "unpot gives the sapling");
    ctx.check(!nw::potSapling(region, bp, "minecraft:oak_sapling"), "vanilla saplings are the vanilla pot's");
});

LP_REGISTER_SELFTEST("newwoods.sapling_bonemeal", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(716, 120);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto const* tree = nw::treeByName("yellow_blossom");
    ctx.require(tree != nullptr, "tree type");
    ctx.require(put(ctx, p, "minecraft:grass_block") && put(ctx, off(p, 0, 1, 0), tree->sapling), "sapling planted");
    stepAside(ctx, p, 8);
    co_await ctx.ticks(2);
    // the player's bone meal reaches the sapling (hook): stage 1 or a tree after a few uses
    bool changed = false;
    for (int i = 0; i < 12 && !changed; ++i) {
        use(ctx, "minecraft:bone_meal", off(p, 0, 1, 0), Facing::Up, Vec3{0.5f, 0.5f, 0.5f});
        co_await ctx.ticks(10);
        auto const& b = at(ctx, off(p, 0, 1, 0));
        changed       = !nw::isOurSapling(b) || bs::getInt(b, "lepton:stage").value_or(0) == 1;
    }
    ctx.check(changed, "bone meal advanced the sapling");
    // then until it grows
    nw::JavaRandom r(99);
    auto&          region = regionOf(ctx);
    for (int i = 0; i < 40 && nw::isOurSapling(at(ctx, off(p, 0, 1, 0))); ++i) nw::bonemealSapling(region, lp::game::blockPos(off(p, 0, 1, 0)), r);
    auto tree1 = collectTree(ctx, off(p, 0, 1, 0), 16, 32);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "lepton:blossom_log", "a trumpet log replaced the sapling, got " + idAt(ctx, off(p, 0, 1, 0)));
    ctx.check(tree1["lepton:yellow_blossom_leaves"] > 20, "yellow leaves: " + std::to_string(tree1["lepton:yellow_blossom_leaves"]));
    ctx.check(idAt(ctx, p) == "minecraft:dirt", "grass under the trunk became dirt");
});

// The random-tick chain: a planted sapling gets a block tick and grows on its own (random tick speed
// raised so the delay is 1 tick: 1 in 7 per tick and stage, light 15 at noon).
LP_REGISTER_SELFTEST("newwoods.sapling_ticks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(740, 400);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto const* tree = nw::treeByName("ancient");
    stepAside(ctx, p, 10);
    auto& rules = ctx.player().getLevel().getGameRules().mGameRules.get();
    auto  idx   = static_cast<size_t>(GameRules::GameRulesIndex::RandomTickSpeed);
    ctx.require(idx < rules.size() && std::holds_alternative<int>(rules[idx].mValue.get()), "random tick speed rule");
    struct SpeedGuard { // restores the rule even when the test aborts
        std::vector<GameRule>& rules;
        size_t                 idx;
        int                    saved;
        ~SpeedGuard() { rules[idx].mValue = std::variant<cereal::NullType, bool, int, float>(saved); }
    } guard{rules, idx, std::get<int>(rules[idx].mValue.get())};
    rules[idx].mValue = std::variant<cereal::NullType, bool, int, float>(5000);
    ctx.check(nw::maxRandomTickDelay() == 1, "delay 1 at speed 5000, got " + std::to_string(nw::maxRandomTickDelay()));
    // planting schedules the first tick (BlockChangedEvent)
    ctx.require(put(ctx, p, "minecraft:grass_block") && put(ctx, off(p, 0, 1, 0), tree->sapling), "sapling planted");
    {
        auto  sp    = lp::game::blockPos(off(p, 0, 1, 0));
        auto* chunk = regionOf(ctx).getChunkAt(sp);
        Tick  next{};
        ctx.check(chunk && chunk->getTickQueue().getNextUpdateForPos(sp, next), "planting scheduled a block tick");
    }
    bool grown = false;
    for (int i = 0; i < 400 && !grown; i += 5) {
        co_await ctx.ticks(5);
        grown = idAt(ctx, off(p, 0, 1, 0)) == "lepton:ancient_log";
    }
    ctx.info("after the ticks: " + idAt(ctx, off(p, 0, 1, 0)) + " stage "
             + std::to_string(bs::getInt(at(ctx, off(p, 0, 1, 0)), "lepton:stage").value_or(-1)) + ", light "
             + std::to_string(nw::saplingLight(regionOf(ctx), lp::game::blockPos(off(p, 0, 1, 0)))) + ", block ticks "
             + std::to_string(nw::blockTickCount()));
    collectTree(ctx, off(p, 0, 1, 0), 8, 32);
    ctx.check(grown, "the sapling grew from its block ticks");
});

LP_REGISTER_SELFTEST("newwoods.ancient_tree", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(730, 160);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto const* tree = nw::treeByName("ancient");
    ctx.require(tree && plantSapling(ctx, p, *tree), "ashen sapling planted");
    stepAside(ctx, p, 10);
    co_await ctx.ticks(2);
    nw::JavaRandom r(5);
    ctx.require(nw::advanceSapling(regionOf(ctx), lp::game::blockPos(off(p, 0, 1, 0)), r), "the ashen tree grew");
    auto blocks = collectTree(ctx, off(p, 0, 1, 0), 8, 32);
    int  trunk  = 0;
    while (idAt(ctx, off(p, 0, 1 + trunk, 0)) == "lepton:ancient_log") ++trunk;
    ctx.check(trunk >= 18 && trunk <= 28, "trunk 18..28 logs, got " + std::to_string(trunk));
    ctx.check(blocks["lepton:ancient_log"] == trunk, "only the trunk is logs");
    ctx.check(idAt(ctx, off(p, 0, 1 + trunk, 0)) == "lepton:ancient_leaves", "leaf cap above the trunk");
    auto top = off(p, 0, trunk, 0);
    ctx.check(idAt(ctx, off(top, 1, 0, 0)) == "lepton:ancient_leaves" && idAt(ctx, off(top, 0, 0, -1)) == "lepton:ancient_leaves",
        "leaves around the top log");
    // the top blob: 3 rows starting 3 below the top log
    ctx.check(idAt(ctx, off(top, 2, -3, 0)) == "lepton:ancient_leaves", "top blob row");
    ctx.check(idAt(ctx, off(top, 3, -4, 0)) == "lepton:ancient_leaves", "wider middle row");
    ctx.check(!bs::getBool(at(ctx, off(top, 2, -3, 0)), "lepton:persistent").value_or(true), "grown leaves are not persistent");
    ctx.check(blocks["lepton:ancient_leaves"] > 60, "leaves: " + std::to_string(blocks["lepton:ancient_leaves"]));
});

LP_REGISTER_SELFTEST("newwoods.blossom_tree", [](Context& ctx) -> ll::coro::CoroTask<> {
    int i = 0;
    for (auto const& t : nw::treeTypes()) {
        if (t.ancient) continue;
        auto p = ctx.testPos(750, 200 + 40 * i++);
        ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
        ctx.require(plantSapling(ctx, p, t), "sapling planted");
        stepAside(ctx, p, 18);
        co_await ctx.ticks(2);
        nw::JavaRandom r(17 + i);
        // the sapling's own tick chain may already have grown it during the wait
        bool early = idAt(ctx, off(p, 0, 1, 0)) == "lepton:blossom_log";
        if (early) ctx.info(std::string(t.name) + " grew on its own tick before the test advanced it");
        ctx.check(early || nw::advanceSapling(regionOf(ctx), lp::game::blockPos(off(p, 0, 1, 0)), r), std::string(t.name) + " grew");
        auto blocks = collectTree(ctx, off(p, 0, 1, 0), 16, 32);
        std::string leaves(t.leaves);
        ctx.check(blocks["lepton:blossom_log"] >= 9, std::string(t.name) + " logs: " + std::to_string(blocks["lepton:blossom_log"]));
        ctx.check(blocks[leaves] > 40, std::string(t.name) + " leaves: " + std::to_string(blocks[leaves]));
        co_await ctx.ticks(1);
    }
});

LP_REGISTER_SELFTEST("newwoods.tree_blocked", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(770, 120);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto const* tree = nw::treeByName("ancient");
    ctx.require(tree && plantSapling(ctx, p, *tree), "sapling planted");
    ctx.require(put(ctx, off(p, 0, 4, 0), "minecraft:stone"), "stone 3 above the sapling");
    nw::JavaRandom r(3);
    ctx.check(!nw::advanceSapling(regionOf(ctx), lp::game::blockPos(off(p, 0, 1, 0)), r), "no room: no tree");
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == tree->sapling, "the sapling is back, got " + idAt(ctx, off(p, 0, 1, 0)));
    co_return;
});

LP_REGISTER_SELFTEST("newwoods.azalea_tree", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(780, 280);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "minecraft:dirt") && put(ctx, off(p, 0, 1, 0), "minecraft:azalea"), "azalea on dirt");
    stepAside(ctx, p, 6);
    co_await ctx.ticks(2);
    bool grown = false;
    for (int i = 0; i < 30 && !grown; ++i) {
        use(ctx, "minecraft:bone_meal", off(p, 0, 1, 0), Facing::Up, Vec3{0.5f, 0.5f, 0.5f});
        co_await ctx.ticks(10);
        grown = idAt(ctx, off(p, 0, 1, 0)) != "minecraft:azalea";
    }
    auto blocks = collectTree(ctx, off(p, 0, 1, 0), 6, 10);
    ctx.require(grown, "the azalea grew a tree");
    ctx.check(blocks["lepton:azalea_log"] >= 2, "azalea logs: " + std::to_string(blocks["lepton:azalea_log"]));
    ctx.check(blocks["minecraft:oak_log"] == 0, "no oak logs left: " + std::to_string(blocks["minecraft:oak_log"]));
});

LP_REGISTER_SELFTEST("newwoods.fruit_xp", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    player.resetPlayerLevel();
    co_await ctx.ticks(1);
    auto fruit = lp::game::makeItem("lepton:ancient_fruit");
    ctx.require(!fruit.isNull(), "fruit item");
    player.eat(fruit);
    co_await ctx.ticks(1);
    // 10 points: level 0 needs 7, so level 1 with 3 points
    ctx.check(player.getXpEarnedAtCurrentLevel() == 3, "xp at level: " + std::to_string(player.getXpEarnedAtCurrentLevel()));
    ctx.check(nw::fruitExperience() == 10, "default 10 xp");
    auto& c     = lp::config::mut<nw::NewWoodsConfig>();
    auto  saved = c;
    c.ancientFruitGivesExp = false;
    ctx.check(nw::fruitExperience() == 0, "ancientFruitGivesExp off");
    c = saved;
});

LP_REGISTER_SELFTEST("newwoods.loot_tables", [](Context& ctx) {
    auto sniff = nw::lootAddition("loot_tables/gameplay/entities/sniffer_seeds.json");
    ctx.require(sniff.has_value(), "sniffer table gets the sapling");
    ctx.check(sniff->item == "lepton:ancient_sapling" && sniff->weight == 1 && sniff->quality == 0, "weight 1, quality 0");
    ctx.check(!nw::lootAddition("loot_tables/chests/ancient_city.json").has_value(), "ancient city off by default");
    auto& c     = lp::config::mut<nw::NewWoodsConfig>();
    auto  saved = c;
    c.ancientCityLootWeight = 8;
    auto city               = nw::lootAddition("loot_tables/chests/ancient_city");
    ctx.check(city && city->weight == 8 && city->quality == 1, "ancient city weight 8 quality 1");
    c.sniffingLootWeight = 0;
    ctx.check(!nw::lootAddition("loot_tables/gameplay/entities/sniffer_seeds.json"), "weight 0 disables sniffing");
    c.sniffingLootWeight = 1;
    c.enableAncientWood  = false;
    ctx.check(!nw::lootAddition("loot_tables/gameplay/entities/sniffer_seeds.json"), "AncientWood off");
    c = saved;
    // the game's sniffer table, as loaded through the hook: the vanilla seeds + the sapling
    auto level = ll::service::getLevel();
    if (auto* packs = level ? level->getServerResourcePackManager() : nullptr) {
        auto* t = level->getLootTables().lookupByName("loot_tables/gameplay/entities/sniffer_seeds.json", *packs);
        ctx.require(t != nullptr, "sniffer loot table loaded");
        auto const& pools = t->mPools.get();
        ctx.require(!pools.empty(), "sniffer pools");
        auto const& entries = pools[0]->mEntries.get();
        ctx.check(entries.size() == 3, "3 entries (2 seeds + the sapling), got " + std::to_string(entries.size()));
        if (entries.size() == 3) ctx.check(entries[2]->mWeight == 1, "sapling weight 1");
    }
});

LP_REGISTER_SELFTEST("newwoods.worldgen_filters", [](Context& ctx) {
    auto const& c = nw::config();
    ctx.check(nw::biomeAllowed("ice_plains", c.blue.biomes) && nw::biomeAllowed("grove", c.blue.biomes), "frosty: snowy biomes");
    ctx.check(!nw::biomeAllowed("plains", c.blue.biomes), "frosty: not plains");
    ctx.check(nw::biomeAllowed("swampland", c.lavender.biomes) && nw::biomeAllowed("mangrove_swamp", c.lavender.biomes), "serene: swamps");
    ctx.check(nw::biomeAllowed("savanna_plateau", c.orange.biomes), "warm: savanna");
    ctx.check(nw::biomeAllowed("plains", c.yellow.biomes) && nw::biomeAllowed("sunflower_plains", c.yellow.biomes), "sunny: plains");
    ctx.check(!nw::biomeAllowed("forest", c.yellow.biomes), "sunny: not forest");
    ctx.check(nw::biomeAllowed("mesa_bryce", c.red.biomes) && nw::biomeAllowed("minecraft:mesa", c.red.biomes), "fiery: badlands");
    nw::BiomesConfig bl{{false, {"c:is_plains"}}, {true, {"sunflower_plains"}}};
    ctx.check(nw::biomeAllowed("plains", bl) && !nw::biomeAllowed("sunflower_plains", bl), "name blacklist");
    ctx.check(nw::dimensionAllowed("minecraft:overworld", c.blue.dimensions) && !nw::dimensionAllowed("minecraft:the_nether", c.blue.dimensions),
        "overworld only");
    // determinism and rarity
    auto a = nw::blossomRoll(42, 3, -7, 0, 200), b = nw::blossomRoll(42, 3, -7, 0, 200), d = nw::blossomRoll(43, 3, -7, 0, 200);
    ctx.check(a.x == b.x && a.z == b.z && a.rarityHit == b.rarityHit, "same seed, same roll");
    ctx.check(a.x >= 48 && a.x < 64 && a.z >= -112 && a.z < -96, "the column is in the chunk");
    ctx.check(d.x >= 48 && d.x < 64, "another seed stays in the chunk");
    int hits = 0;
    for (int cx = 0; cx < 200; ++cx)
        for (int cz = 0; cz < 100; ++cz) hits += nw::blossomRoll(12345, cx, cz, 4, 30).rarityHit ? 1 : 0;
    double rate = hits / 20000.0;
    ctx.check(std::abs(rate - 1.0 / 30) < 0.006, "1 in 30 chunks: " + std::to_string(rate));
});

// A generated azalea tree (rooted dirt, oak trunk, azalea leaves) in a decorated chunk gets azalea logs.
LP_REGISTER_SELFTEST("newwoods.azalea_gen", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(784, 320);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p, 6);
    ctx.require(put(ctx, p, "minecraft:dirt_with_roots"), "rooted dirt");
    for (int y = 1; y <= 3; ++y) ctx.require(put(ctx, off(p, 0, y, 0), "minecraft:oak_log"), "trunk");
    ctx.require(put(ctx, off(p, 1, 3, 0), "minecraft:oak_log", {{"pillar_axis", "x"}}), "branch");
    for (int dx = -1; dx <= 2; ++dx)
        for (int dz = -1; dz <= 1; ++dz) put(ctx, off(p, dx, 4, dz), dz == 0 ? "minecraft:azalea_leaves_flowered" : "minecraft:azalea_leaves");
    ctx.require(put(ctx, off(p, 3, 1, 0), "minecraft:oak_log"), "a lone oak log nearby");
    auto bp = lp::game::blockPos(p);
    int  n  = nw::replaceAzaleaTreesInChunk(regionOf(ctx), bp.x >> 4, bp.z >> 4);
    ctx.check(n == 4, "4 logs replaced, got " + std::to_string(n));
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "lepton:azalea_log", "trunk is azalea");
    auto const& branch = at(ctx, off(p, 1, 3, 0));
    ctx.check(branch.getTypeName() == "lepton:azalea_log" && bs::getString(branch, "minecraft:block_face").value_or("") == "east",
        "branch keeps its x axis");
    ctx.check(idAt(ctx, off(p, 3, 1, 0)) == "minecraft:oak_log", "the lone oak log stays");
});

// Real world generation (server scenario newwoods-worldgen, level-type DEFAULT): trumpet trees stand
// where the generator rolled them. Skipped in flat worlds.
LP_REGISTER_SELFTEST("newwoods.worldgen_live", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    if (player.getLevel().getLevelData().getGenerator() != GeneratorType::Overworld) {
        ctx.info("not a generated overworld: skipped");
        co_return;
    }
    player.setPlayerGameType(GameType::Creative);
    auto spawn      = ctx.testPos(799);
    auto candidates = nw::blossomCandidates(spawn.x >> 4, spawn.z >> 4, 40);
    ctx.info(std::to_string(candidates.size()) + " candidate columns within 40 chunks");
    ctx.require(!candidates.empty(), "blossom tree candidates near the spawn");
    int found = 0, tried = 0;
    for (auto const& c : candidates) {
        if (tried++ >= 12 || found > 0) break;
        GP top{0, c.pos.x, std::min(c.pos.y + 40, 300), c.pos.z};
        if (!co_await ctx.ensureLoaded(top)) continue;
        co_await ctx.ticks(80); // neighbours generate, post-processing runs
        auto&       region = regionOf(ctx);
        auto const& tree   = nw::treeTypes()[1 + c.colour];
        int         logs = 0, leaves = 0;
        for (int x = -16; x <= 16; ++x)
            for (int z = -16; z <= 16; ++z)
                for (int y = -20; y <= 40; ++y) {
                    BlockPos q{c.pos.x + x, c.pos.y + y, c.pos.z + z};
                    if (!region.hasChunksAt(q, 0, false)) continue;
                    auto const& n = region.getBlock(q).getTypeName();
                    if (n == "lepton:blossom_log") ++logs;
                    else if (n == tree.leaves) ++leaves;
                }
        ctx.info("candidate " + std::string(tree.name) + " at " + std::to_string(c.pos.x) + " " + std::to_string(c.pos.z) + ": "
                 + std::to_string(logs) + " logs, " + std::to_string(leaves) + " leaves");
        if (logs > 0 && leaves > 0) ++found;
    }
    ctx.info(nw::genStats());
    ctx.check(found > 0, "a generated trumpet tree was found");
    player.setPlayerGameType(GameType::Survival);
});

LP_REGISTER_SELFTEST("newwoods.fuel_compost", [](Context& ctx) {
    ctx.check(nw::fuelTicks("lepton:azalea_planks") == 300 && nw::fuelTicks("lepton:blossom_log") == 300, "planks / logs 300");
    ctx.check(nw::fuelTicks("lepton:ancient_planks_slab") == 150 && nw::fuelTicks("lepton:azalea_door") == 200, "slab 150, door 200");
    ctx.check(nw::fuelTicks("lepton:red_blossom_sapling") == 100 && nw::fuelTicks("lepton:ancient_leaves") == 0, "sapling 100, leaves none");
    auto burn = [](char const* id) { return FurnaceBlockActor::getBurnDuration(lp::game::makeItem(id), 200.0f); };
    ctx.check(burn("lepton:azalea_planks") > 0 && burn("lepton:azalea_planks") == burn("minecraft:oak_planks"), "furnace burns azalea planks like oak");
    ctx.check(burn("lepton:blue_blossom_sapling") > 0, "furnace burns saplings");
    ctx.check(ComposterBlock::getFillChance(lp::game::makeItem("lepton:ancient_fruit")) == 65, "fruit composts 65%");
    ctx.check(ComposterBlock::getFillChance(lp::game::makeItem("lepton:lavender_blossom_leaves")) == 30, "leaves compost 30%");
    ctx.check(ComposterBlock::getFillChance(lp::game::makeItem("lepton:ancient_sapling")) == 30, "saplings compost 30%");
});

// The gallery for the client screenshots (docs §8): one row per wood set (log, wood, stripped log and
// wood, planks, stairs, slab, vertical slab, fences + gate, door, trapdoor, button, pressure plate,
// vertical planks, bookshelf, ladder, posts, hollow log), one row per tree (leaves, sapling, hedges,
// leaf carpet, potted sapling), and grown trees (ashen + the five trumpets). With a real player
// online the player is moved in front of each part ("GALLERY view <name>" lines).
LP_REGISTER_SELFTEST("newwoods.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(700, 60);
    ctx.require(co_await ctx.ensureLoaded(base), "gallery area loaded");
    int  placed = 0, wanted = 0;
    auto place  = [&](GP const& p, std::string const& id, bs::Changes const& st = {}) {
        ++wanted;
        if (put(ctx, p, id, st)) ++placed;
        else ctx.check(false, "placed " + id);
    };
    std::vector<GP> woodRows, treeRows;
    int             wi = 0;
    for (auto w : {"azalea", "blossom", "ancient"}) {
        std::string s(w);
        auto        row = off(base, 0, 0, -6 * wi++); // rows go north, each viewed from its south side
        woodRows.push_back(row);
        auto at = [&](int i, int dy = 0) { return off(row, i, dy, 0); };
        place(at(0), "lepton:" + s + "_log");
        place(at(1), "lepton:" + s + "_wood");
        place(at(2), "lepton:stripped_" + s + "_log", {{"minecraft:block_face", std::string("east")}});
        place(at(3), "lepton:stripped_" + s + "_wood");
        place(at(4), "lepton:" + s + "_planks");
        place(at(5), "lepton:" + s + "_planks_stairs", {{"minecraft:cardinal_direction", std::string("north")}});
        place(at(6), "lepton:" + s + "_planks_slab");
        place(at(7), "lepton:" + s + "_planks_vertical_slab", {{"lepton:type", std::string("south")}});
        place(at(8), "lepton:" + s + "_fence", {{"lepton:east", true}});
        place(at(9), "lepton:" + s + "_fence_gate", {{"minecraft:cardinal_direction", std::string("east")}});
        place(at(10), "lepton:" + s + "_door", {{"minecraft:cardinal_direction", std::string("north")}});
        place(at(10, 1), "lepton:" + s + "_door", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:upper", true}});
        place(at(11), "lepton:" + s + "_trapdoor", {{"minecraft:cardinal_direction", std::string("north")}});
        place(at(12), "lepton:" + s + "_planks");
        place(at(12, 1), "lepton:" + s + "_button", {{"minecraft:block_face", std::string("up")}});
        place(at(13), "lepton:" + s + "_pressure_plate");
        place(at(14), "lepton:vertical_" + s + "_planks");
        place(at(15), "lepton:" + s + "_bookshelf");
        place(off(row, 16, 0, -1), "lepton:" + s + "_planks");
        place(off(row, 16, 1, -1), "lepton:" + s + "_planks");
        place(at(16), "lepton:" + s + "_ladder", {{"minecraft:block_face", std::string("south")}});
        place(at(16, 1), "lepton:" + s + "_ladder", {{"minecraft:block_face", std::string("south")}});
        place(at(17), "lepton:" + s + "_post");
        place(at(18), "lepton:stripped_" + s + "_post", {{"minecraft:block_face", std::string("east")}});
        place(at(19), "lepton:hollow_" + s + "_log", {{"minecraft:block_face", std::string("south")}});
    }
    int ti = 0;
    for (auto const& t : nw::treeTypes()) {
        auto row = off(base, 0, 0, -20 - 4 * ti++);
        treeRows.push_back(row);
        std::string n(t.name);
        place(row, std::string(t.leaves));
        place(off(row, 1, 0, 0), std::string(t.leaves));
        place(off(row, 1, 1, 0), std::string(t.leaves));
        place(off(row, 3, 0, 0), "minecraft:grass_block");
        place(off(row, 3, 1, 0), std::string(t.sapling));
        place(off(row, 5, 0, 0), "lepton:" + n + "_hedge", {{"lepton:east", true}});
        place(off(row, 6, 0, 0), "lepton:" + n + "_hedge", {{"lepton:west", true}});
        place(off(row, 6, 1, 0), "lepton:" + n + "_hedge", {{"lepton:extend", true}});
        place(off(row, 8, 0, 0), "minecraft:grass_block");
        place(off(row, 8, 1, 0), "lepton:" + n + "_leaf_carpet");
        place(off(row, 9, 0, 0), "minecraft:grass_block");
        place(off(row, 9, 1, 0), "lepton:" + n + "_leaf_carpet");
    }
    // the potted saplings, in their own chunk (z - 64)
    auto potRow = off(base, 0, 0, -64);
    {
        int k = 0;
        for (auto const& t : nw::treeTypes()) {
            place(off(potRow, 2 * k, 0, 0), "minecraft:stone");
            place(off(potRow, 2 * k++, 1, 0), "lepton:potted_" + std::string(t.name) + "_sapling");
        }
    }
    ctx.check(placed == wanted, "placed " + std::to_string(placed) + " / " + std::to_string(wanted) + " gallery blocks");

    // grown trees: the ashen tree and the five trumpets in a line
    auto forest = ctx.testPos(770, 100); // the trees stand in a line along z
    int  grown  = 0, k = 0;
    for (auto const& t : nw::treeTypes()) {
        auto p = off(forest, 0, 0, 30 * k++);
        if (!co_await ctx.ensureLoaded(p)) continue;
        if (!plantSapling(ctx, p, t)) continue;
        nw::JavaRandom r(1000 + k);
        if (nw::advanceSapling(regionOf(ctx), lp::game::blockPos(off(p, 0, 1, 0)), r)) ++grown;
        collectTree(ctx, off(p, 0, 1, 0), 16, 32);
    }
    ctx.check(grown == 6, "6 grown trees, got " + std::to_string(grown));

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](GP const& at, float dx, float dy, float dz, float pitch, float yaw) {
        if (auto* v = ctx.player().getLevel().getPlayer(uid)) {
            v->teleport(
                Vec3{static_cast<float>(at.x) + dx, static_cast<float>(at.y) + dy, static_cast<float>(at.z) + dz},
                DimensionType(at.dim),
                Vec2{pitch, yaw}
            );
        }
    };
    struct View {
        std::string name;
        GP          at;
        float       dx, dy, dz, pitch, yaw;
    };
    std::vector<View> views;
    char const*       woodNames[] = {"azalea", "blossom", "ancient"};
    for (int i = 0; i < 3; ++i) {
        views.push_back({std::string(woodNames[i]) + "-left", woodRows[i], 5.f, 1.f, 5.5f, 15.f, 180.f});
        views.push_back({std::string(woodNames[i]) + "-right", woodRows[i], 15.f, 1.f, 5.5f, 15.f, 180.f});
    }
    views.push_back({"trees-rows-a", treeRows[0], 6.f, 3.f, 4.5f, 25.f, 180.f});
    views.push_back({"trees-rows-b", treeRows[2], 6.f, 3.f, 4.5f, 25.f, 180.f});
    views.push_back({"trees-rows-c", treeRows[4], 6.f, 3.f, 4.5f, 25.f, 180.f});
    views.push_back({"potted", potRow, 5.f, 2.f, 4.5f, 25.f, 180.f});
    views.push_back({"trees-a", forest, 28.f, 8.f, 30.5f, 5.f, 90.f}); // east of the line, looking west
    views.push_back({"trees-b", forest, 28.f, 8.f, 120.5f, 5.f, 90.f});
    for (auto const& v : views) {
        ctx.info("GALLERY view " + v.name);
        for (int t = 0; t < 120; ++t) { // hold the player there (no floor below some views)
            view(v.at, v.dx, v.dy, v.dz, v.pitch, v.yaw);
            co_await ctx.ticks(1);
        }
    }
    view(woodRows[0], 5.f, 1.f, 5.5f, 15.f, 180.f);
    co_await ctx.ticks(10);
    ctx.info("GALLERY done");
});
