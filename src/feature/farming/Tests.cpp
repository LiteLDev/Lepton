// Selftests of U25 farming (dx 2500..2599; structures stay within dz 0..4).
#include "feature/farming/Farming.h"

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components/AgeableComponent.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffectIds.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
namespace bs   = lp::game::blockstates;
namespace ent  = lp::game::entities;
namespace farm = lp::feature::farming;
using lp::game::makeItem;
using Facing = ScriptModuleMinecraft::ScriptFacing;

BlockPos bp(lp::core::GlobalPos const& p, int dx = 0, int dy = 0, int dz = 0) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }

/// Sets a block with typed states, cleaned up after the test.
bool put(Context& ctx, BlockSource& region, BlockPos const& pos, std::string_view id, bs::Changes const& states = {}) {
    auto const* b = bs::of(id, states);
    if (!b) return false;
    ctx.cleanupBlock(lp::game::globalPos(0, pos));
    return lp::game::setBlock(region, pos, *b);
}

std::string idAt(BlockSource& region, BlockPos const& pos) { return region.getBlock(pos).getTypeName(); }

int intAt(BlockSource& region, BlockPos const& pos, std::string_view state) {
    return bs::getInt(region.getBlock(pos), state).value_or(-1);
}

AABB around(BlockPos const& a, BlockPos const& b) {
    return AABB{
        Vec3{static_cast<float>(std::min(a.x, b.x)) - 1.f, static_cast<float>(std::min(a.y, b.y)) - 1.f, static_cast<float>(std::min(a.z, b.z)) - 1.f},
        Vec3{static_cast<float>(std::max(a.x, b.x)) + 2.f, static_cast<float>(std::max(a.y, b.y)) + 3.f, static_cast<float>(std::max(a.z, b.z)) + 2.f}
    };
}

int countItems(BlockSource& region, AABB const& box, std::string const& id) {
    int n = 0;
    for (auto* a : ent::inBox(region, box, "minecraft:item")) {
        auto& item = static_cast<ItemActor&>(*a).mItem.get();
        if (id.empty() || item.getTypeName() == id) n += item.mCount;
    }
    return n;
}

void clearEntities(BlockSource& region, AABB const& box, std::string_view type) {
    for (auto* a : ent::inBox(region, box, type)) ent::despawn(*a);
}

void hand(Player& p, ItemStack const& item) { p.setSelectedItem(item); }

/// Removes the water of a test pond before the cleanup takes its floor away (or it floods the area).
void drain(BlockSource& region, BlockPos const& a, BlockPos const& b) {
    for (int x = a.x; x <= b.x; ++x)
        for (int y = a.y; y <= b.y; ++y)
            for (int z = a.z; z <= b.z; ++z) {
                BlockPos p{x, y, z};
                auto const& id = region.getBlock(p).getTypeName();
                if (id == "minecraft:water" || id == "minecraft:flowing_water" || id == "lepton:water_pink_petals")
                    lp::game::clearBlock(region, p);
            }
}

/// Restores the config after a test changed it.
struct ConfigGuard {
    farm::FarmingConfig saved = lp::config::get<farm::FarmingConfig>();
    ~ConfigGuard() {
        lp::config::mut<farm::FarmingConfig>() = saved;
        farm::rebuildHarvestTables();
    }
};

bool interact(Player& p, Actor& target) { return p.mGameMode->interact(target, target.getPosition(), HandSlot::Mainhand); }

/// The game's own "use item on block" (what a client's click runs on the server).
void useOn(Player& p, ItemStack& item, BlockPos const& pos, int face) {
    auto const& block = p.getDimensionBlockSource().getBlock(pos);
    Vec3        hit{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + (face == 1 ? 1.f : 0.f), static_cast<float>(pos.z) + 0.5f};
    p.mGameMode->useItemOn(item, pos, static_cast<uchar>(face), hit, HandSlot::Mainhand, &block, true);
}

} // namespace

LP_REGISTER_SELFTEST("farming.config", [](Context& ctx) {
    farm::FarmingConfig d;
    ctx.check(d.enableSimpleHarvest && d.emptyHandHarvest && !d.harvestingCostsDurability, "SimpleHarvest defaults");
    ctx.check(d.harvestableBlocks.size() == 9 && d.rightClickableBlocks.size() == 3, "crop lists");
    ctx.check(d.regularHoeRadius == 2 && d.highTierHoeRadius == 3, "hoe radii");
    ctx.check(d.minPigLitterSize == 2 && d.maxPigLitterSize == 3 && d.maxGoldenCarrotBoost == 2, "litter sizes");
    ctx.check(d.chance == 0.1 && d.boneMealChance == 0.2 && d.slownessLevel == 1, "chances");
    ConfigGuard g;
    lp::config::mut<farm::FarmingConfig>().regularHoeRadius = 9;
    ctx.check(farm::cfg().regularHoeRadius == 5, "radius clamped to 5 (Quark @Config.Max)");
});

