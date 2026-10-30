// Selftests of the shared game helpers: items, inventory, forms, entities, virtual containers.
// Test positions: dx 0..39 (U0 range; the shape tests use 40..89).

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"

#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Forms.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "game/WorldDb.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
namespace inv = lp::game::inv;
namespace vc  = lp::game::vc;
using lp::game::makeItem;

int countOf(Container const& c, std::string const& id) {
    return inv::count(c, [&](ItemStack const& s) { return lp::game::itemId(s) == id; }, inv::all(c));
}
} // namespace

LP_REGISTER_SELFTEST("game.items", [](Context& ctx) {
    auto stone = makeItem("minecraft:stone", 5);
    ctx.check(lp::game::itemId(stone) == "minecraft:stone" && stone.mCount == 5, "makeItem builds a stack");
    ctx.check(lp::game::isEmpty(makeItem("lepton:no_such_item")), "unknown ids give an empty stack");
    auto back = lp::game::itemFromSnbt(lp::game::itemToSnbt(stone));
    ctx.check(lp::game::itemId(back) == "minecraft:stone" && back.mCount == 5, "SNBT round trip keeps id and count");
    ctx.check(lp::game::canMerge(stone, makeItem("minecraft:stone")), "same items merge");
    ctx.check(!lp::game::canMerge(stone, makeItem("minecraft:dirt")), "different items don't merge");
    ctx.check(!lp::game::canMerge(makeItem("minecraft:diamond_sword"), makeItem("minecraft:diamond_sword")), "unstackable items don't merge");
});

LP_REGISTER_SELFTEST("game.inventory", [](Context& ctx) {
    auto& p    = ctx.player();
    auto& pinv = inv::playerInventory(p);
    inv::restore(pinv, inv::kPlayerAll, {});
    ctx.check(pinv.getContainerSize() >= inv::kPlayerSlots, "player inventory has 36 slots");

    auto stack = makeItem("minecraft:cobblestone", 64);
    int  moved = inv::insert(pinv, stack, inv::kPlayerMain);
    auto rest  = makeItem("minecraft:cobblestone", 36);
    moved += inv::insert(pinv, rest, inv::kPlayerMain);
    ctx.check(moved == 100 && lp::game::isEmpty(stack) && lp::game::isEmpty(rest), "insert moves the whole stack");
    ctx.check(pinv.getItem(9).mCount == 64 && pinv.getItem(10).mCount == 36, "insert fills slot by slot from the range start");
    auto more = makeItem("minecraft:cobblestone", 30);
    inv::insert(pinv, more, inv::kPlayerMain, true);
    ctx.check(pinv.getItem(10).mCount == 64 && more.mCount == 2, "mergeOnly tops up partial stacks only");
    ctx.check(inv::roomFor(pinv, makeItem("minecraft:cobblestone", 64), {9, 11}) == 0, "roomFor sees full slots");

    ctx.check(inv::remove(pinv, {}, 70, inv::kPlayerAll) == 70, "remove takes items");
    ctx.check(countOf(pinv, "minecraft:cobblestone") == 58, "count after remove");

    inv::restore(pinv, inv::kPlayerAll, {});
    pinv.setItem(20, makeItem("minecraft:dirt", 3));
    pinv.setItem(12, makeItem("minecraft:stone", 10));
    pinv.setItem(30, makeItem("minecraft:stone", 60));
    inv::sort(pinv, inv::kPlayerMain);
    ctx.check(countOf(pinv, "minecraft:stone") == 70 && countOf(pinv, "minecraft:dirt") == 3, "sort keeps every item");
    ctx.check(pinv.getItem(9).mCount == 64 && !lp::game::isEmpty(pinv.getItem(11)) && lp::game::isEmpty(pinv.getItem(12)),
              "sort compacts and packs the stacks at the start");
    ctx.check(lp::game::itemId(pinv.getItem(9)) == lp::game::itemId(pinv.getItem(10)), "sort groups equal items");

    auto& b = ctx.spawnPlayer();
    auto& binv = inv::playerInventory(b);
    inv::restore(binv, inv::kPlayerAll, {});
    int sent = inv::transferAll(pinv, inv::kPlayerMain, binv, inv::kHotbar,
                                [](ItemStack const& s) { return lp::game::itemId(s) == "minecraft:stone"; });
    ctx.check(sent == 70 && countOf(binv, "minecraft:stone") == 70 && countOf(pinv, "minecraft:stone") == 0,
              "transferAll moves the matching stacks");
    inv::restore(pinv, inv::kPlayerAll, {});
    inv::restore(binv, inv::kPlayerAll, {});
    inv::sync(p);
});

