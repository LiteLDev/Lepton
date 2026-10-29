// Selftests of U11 logistics (dx 1100..1199).

#include <cmath>
#include <format>
#include <set>

#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/BreedableComponent.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/logistics/Crate.h"
#include "feature/logistics/Logistics.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Forms.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "mod/Config.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace {

using lp::core::GlobalPos;
using lp::selftest::Context;
namespace lg  = lp::feature::logistics;
namespace ent = lp::game::entities;

GlobalPos at(GlobalPos p, int dx, int dy, int dz) { return {p.dim, p.x + dx, p.y + dy, p.z + dz}; }

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

/// Item entities (and their total count) in the block space of `p` (grown by `grow`).
int itemsNear(BlockSource& region, GlobalPos const& p, float grow = 0.6f, std::string const& id = {}) {
    AABB box{Vec3{p.x - grow, p.y - grow, p.z - grow}, Vec3{p.x + 1 + grow, p.y + 1 + grow, p.z + 1 + grow}};
    int  n = 0;
    for (auto* a : ent::inBox(region, box, "minecraft:item")) {
        auto const& s = static_cast<ItemActor*>(a)->mItem.get();
        if (id.empty() || lp::game::itemId(s) == id) n += s.mCount;
    }
    return n;
}
void describeItems(Context& ctx, BlockSource& region, GlobalPos const& p, float grow) {
    AABB box{Vec3{p.x - grow, p.y - grow, p.z - grow}, Vec3{p.x + 1 + grow, p.y + 1 + grow, p.z + 1 + grow}};
    for (auto* a : ent::inBox(region, box, "minecraft:item")) {
        auto const& s = static_cast<ItemActor*>(a)->mItem.get();
        auto        q = a->getPosition();
        ctx.info(std::format("item {} x{} at {:.2f} {:.2f} {:.2f} (base {} {} {})", lp::game::itemId(s), s.mCount, q.x, q.y, q.z, p.x, p.y, p.z));
    }
}
/// Moves the test player away so it does not pick up the items the test looks for.
void stepAway(Context& ctx, GlobalPos const& p) {
    ctx.player().teleport(Vec3{p.x + 0.5f, p.y + 1.0f, p.z + 12.5f}, DimensionType(p.dim), Vec2{0, 0});
}
void clearItems(BlockSource& region, GlobalPos const& p, float grow = 3) {
    AABB box{Vec3{p.x - grow, p.y - grow, p.z - grow}, Vec3{p.x + 1 + grow, p.y + 1 + grow, p.z + 1 + grow}};
    for (auto* a : ent::inBox(region, box, "minecraft:item")) ent::despawn(*a);
}

Container* container(BlockSource& region, GlobalPos const& p) { return lg::containerAt(region, lp::game::blockPos(p)); }

int countIn(Container* c, std::string const& id) {
    if (!c) return -1;
    int n = 0;
    for (int i = 0; i < c->getContainerSize(); ++i)
        if (lp::game::itemId(c->getItem(i)) == id) n += c->getItem(i).mCount;
    return n;
}

int stateOf(BlockSource& region, GlobalPos const& p, std::string const& state) {
    return lp::game::blockstates::getInt(region.getBlock(lp::game::blockPos(p)), state).value_or(-1);
}

} // namespace

LP_REGISTER_SELFTEST("logistics.config", [](Context& ctx) {
    auto const& c = lg::cfg();
    ctx.check(c.version >= 1, "config section loaded");
    ctx.check(c.pipeSpeed > 0 && c.maxItems > 0 && c.range > 0, "config values are sane");
    ctx.check(lp::game::blockWith(lg::kChute) && lp::game::blockWith(lg::kPipe) && lp::game::blockWith(lg::kEncasedPipe)
                  && lp::game::blockWith(lg::kFeedingTrough) && lp::game::blockWith(lg::kCrate),
              "every logistics block is registered (behaviour pack loaded)");
});