LP_REGISTER_SELFTEST("farming.harvest_parse", [](Context& ctx) {
    auto r = farm::parseHarvestRule("minecraft:cocoa[age=2,direction=1],minecraft:cocoa[age=0,direction=1]");
    ctx.require(r.has_value(), "cocoa rule parses");
    ctx.check(r->id == "minecraft:cocoa" && r->states.size() == 2 && r->states[1].second == "1", "harvest state");
    ctx.check(r->afterId == "minecraft:cocoa" && r->afterStates.size() == 2 && r->afterStates[0].second == "0", "after state");
    auto w = farm::parseHarvestRule("minecraft:wheat[growth=7]");
    ctx.check(w && w->afterId == "minecraft:wheat" && w->afterStates.empty(), "default after = default state");
    ctx.check(!farm::parseHarvestRule("minecraft:wheat[growth]"), "missing value rejected");
    ctx.check(!farm::parseHarvestRule("minecraft:wheat[growth=7"), "unclosed bracket rejected");
    auto const* ripe = bs::of("minecraft:wheat", {{"growth", 7}});
    auto const* young = bs::of("minecraft:wheat", {{"growth", 3}});
    ctx.require(ripe && young, "wheat states");
    ctx.check(farm::actionFor(*ripe, false) == farm::HarvestAction::Harvest, "ripe wheat is harvestable");
    ctx.check(farm::actionFor(*young, false) == farm::HarvestAction::None, "young wheat is not");
    auto const* berries = bs::of("minecraft:sweet_berry_bush", {{"growth", 3}});
    ctx.check(berries && farm::actionFor(*berries, true) == farm::HarvestAction::Click, "berry bush is clicked in hoe range");
    ctx.check(berries && farm::actionFor(*berries, false) == farm::HarvestAction::None, "but not with range 1");
    ctx.check(farm::hoeRange(makeItem("minecraft:iron_hoe")) == 2 && farm::hoeRange(makeItem("minecraft:netherite_hoe")) == 3,
              "hoe ranges");
    ctx.check(farm::hoeRange(makeItem("minecraft:stick")) == 1 && farm::hoeRange(ItemStack{}) == 1, "non hoes range 1");
});