LP_REGISTER_SELFTEST("game.forms", [](Context& ctx) {
    namespace forms = lp::game::forms;
    auto& p       = ctx.player();
    int   clicked = -1;
    bool  closed  = false;
    forms::SimpleForm("Probe", "Pick")
        .header("Header")
        .button("A", [&](Player&) { clicked = 0; })
        .label("between")
        .button("B", [&](Player&) { clicked = 1; })
        .onClose([&](Player&) { closed = true; })
        .sendTo(p);
    ctx.check(forms::testing::pendingTitle(p) == "Probe", "the form is pending for the player");
    ctx.check(forms::testing::click(p, 1) && clicked == 1, "button index counts buttons only");
    ctx.check(forms::testing::pendingTitle(p).empty(), "answered forms are no longer pending");

    forms::SimpleForm("Probe 2").button("A").onClose([&](Player&) { closed = true; }).sendTo(p);
    ctx.check(forms::testing::close(p) && closed, "closing runs onClose");

    int answer = -1;
    forms::ModalForm("Sure?", "Really", "Yes", "No").onYes([&](Player&) { answer = 1; }).onNo([&](Player&) { answer = 0; }).sendTo(p);
    ctx.check(forms::testing::click(p, 1) && answer == 0, "modal lower button = no");

    forms::Response got;
    forms::CustomForm("Settings")
        .input("name", "Name", "", "Steve")
        .toggle("on", "On", true)
        .dropdown("pick", "Pick", {"a", "b", "c"}, 2)
        .slider("vol", "Volume", 0, 10, 1, 5)
        .onSubmit([&](Player&, forms::Response const& r) { got = r; })
        .sendTo(p);
    forms::Response r;
    r.values["vol"] = 7.0;
    ctx.check(forms::testing::submit(p, r), "custom form submitted");
    ctx.check(got.text("name") == "Steve" && got.boolean("on") && got.index("pick") == 2, "defaults fill missing values");
    ctx.check(got.number("vol") == 7.0, "submitted values win");
    forms::closeFor(p);
});

LP_REGISTER_SELFTEST("game.entities", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace ent = lp::game::entities;
    auto pos = ctx.testPos(5);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    Vec3  at{pos.x + 0.5f, pos.y + 1.0f, pos.z + 0.5f};
    auto* e = ent::spawn(region, "lepton:u0_probe_entity", at);
    ctx.require(e != nullptr, "probe entity spawned (behaviour pack loaded)");
    auto id = ent::idOf(*e);
    co_await ctx.ticks(2);
    auto* found = ent::find(id);
    ctx.check(found != nullptr && ent::typeOf(*found) == "lepton:u0_probe_entity", "find by unique id");
    AABB box{Vec3{at.x - 1, at.y - 1, at.z - 1}, Vec3{at.x + 1, at.y + 1, at.z + 1}};
    ctx.check(ent::inBox(region, box, "lepton:u0_probe_entity").size() == 1, "inBox filters by type");
    ctx.check(!ent::ofType("lepton:u0_probe_entity", 0).empty(), "ofType finds it");
    if (found) {
        ctx.check(ent::triggerEvent(*found, "lepton:mark"), "events run");
        ent::despawn(*found);
    }
    co_await ctx.ticks(2);
    ctx.check(ent::find(id) == nullptr, "despawned entity is gone");
    ctx.check(ent::spawn(region, "lepton:no_such_entity", at) == nullptr, "unknown types give nullptr");
});