LP_REGISTER_SELFTEST("logistics.chute_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1100);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  chute  = at(base, 0, 2, 0);
    auto  hopper = at(base, 0, 3, 0);
    stepAway(ctx, base);
    ctx.require(ctx.setBlock(chute, lg::kChute), "chute placed");
    ctx.require(ctx.setBlock(hopper, "minecraft:hopper", {{"facing_direction", 0}}), "hopper placed");
    co_await ctx.ticks(2);
    clearItems(region, base);
    auto* h = container(region, hopper);
    ctx.require(h != nullptr, "hopper container");
    h->setItem(0, lp::game::makeItem("minecraft:cobblestone", 3));
    auto* port = container(region, chute);
    ctx.check(port && port->getContainerSize() == 1 && port->canPushInItem(0, lg::Up, lp::game::makeItem("minecraft:stone")),
              "the chute is a 1-slot container that accepts items from above");
    ctx.check(port && !port->canPushInItem(0, lg::Down, lp::game::makeItem("minecraft:stone")), "but not through its bottom");
    co_await ctx.ticks(60);
    ctx.check(countIn(container(region, hopper), "minecraft:cobblestone") == 0, "the hopper emptied into the chute");
    describeItems(ctx, region, base, 12);
    ctx.check(itemsNear(region, at(base, 0, 0, 0), 1.2f, "minecraft:cobblestone") == 3, "the items dropped out below the chute");
    clearItems(region, base);
});

LP_REGISTER_SELFTEST("logistics.chute_blocked", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1105);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  chute  = at(base, 0, 2, 0);
    auto  hopper = at(base, 0, 3, 0);
    ctx.require(ctx.setBlock(at(base, 0, 1, 0), "minecraft:stone"), "solid block under the chute");
    ctx.require(ctx.setBlock(chute, lg::kChute), "chute placed");
    ctx.require(ctx.setBlock(hopper, "minecraft:hopper", {{"facing_direction", 0}}), "hopper placed");
    co_await ctx.ticks(2);
    container(region, hopper)->setItem(0, lp::game::makeItem("minecraft:cobblestone", 2));
    co_await ctx.ticks(30);
    ctx.check(countIn(container(region, hopper), "minecraft:cobblestone") == 2, "a chute over a solid block takes nothing");
    // redstone disables the chute
    ctx.require(ctx.setBlock(at(base, 1, 2, 0), "minecraft:redstone_block"), "redstone next to the chute");
    co_await ctx.ticks(4);
    ctx.require(ctx.setBlock(at(base, 0, 1, 0), "minecraft:air"), "space below again");
    ctx.check(stateOf(region, chute, "lepton:enabled") == 0, "redstone turns lepton:enabled off");
    co_await ctx.ticks(20);
    ctx.check(countIn(container(region, hopper), "minecraft:cobblestone") == 2, "a powered chute takes nothing");
    ctx.require(ctx.setBlock(at(base, 1, 2, 0), "minecraft:air"), "redstone removed");
    co_await ctx.ticks(40);
    ctx.check(stateOf(region, chute, "lepton:enabled") == 1, "the chute is enabled again");
    ctx.check(countIn(container(region, hopper), "minecraft:cobblestone") == 0, "and passes the items on");
    clearItems(region, base);
});

LP_REGISTER_SELFTEST("logistics.pipe_transport", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1110);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  a = at(base, 0, 2, 0), b = at(base, 1, 2, 0), chest = at(base, 2, 2, 0), hopper = at(base, 0, 3, 0);
    ctx.require(ctx.setBlock(at(base, 0, 1, 0), "minecraft:stone") && ctx.setBlock(at(base, 1, 1, 0), "minecraft:stone"), "floor");
    ctx.require(ctx.setBlock(chest, "minecraft:chest"), "chest placed");
    ctx.require(ctx.setBlock(a, lg::kPipe) && ctx.setBlock(b, lg::kPipe), "pipes placed");
    ctx.require(ctx.setBlock(hopper, "minecraft:hopper", {{"facing_direction", 0}}), "hopper placed");
    co_await ctx.ticks(3);
    ctx.check(stateOf(region, a, "lepton:conn_east") == 1, "pipe to pipe connection (state 1)");
    ctx.check(stateOf(region, b, "lepton:conn_east") == 3, "pipe to chest connection is a terminal (state 3)");
    ctx.check(stateOf(region, a, "lepton:conn_west") == 0, "no opening when a perpendicular side (the hopper) is connected");
    ctx.check(stateOf(region, a, "lepton:conn_up") == 3, "the hopper above is a terminal");
    container(region, hopper)->setItem(0, lp::game::makeItem("minecraft:cobblestone", 3));
    co_await ctx.ticks(100);
    ctx.check(countIn(container(region, chest), "minecraft:cobblestone") == 3, "items travelled hopper -> pipe -> pipe -> chest");
    ctx.check(lg::pipes::itemCount(a) == 0 && lg::pipes::itemCount(b) == 0, "the pipes are empty again");
    if (auto* c = container(region, chest)) c->removeAllItems();
    clearItems(region, base);
});