LP_REGISTER_SELFTEST("farming.simple_harvest", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto base = ctx.testPos(2500);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    // a 5x5 farmland field with ripe wheat
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 5; ++z) {
            put(ctx, region, bp(base, x, 0, z), "minecraft:farmland");
            put(ctx, region, bp(base, x, 1, z), "minecraft:wheat", {{"growth", 7}});
        }
    auto box = around(bp(base), bp(base, 4, 1, 4));
    clearEntities(region, box, "minecraft:item");

    hand(p, ItemStack{});
    ctx.check(farm::simpleHarvestClick(p, bp(base, 0, 1, 0), false), "empty hand harvests ripe wheat");
    ctx.check(idAt(region, bp(base, 0, 1, 0)) == "minecraft:wheat" && intAt(region, bp(base, 0, 1, 0), "growth") == 0,
              "wheat is replanted at growth 0");
    ctx.check(intAt(region, bp(base, 1, 1, 0), "growth") == 7, "range 1 leaves the neighbours");
    co_await ctx.ticks(2);
    ctx.check(countItems(region, box, "minecraft:wheat") >= 1, "wheat dropped");
    ctx.check(!farm::simpleHarvestClick(p, bp(base, 0, 1, 0), false), "young wheat is not harvested");

    ctx.check(farm::simpleHarvestClick(p, bp(base, 1, 0, 0), false), "clicking the farmland harvests the crop above");
    ctx.check(intAt(region, bp(base, 1, 1, 0), "growth") == 0, "crop above replanted");

    hand(p, makeItem("minecraft:wheat_seeds", 3));
    ctx.check(farm::simpleHarvestClick(p, bp(base, 2, 1, 0), false), "any item harvests with emptyHandHarvest");
    lp::config::mut<farm::FarmingConfig>().emptyHandHarvest = false;
    ctx.check(!farm::simpleHarvestClick(p, bp(base, 3, 1, 0), false), "without emptyHandHarvest only hoes harvest");
    hand(p, ItemStack{});
    ctx.check(!farm::simpleHarvestClick(p, bp(base, 3, 1, 0), false), "...not empty hands either");
    lp::config::mut<farm::FarmingConfig>().emptyHandHarvest = true;

    // hoe: 3x3 around the centre (2,2)
    hand(p, makeItem("minecraft:iron_hoe"));
    ctx.check(farm::simpleHarvestClick(p, bp(base, 2, 1, 2), false), "iron hoe harvests");
    int replanted = 0;
    for (int x = 1; x <= 3; ++x)
        for (int z = 1; z <= 3; ++z) replanted += intAt(region, bp(base, x, 1, z), "growth") == 0;
    ctx.check(replanted == 9, "iron hoe harvests 3x3 (" + std::to_string(replanted) + ")");
    ctx.check(intAt(region, bp(base, 4, 1, 4), "growth") == 7, "outside the 3x3 untouched");
    // diamond hoe: 5x5
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 5; ++z) put(ctx, region, bp(base, x, 1, z), "minecraft:wheat", {{"growth", 7}});
    hand(p, makeItem("minecraft:diamond_hoe"));
    ctx.check(farm::simpleHarvestClick(p, bp(base, 2, 1, 2), false), "diamond hoe harvests");
    replanted = 0;
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 5; ++z) replanted += intAt(region, bp(base, x, 1, z), "growth") == 0;
    ctx.check(replanted == 25, "diamond hoe harvests 5x5 (" + std::to_string(replanted) + ")");

    // tillable blocks are never harvest targets
    put(ctx, region, bp(base, 6, 0, 0), "minecraft:grass_block");
    ctx.check(!farm::simpleHarvestClick(p, bp(base, 6, 0, 0), false), "grass is tilled, not harvested");

    // cocoa on a jungle log (logs all around so every direction is supported)
    auto c = bp(base, 8, 1, 2);
    for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) put(ctx, region, BlockPos{c.x + dx, c.y, c.z + dz}, "minecraft:jungle_log");
    put(ctx, region, c, "minecraft:cocoa", {{"age", 2}, {"direction", 1}});
    hand(p, ItemStack{});
    ctx.check(farm::simpleHarvestClick(p, c, false), "ripe cocoa harvested");
    ctx.check(intAt(region, c, "age") == 0 && intAt(region, c, "direction") == 1, "cocoa replanted facing the same way");

    // the real interaction path (a hoe reaches useItemOn for a simulated player)
    put(ctx, region, bp(base, 11, 0, 2), "minecraft:farmland");
    put(ctx, region, bp(base, 11, 1, 2), "minecraft:carrots", {{"growth", 7}});
    p.teleport(Vec3{static_cast<float>(base.x) + 11.5f, static_cast<float>(base.y) + 1.f, static_cast<float>(base.z) + 4.5f}, p.getDimensionId());
    auto& sim = static_cast<SimulatedPlayer&>(p);
    auto  hoe = makeItem("minecraft:wooden_hoe");
    hand(p, hoe);
    co_await ctx.ticks(10);
    // SimulatedPlayer refuses to click blocks without a collision box: drive GameMode::useItemOn
    useOn(p, hoe, bp(base, 11, 1, 2), 1);
    (void)sim;
    for (int i = 0; i < 10 && intAt(region, bp(base, 11, 1, 2), "growth") != 0; ++i) co_await ctx.ticks(1);
    ctx.check(intAt(region, bp(base, 11, 1, 2), "growth") == 0, "using a hoe on ripe carrots harvests them");
    co_await ctx.ticks(2);
    clearEntities(region, around(bp(base), bp(base, 12, 1, 4)), "minecraft:item");
    hand(p, ItemStack{});
});

LP_REGISTER_SELFTEST("farming.hoe_harvest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2520);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 5; ++z) {
            put(ctx, region, bp(base, x, 0, z), "minecraft:grass_block");
            put(ctx, region, bp(base, x, 1, z), "minecraft:short_grass");
        }
    put(ctx, region, bp(base, 1, 0, 1), "minecraft:farmland");
    put(ctx, region, bp(base, 1, 1, 1), "minecraft:wheat", {{"growth", 3}});
    hand(p, makeItem("minecraft:stone_hoe"));
    auto const& centre = region.getBlock(bp(base, 2, 1, 2));
    ctx.require(farm::isHoeHarvestable(centre), "short grass is hoe harvestable");
    farm::hoeHarvest(p, bp(base, 2, 1, 2), centre);
    int gone = 0;
    for (int x = 1; x <= 3; ++x)
        for (int z = 1; z <= 3; ++z) gone += region.getBlock(bp(base, x, 1, z)).isAir();
    ctx.check(gone == 7, "3x3 minus the centre and the young wheat broke (" + std::to_string(gone) + ")");
    ctx.check(idAt(region, bp(base, 1, 1, 1)) == "minecraft:wheat", "young wheat is kept");
    ctx.check(idAt(region, bp(base, 0, 1, 0)) == "minecraft:short_grass", "outside the 3x3 kept");
    hand(p, makeItem("minecraft:netherite_hoe"));
    farm::hoeHarvest(p, bp(base, 2, 1, 2), centre);
    ctx.check(region.getBlock(bp(base, 0, 1, 0)).isAir() && region.getBlock(bp(base, 4, 1, 4)).isAir(), "netherite hoe reaches 5x5");
    auto const* leaves = bs::of("minecraft:oak_leaves", {{"persistent_bit", true}});
    ctx.check(leaves && !farm::isHoeHarvestable(*leaves), "placed leaves are not hoe harvestable");
    auto const* ripe = bs::of("minecraft:cocoa", {{"age", 2}});
    auto const* raw  = bs::of("minecraft:cocoa", {{"age", 1}});
    ctx.check(ripe && raw && farm::isHoeHarvestable(*ripe) && !farm::isHoeHarvestable(*raw), "cocoa only when ripe");
    co_await ctx.ticks(2);
    clearEntities(region, around(bp(base), bp(base, 4, 1, 4)), "minecraft:item");
    hand(p, ItemStack{});
});

