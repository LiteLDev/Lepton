// Selftests of U23 gadgets (dx 2300..2399). See docs/spec/gadgets.md §8.

#include <algorithm>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/entity/components_json_legacy/EconomyTradeableComponent.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/util/LootTableUtils.h"
#include "mc/world/Container.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/ComparatorBlockActor.h"
#include "mc/world/actor/ActorDamageByActorSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/item/trading/MerchantRecipeList.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/gadgets/Discs.h"
#include "feature/gadgets/Gadgets.h"
#include "feature/gadgets/Pathfinder.h"
#include "feature/gadgets/Potato.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
using lp::core::GlobalPos;
using Facing = ScriptModuleMinecraft::ScriptFacing;
namespace g  = lp::feature::gadgets;
namespace bs = lp::game::blockstates;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

GlobalPos off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

Vec3 center(GlobalPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

void teleport(Context& ctx, Vec3 const& at, int dim) { ctx.player().teleport(at, DimensionType(dim)); }

/// Items (of `id`, or any when empty) lying within 3 blocks of `p`.
std::vector<ItemStack> itemsNear(Context& ctx, GlobalPos const& p, std::string_view id = {}) {
    AABB box{Vec3{p.x - 3.f, p.y - 3.f, p.z - 3.f}, Vec3{p.x + 4.f, p.y + 4.f, p.z + 4.f}};
    std::vector<ItemStack> out;
    for (auto* a : lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item")) {
        auto const& s = *static_cast<ItemActor*>(a)->mItem;
        if (id.empty() || lp::game::itemId(s) == id) out.push_back(s);
    }
    return out;
}

void clearItemsNear(Context& ctx, GlobalPos const& p) {
    AABB box{Vec3{p.x - 3.f, p.y - 3.f, p.z - 3.f}, Vec3{p.x + 4.f, p.y + 4.f, p.z + 4.f}};
    for (auto* a : lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item")) lp::game::entities::despawn(*a);
}

/// Selects hotbar slot 0 holding `s`.
void hold(Context& ctx, ItemStack const& s) {
    auto& inv = lp::game::inv::playerInventory(ctx.player());
    inv.setItem(0, s);
    ctx.player().setSelectedSlot(0);
    lp::game::inv::sync(ctx.player());
}

} // namespace

// ---- config ---------------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("gadgets.config", [](Context& ctx) {
    g::GadgetsConfig d{};
    ctx.check(d.enableAbacus && d.enablePathfinderMaps && d.enableAmbientDiscs && d.enableEndermoshMusicDisc
                  && d.enableCompassesWorkEverywhere && d.enableTinyPotato,
              "every module is enabled by default");
    ctx.check(d.highlightColor.r == 0 && d.highlightColor.g == 0 && d.highlightColor.b == 0 && d.highlightColor.a == 0.4,
              "abacus highlight colour 0,0,0,0.4");
    ctx.check(d.pathfindersQuillSpeed == 32 && d.multiThreaded && d.allowRetrying && d.searchRadius == 6400 && d.xpFromTrade == 5,
              "quill search defaults");
    ctx.check(d.addToCartographer && d.addToWanderingTraderForced && !d.addToWanderingTraderGeneric && !d.addToWanderingTraderRare,
              "trade defaults");
    ctx.check(d.drawHud && !d.hudOnTop && d.applyDefaultTrades && d.customs.empty(), "hud / customs defaults");
    ctx.check(d.builtinTrades.size() == 15, "15 built-in pathfinder trades");
    ctx.check(d.dropOnSpiderKill && d.volume == 3, "ambience disc defaults");
    ctx.check(!d.playEndermoshDuringEnderdragonFight && d.addToEndCityLoot && d.lootWeight == 5 && d.lootQuality == 1,
              "Endermosh defaults");
    ctx.check(d.enableCompassNerf && d.enableClockNerf && d.enableNether && d.enableEnd, "compass defaults");
    ctx.check(!d.neverUseHeartOfDiamond, "tiny potato default");
});

// ---- abacus ---------------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("gadgets.abacus_count", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2300);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "block to bind");
    auto abacus = lp::game::makeItem(g::kAbacus);
    ctx.require(!lp::game::isEmpty(abacus), "lepton:abacus exists");
    BlockPos bp = lp::game::blockPos(pos);
    ctx.check(g::abacus::countFor(abacus, bp, false) == -1, "an unbound abacus shows nothing");

    // the real use: the simulated player clicks the block with the abacus
    teleport(ctx, Vec3{pos.x + 2.5f, static_cast<float>(pos.y + 1), pos.z + 0.5f}, pos.dim);
    hold(ctx, abacus);
    co_await ctx.ticks(2);
    auto* sp   = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  held = ctx.player().getSelectedItem();
    ctx.check(sp->simulateUseItemOnBlock(held, bp, Facing::Up, Vec3{0.5f, 1.f, 0.5f}), "use the abacus on the block");
    co_await ctx.ticks(3);
    auto bound = ctx.player().getSelectedItem();
    ctx.check(g::getInt(bound, "boundPosY").value_or(-999) == pos.y, "using the abacus binds the block");
    ctx.check(g::abacus::countFor(bound, bp, false) == 0, "the bound block itself counts 0 (shown 1)");
    ctx.check(g::abacus::countFor(bound, BlockPos(pos.x + 3, pos.y + 1, pos.z - 2), false) == 6, "Manhattan distance");
    ctx.check(g::abacus::countFor(bound, BlockPos(pos.x + 100, pos.y, pos.z), false) == g::kAbacusMax, "clamped to 48");
    ctx.check(g::abacus::countFor(bound, BlockPos(pos.x + 1, pos.y, pos.z), true) == -1, "nothing on air");

    co_await ctx.ticks(10);
    held = ctx.player().getSelectedItem();
    ctx.check(sp->simulateUseItemOnBlock(held, bp, Facing::Up, Vec3{0.5f, 1.f, 0.5f}), "use the abacus again");
    co_await ctx.ticks(3);
    ctx.check(g::getInt(ctx.player().getSelectedItem(), "boundPosY").value_or(0) == -999, "using it again unbinds it");
});