LP_REGISTER_SELFTEST("logistics.pipe_eject_absorb", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1115);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  a = at(base, 0, 2, 0), b = at(base, 1, 2, 0);
    stepAway(ctx, base);
    ctx.require(ctx.setBlock(a, lg::kPipe) && ctx.setBlock(b, lg::kPipe), "pipes placed");
    co_await ctx.ticks(3);
    clearItems(region, base);
    ctx.require(lg::pipes::passIn(b, lp::game::makeItem("minecraft:apple", 1), lg::East), "item passed into the pipe");
    co_await ctx.ticks(40);
    ctx.check(stateOf(region, a, "lepton:conn_west") == 2, "the open end opposite a connection is an opening (state 2)");
    describeItems(ctx, region, base, 16);
    ctx.check(itemsNear(region, at(base, -6, 2, 0), 6.0f, "minecraft:apple") == 1, "the item was shot out of the open west end");
    clearItems(region, base);
    // an item falling onto an opening that faces up is absorbed
    auto top = at(base, 0, 3, 0);
    ctx.require(ctx.setBlock(top, lg::kPipe), "upright pipe");
    co_await ctx.ticks(3);
    ctx.check(stateOf(region, top, "lepton:conn_up") == 2, "the top of the upright pipe is an opening");
    lg::spawnItemAt(region, lp::game::makeItem("minecraft:carrot", 2), Vec3{top.x + 0.5f, top.y + 1.1f, top.z + 0.5f}, Vec3{0, -0.2f, 0}, false);
    co_await ctx.ticks(10);
    ctx.check(lg::pipes::itemCount(top) + lg::pipes::itemCount(a) + lg::pipes::itemCount(b) >= 1, "the falling item was absorbed");
    co_await ctx.ticks(60);
    clearItems(region, base);
});

LP_REGISTER_SELFTEST("logistics.pipe_limits", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1120);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  p = at(base, 0, 2, 0), q = at(base, 1, 2, 0), hopper = at(base, 0, 3, 0);
    stepAway(ctx, base);
    ctx.require(ctx.setBlock(p, lg::kPipe) && ctx.setBlock(q, lg::kPipe), "pipes placed");
    ctx.require(ctx.setBlock(at(base, -1, 2, 0), "minecraft:redstone_block"), "redstone next to the pipe");
    ctx.require(ctx.setBlock(hopper, "minecraft:hopper", {{"facing_direction", 0}}), "hopper placed");
    co_await ctx.ticks(3);
    ctx.check(!lg::pipes::isEnabledPipe(p), "a powered pipe is disabled");
    container(region, hopper)->setItem(0, lp::game::makeItem("minecraft:cobblestone", 1));
    co_await ctx.ticks(20);
    ctx.check(countIn(container(region, hopper), "minecraft:cobblestone") == 1, "a disabled pipe accepts nothing");
    container(region, hopper)->removeAllItems();
    // more than maxPipeItems items break the pipe
    int max = lg::cfg().maxPipeItems;
    if (max > 0) {
        for (int i = 0; i <= max; ++i) lg::pipes::passIn(q, lp::game::makeItem("minecraft:stick", 1), lg::Up);
        co_await ctx.ticks(3);
        ctx.check(lp::game::blockIdAt(region, lp::game::blockPos(q)) != lg::kPipe, "an overfull pipe breaks");
        co_await ctx.ticks(2);
        ctx.check(itemsNear(region, q, 1.0f, lg::kPipe) == 1, "and drops itself");
        ctx.check(itemsNear(region, q, 3.0f, "minecraft:stick") == max + 1, "and the items inside");
    }
    clearItems(region, base);
});

