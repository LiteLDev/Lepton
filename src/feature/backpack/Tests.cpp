// Selftests of U14 backpack (dx 1400..1499). See docs/spec/backpack.md §8.

#include "feature/backpack/Backpack.h"

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/backpack/Messages.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
using SharedTypes::Legacy::ArmorSlot;
namespace bp  = lp::feature::backpack;
namespace inv = lp::game::inv;
using lp::game::makeItem;

ItemStack fullBackpack(std::string const& id = "minecraft:dirt", int count = 5, int slot = 3) {
    auto b     = makeItem(bp::kBackpack);
    auto items = std::vector<ItemStack>(bp::kBackpackSlots);
    items[static_cast<size_t>(slot)] = makeItem(id, count);
    bp::setBackpackItems(b, items);
    return b;
}

void survival(Player& p) { p.setPlayerGameType(GameType::Survival); }

void clearPlayer(Player& p) {
    auto& pinv = inv::playerInventory(p);
    for (int i = 0; i < inv::kPlayerSlots; ++i) pinv.setItem(i, ItemStack{});
    for (int a = 0; a < 4; ++a) p.setArmor(static_cast<ArmorSlot>(a), ItemStack{});
    p.setOffhandSlot(ItemStack{});
}

int countItemsNear(Player& p, lp::core::GlobalPos const& pos, std::string const& id, float r = 4) {
    auto& region = p.getDimensionBlockSource();
    AABB  box{
        Vec3{pos.x - r, pos.y - r, pos.z - r},
        Vec3{pos.x + r, pos.y + r + 2, pos.z + r}
    };
    int n = 0;
    for (auto* a : lp::game::entities::inBox(region, box, "minecraft:item")) {
        auto& s = static_cast<ItemActor*>(a)->item();
        if (lp::game::itemId(s) == id) n += s.mCount;
    }
    return n;
}

void clearItemsNear(Player& p, lp::core::GlobalPos const& pos, float r = 6) {
    auto& region = p.getDimensionBlockSource();
    AABB  box{
        Vec3{pos.x - r, pos.y - r, pos.z - r},
        Vec3{pos.x + r, pos.y + r + 2, pos.z + r}
    };
    for (auto* a : lp::game::entities::inBox(region, box, "minecraft:item")) lp::game::entities::despawn(*a);
}

} // namespace

LP_REGISTER_SELFTEST("backpack.items", [](Context& ctx) {
    for (auto id : {bp::kBackpack, bp::kRavagerHide, bp::kSeedPouch})
        ctx.check(!lp::game::isEmpty(makeItem(id)), std::string(id) + " is registered");
    ctx.check(lp::game::blockWith(bp::kBondedHide) != nullptr, "bonded ravager hide block is registered");
    ctx.check(bp::hasItemTag(makeItem(bp::kBackpack), "lepton:backpack_blocked"), "backpacks carry the blocked tag");
    ctx.check(bp::cfg().version >= 1 && bp::cfg().maxItems == 640, "config loaded with Quark's defaults");
});

LP_REGISTER_SELFTEST("backpack.storage", [](Context& ctx) {
    auto b     = makeItem(bp::kBackpack);
    auto items = std::vector<ItemStack>(bp::kBackpackSlots);
    items[3]   = makeItem("minecraft:dirt", 5);
    items[26]  = makeItem("minecraft:diamond_sword");
    ctx.check(!bp::backpackHasItems(b), "a new backpack is empty");
    bp::setBackpackItems(b, items);
    ctx.require(bp::backpackHasItems(b), "contents are stored in the item");
    auto copy = ItemStack(b); // travels with the item
    auto back = bp::backpackItems(copy);
    ctx.check(lp::game::itemId(back[3]) == "minecraft:dirt" && back[3].mCount == 5, "slot 3 reads back");
    ctx.check(lp::game::itemId(back[26]) == "minecraft:diamond_sword", "slot 26 reads back");
    auto snbt = lp::game::itemToSnbt(b);
    ctx.check(bp::backpackHasItems(lp::game::itemFromSnbt(snbt)), "contents survive a save/load round trip");
    bp::setBackpackItems(b, {});
    ctx.check(!bp::backpackHasItems(b) && !b.mUserData, "emptying removes the data");
});