LP_REGISTER_SELFTEST("gadgets.abacus_net", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2302);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto target = off(pos, 0, 0, 3);
    ctx.require(ctx.setBlock(pos, "minecraft:stone") && ctx.setBlock(target, "minecraft:gold_block"), "blocks placed");
    auto& p = ctx.player();
    lp::net::testing::setClientMod(p, true);
    (void)lp::net::testing::takeOutbox(p);
    auto abacus = lp::game::makeItem(g::kAbacus);
    g::putInt(abacus, "boundPosX", pos.x);
    g::putInt(abacus, "boundPosY", pos.y);
    g::putInt(abacus, "boundPosZ", pos.z);
    hold(ctx, abacus);
    teleport(ctx, Vec3{target.x + 0.5f, static_cast<float>(target.y + 1), target.z + 2.5f}, pos.dim);
    auto* sp = static_cast<SimulatedPlayer*>(&p);
    sp->simulateLookAt(lp::game::blockPos(target), sim::LookDuration::Continuous);
    bool got = false;
    for (int i = 0; i < 20 && !got; ++i) {
        co_await ctx.ticks(1);
        for (auto const& [id, json] : lp::net::testing::takeOutbox(p))
            if (id == "gadgets:abacus" && json.find("\"count\":3") != std::string::npos) got = true;
    }
    ctx.check(got, "the mod client gets gadgets:abacus with count 3 (shown 4)");
    lp::net::testing::setClientMod(p, false);
});

// ---- pathfinder maps ----------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("gadgets.quill_spiral", [](Context& ctx) {
    auto s = g::pathfinder::spiralForTest(5, 6400);
    std::vector<int> want{32, 0, 64, 0, 64, 32, 32, 32, 0, 32};
    ctx.check(s == want, "the spiral follows Quark's sequence");
    auto small = g::pathfinder::spiralForTest(100000, 64);
    ctx.check(!small.empty() && small.size() < 200, "the spiral ends at the search radius");
    auto h = g::pathfinder::searchHeights(64, -63, 320);
    ctx.check(h.size() >= 3 && h[0] == 64 && h[1] == 128 && h[2] == 0, "heights go out from the player's y by 64");
    ctx.check(std::find(h.begin(), h.end(), -64) == h.end(), "heights stay in the dimension");
    ctx.check(g::pathfinder::bedrockBiome("minecraft:snowy_plains") == "ice_plains", "Java ids map to Bedrock names");
    ctx.check(g::pathfinder::bedrockBiome("minecraft:jungle") == "jungle", "unchanged names pass through");
});

LP_REGISTER_SELFTEST("gadgets.quill_search", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p     = ctx.player();
    auto& dim   = p.getDimension();
    auto  bp    = lp::game::blockPosOf(p.getPosition());
    // the spiral starts one step (32 blocks) east of the player
    auto  biome = g::pathfinder::biomeAt(dim, bp.x + 32, bp.y, bp.z);
    ctx.require(!biome.empty(), "the biome source answers");
    ctx.info("biome next to spawn: " + biome);
    auto& c          = lp::config::mut<g::GadgetsConfig>();
    bool  savedMt    = c.multiThreaded;
    for (bool mt : {false, true}) {
        c.multiThreaded = mt;
        auto quill      = g::pathfinder::makeQuill("minecraft:" + biome, 0xFF22B600, &p);
        ctx.require(!lp::game::isEmpty(quill), "lepton:pathfinders_quill exists");
        hold(ctx, quill);
        g::pathfinder::onUseItem(p, p.getSelectedItem());
        ctx.check(g::getBool(p.getSelectedItem(), "isSearching"), "using an inscribed quill starts the search");
        bool done = false;
        for (int i = 0; i < 100 && !done; ++i) {
            co_await ctx.ticks(1);
            done = lp::game::itemId(p.getSelectedItem()) == "minecraft:filled_map";
        }
        ctx.check(done, std::string("the quill became a filled map (") + (mt ? "worker thread" : "iterative") + ")");
        ctx.check(p.getSelectedItem().getCustomName().find("Pathfinder Map") != std::string::npos, "the map is named <Biome> Pathfinder Map");
    }
    c.multiThreaded = savedMt;
    hold(ctx, ItemStack{});
});