LP_REGISTER_SELFTEST("logistics.pipe_encase", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1125);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  p      = at(base, 0, 2, 0);
    ctx.require(ctx.setBlock(p, lg::kPipe) && ctx.setBlock(at(base, 1, 2, 0), lg::kPipe), "pipes placed");
    co_await ctx.ticks(3);
    auto& player = static_cast<SimulatedPlayer&>(ctx.player());
    player.teleport(Vec3{p.x + 0.5f, p.y + 1.0f, p.z - 1.5f}, p.dim);
    co_await ctx.ticks(5);
    auto glass = lp::game::makeItem("minecraft:glass", 4);
    auto& inv  = lp::game::inv::playerInventory(player);
    inv.removeAllItems();
    ctx.give(player, glass, player.getSelectedItemSlot());
    bool creative = player.isCreative();
    player.simulateUseItemOnBlock(glass, lp::game::blockPos(p), ScriptModuleMinecraft::ScriptFacing::North, Vec3{0.5f, 0.5f, 0.0f});
    co_await ctx.ticks(5);
    ctx.check(lp::game::blockIdAt(region, lp::game::blockPos(p)) == lg::kEncasedPipe, "glass encases a copper pipe");
    ctx.check(creative || lp::game::inv::count(inv, [](ItemStack const& s) { return lp::game::itemId(s) == "minecraft:glass"; }, lp::game::inv::kPlayerAll) == 3,
              "it uses up one glass");
    inv.removeAllItems();
    ctx.check(stateOf(region, p, "lepton:conn_east") == 1, "the encased pipe keeps its connections");
    clearItems(region, base);
});

LP_REGISTER_SELFTEST("logistics.trough", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1130);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  t      = at(base, 0, 0, 0);
    ctx.require(ctx.setBlock(t, lg::kFeedingTrough), "trough placed");
    ctx.require(ctx.setBlock(at(base, 0, 1, 0), "minecraft:hopper", {{"facing_direction", 0}}), "hopper placed");
    co_await ctx.ticks(2);
    lp::game::vc::takeAll(t);
    container(region, at(base, 0, 1, 0))->setItem(0, lp::game::makeItem("minecraft:wheat", 5));
    co_await ctx.ticks(60);
    ctx.check(lp::game::vc::items(t, 9)[0].mCount == 5, "a hopper fills the trough");
    ctx.check(stateOf(region, t, "lepton:full") == 1, "a trough with food is full");
    ctx.setBlock(at(base, 0, 1, 0), "minecraft:air");
    // a cow nearby eats and falls in love (love chance forced to 1)
    auto& conf      = lp::config::mut<lg::LogisticsConfig>();
    auto  saved     = conf;
    conf.loveChance = 1.0;
    conf.lookChance = 1.0;
    // 5% of spawned cows are babies, which cannot fall in love: spawn until an adult comes out
    Actor* cow = nullptr;
    for (int i = 0; i < 10 && (!cow || cow->isBaby()); ++i) {
        if (cow) ent::despawn(*cow);
        cow = ent::spawn(region, "minecraft:cow", Vec3{t.x + 0.5f, t.y + 0.0f, t.z + 1.6f});
    }
    ctx.require(cow != nullptr && !cow->isBaby(), "adult cow spawned");
    auto cowId = ent::idOf(*cow);
    bool loved = false;
    for (int i = 0; i < 40 && !loved; ++i) {
        co_await ctx.ticks(5);
        if (auto* c = ent::find(cowId)) {
            auto b = c->getEntityContext().tryGetComponent<BreedableComponent>();
            loved  = b && b->mLoveTimer > 0;
        }
    }
    conf = saved;
    ctx.check(loved, "the cow ate from the trough and fell in love");
    int left = 0;
    for (auto const& s : lp::game::vc::items(t, 9)) left += lp::game::itemId(s) == "minecraft:wheat" ? s.mCount : 0;
    ctx.check(left == 4, "it ate one wheat");
    if (auto* c = ent::find(cowId)) ent::despawn(*c);
    // using the trough opens its screen (whether the game shows the dispenser screen for the helper
    // entity is up to game/VirtualContainer; the client scenario checks it)
    auto& p = ctx.player();
    p.teleport(Vec3{t.x + 0.5f, t.y + 1.0f, t.z + 1.5f}, t.dim);
    co_await ctx.ticks(3);
    ctx.check(lg::trough::open(p, t), "trough open requested");
    ctx.check(lp::game::vc::viewers(t) == 1, "the player views the trough");
    lp::game::vc::closeAll(t);
    lp::game::vc::takeAll(t);
    co_await ctx.ticks(2);
});