LP_REGISTER_SELFTEST("game.vc.storage", [](Context& ctx) {
    lp::core::GlobalPos pos{0, 1000000, 64, 1000000};
    vc::setItems(pos, {makeItem("minecraft:apple", 3), {}, makeItem("minecraft:stick", 9)});
    auto items = vc::items(pos, 27);
    ctx.check(items.size() == 27, "items() pads to the requested size");
    ctx.check(lp::game::itemId(items[0]) == "minecraft:apple" && items[2].mCount == 9, "stored items read back");
    vc::flush();
    auto stored = lp::game::db::load("lp:vc:" + pos.key());
    ctx.check(stored && stored->value("items", nlohmann::json::array()).size() == 2, "flush writes the non-empty slots to the DB");
    auto taken = vc::takeAll(pos);
    ctx.check(taken.size() == 2 && !vc::hasItems(pos), "takeAll empties the container");
    vc::flush();
    ctx.check(!lp::game::db::load("lp:vc:" + pos.key()), "an empty container leaves no DB entry");
});

// Screen::Dispenser: a 3x3 screen served by the chest model of the helper (the game's dispenser
// model needs a dispenser block). Items move through the model like a client's item requests.
LP_REGISTER_SELFTEST("game.vc.dispenser", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(15, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "container block placed");
    auto& p = ctx.player();
    co_await ctx.ticks(2);
    vc::takeAll(pos);
    vc::setItems(pos, {makeItem("minecraft:apple", 2)});
    vc::Spec spec;
    spec.screen        = vc::Screen::Dispenser;
    spec.titleFallback = "Probe";
    ctx.require(vc::open(p, pos, spec), "dispenser screen requested");
    co_await ctx.ticks(6);
    auto const& model = p.mContainerManager.get();
    ctx.require(model != nullptr, "the player has a container manager");
    ctx.check(model->getContainerType() == SharedTypes::Legacy::ContainerType::Dispenser, "it is a dispenser screen");
    auto* live = vc::liveContainer(pos);
    ctx.require(live && live->getContainerSize() == 9, "nine slots");
    FullContainerName level{};
    level.mName = ContainerEnumName::LevelEntityContainer;
    ctx.check(lp::game::itemId(model->getFullContainerSlot(0, level)) == "minecraft:apple", "the screen shows the stored items");
    // Move an item from the player into the container: the screen (model) sees it in the slot.
    auto& pinv = inv::playerInventory(p);
    pinv.setItem(0, makeItem("minecraft:stick", 5));
    ctx.check(inv::transferSlot(pinv, 0, *live, {8, 9}) == 5, "an item moved into the last slot");
    co_await ctx.ticks(2);
    ctx.check(lp::game::itemId(model->getFullContainerSlot(8, level)) == "minecraft:stick", "the screen shows the moved item");
    ctx.check(lp::game::itemId(vc::items(pos, 9)[8]) == "minecraft:stick", "the container keeps it");
    vc::closeAll(pos);
    co_await ctx.ticks(2);
    ctx.check(p.mContainerManager.get() == nullptr && vc::viewers(pos) == 0, "closeAll closes the screen");
    ctx.check(vc::items(pos, 9)[8].mCount == 5, "contents persist");
    vc::takeAll(pos);
    ctx.cleanupBlock(pos);
});