LP_REGISTER_SELFTEST("gadgets.quill_only_one", [](Context& ctx) {
    auto& p = ctx.player();
    auto  a = g::pathfinder::makeQuill("minecraft:jungle", -1, &p);
    g::putBool(a, "isSearching", true);
    auto& inv = lp::game::inv::playerInventory(p);
    inv.setItem(5, a);
    auto b = g::pathfinder::makeQuill("minecraft:desert", -1, &p);
    hold(ctx, b);
    g::pathfinder::onUseItem(p, p.getSelectedItem());
    ctx.check(!g::getBool(p.getSelectedItem(), "isSearching"), "a second quill does not start while one searches");
    inv.setItem(5, ItemStack{});
    hold(ctx, ItemStack{});
    auto blank = lp::game::makeItem(g::kQuill);
    hold(ctx, blank);
    g::pathfinder::onUseItem(p, p.getSelectedItem());
    ctx.check(!g::getBool(p.getSelectedItem(), "isSearching"), "a blank quill does nothing");
    hold(ctx, ItemStack{});
});

LP_REGISTER_SELFTEST("gadgets.trades", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto ok = g::pathfinder::parseCustom("minecraft:plains,2,3,5,FF00FF00");
    ctx.check(ok && ok->level == 2 && ok->minPrice == 3 && ok->maxPrice == 5 && ok->color == static_cast<int>(0xFF00FF00u),
              "custom lines parse");
    ctx.check(g::pathfinder::parseCustom("minecraft:plains,2,3,5,FF00FF00,Plains") .has_value(), "6-token lines parse");
    ctx.check(!g::pathfinder::parseCustom("minecraft:plains,2,3").has_value(), "short lines are refused");
    ctx.check(!g::pathfinder::parseCustom("minecraft:plains,x,3,5,FF").has_value(), "bad numbers are refused");
    ctx.check(g::pathfinder::tradeList().size() >= 15, "the trade list has the built-ins");

    // a real wandering trader: the forced trade is added when its offers are generated
    auto pos = ctx.testPos(2304);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* trader = lp::game::entities::spawn(pos.dim, "minecraft:wandering_trader", center(off(pos, 0, 1, 0)));
    ctx.require(trader != nullptr, "wandering trader spawned");
    auto  id   = lp::game::entities::idOf(*trader);
    auto  comp = trader->getEntityContext().tryGetComponent<EconomyTradeableComponent>();
    ctx.require(comp.has_value(), "the trader has an economy component");
    comp->getOffers();
    co_await ctx.ticks(2);
    bool found = false;
    if (auto* t = lp::game::entities::find(id)) {
        auto c2 = t->getEntityContext().tryGetComponent<EconomyTradeableComponent>();
        if (c2 && c2->mOffers.get())
            for (auto const& r : *c2->mOffers.get()->mRecipeList)
                if (lp::game::itemId(*r.mSell) == g::kQuill) found = true;
        lp::game::entities::despawn(*t);
    }
    ctx.check(found, "a wandering trader sells a Pathfinder's Quill");
});

// ---- ambience discs and Endermosh ---------------------------------------------------------------------

LP_REGISTER_SELFTEST("gadgets.disc_drop", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2310);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    clearItemsNear(ctx, pos);
    // the e2e worlds are peaceful: hostile mobs need another difficulty while the test runs
    struct DifficultyGuard {
        Level&                       level;
        SharedTypes::Legacy::Difficulty saved;
        ~DifficultyGuard() { level.setDifficulty(saved); }
    } guard{static_cast<Level&>(ctx.player().getLevel()), ctx.player().getLevel().getDifficulty()};
    guard.level.setDifficulty(SharedTypes::Legacy::Difficulty::Normal);
    auto* skeleton = lp::game::entities::spawn(pos.dim, "minecraft:skeleton", center(off(pos, 2, 1, 0)));
    auto* spider   = lp::game::entities::spawn(pos.dim, "minecraft:spider", center(off(pos, 0, 1, 0)));
    ctx.require(skeleton && spider, "skeleton and spider spawned");
    auto skId = lp::game::entities::idOf(*skeleton);
    static_cast<Mob*>(spider)->die(ActorDamageByActorSource(*skeleton, SharedTypes::Legacy::ActorDamageCause::Projectile));
    co_await ctx.ticks(3);
    std::vector<ItemStack> discs;
    for (auto const& s : itemsNear(ctx, pos))
        if (!g::ambientNameOf(lp::game::itemId(s)).empty()) discs.push_back(s);
    {
        AABB box{Vec3{pos.x - 4.f, pos.y - 4.f, pos.z - 4.f}, Vec3{pos.x + 5.f, pos.y + 5.f, pos.z + 5.f}};
        for (auto* a : lp::game::entities::inBox(regionOf(ctx), box)) {
            auto ap = a->getPosition();
            ctx.info("entity near: " + lp::game::entities::typeOf(*a) + " at " + std::to_string(ap.x) + " " + std::to_string(ap.y) + " " + std::to_string(ap.z));
        }
        ctx.info("test pos " + pos.key());
    }
    ctx.check(discs.size() == 1, "one ambience disc dropped (" + std::to_string(discs.size()) + ")");
    if (!discs.empty()) ctx.check(!discs[0].getCustomLore().empty(), "the disc has its description");
    if (auto* sk = lp::game::entities::find(skId)) lp::game::entities::despawn(*sk);
    clearItemsNear(ctx, pos);
    // a zombie killing a spider drops nothing
    auto* zombie  = lp::game::entities::spawn(pos.dim, "minecraft:zombie", center(off(pos, 2, 1, 0)));
    auto* spider2 = lp::game::entities::spawn(pos.dim, "minecraft:spider", center(off(pos, 0, 1, 0)));
    ctx.require(zombie && spider2, "zombie and spider spawned");
    auto zId = lp::game::entities::idOf(*zombie);
    static_cast<Mob*>(spider2)->die(ActorDamageByActorSource(*zombie, SharedTypes::Legacy::ActorDamageCause::EntityAttack));
    co_await ctx.ticks(3);
    int n = 0;
    for (auto const& s : itemsNear(ctx, pos))
        if (!g::ambientNameOf(lp::game::itemId(s)).empty()) ++n;
    ctx.check(n == 0, "other killers drop no disc");
    if (auto* z = lp::game::entities::find(zId)) lp::game::entities::despawn(*z);
    clearItemsNear(ctx, pos);
});