LP_REGISTER_SELFTEST("backpack.open_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(1405)), "test area loaded");
    survival(p);
    clearPlayer(p);
    p.setArmor(ArmorSlot::Torso, fullBackpack());
    static_cast<ServerPlayer&>(p).openInventory();
    co_await ctx.ticks(1);
    ctx.require(bp::backpackOpen(p), "opening the inventory opens the backpack screen");
    auto* c = bp::backpackContainer(p);
    ctx.require(c != nullptr, "the helper container exists");
    ctx.check(lp::game::itemId(c->getItem(3)) == "minecraft:dirt", "the screen shows the stored items");
    c->setItem(0, makeItem("minecraft:apple", 7));
    co_await ctx.ticks(2);
    auto worn = bp::backpackItems(p.getArmor(ArmorSlot::Torso));
    ctx.check(lp::game::itemId(worn[0]) == "minecraft:apple" && worn[0].mCount == 7, "changes are written to the item");
    c->setItem(1, makeItem(bp::kBackpack));
    co_await ctx.ticks(2);
    ctx.check(lp::game::isEmpty(c->getItem(1)), "a backpack can't go into a backpack");
    bp::closeBackpack(p);
    co_await ctx.ticks(1);
    ctx.check(!bp::backpackOpen(p), "closing ends the session");
    ctx.check(lp::game::entities::ofType(bp::kStorageEntity).empty(), "the helper entity is removed");
    worn = bp::backpackItems(p.getArmor(ArmorSlot::Torso));
    ctx.check(lp::game::itemId(worn[3]) == "minecraft:dirt" && lp::game::itemId(worn[0]) == "minecraft:apple",
              "the contents stay in the worn backpack");
    p.setArmor(ArmorSlot::Torso, makeItem(bp::kBackpack));
    p.setPlayerGameType(GameType::Creative);
    static_cast<ServerPlayer&>(p).openInventory();
    co_await ctx.ticks(1);
    ctx.check(!bp::backpackOpen(p), "creative players get the normal inventory");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("backpack.lock", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1410);
    auto& p  = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    survival(p);
    clearPlayer(p);
    p.setArmor(ArmorSlot::Torso, fullBackpack());
    co_await ctx.ticks(2);
    auto& pinv = inv::playerInventory(p);
    pinv.setItem(5, p.getArmor(ArmorSlot::Torso)); // "takes it off"
    p.setArmor(ArmorSlot::Torso, ItemStack{});
    co_await ctx.ticks(2);
    ctx.check(bp::backpackHasItems(p.getArmor(ArmorSlot::Torso)), "a backpack with items goes back to the chest slot");
    ctx.check(lp::game::isEmpty(pinv.getItem(5)), "and leaves the inventory slot");

    // A non-empty backpack that is not worn spills (survival).
    clearPlayer(p);
    co_await ctx.ticks(25); // forget the worn one
    clearItemsNear(p, ctx.testPos(1410));
    pinv.setItem(6, fullBackpack("minecraft:cobblestone", 9));
    co_await ctx.ticks(3);
    ctx.check(!bp::backpackHasItems(pinv.getItem(6)), "an unworn backpack is emptied");
    co_await ctx.ticks(5);
    ctx.check(countItemsNear(p, lp::game::globalPos(p.getDimensionBlockSource(), lp::game::blockPosOf(p.getPosition())), "minecraft:cobblestone") >= 9,
              "its contents spill at the player");

    // Creative: kept, with Quark's warning lore.
    p.setPlayerGameType(GameType::Creative);
    pinv.setItem(7, fullBackpack());
    co_await ctx.ticks(2);
    ctx.check(bp::backpackHasItems(pinv.getItem(7)), "creative players keep unworn backpacks");
    ctx.check(pinv.getItem(7).getCustomLore().size() == 2, "with the two warning lines");
    survival(p);
    pinv.setItem(7, ItemStack{});
    clearPlayer(p);
    clearItemsNear(p, ctx.testPos(1410));
});