LP_REGISTER_SELFTEST("game.vc.open", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(10, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:stone"), "container block placed");
    auto& a = ctx.player();
    auto& b = ctx.spawnPlayer();
    b.teleport(a.getPosition(), a.getDimensionId());
    co_await ctx.ticks(2);
    vc::takeAll(pos);
    vc::setItems(pos, {makeItem("minecraft:apple", 2)});
    vc::Spec spec;
    spec.size          = 27;
    spec.titleFallback = "Probe";
    int changes        = 0;
    spec.onChanged     = [&](lp::core::GlobalPos const&) { ++changes; };
    ctx.require(vc::open(a, pos, spec), "opened for the first player");
    ctx.check(vc::open(b, pos, spec), "a second player joins the same container");
    co_await ctx.ticks(3);
    ctx.check(vc::viewers(pos) == 2, "two viewers");
    ctx.check(vc::openBy(a) == pos, "openBy finds the position");
    auto* live = vc::liveContainer(pos);
    ctx.require(live != nullptr, "live container while open");
    ctx.check(live->getContainerSize() == 27 && lp::game::itemId(live->getItem(0)) == "minecraft:apple", "the screen shows the stored items");
    live->setItem(5, makeItem("minecraft:diamond", 4));
    co_await ctx.ticks(2);
    ctx.check(changes >= 1, "onChanged fires");
    ctx.check(lp::game::itemId(vc::items(pos, 27)[5]) == "minecraft:diamond", "items() sees live edits");
    ctx.check(lp::game::entities::ofType(vc::kHelperEntity, 0).size() == 1, "one helper entity for both viewers");
    vc::closeAll(pos);
    co_await ctx.ticks(3);
    ctx.check(vc::viewers(pos) == 0 && vc::liveContainer(pos) == nullptr, "closeAll ends the session");
    ctx.check(lp::game::entities::ofType(vc::kHelperEntity, 0).empty(), "the helper entity is removed");
    auto after = vc::items(pos, 27);
    ctx.check(after[5].mCount == 4 && after[0].mCount == 2, "contents persist after closing");

    vc::Spec hopper;
    hopper.screen = vc::Screen::Hopper;
    ctx.require(vc::open(a, pos, hopper), "reopened as a hopper screen");
    co_await ctx.ticks(1);
    auto* h = vc::liveContainer(pos);
    ctx.check(h && h->getContainerSize() == 5, "hopper screens have 5 slots");
    vc::closeAll(pos);
    co_await ctx.ticks(1);
    auto small = vc::items(pos, 27);
    ctx.check(small[0].mCount == 2 && lp::game::isEmpty(small[5]), "a smaller screen keeps the first slots and drops the rest");
    vc::takeAll(pos);
    auto& region = a.getDimensionBlockSource();
    AABB  box{Vec3{pos.x - 2.f, pos.y - 2.f, pos.z - 2.f}, Vec3{pos.x + 3.f, pos.y + 3.f, pos.z + 3.f}};
    for (auto* d : lp::game::entities::inBox(region, box, "minecraft:item")) lp::game::entities::despawn(*d);
});

LP_REGISTER_SELFTEST("game.vc.bind", [](Context& ctx) -> ll::coro::CoroTask<> {
    std::string const id  = "lepton:u0_probe_stone";
    auto              pos = ctx.testPos(20, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, id), "probe block placed");
    auto& p  = static_cast<SimulatedPlayer&>(ctx.player());
    vc::Spec spec;
    spec.titleFallback = "Bound";
    vc::bindBlock(id, spec);
    vc::takeAll(pos);
    vc::setItems(pos, {makeItem("minecraft:emerald", 3)});
    co_await ctx.ticks(10);
    auto hand = makeItem("minecraft:stick");  // an empty hand does not reach useItemOn
    p.simulateUseItemOnBlock(hand, lp::game::blockPos(pos), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    for (int i = 0; i < 10 && vc::viewers(pos) == 0; ++i) co_await ctx.ticks(1);
    ctx.check(vc::viewers(pos) == 1, "using the bound block opens it");
    vc::closeAll(pos);
    co_await ctx.ticks(1);
    auto& region = p.getDimensionBlockSource();
    lp::game::clearBlock(region, lp::game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(!vc::hasItems(pos), "breaking the block drops the contents");
    AABB box{Vec3{pos.x - 2.f, pos.y - 2.f, pos.z - 2.f}, Vec3{pos.x + 3.f, pos.y + 3.f, pos.z + 3.f}};
    auto drops = lp::game::entities::inBox(region, box, "minecraft:item");
    ctx.check(!drops.empty(), "the items were dropped as item entities");
    for (auto* d : drops) lp::game::entities::despawn(*d);
    vc::unbindBlock(id);
});