LP_REGISTER_SELFTEST("gadgets.jukebox_loop", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2312);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:jukebox"), "jukebox placed");
    co_await ctx.ticks(1);
    // the player puts the disc in, as by hand
    teleport(ctx, Vec3{pos.x + 2.5f, static_cast<float>(pos.y + 1), pos.z + 0.5f}, pos.dim);
    hold(ctx, g::discs::makeDisc("rain", nullptr));
    co_await ctx.ticks(2);
    auto* sp   = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  held = ctx.player().getSelectedItem();
    ctx.check(sp->simulateUseItemOnBlock(held, lp::game::blockPos(pos), Facing::Up, Vec3{0.5f, 1.f, 0.5f}), "use the disc on the jukebox");
    co_await ctx.ticks(3);
    auto* jb = static_cast<JukeboxBlockActor*>(regionOf(ctx).getBlockEntity(lp::game::blockPos(pos)));
    ctx.require(jb != nullptr, "the jukebox has a block actor");
    ctx.check(lp::game::itemId(*jb->mRecord) == "lepton:music_disc_rain", "the disc is in the jukebox");
    std::string name;
    ctx.check(g::discs::isTracked(pos, &name) && name == "rain", "an ambience disc in a jukebox is tracked");
    g::discs::ageForTest(pos, 24 * 20);
    co_await ctx.ticks(2);
    ctx.check(g::discs::replaysAt(pos) >= 1, "the ambience restarts after its 24 s loop");
    // using the jukebox again takes the disc out
    co_await ctx.ticks(10);
    hold(ctx, lp::game::makeItem("minecraft:stick"));
    held = ctx.player().getSelectedItem();
    sp->simulateUseItemOnBlock(held, lp::game::blockPos(pos), Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    hold(ctx, ItemStack{});
    ctx.check(!g::discs::isTracked(pos), "taking the disc out stops it");
    clearItemsNear(ctx, pos);
});

LP_REGISTER_SELFTEST("gadgets.endermosh_loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2314);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:chest"), "chest placed");
    co_await ctx.ticks(1);
    auto* be = static_cast<ChestBlockActor*>(regionOf(ctx).getBlockEntity(lp::game::blockPos(pos)));
    ctx.require(be && be->getContainer(), "the chest has a container");
    auto& c     = lp::config::mut<g::GadgetsConfig>();
    int   saved = c.lootWeight;
    auto  fill  = [&](int weight) {
        c.lootWeight     = weight;
        auto& container  = *be->getContainer();
        for (int i = 0; i < container.getContainerSize(); ++i) container.setItem(i, ItemStack{});
        auto& level = ctx.player().getLevel();
        Util::LootTableUtils::fillContainer(static_cast<Level&>(level), container, level.getRandom(),
                                            "loot_tables/chests/end_city_treasure.json", DimensionType(pos.dim), nullptr);
        int n = 0;
        for (int i = 0; i < container.getContainerSize(); ++i)
            if (lp::game::itemId(container.getItem(i)) == g::kEndermosh) ++n;
        for (int i = 0; i < container.getContainerSize(); ++i) container.setItem(i, ItemStack{});
        return n;
    };
    int many = fill(1000000);
    int none = fill(0);
    c.lootWeight = saved;
    ctx.check(many >= 1, "with a huge weight the End City treasure holds Endermosh (" + std::to_string(many) + ")");
    ctx.check(none == 0, "with weight 0 it never does");
});