LP_REGISTER_SELFTEST("backpack.dropped_spills", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(1415);
    auto& p   = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    clearItemsNear(p, pos);
    auto level = ll::service::getLevel();
    auto b     = fullBackpack("minecraft:gold_ingot", 4);
    level->getSpawner().spawnItem(p.getDimensionBlockSource(), b, nullptr, Vec3{pos.x + 2.5f, pos.y + 1.0f, pos.z + 0.5f}, 0);
    co_await ctx.ticks(5);
    ctx.check(countItemsNear(p, pos, "minecraft:gold_ingot", 6) >= 4, "a dropped backpack spills its contents");
    clearItemsNear(p, pos);
});

LP_REGISTER_SELFTEST("backpack.armor_stand", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(1420);
    auto& p   = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    survival(p);
    clearPlayer(p);
    auto* stand = lp::game::entities::spawn(p.getDimensionBlockSource(), "minecraft:armor_stand",
                                            Vec3{pos.x + 1.5f, static_cast<float>(pos.y), pos.z + 0.5f});
    ctx.require(stand != nullptr, "armor stand spawned");
    auto standId = lp::game::entities::idOf(*stand);
    p.setArmor(ArmorSlot::Torso, fullBackpack());
    (void)p.interact(*stand, stand->getPosition());
    co_await ctx.ticks(1);
    stand = lp::game::entities::find(standId);
    ctx.require(stand != nullptr, "armor stand still there");
    ctx.check(bp::backpackHasItems(stand->getArmor(ArmorSlot::Torso)), "the stand wears the backpack with its contents");
    ctx.check(lp::game::isEmpty(p.getArmor(ArmorSlot::Torso)), "the player's chest slot is empty");
    co_await ctx.ticks(3);
    ctx.check(lp::game::isEmpty(p.getArmor(ArmorSlot::Torso)), "and stays empty (no lock)");
    (void)p.interact(*stand, stand->getPosition());
    co_await ctx.ticks(1);
    ctx.check(bp::backpackHasItems(p.getArmor(ArmorSlot::Torso)), "using it again takes the backpack back");
    lp::game::entities::despawn(*stand);
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("backpack.ravager_hide", [](Context& ctx) {
    ctx.check(bp::ravagerHideDrop(0, 0.0) == 1, "base drop is 1");
    ctx.check(bp::ravagerHideDrop(1, 0.4) == 2 && bp::ravagerHideDrop(1, 0.6) == 1, "looting I: 50% for a second hide");
    ctx.check(bp::ravagerHideDrop(3, 0.4) == 3 && bp::ravagerHideDrop(3, 0.6) == 2, "looting III: 2 + 50%");
});

namespace {
ll::coro::CoroTask<Actor*> killWithItems(Context& ctx, Player& p, lp::core::GlobalPos pos) {
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    survival(p);
    clearPlayer(p);
    auto& pinv = inv::playerInventory(p);
    pinv.setItem(0, makeItem("minecraft:stone", 10));
    pinv.setItem(1, makeItem("minecraft:bread", 3));
    pinv.setItem(2, makeItem("minecraft:torch", 16));
    pinv.setItem(3, makeItem("minecraft:arrow", 20));
    pinv.setItem(4, makeItem("minecraft:apple", 2));
    p.setArmor(ArmorSlot::Head, makeItem("minecraft:iron_helmet"));
    p.setOffhandSlot(makeItem("minecraft:shield"));
    if (bp::keepInventoryOn()) {
        // keepInventory worlds (the client e2e fixture) never drop: test the totem itself.
        ctx.info("keepInventory is on: spawning the totem directly");
        bp::spawnTotemNow(p);
    } else {
        p.kill();
    }
    co_await ctx.ticks(2);
    ctx.info("after death: alive=" + std::to_string(p.isAlive()) + " lastTotem=" + std::to_string(bp::lastTotemOf(p))
             + " stone left=" + std::to_string(inv::count(inv::playerInventory(p), [](ItemStack const& s) {
                   return lp::game::itemId(s) == "minecraft:stone"; }, inv::kPlayerAll)));
    co_return lp::game::entities::find(bp::lastTotemOf(p));
}
void respawn(Player& p) { static_cast<SimulatedPlayer&>(p).simulateRespawn(); }
} // namespace

LP_REGISTER_SELFTEST("backpack.totem_spawn", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p     = ctx.spawnPlayer("lp_totem_a");
    auto* totem = co_await killWithItems(ctx, p, ctx.testPos(1430));
    ctx.require(totem != nullptr, "a totem spawned for the dead player");
    ctx.check(lp::game::entities::typeOf(*totem) == bp::kTotem, "it is a lepton:totem");
    ctx.check(bp::totemItems(*totem).size() == 7, "it holds the 7 stacks (inventory, armour, off-hand)");
    ctx.check(totem->getNameTag() == p.getRealName(), "it is named after the player");
    ctx.check(bp::totemOwner(lp::game::entities::idOf(*totem)) == p.getUuid().asString(), "the owner is recorded");
    ctx.check(totem->getPosition().y >= p.getFeetPos().y, "it floats above the death position");
    respawn(p);
    lp::game::entities::despawn(*totem);
});