LP_REGISTER_SELFTEST("logistics.crate_capacity", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace crate = lg::crate;
    auto base = ctx.testPos(1140);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  c      = at(base, 0, 2, 0);
    ctx.require(ctx.setBlock(c, lg::kCrate), "crate placed");
    co_await ctx.ticks(2);
    int                    max = lg::cfg().maxItems;
    std::vector<ItemStack> stacks;
    for (int left = max - 1; left > 0; left -= 64) stacks.push_back(lp::game::makeItem("minecraft:cobblestone", std::min(64, left)));
    crate::setContents(c, stacks);
    ctx.check(crate::total(c) == max - 1, "contents set");
    auto* port = container(region, c);
    ctx.require(port != nullptr, "the crate is a container");
    auto more  = lp::game::makeItem("minecraft:dirt", 5);
    int  moved = lg::insertThroughFace(*port, more, lg::Up);
    ctx.check(moved == 1 && crate::total(c) == max, "only one item fits into a crate holding maxItems - 1");
    // unstackables count one each
    crate::setContents(c, {});
    for (int i = 0; i < 3; ++i) {
        auto sword = lp::game::makeItem("minecraft:iron_sword", 1);
        auto* pc   = container(region, c);
        lg::insertThroughFace(*pc, sword, lg::Up);
    }
    ctx.check(crate::stackCount(c) == 3 && crate::total(c) == 3, "every unstackable item takes a stack");
    crate::setContents(c, {});
});

LP_REGISTER_SELFTEST("logistics.crate_hoppers", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace crate = lg::crate;
    auto base = ctx.testPos(1145);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  c = at(base, 0, 2, 0), above = at(base, 0, 3, 0), below = at(base, 0, 1, 0);
    ctx.require(ctx.setBlock(c, lg::kCrate), "crate placed");
    crate::setContents(c, {});
    ctx.require(ctx.setBlock(above, "minecraft:hopper", {{"facing_direction", 0}}), "hopper above");
    co_await ctx.ticks(2);
    container(region, above)->setItem(0, lp::game::makeItem("minecraft:cobblestone", 4));
    co_await ctx.ticks(60);
    ctx.check(crate::total(c) == 4, "a hopper above fills the crate");
    ctx.require(ctx.setBlock(below, "minecraft:hopper", {{"facing_direction", 2}}), "hopper below");
    co_await ctx.ticks(60);
    ctx.check(crate::total(c) == 0 && countIn(container(region, below), "minecraft:cobblestone") == 4, "a hopper below empties it");
    container(region, below)->removeAllItems();
});