LP_REGISTER_SELFTEST("farming.right_click_crops", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2535);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    put(ctx, region, bp(base, 0, 0, 1), "minecraft:farmland");
    put(ctx, region, bp(base, 0, 1, 1), "minecraft:wheat", {{"growth", 7}});
    put(ctx, region, bp(base, 1, 0, 1), "minecraft:grass_block");
    put(ctx, region, bp(base, 1, 1, 1), "minecraft:sweet_berry_bush", {{"growth", 7}});
    hand(p, makeItem("minecraft:iron_hoe"));
    ctx.check(farm::simpleHarvestClick(p, bp(base, 0, 1, 1), false), "hoe click harvests");
    ctx.check(intAt(region, bp(base, 1, 1, 1), "growth") < 7, "the berry bush next to it was picked");
    co_await ctx.ticks(2);
    clearEntities(region, around(bp(base), bp(base, 1, 1, 1)), "minecraft:item");
    hand(p, ItemStack{});
});

LP_REGISTER_SELFTEST("farming.coral_on_cactus", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2540);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    put(ctx, region, bp(base, 0, 0, 1), "minecraft:sand");
    put(ctx, region, bp(base, 0, 1, 1), "minecraft:cactus");
    put(ctx, region, bp(base, 0, 2, 1), "minecraft:tube_coral_fan");
    put(ctx, region, bp(base, 2, 0, 1), "minecraft:stone");
    put(ctx, region, bp(base, 2, 1, 1), "minecraft:tube_coral_fan");
    // a neighbour update runs the survival/alive checks
    put(ctx, region, bp(base, 1, 2, 1), "minecraft:stone");
    put(ctx, region, bp(base, 3, 1, 1), "minecraft:stone");
    bool controlDied = false;
    for (int i = 0; i < 300 && !controlDied; i += 10) {
        co_await ctx.ticks(10);
        controlDied = idAt(region, bp(base, 2, 1, 1)) != "minecraft:tube_coral_fan";
    }
    ctx.info(controlDied ? "a fan on stone died" : "the fan on stone did not die within 15 s");
    ctx.check(idAt(region, bp(base, 0, 2, 1)) == "minecraft:tube_coral_fan", "the fan on the cactus stays alive");

    // placing a fan on a cactus
    put(ctx, region, bp(base, 4, 0, 1), "minecraft:sand");
    put(ctx, region, bp(base, 4, 1, 1), "minecraft:cactus");
    ctx.cleanupBlock(lp::game::globalPos(0, bp(base, 4, 2, 1)));
    auto& sim = static_cast<SimulatedPlayer&>(p);
    auto  fan = makeItem("minecraft:brain_coral_fan", 2);
    hand(p, fan);
    co_await ctx.ticks(10);
    sim.simulateUseItemOnBlock(fan, bp(base, 4, 1, 1), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    for (int i = 0; i < 10 && idAt(region, bp(base, 4, 2, 1)) != "minecraft:brain_coral_fan"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(region, bp(base, 4, 2, 1)) == "minecraft:brain_coral_fan", "a coral fan can be placed on a cactus");
    hand(p, ItemStack{});
});

LP_REGISTER_SELFTEST("farming.spore_blossom", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto base = ctx.testPos(2545);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    put(ctx, region, bp(base, 0, 3, 1), "minecraft:stone");
    put(ctx, region, bp(base, 0, 2, 1), "minecraft:spore_blossom");
    auto box = around(bp(base, 0, 0, 1), bp(base, 0, 3, 1));
    clearEntities(region, box, "minecraft:item");
    lp::config::mut<farm::FarmingConfig>().boneMealChance = 1.0;
    ctx.check(farm::boneMealSporeBlossom(region, bp(base, 0, 2, 1)), "bone meal works on spore blossoms");
    co_await ctx.ticks(2);
    ctx.check(countItems(region, box, "minecraft:spore_blossom") == 1, "chance 1.0 drops a spore blossom");
    clearEntities(region, box, "minecraft:item");
    lp::config::mut<farm::FarmingConfig>().boneMealChance = 0.0;
    ctx.check(!farm::boneMealSporeBlossom(region, bp(base, 0, 2, 1)), "chance 0 disables it");
    lp::config::mut<farm::FarmingConfig>().boneMealChance = 1.0;

    // through the bone meal item (FertilizerItem hook)
    p.setPlayerGameType(GameType::Survival);
    auto& sim = static_cast<SimulatedPlayer&>(p);
    auto  bm  = makeItem("minecraft:bone_meal", 5);
    hand(p, bm);
    co_await ctx.ticks(10);
    useOn(p, bm, bp(base, 0, 2, 1), 0);
    (void)sim;
    co_await ctx.ticks(3);
    ctx.check(bm.mCount == 4, "one bone meal used (" + std::to_string(bm.mCount) + " left)");
    ctx.check(countItems(region, box, "minecraft:spore_blossom") == 1, "the spore blossom popped");
    clearEntities(region, box, "minecraft:item");
    hand(p, ItemStack{});
});