LP_REGISTER_SELFTEST("backpack.totem_collect", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p     = ctx.spawnPlayer("lp_totem_b");
    auto* totem = co_await killWithItems(ctx, p, ctx.testPos(1435));
    ctx.require(totem != nullptr, "a totem spawned");
    auto id = lp::game::entities::idOf(*totem);
    respawn(p);
    co_await ctx.ticks(2);
    clearPlayer(p);
    int got = bp::collectFromTotem(p, *totem);
    ctx.check(got >= 3 && got <= 6, "one hit gives 3 to 6 stacks (" + std::to_string(got) + ")");
    int guard = 0;
    while (!bp::totemItems(*totem).empty() && guard++ < 5) bp::collectFromTotem(p, *totem);
    ctx.check(bp::totemItems(*totem).empty(), "hits empty the totem");
    ctx.check(lp::game::itemId(p.getArmor(ArmorSlot::Head)) == "minecraft:iron_helmet", "armour is equipped");
    ctx.check(lp::game::itemId(p.getOffhandSlot()) == "minecraft:shield", "the shield goes to the off-hand");
    ctx.check(inv::count(inv::playerInventory(p), [](ItemStack const& s) { return lp::game::itemId(s) == "minecraft:stone"; }, inv::kPlayerAll) == 10,
              "items go back to the inventory");
    co_await ctx.ticks(45);
    ctx.check(lp::game::entities::find(id) == nullptr, "an empty totem fades away after 40 ticks");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("backpack.totem_darksouls", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p     = ctx.spawnPlayer("lp_totem_c");
    auto  pos   = ctx.testPos(1440);
    auto* first = co_await killWithItems(ctx, p, pos);
    ctx.require(first != nullptr, "first totem spawned");
    auto firstId = lp::game::entities::idOf(*first);
    respawn(p);
    co_await ctx.ticks(2);
    auto* second = co_await killWithItems(ctx, p, ctx.testPos(1445));
    ctx.require(second != nullptr, "second totem spawned");
    co_await ctx.ticks(3);
    ctx.check(lp::game::entities::find(firstId) == nullptr, "the first totem shattered");
    ctx.check(lp::game::entities::find(lp::game::entities::idOf(*second)) != nullptr, "the second stays");
    ctx.check(countItemsNear(p, pos, "minecraft:stone", 6) >= 10, "the shattered totem dropped its items");
    respawn(p);
    if (auto* s = lp::game::entities::find(bp::lastTotemOf(p))) lp::game::entities::despawn(*s);
    clearItemsNear(p, pos);
});