// ---- compasses ----------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("gadgets.compass_tick", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace cw = g::compasses;
    auto c       = lp::game::makeItem("minecraft:compass");
    ctx.check(cw::tickStack(c, false, false, BlockPos(0, 0, 0)), "a fresh compass changes");
    ctx.check(g::getBool(c, cw::kCompassCalculated) && !g::getBool(c, cw::kWasInNether), "calculated, not in the nether");
    ctx.check(!cw::tickStack(c, false, false, BlockPos(0, 0, 0)), "nothing changes afterwards");
    auto n = lp::game::makeItem("minecraft:compass");
    cw::tickStack(n, true, false, BlockPos(0, 0, 0));
    ctx.check(g::getBool(n, cw::kWasInNether), "a compass first ticked in the nether is marked");
    ctx.check(!cw::tickStack(n, true, true, BlockPos(5, 64, 7)), "already marked: the portal is not stored");
    cw::tickStack(c, true, false, BlockPos(0, 0, 0));
    ctx.check(!g::getBool(c, cw::kPosSet), "not in a portal: nothing stored");
    ctx.check(cw::tickStack(c, true, true, BlockPos(5, 64, 7)), "standing in a portal in the nether stores it");
    ctx.check(g::getBool(c, cw::kPosSet) && g::getInt(c, cw::kNetherX) == 5 && g::getInt(c, cw::kNetherZ) == 7, "portal x/z stored");
    ctx.check(cw::tickStack(c, false, false, BlockPos(0, 0, 0)) && !g::getBool(c, cw::kPosSet), "leaving the nether clears it");
    auto clock = lp::game::makeItem("minecraft:clock");
    ctx.check(cw::tickStack(clock, false, false, BlockPos(0, 0, 0)) && g::getBool(clock, cw::kClockCalculated), "clocks are marked");

    // in a real inventory
    auto& p = ctx.player();
    hold(ctx, lp::game::makeItem("minecraft:compass"));
    co_await ctx.ticks(3);
    ctx.check(g::getBool(p.getSelectedItem(), cw::kCompassCalculated), "a compass in the inventory gets calculated");
    hold(ctx, ItemStack{});
});

// ---- tiny potato -----------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("gadgets.potato_names", [](Context& ctx) {
    namespace pt = g::potato;
    ctx.check(pt::cleanName("§cEnchanted Light Blue Bob").name == "bob", "formatting, enchanted and rune colour removed");
    ctx.check(pt::cleanName("Shiny Grey Tomater").name == "tomater", "grey spelling of rune colours");
    ctx.check(pt::cleanName("  Pahimar ").name == "pahimar", "trim + lower case");
    ctx.check(pt::variantFor("", false, false, false) == 0, "unnamed: default");
    ctx.check(pt::variantFor("", true, false, false) == 1, "angry");
    ctx.check(pt::variantFor("Someone", false, true, false) == 2, "unknown names on Halloween");
    ctx.check(pt::variantFor("Dinnerbone", false, false, false) == 10, "dinnerbone: upside down");
    ctx.check(pt::variantFor("Kyle Hyde", false, false, false) == 15, "spaces become _ (kyle_hyde)");
    ctx.check(pt::variantFor("tatersus", false, false, false) == 21, "sus");
    ctx.check(pt::variantFor("Eloraam", false, false, true) == 24 && pt::variantFor("Eloraam", false, false, false) == 0,
              "eloraam vanishes while jumping");
    ctx.check(pt::variantFor("bob", true, false, false) == 8, "a known name wins over angry");
    ctx.check(pt::genderFor("lesbiantater") == "daughter" && pt::genderFor("tomater") == "tomato"
                  && pt::genderFor("snorps") == "children" && pt::genderFor("Bob") == "child" && pt::genderFor("x") == "son",
              "gender table");
    ctx.check(pt::jumpFrame(20) == 10 && pt::jumpFrame(19) == 10 && pt::jumpFrame(1) == 1 && pt::jumpFrame(0) == 0, "jump frames");
});

LP_REGISTER_SELFTEST("gadgets.potato_place", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2320);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "support block");
    auto above = off(pos, 0, 1, 0);
    ctx.cleanupBlock(above);
    teleport(ctx, Vec3{pos.x + 0.5f, static_cast<float>(pos.y + 1), pos.z + 3.5f}, pos.dim); // south of it
    auto potato = g::potato::makePotatoItem("Dinnerbone", false, nullptr);
    ctx.require(!lp::game::isEmpty(potato), "lepton:tiny_potato exists");
    hold(ctx, potato);
    co_await ctx.ticks(2);
    auto* sp   = static_cast<SimulatedPlayer*>(&ctx.player());
    sp->simulateLookAt(lp::game::blockPos(pos), sim::LookDuration::Instant); // looking north
    co_await ctx.ticks(1);
    auto  held = ctx.player().getSelectedItem();
    ctx.check(sp->simulateUseItemOnBlock(held, lp::game::blockPos(pos), Facing::Up, Vec3{0.5f, 1.f, 0.5f}), "place the potato");
    co_await ctx.ticks(4);
    auto const& b = regionOf(ctx).getBlock(lp::game::blockPos(above));
    ctx.require(lp::game::blockId(b) == g::kPotato, "the tiny potato is placed");
    ctx.check(g::potato::variantOf(b) == 10, "the name selects the look (dinnerbone)");
    ctx.info("facing: " + bs::getString(b, "minecraft:cardinal_direction").value_or("?"));
    ctx.check(bs::getString(b, "minecraft:cardinal_direction").value_or("") == "south", "it faces the player");
    auto data = g::potato::dataAt(above);
    ctx.check(data && data->name == "Dinnerbone", "the name is stored");
    hold(ctx, ItemStack{});
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(above));
    co_await ctx.ticks(3);
    ctx.check(itemsNear(ctx, above, g::kPotato).size() == 1, "breaking drops the potato");
    ctx.check(itemsNear(ctx, above, g::kPotato).empty() || itemsNear(ctx, above, g::kPotato)[0].getCustomName() == "Dinnerbone", "the dropped potato keeps its name");
    clearItemsNear(ctx, above);
});