LP_REGISTER_SELFTEST("farming.water_petals", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2548);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    // a 1-deep pond: stone floor and rim around water at y+1
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 5; ++z) {
            put(ctx, region, bp(base, x, 0, z), "minecraft:stone");
            bool rim = x == 0 || z == 0 || x == 4 || z == 4;
            put(ctx, region, bp(base, x, 1, z), rim ? "minecraft:stone" : "minecraft:water");
            ctx.cleanupBlock(lp::game::globalPos(0, bp(base, x, 2, z)));
        }
    co_await ctx.ticks(2);
    auto w = bp(base, 2, 1, 2);
    auto a = bp(base, 2, 2, 2);
    for (int i = 1; i <= 4; ++i) {
        ctx.check(farm::placePetal(p, region, w, 1, false), "petal " + std::to_string(i) + " placed");
        ctx.check(idAt(region, a) == "lepton:water_pink_petals" && intAt(region, a, "lepton:amount") == i, "amount " + std::to_string(i));
    }
    ctx.check(!farm::placePetal(p, region, a, 1, true), "no fifth petal");

    // the real interaction: pink petals on the pond floor (top face) under 1-deep water
    auto& sim    = static_cast<SimulatedPlayer&>(p);
    auto  petals = makeItem("minecraft:pink_petals", 4);
    hand(p, petals);
    co_await ctx.ticks(10);
    sim.simulateUseItemOnBlock(petals, bp(base, 1, 0, 1), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    for (int i = 0; i < 10 && idAt(region, bp(base, 1, 2, 1)) != "lepton:water_pink_petals"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(region, bp(base, 1, 2, 1)) == "lepton:water_pink_petals", "using petals on the pond floor puts them on the water");

    // no water source below: the petals break
    put(ctx, region, bp(base, 3, 1, 3), "minecraft:water");
    co_await ctx.ticks(2);
    ctx.check(farm::placePetal(p, region, bp(base, 3, 1, 3), 1, false), "petals on the corner water");
    put(ctx, region, bp(base, 3, 1, 3), "minecraft:stone");
    for (int i = 0; i < 10 && idAt(region, bp(base, 3, 2, 3)) == "lepton:water_pink_petals"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(region, bp(base, 3, 2, 3)) != "lepton:water_pink_petals", "petals without water below break");
    drain(region, bp(base, -1, 0, -1), bp(base, 5, 2, 5));
    co_await ctx.ticks(2);
    clearEntities(region, around(bp(base), bp(base, 4, 2, 4)), "minecraft:item");
    hand(p, ItemStack{});
});

LP_REGISTER_SELFTEST("farming.poison_potato", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2557);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    p.setPlayerGameType(GameType::Survival);
    Vec3  at{static_cast<float>(base.x) + 0.5f, static_cast<float>(base.y) + 0.f, static_cast<float>(base.z) + 2.5f};
    auto* baby    = ent::spawn(region, "minecraft:pig<minecraft:entity_born>", at);
    auto* control = ent::spawn(region, "minecraft:pig<minecraft:entity_born>", Vec3{at.x + 2, at.y, at.z});
    auto* adult   = ent::spawn(region, "minecraft:pig", Vec3{at.x + 4, at.y, at.z});
    ctx.require(baby && control && adult, "pigs spawned");
    auto babyId = ent::idOf(*baby), controlId = ent::idOf(*control), adultId = ent::idOf(*adult);
    co_await ctx.ticks(2);
    baby = ent::find(babyId);
    ctx.require(baby && baby->isBaby(), "the entity_born pig is a baby");
    hand(p, makeItem("minecraft:poisonous_potato", 3));
    ctx.check(farm::usePoisonPotato(p, *baby, true), "poisoned");
    ctx.check(farm::isPoisoned(*baby), "marked as poisoned");
    ctx.check(baby->getEffect(static_cast<uint>(MobEffectIds::Poison)) != nullptr, "poison effect applied");
    auto age = baby->getEntityContext().tryGetComponent<AgeableComponent>();
    ctx.check(age && age->mGrowthPaused, "growth paused");
    ctx.check(p.getSelectedItem().mCount == 2, "one potato used");
    ctx.check(!farm::usePoisonPotato(p, *baby, true), "an already poisoned baby ignores potatoes");
    if (auto* ad = ent::find(adultId)) ctx.check(!farm::usePoisonPotato(p, *ad, true), "adults cannot be poisoned");
    // through the interaction hook (creative: always succeeds)
    p.setPlayerGameType(GameType::Creative);
    if (auto* c = ent::find(controlId)) {
        ctx.check(interact(p, *c), "the interaction is consumed");
        ctx.check(farm::isPoisoned(*c), "creative players always poison");
    }
    p.setPlayerGameType(GameType::Survival);
    for (auto id : {babyId, controlId, adultId})
        if (auto* e = ent::find(id)) ent::despawn(*e);
    hand(p, ItemStack{});
});