LP_REGISTER_SELFTEST("logistics.crate_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace crate = lg::crate;
    namespace forms = lp::game::forms;
    auto base = ctx.testPos(1150);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  c      = at(base, 0, 2, 0);
    ctx.require(ctx.setBlock(c, lg::kCrate), "crate placed");
    co_await ctx.ticks(2);
    std::vector<ItemStack> stacks;
    for (int i = 0; i < 70; ++i) stacks.push_back(lp::game::makeItem("minecraft:iron_sword", 1));
    stacks[60] = lp::game::makeItem("minecraft:apple", 7);
    crate::setContents(c, stacks);
    auto& p = ctx.player();
    p.teleport(Vec3{c.x + 0.5f, c.y + 1.0f, c.z + 1.5f}, c.dim);
    co_await ctx.ticks(3);
    // vanilla client: pick the rows first
    lp::net::testing::setClientMod(p, false);
    ctx.require(crate::open(p, c), "open requested");
    ctx.check(!forms::testing::pendingTitle(p).empty(), "a vanilla client with 70 stacks gets the row picker");
    ctx.check(forms::testing::click(p, 1), "second page picked");
    co_await ctx.ticks(3);
    ctx.check(crate::scrollOf(c) == 54, "the window starts at rows 7-8 (stack 54)");
    co_await ctx.ticks(6);
    ctx.check(lp::game::vc::viewers(c) == 1, "the crate screen is open");
    ctx.check(p.mContainerManager.get() != nullptr, "the crate screen is open on the server");
    ctx.check(stateOf(region, c, "lepton:open") == 1, "the crate shows its open lid");
    int scroll = crate::scrollOf(c);
    auto* live = lp::game::vc::liveContainer(c);
    ctx.require(live != nullptr, "live window");
    ctx.check(lp::game::itemId(live->getItem(60 - scroll)) == "minecraft:apple", "the window shows the crate's stacks");
    // taking a stack out of the window takes it out of the crate (and compacts it)
    live->setItem(60 - scroll, ItemStack{});
    co_await ctx.ticks(3);
    ctx.check(crate::stackCount(c) == 69 && crate::total(c) == 69, "the change went back to the crate");
    // mod client: scroll by rows
    ctx.check(crate::scroll(c, false), "scroll up one row");
    ctx.check(crate::scrollOf(c) == scroll - 9, "scrolled by 9 stacks");
    ctx.check(!crate::scroll(c, false) || crate::scrollOf(c) >= 0, "never above the top");
    lp::game::vc::closeAll(c);
    co_await ctx.ticks(5);
    ctx.check(crate::scrollOf(c) == -1, "the session ended");
    ctx.check(stateOf(region, c, "lepton:open") == 0, "the lid closed");
    ctx.check(crate::total(c) == 69, "closing keeps the contents");
    lp::net::testing::setClientMod(p, true);
    ctx.require(crate::open(p, c), "mod client opens directly");
    co_await ctx.ticks(2);
    ctx.check(crate::scrollOf(c) == 0 && forms::testing::pendingTitle(p).empty(), "no form for mod clients");
    lp::game::vc::closeAll(c);
    lp::net::testing::setClientMod(p, false);
    co_await ctx.ticks(3);
    crate::setContents(c, {});
});

LP_REGISTER_SELFTEST("logistics.crate_spill", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace crate = lg::crate;
    auto base = ctx.testPos(1155);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    auto  c      = at(base, 0, 2, 0);
    stepAway(ctx, base);
    ctx.require(ctx.setBlock(c, lg::kCrate), "crate placed");
    co_await ctx.ticks(2);
    clearItems(region, base);
    crate::setContents(c, {lp::game::makeItem("minecraft:stick", 10), lp::game::makeItem("minecraft:stick", 10)});
    ctx.setBlock(c, "minecraft:air");
    co_await ctx.ticks(4);
    ctx.check(itemsNear(region, c, 2.0f, "minecraft:stick") == 20, "breaking the crate spills its items");
    ctx.check(crate::total(c) == 0, "and empties it");
    clearItems(region, base);
});

// Every block drops itself when broken (loot tables), the trough also its contents, and every
// recipe of the unit is in the level's recipe table (behaviour pack recipes parsed).
LP_REGISTER_SELFTEST("logistics.drops_recipes", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(1160);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    stepAway(ctx, base);
    char const* ids[] = {lg::kChute, lg::kPipe, lg::kEncasedPipe, lg::kFeedingTrough, lg::kCrate};
    for (int i = 0; i < 5; ++i) {
        ctx.setBlock(at(base, i * 3, 0, 0), "minecraft:stone");
        ctx.require(ctx.setBlock(at(base, i * 3, 1, 0), ids[i]), std::format("{} placed", ids[i]));
    }
    co_await ctx.ticks(2);
    clearItems(region, base, 16);
    auto trough = at(base, 9, 1, 0);
    lp::game::vc::setItems(trough, {lp::game::makeItem("minecraft:wheat", 3)});
    for (int i = 0; i < 5; ++i) {
        BlockChangeContext change{};
        ctx.player().getLevel().destroyBlock(region, lp::game::blockPos(at(base, i * 3, 1, 0)), true, change);
    }
    co_await ctx.ticks(5);
    for (int i = 0; i < 5; ++i)
        ctx.check(itemsNear(region, at(base, i * 3, 1, 0), 1.2f, ids[i]) == 1, std::format("{} drops itself", ids[i]));
    ctx.check(itemsNear(region, trough, 1.5f, "minecraft:wheat") == 3, "a broken trough drops its contents");
    clearItems(region, base, 16);
    std::set<std::string> want{"lepton:chute", "lepton:pipe", "lepton:encased_pipe", "lepton:encased_pipe_revert",
                               "lepton:feeding_trough", "lepton:crate"};
    for (auto& [tag, byId] : ctx.player().getLevel().getRecipes().mRecipes.get())
        for (auto const& [id, recipe] : byId) want.erase(id);
    for (auto const& id : want) ctx.check(false, std::format("recipe {} is registered", id));
});