LP_REGISTER_SELFTEST("gadgets.potato_slots", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2330);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    clearItemsNear(ctx, pos);
    ctx.require(ctx.setBlock(pos, std::string(g::kPotato)), "potato placed");
    g::potato::registerAt(pos, "", false);
    auto& p = ctx.player();
    teleport(ctx, Vec3{pos.x + 2.5f, static_cast<float>(pos.y), pos.z + 0.5f}, pos.dim);
    hold(ctx, lp::game::makeItem("minecraft:diamond", 2));
    g::potato::interact(p, pos, 1);
    auto d = g::potato::dataAt(pos);
    ctx.check(d && !d->items[1].empty(), "one item goes into the top slot");
    ctx.check(p.getSelectedItem().mCount == 1, "the hand keeps the rest");
    {
        auto* shown = lp::game::entities::find(g::potato::displayAt(pos, 1));
        ctx.check(shown && lp::game::itemId(shown->getCarriedItem()) == "minecraft:diamond", "a display entity shows the held item");
    }
    hold(ctx, ItemStack{});
    g::potato::interact(p, pos, 1);
    ctx.check(lp::game::itemId(p.getSelectedItem()) == "minecraft:diamond", "an empty hand takes it back");
    ctx.check(g::potato::dataAt(pos)->items[1].empty(), "the slot is empty again");
    ctx.check(g::potato::displayAt(pos, 1) == 0, "its display entity is gone");

    // the real use event: a stick into the north side
    co_await ctx.ticks(10);
    hold(ctx, lp::game::makeItem("minecraft:stick"));
    auto* sp   = static_cast<SimulatedPlayer*>(&p);
    auto  held = p.getSelectedItem();
    ctx.check(sp->simulateUseItemOnBlock(held, lp::game::blockPos(pos), Facing::North, Vec3{0.5f, 0.3f, 0.375f}), "use a stick on the potato");
    co_await ctx.ticks(3);
    d = g::potato::dataAt(pos);
    ctx.check(d && lp::game::itemId(lp::game::itemFromSnbt(d->items[2])) == "minecraft:stick", "the stick is in the north slot");

    // a held potato talks back; breaking drops everything
    hold(ctx, g::potato::makePotatoItem("tomater", false, nullptr));
    g::potato::interact(p, pos, 4);
    hold(ctx, ItemStack{});
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(itemsNear(ctx, pos, "minecraft:stick").size() == 1, "the held stick drops");
    for (auto const& s : itemsNear(ctx, pos)) ctx.info("item near: " + lp::game::itemId(s) + " x" + std::to_string(s.mCount) + " " + s.getCustomName());
    ctx.check(itemsNear(ctx, pos, g::kPotato).size() == 2, "the held potato and the potato itself drop");
    ctx.check(!g::potato::dataAt(pos).has_value(), "the data is gone");
    clearItemsNear(ctx, pos);
});

LP_REGISTER_SELFTEST("gadgets.potato_jump", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2340);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, std::string(g::kPotato)), "potato placed");
    g::potato::registerAt(pos, "", false);
    g::potato::jump(pos);
    int  firstFrame = bs::getInt(regionOf(ctx).getBlock(lp::game::blockPos(pos)), "lepton:jump").value_or(-1);
    bool sawZero    = false;
    for (int i = 0; i < 25; ++i) {
        co_await ctx.ticks(1);
        if (bs::getInt(regionOf(ctx).getBlock(lp::game::blockPos(pos)), "lepton:jump").value_or(-1) == 0) sawZero = true;
    }
    ctx.check(firstFrame == 10, "a jump starts at frame 10 (" + std::to_string(firstFrame) + ")");
    ctx.check(sawZero, "the jump ends at frame 0");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(pos));
    co_await ctx.ticks(3);
    clearItemsNear(ctx, pos);
});

LP_REGISTER_SELFTEST("gadgets.recipes", [](Context& ctx) {
    ctx.check(g::hasRecipe("lepton:abacus") == g::cfg().enableAbacus, "abacus recipe follows enableAbacus");
    bool heart = g::potatoUsesHeart();
    ctx.info(std::string("tiny potato recipe: ") + (heart ? "heart of diamond" : "diamond / emerald"));
    ctx.check(g::hasRecipe("lepton:tiny_potato_heart") == heart, "the heart recipe is active only with the Heart of Diamond");
    ctx.check(g::hasRecipe("lepton:tiny_potato_no_heart") == !heart && g::hasRecipe("lepton:tiny_potato_no_heart_emerald") == !heart,
              "the diamond / emerald recipes otherwise");
});