LP_REGISTER_SELFTEST("farming.pig_litters", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto base = ctx.testPos(2563);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    p.setPlayerGameType(GameType::Survival);
    // a pen 3x3 inside fences
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 5; ++z) {
            put(ctx, region, bp(base, x, -1, z), "minecraft:grass_block");
            if (x == 0 || z == 0 || x == 4 || z == 4) put(ctx, region, bp(base, x, 0, z), "minecraft:oak_fence");
        }
    auto  box = around(bp(base), bp(base, 4, 1, 4));
    clearEntities(region, box, "minecraft:pig");
    Vec3  at{static_cast<float>(base.x) + 1.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 2.5f};
    auto* a = ent::spawn(region, "minecraft:pig", at);
    auto* b = ent::spawn(region, "minecraft:pig", Vec3{at.x + 2, at.y, at.z});
    ctx.require(a && b, "pigs spawned");
    auto ida = ent::idOf(*a), idb = ent::idOf(*b);
    co_await ctx.ticks(2);
    auto& c                = lp::config::mut<farm::FarmingConfig>();
    c.minPigLitterSize     = 3;
    c.maxPigLitterSize     = 3;
    c.minGoldenCarrotBoost = 1;
    c.maxGoldenCarrotBoost = 1;
    hand(p, makeItem("minecraft:golden_carrot", 4));
    a = ent::find(ida);
    b = ent::find(idb);
    ctx.require(a && b, "pigs still there");
    ctx.check(interact(p, *a), "pig A eats a golden carrot");
    ctx.check(interact(p, *b), "pig B eats a golden carrot");
    ctx.check(farm::inLove(*a) && farm::inLove(*b), "both pigs are in love");
    ctx.check(a->hasTag("quark:AteGoldenCarrot"), "golden carrot mark");
    ctx.check(p.getSelectedItem().mCount == 2, "two carrots used");
    ctx.check(!interact(p, *a), "a pig in love does not eat another one");
    int babies = 0;
    for (int i = 0; i < 40 && babies == 0; ++i) {
        co_await ctx.ticks(10);
        babies = 0;
        for (auto* e : ent::inBox(region, box, "minecraft:pig")) babies += e->isBaby();
    }
    co_await ctx.ticks(5);
    babies = 0;
    for (auto* e : ent::inBox(region, box, "minecraft:pig")) babies += e->isBaby();
    ctx.check(babies == 5, "litter of 3 + 1 + 1 for two golden-carrot parents (" + std::to_string(babies) + ")");
    clearEntities(region, box, "minecraft:pig");
    co_await ctx.ticks(2);
    clearEntities(region, box, "minecraft:item");
    clearEntities(region, box, "minecraft:xp_orb");
    hand(p, ItemStack{});
});