// Showcase for the client e2e scenario: builds a chute, a pipe line with items moving, a trough with
// a cow and a crate, moves the real player in front of them and waits for the client scenario to use
// the trough and the crate (real right clicks), scroll the crate and close the screens.
// On a server without a real player it only builds and checks the setup.
LP_REGISTER_SELFTEST("logistics.showcase", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace crate = lg::crate;
    auto base = ctx.testPos(1170, -3);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = regionOf(ctx);
    for (int dx = -1; dx <= 12; ++dx)
        for (int dz = -1; dz <= 3; ++dz) ctx.setBlock(at(base, dx, -1, dz), "minecraft:smooth_stone");
    auto chute = at(base, 0, 1, 0), chuteHopper = at(base, 0, 2, 0);
    ctx.setBlock(chute, lg::kChute);
    ctx.setBlock(chuteHopper, "minecraft:hopper", {{"facing_direction", 0}});
    auto feed = at(base, 2, 2, 0);
    ctx.setBlock(feed, "minecraft:hopper", {{"facing_direction", 0}});
    for (int dx = 2; dx <= 6; ++dx) ctx.setBlock(at(base, dx, 1, 0), dx == 4 ? lg::kEncasedPipe : lg::kPipe);
    ctx.setBlock(at(base, 2, 1, 2), lg::kPipe);
    ctx.setBlock(at(base, 3, 1, 2), lg::kEncasedPipe);
    auto chest = at(base, 7, 1, 0);
    ctx.setBlock(chest, "minecraft:chest");
    auto trough = at(base, 9, 0, 0);
    ctx.setBlock(trough, lg::kFeedingTrough);
    auto crateA = at(base, 11, 0, 0);
    ctx.setBlock(crateA, lg::kCrate);
    co_await ctx.ticks(3);
    container(region, chuteHopper)->setItem(0, lp::game::makeItem("minecraft:cobblestone", 64));
    container(region, feed)->setItem(0, lp::game::makeItem("minecraft:apple", 64));
    container(region, feed)->setItem(1, lp::game::makeItem("minecraft:diamond", 64));
    lp::game::vc::setItems(trough, {lp::game::makeItem("minecraft:wheat", 32), lp::game::makeItem("minecraft:carrot", 12)});
    lg::trough::updateFull(trough);
    std::vector<ItemStack> stacks;
    for (int i = 0; i < 40; ++i) stacks.push_back(lp::game::makeItem(i % 3 ? "minecraft:iron_sword" : "minecraft:bread", i % 3 ? 1 : 16));
    for (int i = 0; i < 24; ++i) stacks.push_back(lp::game::makeItem("minecraft:oak_log", 16)); // 64 stacks, 634 items
    crate::setContents(crateA, stacks);
    auto* cow = ent::spawn(region, "minecraft:cow", Vec3{trough.x + 0.5f, trough.y + 0.0f, trough.z + 2.5f});
    co_await ctx.ticks(40);
    ctx.check(stateOf(region, at(base, 2, 1, 2), "lepton:conn_west") == 2, "the pipe stub has an open end");
    ctx.check(stateOf(region, trough, "lepton:full") == 1, "the trough shows its food");

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (viewer) {
        // The real player is moved in front of each block; the client scenario uses the blocks with
        // a real right click (the path a player takes), takes screenshots and closes the screens.
        auto uid  = viewer->getOrCreateUniqueID();
        auto real = [&]() { return ctx.player().getLevel().getPlayer(uid); };
        // stand on the floor at (x, z) and look at the point `target` (coordinates relative to base)
        auto look = [&](float x, float z, Vec3 target) {
            auto* v = real();
            if (!v) return;
            Vec3  eye{x, 1.62f, z};
            Vec3  d     = target - eye;
            float yaw   = std::atan2(-d.x, d.z) * 57.29578f;
            float pitch = -std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) * 57.29578f;
            v->teleport(Vec3{base.x + x, base.y + eye.y, base.z + z}, DimensionType(base.dim), Vec2{pitch, yaw});
        };
        auto vcOpen  = [&](GlobalPos const& p) { return lp::game::vc::viewers(p) > 0; };
        auto formFor = [&]() { auto* v = real(); return v ? lp::game::forms::testing::pendingTitle(*v) : std::string{}; };
        constexpr int kWait = 30 * 20; // ticks the scenario has for each step

        look(6.f, 7.f, Vec3{6.f, 1.f, 0.5f});
        ctx.info("LOGISTICS view blocks");
        co_await ctx.ticks(120);
        look(4.5f, 2.6f, Vec3{4.5f, 1.5f, 0.5f});
        ctx.info("LOGISTICS view pipes");
        co_await ctx.ticks(120);

        // feeding trough: right click opens the dispenser-style screen
        look(9.5f, 2.2f, Vec3{9.5f, 0.3f, 0.5f});
        co_await ctx.ticks(10);
        ctx.info("LOGISTICS aim trough");
        int t = 0;
        for (; t < kWait && !vcOpen(trough); ++t) co_await ctx.ticks(1);
        ctx.check(vcOpen(trough), "using the trough opens its screen");
        if (vcOpen(trough)) ctx.info("LOGISTICS screen trough");
        bool shown = false;
        for (t = 0; t < kWait && vcOpen(trough); ++t) {
            if (!shown && real() && real()->mContainerManager.get()) {
                shown = true;
                ctx.info(std::format("LOGISTICS trough container open on the server after {} ticks", t));
            }
            co_await ctx.ticks(1);
        }
        ctx.check(shown, "the trough's container was open for the player");
        ctx.check(!vcOpen(trough), "the trough screen was closed");
        lp::game::vc::closeAll(trough);

        // storage crate: a vanilla client with more than 54 stacks first picks the rows
        look(11.5f, 2.2f, Vec3{11.5f, 0.5f, 0.5f});
        co_await ctx.ticks(10);
        ctx.info("LOGISTICS aim crate");
        for (t = 0; t < kWait && !vcOpen(crateA) && formFor().empty(); ++t) co_await ctx.ticks(1);
        if (!formFor().empty()) {
            ctx.info("LOGISTICS form crate");
            for (t = 0; t < kWait && !formFor().empty() && !vcOpen(crateA); ++t) co_await ctx.ticks(1);
            if (!vcOpen(crateA)) {
                // the form was dismissed: with one screen of stacks the crate opens directly
                stacks.resize(crate::kWindow);
                crate::setContents(crateA, stacks);
                ctx.info("LOGISTICS aim crate again");
                for (t = 0; t < kWait && !vcOpen(crateA); ++t) co_await ctx.ticks(1);
            }
        }
        ctx.check(vcOpen(crateA), "using the crate opens its screen");
        if (vcOpen(crateA)) {
            ctx.info(std::format("LOGISTICS screen crate (scroll {})", crate::scrollOf(crateA)));
            int first = crate::scrollOf(crateA);
            for (t = 0; t < kWait && vcOpen(crateA); ++t) {
                if (crate::scrollOf(crateA) > first) {
                    ctx.info(std::format("LOGISTICS crate scrolled to {}", crate::scrollOf(crateA)));
                    first = crate::scrollOf(crateA);
                }
                co_await ctx.ticks(1);
            }
            ctx.check(!vcOpen(crateA), "the crate screen was closed");
        }
        lp::game::vc::closeAll(crateA);
        if (auto* v = real()) lp::game::forms::closeFor(*v);
        co_await ctx.ticks(2);
        ctx.check(stateOf(region, crateA, "lepton:open") == 0, "the crate lid closed");
        ctx.info("LOGISTICS done");
    }    if (cow) ent::despawn(*cow);
    crate::setContents(crateA, {});
    lp::game::vc::takeAll(trough);
    if (auto* c = container(region, chest)) c->removeAllItems();
    if (auto* h = container(region, feed)) h->removeAllItems();
    if (auto* h = container(region, chuteHopper)) h->removeAllItems();
    co_await ctx.ticks(2);
    clearItems(region, base, 8);
});