// ---- gallery (client e2e screenshots) ----------------------------------------------------------------

// Every tiny potato look in a 5x5 grid, then (with a real player in the world) views for the client
// scenario: the grid, a close-up with the name label, the abacus HUD + outline and the quill HUD.
LP_REGISTER_SELFTEST("gadgets.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    static char const* const kNames[] = {
        "",         "",            "halloween", "acetater", "agendertater", "arotater",  "aureylian",
        "bitater",  "bob",         "botater",   "Dinnerbone", "enbytater",  "gaytater",  "taterfluid",
        "lesbiantater", "graytater", "kingdaddydmac", "pahimar", "pantater", "robotater", "snorps",
        "tatersus", "tomater",     "transtater", "soaryn",
    };
    auto base = ctx.testPos(2360);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    std::vector<GlobalPos> placed;
    for (int v = 0; v < 25; ++v) {
        auto p = off(base, (v % 5) * 2, 0, (v / 5) * 2);
        ctx.require(ctx.setBlock(off(p, 0, -1, 0), "minecraft:smooth_stone"), "floor");
        auto const* b = bs::of(g::kPotato, {{"minecraft:cardinal_direction", "south"}});
        ctx.require(b && lp::game::setBlock(region, lp::game::blockPos(p), *b), "potato placed");
        g::potato::registerAt(p, kNames[v], v == 1);
        placed.push_back(p);
    }
    // held items on the default potato (dx 0, dz 0): one per side, as Quark draws them
    {
        auto& sp = ctx.player();
        teleport(ctx, Vec3{base.x + 3.5f, static_cast<float>(base.y), base.z + 0.5f}, base.dim);
        std::pair<int, ItemStack> held[] = {
            {1, lp::game::makeItem("minecraft:diamond")},
            {2, lp::game::makeItem("minecraft:apple")},
            {3, lp::game::makeItem("minecraft:stick")},
            {4, lp::game::makeItem("minecraft:stone")},
            {5, g::potato::makePotatoItem("", false, nullptr)},
        };
        for (auto& [face, item] : held) {
            hold(ctx, item);
            g::potato::interact(sp, placed[0], face);
        }
        hold(ctx, ItemStack{});
    }
    co_await ctx.ticks(2);
    int shown = 0;
    for (int face = 1; face <= 5; ++face)
        if (lp::game::entities::find(g::potato::displayAt(placed[0], face))) ++shown;
    ctx.check(shown == 5, "5 held items are shown (" + std::to_string(shown) + ")");
    int ok = 0;
    for (int v = 0; v < 25; ++v)
        if (g::potato::variantOf(region.getBlock(lp::game::blockPos(placed[v]))) == v || v == 2) ++ok; // 2: also the date
    ctx.check(ok == 25, "every look is selected by its name (" + std::to_string(ok) + "/25)");
    auto bound  = off(base, 12, -1, 0);
    auto target = off(base, 12, -1, 8);
    ctx.require(ctx.setBlock(bound, "minecraft:gold_block") && ctx.setBlock(target, "minecraft:diamond_block"), "abacus blocks");

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (viewer) {
        auto uid  = viewer->getOrCreateUniqueID();
        auto view = [&](float dx, float dy, float dz, float pitch, float yaw) {
            if (auto* v = ctx.player().getLevel().getPlayer(uid))
                v->teleport(Vec3{base.x + dx, base.y + dy, base.z + dz}, DimensionType(base.dim), Vec2{pitch, yaw});
        };
        auto viewerRef = [&]() { return ctx.player().getLevel().getPlayer(uid); };
        ctx.info("GALLERY view grid");
        view(4.5f, 3.0f, 12.5f, 35.f, 180.f); // south of the grid, looking north
        co_await ctx.ticks(120);
        ctx.info("GALLERY view closeup");
        view(0.5f, 0.0f, 6.5f, 35.f, 180.f); // south of "Dinnerbone" (dx 0, dz 4), looking at it
        co_await ctx.ticks(120);
        if (auto* v = viewerRef()) {
            auto abacus = lp::game::makeItem(g::kAbacus);
            g::putInt(abacus, "boundPosX", bound.x);
            g::putInt(abacus, "boundPosY", bound.y);
            g::putInt(abacus, "boundPosZ", bound.z);
            lp::game::inv::playerInventory(*v).setItem(v->getSelectedItemSlot(), abacus);
            lp::game::inv::sync(*v);
        }
        ctx.info("GALLERY view items");
        view(1.8f, 0.0f, 1.8f, 37.f, 135.f); // south-east of the default potato, looking at it
        co_await ctx.ticks(120);
        ctx.info("GALLERY view abacus");
        view(14.5f, 1.0f, 10.5f, 25.f, 160.f); // looking at the diamond block
        co_await ctx.ticks(120);
        auto& c           = lp::config::mut<g::GadgetsConfig>();
        bool  savedMt     = c.multiThreaded;
        int   savedSpeed  = c.pathfindersQuillSpeed;
        c.multiThreaded   = false;
        c.pathfindersQuillSpeed = 1;
        if (auto* v = viewerRef()) {
            auto quill = g::pathfinder::makeQuill("minecraft:jungle", 0xFF22B600, v);
            lp::game::inv::playerInventory(*v).setItem(v->getSelectedItemSlot(), quill);
            lp::game::inv::sync(*v);
            g::pathfinder::onUseItem(*v, v->getSelectedItem());
        }
        ctx.info("GALLERY view quill");
        view(4.5f, 3.0f, 12.5f, 35.f, 180.f);
        co_await ctx.ticks(120);
        if (auto* v = viewerRef()) {
            lp::game::inv::playerInventory(*v).setItem(v->getSelectedItemSlot(), ItemStack{});
            lp::game::inv::sync(*v);
        }
        c.multiThreaded         = savedMt;
        c.pathfindersQuillSpeed = savedSpeed;
        // compasses in the End point at the exit portal (0, 0): stand south-east of it, look north
        if (auto* v = viewerRef()) {
            auto compass = lp::game::makeItem("minecraft:compass");
            lp::game::inv::playerInventory(*v).setItem(v->getSelectedItemSlot(), compass);
            lp::game::inv::sync(*v);
            v->teleport(Vec3{40.5f, 70.f, 40.5f}, DimensionType(2), Vec2{0.f, 180.f});
        }
        co_await ctx.ticks(60);
        if (auto* end = lp::game::regionOf(2))
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    if (auto const* ob = bs::of("minecraft:obsidian")) lp::game::setBlock(*end, BlockPos(40 + dx, 69, 40 + dz), *ob);
        if (auto* v = viewerRef()) v->teleport(Vec3{40.5f, 70.f, 40.5f}, DimensionType(2), Vec2{20.f, 180.f});
        co_await ctx.ticks(60);
        ctx.info("GALLERY view compass");
        co_await ctx.ticks(100);
        if (auto* v = viewerRef()) {
            lp::game::inv::playerInventory(*v).setItem(v->getSelectedItemSlot(), ItemStack{});
            lp::game::inv::sync(*v);
            v->teleport(Vec3{base.x + 4.5f, static_cast<float>(base.y), base.z + 12.5f}, DimensionType(base.dim), Vec2{0.f, 180.f});
        }
        co_await ctx.ticks(40);
        ctx.info("GALLERY done");
    }
    for (auto const& p : placed) lp::game::clearBlock(region, lp::game::blockPos(p));
    co_await ctx.ticks(3);
    for (auto const& p : placed) clearItemsNear(ctx, p);
});