LP_REGISTER_SELFTEST("farming.grab_chickens", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2573);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    p.setPlayerGameType(GameType::Survival);
    p.setArmor(SharedTypes::Legacy::ArmorSlot::Head, ItemStack{});
    Vec3  at{static_cast<float>(base.x) + 1.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 2.5f};
    auto* ch = ent::spawn(region, "minecraft:chicken", at);
    ctx.require(ch != nullptr, "chicken spawned");
    auto id = ent::idOf(*ch);
    co_await ctx.ticks(2);
    hand(p, ItemStack{});
    ch = ent::find(id);
    ctx.require(ch != nullptr, "chicken still there");
    ctx.check(interact(p, *ch), "empty-hand interaction picks the chicken up");
    co_await ctx.ticks(3);
    ch = ent::find(id);
    ctx.require(ch != nullptr, "chicken alive");
    ctx.check(ch->getVehicle() == &p, "the chicken rides the player");
    ctx.check(farm::carriedChickens(p).size() == 1, "carriedChickens finds it");
    ctx.check(p.getEffect(static_cast<uint>(MobEffectIds::SlowFalling)) != nullptr, "slow falling while carrying");
    ctx.check(p.getEffect(static_cast<uint>(MobEffectIds::MovementSlowdown)) != nullptr, "slowness while carrying");
    ctx.check(interact(p, *ch), "interacting again puts it down");
    co_await ctx.ticks(2);
    ch = ent::find(id);
    ctx.check(ch && ch->getVehicle() == nullptr, "the chicken is down");
    // a helmet makes the player drop it
    if (ch) {
        interact(p, *ch);
        co_await ctx.ticks(2);
        p.setArmor(SharedTypes::Legacy::ArmorSlot::Head, makeItem("minecraft:iron_helmet"));
        co_await ctx.ticks(3);
        ch = ent::find(id);
        ctx.check(ch && ch->getVehicle() == nullptr, "a helmet drops the chicken");
        p.setArmor(SharedTypes::Legacy::ArmorSlot::Head, ItemStack{});
        if (ch) ctx.check(!p.isSneaking() && interact(p, *ch) == true, "can pick it up again without the helmet");
        co_await ctx.ticks(1);
        if ((ch = ent::find(id))) ch->stopRiding(true, false, false, false);
    }
    if ((ch = ent::find(id))) ent::despawn(*ch);
});

LP_REGISTER_SELFTEST("farming.horses_swim", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2578);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    // a 3-deep pool, 3x3 water inside a stone rim
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 5; ++z) {
            put(ctx, region, bp(base, x, -1, z), "minecraft:stone");
            bool rim = x == 0 || z == 0 || x == 4 || z == 4;
            for (int y = 0; y < 3; ++y) put(ctx, region, bp(base, x, y, z), rim ? "minecraft:stone" : "minecraft:water");
        }
    co_await ctx.ticks(2);
    Vec3  at{static_cast<float>(base.x) + 2.5f, static_cast<float>(base.y) + 0.5f, static_cast<float>(base.z) + 2.5f};
    auto* horse = ent::spawn(region, "minecraft:horse", at);
    ctx.require(horse != nullptr, "horse spawned");
    auto id = ent::idOf(*horse);
    co_await ctx.ticks(2);
    horse = ent::find(id);
    ctx.require(horse != nullptr, "horse alive");
    ctx.check(!farm::horseSwimTick(*horse), "an unridden horse is left alone");
    p.startRiding(*horse, true);
    co_await ctx.ticks(2);
    horse = ent::find(id);
    ctx.require(horse && horse->hasPassenger(), "the player rides the horse");
    horse->teleport(Vec3{at.x, at.y + 1.0f, at.z}, horse->getDimensionId());
    co_await ctx.ticks(1);
    horse = ent::find(id);
    ctx.require(horse != nullptr, "horse still there");
    float y0 = horse->getPosition().y;
    bool  pushed = farm::horseSwimTick(*horse);
    ctx.check(pushed, "a ridden horse in deep water is pushed up");
    co_await ctx.ticks(10);
    if ((horse = ent::find(id))) ctx.check(horse->getPosition().y >= y0 - 0.05f, "it does not sink");
    p.stopRiding(true, false, false, false);
    if ((horse = ent::find(id))) ent::despawn(*horse);
    drain(region, bp(base, -1, -1, -1), bp(base, 5, 3, 5));
    co_await ctx.ticks(1);
});

LP_REGISTER_SELFTEST("farming.villagers_follow", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2588);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    for (int x = 0; x < 11; ++x)
        for (int z = 0; z < 5; ++z) {
            put(ctx, region, bp(base, x, -1, z), "minecraft:grass_block");
            ctx.cleanupBlock(lp::game::globalPos(0, bp(base, x, 0, z)));
            ctx.cleanupBlock(lp::game::globalPos(0, bp(base, x, 1, z)));
        }
    co_await ctx.ticks(1);
    for (int x = 0; x < 11; ++x)
        for (int z = 0; z < 5; ++z) {
            lp::game::clearBlock(region, bp(base, x, 0, z));
            lp::game::clearBlock(region, bp(base, x, 1, z));
        }
    Vec3 pp{static_cast<float>(base.x) + 0.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 2.5f};
    p.teleport(pp, p.getDimensionId());
    auto* v = ent::spawn(region, "minecraft:villager_v2", Vec3{pp.x + 9, pp.y, pp.z});
    ctx.require(v != nullptr, "villager spawned");
    auto id = ent::idOf(*v);
    hand(p, makeItem("minecraft:emerald_block"));
    co_await ctx.ticks(2);
    auto start = ent::find(id) ? ent::find(id)->getPosition() : Vec3{};
    float d0   = std::abs(start.x - pp.x);
    float d    = d0;
    for (int i = 0; i < 30 && d > 3.5f; ++i) {
        co_await ctx.ticks(5);
        if (auto* e = ent::find(id)) d = std::abs(e->getPosition().x - pp.x);
    }
    ctx.check(d < d0 - 2.0f, "the villager walks to the emerald block (" + std::to_string(d0) + " -> " + std::to_string(d) + ")");
    hand(p, ItemStack{});
    if (auto* e = ent::find(id)) ent::despawn(*e);
});