LP_REGISTER_SELFTEST("backpack.pouch_contents", [](Context& ctx) {
    bp::PouchContents c;
    ctx.check(bp::pouchCanFit(c, makeItem("minecraft:wheat_seeds")), "empty pouch accepts seeds");
    ctx.check(bp::pouchCanFit(c, makeItem("minecraft:oak_sapling")), "and saplings");
    ctx.check(bp::pouchCanFit(c, makeItem("minecraft:bone_meal")) == bp::cfg().allowFertilizer, "bone meal per allowFertilizer");
    ctx.check(!bp::pouchCanFit(c, makeItem("minecraft:dirt")), "not dirt");
    auto seeds = makeItem("minecraft:wheat_seeds", 64);
    ctx.require(bp::pouchAbsorb(c, seeds), "absorbs seeds");
    ctx.check(c.count == 64 && lp::game::isEmpty(seeds), "all 64 moved");
    ctx.check(!bp::pouchCanFit(c, makeItem("minecraft:beetroot_seeds")), "one type only");
    for (int i = 0; i < 12; ++i) {
        auto more = makeItem("minecraft:wheat_seeds", 64);
        bp::pouchAbsorb(c, more);
    }
    ctx.check(c.count == bp::cfg().maxItems, "capped at maxItems");
    auto out = bp::pouchSplitStack(c);
    ctx.check(out.mCount == 64 && c.count == bp::cfg().maxItems - 64, "takes one stack out");
    auto pouch = makeItem(bp::kSeedPouch);
    bp::setPouchContents(pouch, c);
    auto back = bp::pouchContents(pouch);
    ctx.check(back.count == c.count && !back.empty(), "contents round-trip through the item");
    ctx.check(!pouch.getCustomLore().empty(), "lore shows the contents");
    bp::setPouchContents(pouch, {});
    ctx.check(bp::pouchContents(pouch).empty() && pouch.getCustomLore().empty(), "emptied pouch has no data");
});

namespace {
ItemStack pouchOf(std::string const& id, int count) {
    bp::PouchContents c;
    c.snbt  = lp::game::itemToSnbt(makeItem(id, 1));
    c.count = count;
    auto pouch = makeItem(bp::kSeedPouch);
    bp::setPouchContents(pouch, c);
    return pouch;
}
} // namespace