LP_REGISTER_SELFTEST("gadgets.potato_comparator", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2350);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = regionOf(ctx);
    for (int dz = 0; dz <= 1; ++dz) ctx.require(ctx.setBlock(off(pos, 0, -1, dz), "minecraft:stone"), "floor");
    ctx.require(ctx.setBlock(pos, std::string(g::kPotato)), "potato placed");
    g::potato::registerAt(pos, "", false);
    auto& p = ctx.player();
    teleport(ctx, Vec3{pos.x + 2.5f, static_cast<float>(pos.y), pos.z + 0.5f}, pos.dim);
    auto fill = [&](int face) {
        hold(ctx, lp::game::makeItem("minecraft:stick"));
        g::potato::interact(p, pos, face);
        hold(ctx, ItemStack{});
    };
    for (int face : {1, 2, 4}) fill(face);
    auto signal = g::potato::comparatorSignal(*g::potato::dataAt(pos));
    ctx.check(signal == 8, "3 of 6 slots: signal 8 (" + std::to_string(signal) + ")");
    auto cpos   = off(pos, 0, 0, 1);
    auto output = [&] {
        auto* be = static_cast<ComparatorBlockActor*>(region.getBlockEntity(lp::game::blockPos(cpos)));
        return be ? static_cast<int>(be->mOutput) : -1;
    };
    ctx.cleanupBlock(cpos);
    // the comparator south of the potato, reading it (its cardinal_direction is then "north")
    auto const* comp = bs::of("minecraft:unpowered_comparator", {{"minecraft:cardinal_direction", "north"}});
    ctx.require(comp && lp::game::setBlock(region, lp::game::blockPos(cpos), *comp), "comparator placed");
    co_await ctx.ticks(8);
    ctx.info("comparator " + lp::game::blockIdAt(region, lp::game::blockPos(cpos)) + ", output " + std::to_string(output()));
    ctx.check(output() == 8, "a comparator placed next to it reads 8 (" + std::to_string(output()) + ")");
    fill(5);
    co_await ctx.ticks(8);
    ctx.check(output() == 10, "a 4th item: the comparator updates to 10 (" + std::to_string(output()) + ")");
    lp::game::clearBlock(region, lp::game::blockPos(pos));
    co_await ctx.ticks(8);
    ctx.check(output() == 0, "no potato, no signal (" + std::to_string(output()) + ")");
    clearItemsNear(ctx, pos);
});