// Visual check for the client e2e scenario: a small farm (ripe and replanted wheat), water petals
// with 1..4 petals in all four directions, coral fans on cacti and a chicken carried by the test
// player. With a real player in the world it is moved in front of the scene ("FARMING view").
LP_REGISTER_SELFTEST("farming.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2500);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    for (int x = 0; x < 3; ++x) {
        put(ctx, region, bp(base, x, 0, 1), "minecraft:farmland");
        put(ctx, region, bp(base, x, 1, 1), "minecraft:wheat", {{"growth", 7}});
    }
    hand(p, ItemStack{});
    farm::simpleHarvestClick(p, bp(base, 1, 1, 1), false);
    for (int x = 4; x < 9; ++x)
        for (int z = 0; z < 5; ++z) {
            put(ctx, region, bp(base, x, 0, z), "minecraft:stone");
            bool rim = x == 4 || x == 8 || z == 0 || z == 4;
            put(ctx, region, bp(base, x, 1, z), rim ? "minecraft:stone" : "minecraft:water");
            ctx.cleanupBlock(lp::game::globalPos(0, bp(base, x, 2, z)));
        }
    co_await ctx.ticks(2);
    char const* dirs[] = {"north", "east", "south", "west"};
    int         i      = 0;
    for (auto [x, z] : {std::pair{5, 1}, {6, 1}, {7, 1}, {5, 3}, {6, 3}, {7, 3}}) {
        int amount = i % 4 + 1;
        ctx.check(
            put(ctx, region, bp(base, x, 2, z), "lepton:water_pink_petals",
                {{"lepton:amount", amount}, {"minecraft:cardinal_direction", std::string(dirs[i % 4])}}),
            "water petals placed"
        );
        ++i;
    }
    put(ctx, region, bp(base, 10, 0, 2), "minecraft:sand");
    put(ctx, region, bp(base, 10, 1, 2), "minecraft:cactus");
    put(ctx, region, bp(base, 10, 2, 2), "minecraft:fire_coral_fan");
    put(ctx, region, bp(base, 11, 0, 2), "minecraft:sand");
    put(ctx, region, bp(base, 11, 1, 2), "minecraft:cactus");
    put(ctx, region, bp(base, 11, 2, 2), "minecraft:horn_coral_fan");
    // the test player carries a chicken next to the farm
    p.teleport(
        Vec3{static_cast<float>(base.x) + 2.5f, static_cast<float>(base.y), static_cast<float>(base.z) + 3.5f},
        p.getDimensionId(),
        Vec2{0.f, 180.f}
    );
    co_await ctx.ticks(5);
    auto      pos  = p.getPosition();
    auto*     ch   = ent::spawn(region, "minecraft:chicken", Vec3{pos.x + 1.f, static_cast<float>(base.y), pos.z});
    long long chId = ch ? ent::idOf(*ch) : 0;
    co_await ctx.ticks(5);
    if (auto* c = ent::find(chId)) ctx.check(interact(p, *c), "the test player picks up a chicken");

    Player* viewer = nullptr;
    p.getLevel().forEachPlayer([&](Player& pl) {
        if (!pl.isSimulated()) viewer = &pl;
        return viewer == nullptr;
    });
    if (viewer) {
        auto uid  = viewer->getOrCreateUniqueID();
        auto view = [&](float dx, float dy, float dz, float pitch, float yaw) {
            if (auto* v = p.getLevel().getPlayer(uid))
                v->teleport(
                    Vec3{static_cast<float>(base.x) + dx, static_cast<float>(base.y) + dy, static_cast<float>(base.z) + dz},
                    DimensionType(base.dim),
                    Vec2{pitch, yaw}
                );
        };
        ctx.info("FARMING view farm");
        view(6.5f, 3.5f, 8.5f, 35.f, 180.f); // south of the scene, looking north and down
        co_await ctx.ticks(150);
        ctx.info("FARMING view petals");
        view(6.5f, 4.5f, 2.5f, 85.f, 180.f); // straight above the pond
        co_await ctx.ticks(150);
        ctx.info("FARMING view chicken");
        view(2.5f, 2.0f, 7.5f, 15.f, 180.f);
        co_await ctx.ticks(150);
        ctx.info("FARMING done");
    }
    if (auto* c = ent::find(chId)) {
        c->stopRiding(true, false, false, false);
        ent::despawn(*c);
    }
    drain(region, bp(base, 3, 0, -1), bp(base, 9, 2, 5));
    co_await ctx.ticks(2);
    clearEntities(region, around(bp(base), bp(base, 11, 2, 4)), "minecraft:item");
});