LP_REGISTER_SELFTEST("backpack.pouch_plant", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(1450);
    auto& p   = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    survival(p);
    clearPlayer(p);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            auto at = pos;
            at.x += dx;
            at.z += dz;
            ctx.setBlock(at, "minecraft:farmland");
            auto up = at;
            up.y += 1;
            ctx.cleanupBlock(up);
        }
    co_await ctx.ticks(1);
    p.setSelectedItem(pouchOf("minecraft:wheat_seeds", 20));
    auto center = lp::game::blockPos(pos);
    int  used   = bp::pouchUseOn(p, center, 1);
    ctx.check(used == 1, "one seed planted (" + std::to_string(used) + ")");
    auto& region = p.getDimensionBlockSource();
    ctx.check(lp::game::blockIdAt(region, BlockPos{center.x, center.y + 1, center.z}) == "minecraft:wheat", "wheat planted");
    static_cast<SimulatedPlayer&>(p).simulateSneaking();
    co_await ctx.ticks(1);
    used = bp::pouchUseOn(p, center, 1);
    ctx.check(used == 8, "sneaking plants the rest of the 3x3 (" + std::to_string(used) + ")");
    ctx.check(bp::pouchContents(p.getSelectedItem()).count == 11, "the pouch lost 9 seeds");
    static_cast<SimulatedPlayer&>(p).simulateStopSneaking();
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("backpack.pouch_pickup", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(1455);
    auto& p   = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    survival(p);
    clearPlayer(p);
    p.setSelectedItem(pouchOf("minecraft:pumpkin_seeds", 5));
    auto level = ll::service::getLevel();
    auto seeds = makeItem("minecraft:pumpkin_seeds", 12);
    level->getSpawner().spawnItem(p.getDimensionBlockSource(), seeds, nullptr, p.getPosition(), 0);
    int count = 5;
    for (int i = 0; i < 60 && count == 5; ++i) {
        co_await ctx.ticks(1);
        count = bp::pouchContents(p.getSelectedItem()).count;
    }
    ctx.check(count == 17, "picked up seeds go into the held pouch (" + std::to_string(count) + ")");
    ctx.check(inv::count(inv::playerInventory(p), [](ItemStack const& s) { return lp::game::itemId(s) == "minecraft:pumpkin_seeds"; }, inv::kPlayerAll) == 0,
              "not into the inventory");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("backpack.pouch_chest", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(1460);
    auto& p   = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    survival(p);
    clearPlayer(p);
    ctx.require(ctx.setBlock(pos, "minecraft:chest"), "chest placed");
    co_await ctx.ticks(1);
    p.setSelectedItem(pouchOf("minecraft:carrot", 100));
    static_cast<SimulatedPlayer&>(p).simulateSneaking();
    co_await ctx.ticks(1);
    bp::pouchUseOn(p, lp::game::blockPos(pos), 1);
    static_cast<SimulatedPlayer&>(p).simulateStopSneaking();
    auto* ba = p.getDimensionBlockSource().getBlockEntity(lp::game::blockPos(pos));
    ctx.require(ba != nullptr, "chest block entity");
    auto* c = static_cast<ChestBlockActor*>(ba)->getContainer();
    ctx.require(c != nullptr, "chest container");
    ctx.check(inv::count(*c, [](ItemStack const& s) { return lp::game::itemId(s) == "minecraft:carrot"; }, inv::all(*c)) == 100,
              "sneak-use on a chest inserts everything");
    ctx.check(bp::pouchContents(p.getSelectedItem()).empty(), "the pouch is empty");

    // A full chest: nothing moves and nothing is planted around it.
    for (int i = 0; i < c->getContainerSize(); ++i) c->setItem(i, makeItem("minecraft:stone", 64));
    auto side = pos;
    side.x += 1;
    ctx.setBlock(side, "minecraft:farmland");
    auto above = side;
    above.y += 1;
    ctx.cleanupBlock(above);
    co_await ctx.ticks(1);
    p.setSelectedItem(pouchOf("minecraft:carrot", 10));
    static_cast<SimulatedPlayer&>(p).simulateSneaking();
    co_await ctx.ticks(1);
    bp::pouchUseOn(p, lp::game::blockPos(pos), 1);
    static_cast<SimulatedPlayer&>(p).simulateStopSneaking();
    ctx.check(bp::pouchContents(p.getSelectedItem()).count == 10, "a full chest takes nothing");
    ctx.check(lp::game::blockIdAt(p.getDimensionBlockSource(), lp::game::blockPos(above)) != "minecraft:carrots",
              "and nothing is planted next to it");
    c->removeAllItems();
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("backpack.net", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(1465)), "test area loaded");
    survival(p);
    clearPlayer(p);
    lp::net::testing::setClientMod(p, true);
    (void)lp::net::testing::takeOutbox(p);
    p.setArmor(ArmorSlot::Torso, fullBackpack());
    ctx.require(bp::openBackpack(p), "backpack opened");
    co_await ctx.ticks(4); // the screen opens once the helper reached the client
    bool told = false;
    for (auto const& [id, payload] : lp::net::testing::takeOutbox(p))
        told = told || (id == bp::BackpackScreenMsg::kId && payload.find("true") != std::string::npos);
    ctx.check(told, "mod clients are told that the backpack screen is open");
    lp::net::testing::receiveFromClient(p, bp::BackpackSwitchMsg{"inventory"});
    co_await ctx.ticks(1);
    ctx.check(!bp::backpackOpen(p), "a switch request closes the backpack screen");
    co_await ctx.ticks(4);
    lp::net::testing::setClientMod(p, false);
    clearPlayer(p);
});
