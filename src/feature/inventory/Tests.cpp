// U15 inventory selftests (dx 1500..1599). docs/spec/inventory.md §8.

#include <algorithm>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"

#include "feature/inventory/Common.h"
#include "feature/inventory/Config.h"
#include "feature/inventory/Messages.h"
#include "feature/inventory/Search.h"
#include "feature/inventory/Server.h"
#include "feature/inventory/Sorting.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
using namespace lp::feature::inventory;
namespace inv = lp::game::inv;
namespace vc  = lp::game::vc;
using lp::game::isEmpty;
using lp::game::itemId;
using lp::game::makeItem;

ItemStack enchanted(std::string_view id, Enchant::Type type, int level) {
    auto                s = makeItem(id);
    EnchantmentInstance e{};
    e.mEnchantType = type;
    e.mLevel       = level;
    EnchantUtils::applyEnchant(s, e, true);
    return s;
}

void clearPlayer(Player& p) {
    inv::restore(p.getInventory(), inv::kPlayerAll, {});
    for (int s = 0; s < 4; ++s) p.setArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(s), ItemStack{});
    p.setOffhandSlot(ItemStack{});
    setCursorItem(p, ItemStack{});
    inv::sync(p);
}

int countId(Container const& c, std::string const& id) {
    return inv::count(c, [&](ItemStack const& s) { return itemId(s) == id; }, inv::all(c));
}

/// Opens a 27-slot virtual container next to the test position (an "accepted" container screen).
ll::coro::CoroTask<bool> openTestContainer(Context& ctx, lp::core::GlobalPos pos, std::vector<ItemStack> items) {
    if (!co_await ctx.ensureLoaded(pos)) co_return false;
    if (!ctx.setBlock(pos, "minecraft:stone")) co_return false;
    vc::takeAll(pos);
    vc::setItems(pos, std::move(items));
    vc::Spec spec;
    spec.size          = 27;
    spec.titleFallback = "U15 probe";
    if (!vc::open(ctx.player(), pos, spec)) co_return false;
    co_await ctx.ticks(3);
    co_return vc::liveContainer(pos) != nullptr;
}

void closeTestContainer(lp::core::GlobalPos pos) {
    vc::closeAll(pos);
    vc::takeAll(pos);
}

} // namespace

LP_REGISTER_SELFTEST("inventory.sort.order", [](Context& ctx) {
    ctx.check(categoryOf(makeItem("minecraft:torch")) == Category::Torch, "torches first");
    ctx.check(categoryOf(makeItem("minecraft:iron_pickaxe")) == Category::Pickaxe, "pickaxes");
    ctx.check(categoryOf(makeItem("minecraft:iron_shovel")) == Category::Shovel, "shovels");
    ctx.check(categoryOf(makeItem("minecraft:iron_axe")) == Category::Axe, "axes");
    ctx.check(categoryOf(makeItem("minecraft:iron_sword")) == Category::Sword, "swords");
    ctx.check(categoryOf(makeItem("minecraft:iron_hoe")) == Category::GenericTool, "hoes");
    ctx.check(categoryOf(makeItem("minecraft:iron_helmet")) == Category::Armor, "armour");
    ctx.check(categoryOf(makeItem("minecraft:elytra")) != Category::Armor, "elytra is not armour");
    ctx.check(categoryOf(makeItem("minecraft:bow")) == Category::Bow, "bows");
    ctx.check(categoryOf(makeItem("minecraft:arrow")) == Category::Arrows, "arrows");
    ctx.check(categoryOf(makeItem("minecraft:potion", 1, 5)) == Category::Potion, "potions");
    ctx.check(categoryOf(makeItem("minecraft:chest_minecart")) == Category::Minecart, "minecarts");
    ctx.check(categoryOf(makeItem("minecraft:detector_rail")) == Category::Rail, "rails");
    ctx.check(categoryOf(makeItem("minecraft:red_dye")) == Category::Dye, "dyes");
    ctx.check(categoryOf(makeItem("minecraft:bread")) == Category::Food, "food");
    ctx.check(categoryOf(makeItem("minecraft:stick")) == Category::Any, "plain items");
    ctx.check(categoryOf(makeItem("minecraft:stone")) == Category::Block, "blocks last");

    auto less = [](ItemStack const& a, ItemStack const& b) { return compareStacks(a, b) < 0; };
    ctx.check(less(makeItem("minecraft:torch"), makeItem("minecraft:wooden_pickaxe")), "torch before tools");
    ctx.check(less(makeItem("minecraft:diamond_pickaxe"), makeItem("minecraft:wooden_pickaxe")), "faster tiers first");
    ctx.check(less(makeItem("minecraft:golden_pickaxe"), makeItem("minecraft:diamond_pickaxe")), "gold is the fastest tier");
    ctx.check(less(makeItem("minecraft:diamond_sword"), makeItem("minecraft:stone_sword")), "stronger swords first");
    ctx.check(less(makeItem("minecraft:iron_helmet"), makeItem("minecraft:diamond_boots")), "helmets before boots");
    ctx.check(less(makeItem("minecraft:cooked_beef"), makeItem("minecraft:bread")), "more nutrition first");
    ctx.check(less(makeItem("minecraft:rail"), makeItem("minecraft:activator_rail")), "rails in list order");
    ctx.check(less(makeItem("minecraft:stone", 64), makeItem("minecraft:stone", 3)), "bigger stacks first");
    ctx.check(less(enchanted("minecraft:iron_pickaxe", Enchant::Type::Efficiency, 3), makeItem("minecraft:iron_pickaxe")),
              "enchanted tools first");
    ctx.check(less(makeItem("minecraft:stick"), makeItem("minecraft:dirt")), "items before blocks");
});

LP_REGISTER_SELFTEST("inventory.sort.player", [](Context& ctx) {
    auto& p = ctx.player();
    clearPlayer(p);
    auto& pinv = p.getInventory();
    pinv.setItem(0, makeItem("minecraft:dirt", 5));
    pinv.setItem(12, makeItem("minecraft:stone", 10));
    pinv.setItem(20, makeItem("minecraft:torch", 3));
    pinv.setItem(25, makeItem("minecraft:stone", 60));
    pinv.setItem(30, makeItem("minecraft:wooden_pickaxe"));
    ctx.require(sortPlayerInventory(p), "sorted");
    ctx.check(itemId(pinv.getItem(0)) == "minecraft:dirt" && pinv.getItem(0).mCount == 5, "the hotbar is untouched");
    ctx.check(itemId(pinv.getItem(9)) == "minecraft:torch", "torch first");
    ctx.check(itemId(pinv.getItem(10)) == "minecraft:wooden_pickaxe", "then tools");
    ctx.check(itemId(pinv.getItem(11)) == "minecraft:stone" && pinv.getItem(11).mCount == 64, "stacks merged");
    ctx.check(pinv.getItem(12).mCount == 6 && isEmpty(pinv.getItem(13)), "rest packed, empty slots last");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("inventory.sort.container", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(1500, 2);
    auto& p   = ctx.player();
    clearPlayer(p);
    ctx.require(co_await openTestContainer(ctx, pos, {makeItem("minecraft:stone", 3), {}, makeItem("minecraft:torch"),
                                                      makeItem("minecraft:stone", 4)}),
                "container open");
    ctx.require(openContainer(p, true) == vc::liveContainer(pos), "the open container is resolved from the screen context");
    lp::net::testing::setClientMod(p, true);
    lp::net::testing::receiveFromClient(p, SortRequest{false});
    auto* live = vc::liveContainer(pos);
    ctx.require(live != nullptr, "still open");
    ctx.check(itemId(live->getItem(0)) == "minecraft:torch", "the container message sorts the container");
    ctx.check(live->getItem(1).mCount == 7 && isEmpty(live->getItem(2)), "merged and packed");
    lp::net::testing::setClientMod(p, false);
    closeTestContainer(pos);
});

LP_REGISTER_SELFTEST("inventory.transfer", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto  pos = ctx.testPos(1505, 2);
    auto& p   = ctx.player();
    clearPlayer(p);
    auto& pinv = p.getInventory();
    pinv.setItem(0, makeItem("minecraft:apple", 5)); // hotbar: never inserted
    pinv.setItem(10, makeItem("minecraft:stone", 20));
    pinv.setItem(11, makeItem("minecraft:dirt", 7));
    ctx.require(co_await openTestContainer(ctx, pos, {makeItem("minecraft:stone", 1)}), "container open");
    ctx.check(handleTransfer(p, false, true) == 20, "filtered insert moves only items the container has");
    auto* live = vc::liveContainer(pos);
    ctx.require(live != nullptr, "still open");
    ctx.check(countId(*live, "minecraft:stone") == 21 && countId(pinv, "minecraft:dirt") == 7, "stone in, dirt kept");
    ctx.check(handleTransfer(p, false, false) == 7, "insert moves the rest of the main inventory");
    ctx.check(countId(pinv, "minecraft:apple") == 5, "the hotbar stays");
    ctx.check(handleTransfer(p, true, true) == 0, "filtered extract: the player has none of these items");
    ctx.check(handleTransfer(p, true, false) == 28, "extract moves everything back");
    ctx.check(countId(*live, "minecraft:stone") == 0 && countId(pinv, "minecraft:stone") == 21, "container emptied");
    pinv.setItem(12, makeItem("minecraft:diamond", 2));
    ctx.check(quickMove(p, "inventory_items", 3) == 2, "shift-lock quick move from the inventory into the container");
    ctx.check(countId(*live, "minecraft:diamond") == 2, "diamonds moved");
    ctx.check(quickMove(p, "container_items", 0) == 2, "and back");
    // A shulker box target never takes shulker boxes (vanilla forbids nesting them).
    inv::restore(pinv, inv::kPlayerAll, {});
    pinv.setItem(13, makeItem("minecraft:red_shulker_box"));
    pinv.setItem(14, makeItem("minecraft:apple", 3));
    ctx.check(transferItems(p, *live, false, false, true) == 3 && isShulkerBox(pinv.getItem(13)),
              "no shulker box goes into a shulker box");
    closeTestContainer(pos);
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("inventory.quickmove", [](Context& ctx) {
    auto& p = ctx.player();
    clearPlayer(p);
    auto& pinv = p.getInventory();
    pinv.setItem(2, makeItem("minecraft:iron_ingot", 9));
    ctx.check(quickMove(p, "hotbar_items", 2) == 9 && countId(pinv, "minecraft:iron_ingot") == 9 && isEmpty(pinv.getItem(2)),
              "hotbar -> main inventory");
    ctx.check(quickMove(p, "inventory_items", 0) == 9 && !isEmpty(pinv.getItem(0)), "main inventory -> hotbar");
    ctx.check(quickMove(p, "bogus_items", 0) == 0, "unknown collections are refused");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("inventory.hotbar", [](Context& ctx) {
    auto& p = ctx.player();
    clearPlayer(p);
    auto& pinv = p.getInventory();
    pinv.setItem(0, makeItem("minecraft:dirt"));
    pinv.setItem(18, makeItem("minecraft:stone"));
    ctx.check(changeHotbar(p, 2), "row 2");
    ctx.check(itemId(pinv.getItem(0)) == "minecraft:stone" && itemId(pinv.getItem(18)) == "minecraft:dirt", "slot 0 <-> 18");
    lp::net::testing::setClientMod(p, true);
    lp::net::testing::receiveFromClient(p, ChangeHotbarRequest{2});
    ctx.check(itemId(pinv.getItem(0)) == "minecraft:dirt", "the message swaps back");
    lp::net::testing::setClientMod(p, false);
    ctx.check(!changeHotbar(p, 4) && !changeHotbar(p, 0), "only rows 1..3");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("inventory.restock", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& p = ctx.player();
    clearPlayer(p);
    auto& pinv = p.getInventory();
    auto  sel  = p.getSelectedItemSlot();
    // Same item.
    pinv.setItem(15, makeItem("minecraft:stone_pickaxe"));
    ctx.check(restockTool(p, sel, makeItem("minecraft:stone_pickaxe")) && itemId(pinv.getItem(sel)) == "minecraft:stone_pickaxe"
                  && isEmpty(pinv.getItem(15)),
              "same item restocked");
    // Loose matching: another pickaxe.
    pinv.setItem(sel, ItemStack{});
    pinv.setItem(20, makeItem("minecraft:diamond_pickaxe"));
    ctx.check(restockTool(p, sel, makeItem("minecraft:iron_pickaxe")) && itemId(pinv.getItem(sel)) == "minecraft:diamond_pickaxe",
              "loose matching by tool class");
    // Important enchantments win.
    pinv.setItem(sel, ItemStack{});
    pinv.setItem(10, makeItem("minecraft:iron_pickaxe"));
    pinv.setItem(11, enchanted("minecraft:iron_pickaxe", Enchant::Type::SilkTouch, 1));
    ctx.check(restockTool(p, sel, enchanted("minecraft:iron_pickaxe", Enchant::Type::SilkTouch, 1))
                  && lp::feature::inventory::enchantPower(pinv.getItem(sel)) == 1,
              "the silk touch pickaxe is preferred");
    // Hotbar is not searched by default; armour never restocks.
    clearPlayer(p);
    pinv.setItem((sel + 1) % 9, makeItem("minecraft:stone_pickaxe"));
    ctx.check(!restockTool(p, sel, makeItem("minecraft:stone_pickaxe")), "checkHotbar off");
    pinv.setItem(14, makeItem("minecraft:iron_helmet"));
    ctx.check(!restockTool(p, sel, makeItem("minecraft:iron_helmet")), "armour is never restocked");
    // Blocks: the last block placed is replaced (not unstackablesOnly).
    pinv.setItem(16, makeItem("minecraft:cobblestone", 30));
    ctx.check(restockTool(p, sel, makeItem("minecraft:cobblestone")) && pinv.getItem(sel).mCount == 30, "blocks restock");
    // Through the tool break hook.
    clearPlayer(p);
    pinv.setItem(17, makeItem("minecraft:wooden_shovel"));
    auto shovel = makeItem("minecraft:wooden_shovel");
    shovel.setDamageValue(58);
    pinv.setItem(sel, shovel);
    inv::sync(p);
    auto& held = const_cast<ItemStack&>(pinv.getItem(sel));
    held.hurtAndBreak(10, &p);
    if (!isEmpty(held)) pinv.setItem(sel, ItemStack{}); // the game clears broken stacks after the hook
    co_await ctx.ticks(3);
    ctx.check(itemId(pinv.getItem(sel)) == "minecraft:wooden_shovel" && isEmpty(pinv.getItem(17)),
              "a broken tool is replaced at the end of the tick");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("inventory.armorswap", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1510, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = static_cast<SimulatedPlayer&>(ctx.player());
    clearPlayer(p);
    auto& region = p.getDimensionBlockSource();
    auto* stand  = lp::game::entities::spawn(region, "minecraft:armor_stand", Vec3{pos.x + 0.5f, pos.y + 1.f, pos.z + 0.5f});
    ctx.require(stand != nullptr, "armour stand spawned");
    auto id = lp::game::entities::idOf(*stand);
    using AS = SharedTypes::Legacy::ArmorSlot;
    stand->setArmor(AS::Head, makeItem("minecraft:diamond_helmet"));
    p.setArmor(AS::Feet, makeItem("minecraft:iron_boots"));
    p.setArmor(AS::Legs, enchanted("minecraft:iron_leggings", Enchant::Type::CurseOfBinding, 1));
    p.setOffhandSlot(makeItem("minecraft:shield"));
    p.simulateSneaking();
    auto result = p.interact(*stand, stand->getPosition());
    ctx.check(result.mSuccess, "sneak-using the stand is handled");
    ctx.check(itemId(p.getArmor(AS::Head)) == "minecraft:diamond_helmet", "the stand's helmet goes to the player");
    ctx.check(itemId(stand->getArmor(AS::Feet)) == "minecraft:iron_boots", "the player's boots go to the stand");
    ctx.check(itemId(p.getArmor(AS::Legs)) == "minecraft:iron_leggings", "curse of binding stays on the player");
    ctx.check(itemId(stand->getCarriedItem()) == "minecraft:shield", "the off hand swaps with the stand's hand");
    p.simulateStopSneaking();
    if (auto* s = lp::game::entities::find(id)) lp::game::entities::despawn(*s);
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("inventory.search", [](Context& ctx) {
    auto sword = makeItem("minecraft:diamond_sword");
    auto name  = normalizeSearch(lp::feature::inventory::displayName(sword));
    ctx.info("diamond sword name: " + name);
    ctx.check(namesMatch(sword, ""), "empty search matches everything");
    ctx.check(!namesMatch(ItemStack{}, "a"), "empty slots never match");
    ctx.check(namesMatch(sword, "SWORD"), "case-insensitive contains");
    ctx.check(namesMatch(sword, "\"" + name + "\""), "quoted: exact name");
    ctx.check(!namesMatch(sword, "\"sword\""), "quoted: not a substring");
    ctx.check(namesMatch(sword, "/dia.*d/"), "regex");
    ctx.check(!namesMatch(sword, "/[/"), "a bad regex matches nothing");
    ctx.check(namesMatch(sword, "minecraft"), "mod name");
    ctx.check(!namesMatch(sword, "zzz"), "no match");
    auto book = enchanted("minecraft:enchanted_book", Enchant::Type::Sharpness, 5);
    auto shown = normalizeSearch(enchantDisplay("minecraft:sharpness", 5));
    ctx.info("enchantment shown as: " + shown);
    ctx.check(!shown.empty() && namesMatch(book, shown), "enchantment names match");
    auto box = withShulkerContents(makeItem("minecraft:red_shulker_box"), {makeItem("minecraft:emerald", 3)});
    ctx.check(namesMatch(box, "emerald"), "shulker boxes match their contents");
});

LP_REGISTER_SELFTEST("inventory.shulker", [](Context& ctx) {
    auto box = makeItem("minecraft:blue_shulker_box");
    ctx.require(isShulkerBox(box), "shulker box detected");
    ctx.check(!isShulkerBox(makeItem("minecraft:chest")), "chests are not shulker boxes");
    auto stone = makeItem("minecraft:stone", 40);
    ctx.check(insertIntoShulker(box, stone) == 40 && isEmpty(stone), "insert a stack");
    auto contents = shulkerContents(box);
    ctx.check(itemId(contents[0]) == "minecraft:stone" && contents[0].mCount == 40, "contents read back");
    auto other = withShulkerContents(makeItem("minecraft:red_shulker_box"), {makeItem("minecraft:stone", 30), makeItem("minecraft:apple", 2)});
    ctx.check(dumpShulker(box, other) == 32, "dump one box into another");
    ctx.check(lp::game::isEmpty(shulkerContents(other)[0]), "the dumped box is empty");
    auto inner = makeItem("minecraft:red_shulker_box");
    ctx.check(insertIntoShulker(box, inner) == 0, "boxes don't go into boxes");

    auto& p = ctx.player();
    clearPlayer(p);
    auto& pinv = p.getInventory();
    // Lava bucket trashes the cursor stack.
    pinv.setItem(9, makeItem("minecraft:lava_bucket"));
    setCursorItem(p, makeItem("minecraft:rotten_flesh", 10));
    ctx.check(slotInteract(p, "inventory_items", 0) == "trash" && isEmpty(cursorItem(p)), "lava bucket trashes the stack");
    // Insert the cursor stack into a shulker box slot.
    pinv.setItem(10, makeItem("minecraft:white_shulker_box"));
    setCursorItem(p, makeItem("minecraft:gold_ingot", 12));
    ctx.check(slotInteract(p, "inventory_items", 1) == "insert", "insert into the box");
    ctx.check(isEmpty(cursorItem(p)) && shulkerContents(pinv.getItem(10))[0].mCount == 12, "the box holds the stack");
    // Right-click armour with an empty cursor equips it.
    pinv.setItem(11, makeItem("minecraft:golden_helmet"));
    ctx.check(slotInteract(p, "inventory_items", 2) == "equip", "equip armour");
    ctx.check(itemId(p.getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == "minecraft:golden_helmet" && isEmpty(pinv.getItem(11)),
              "the helmet is worn");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("inventory.heldshulker", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1515, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    clearPlayer(p);
    auto& pinv = p.getInventory();
    pinv.setItem(12, withShulkerContents(makeItem("minecraft:lime_shulker_box"), {makeItem("minecraft:diamond", 4)}));
    ctx.require(openHeldShulker(p, 12), "opened");
    ctx.check(isEmpty(pinv.getItem(12)), "the box is held by the server while open");
    auto at = heldShulkerPos(p);
    ctx.require(at.has_value(), "session position");
    for (int i = 0; i < 20 && vc::viewers(*at) == 0; ++i) co_await ctx.ticks(1);
    ctx.require(vc::viewers(*at) == 1, "the chest screen is open");
    auto* live = vc::liveContainer(*at);
    ctx.require(live != nullptr, "live container");
    ctx.check(live->getItem(0).mCount == 4, "the screen shows the box contents");
    live->setItem(1, makeItem("minecraft:emerald", 5));
    co_await ctx.ticks(2);
    vc::closeAll(*at);
    for (int i = 0; i < 10 && heldShulkerPos(p); ++i) co_await ctx.ticks(1);
    ctx.check(!heldShulkerPos(p), "the session ended");
    auto back = pinv.getItem(12);
    ctx.require(isShulkerBox(back), "the box is back in its slot");
    auto contents = shulkerContents(back);
    ctx.check(contents[0].mCount == 4 && itemId(contents[1]) == "minecraft:emerald", "edits were written into the box");
    // While held, the box's screen counts as a shulker box: Insert never puts boxes into it.
    pinv.setItem(13, makeItem("minecraft:red_shulker_box"));
    ctx.require(openHeldShulker(p, 12), "opened again");
    at = heldShulkerPos(p);
    for (int i = 0; i < 20 && vc::viewers(*at) == 0; ++i) co_await ctx.ticks(1);
    if (auto* again = vc::liveContainer(*at)) {
        transferItems(p, *again, false, false);
        ctx.check(isShulkerBox(pinv.getItem(13)), "a held box rejects boxes from Insert");
    } else {
        ctx.fail("the held box did not open again");
    }
    vc::closeAll(*at);
    for (int i = 0; i < 10 && heldShulkerPos(p); ++i) co_await ctx.ticks(1);
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("inventory.share", [](Context& ctx) {
    auto& p = ctx.player();
    resetShareCooldowns();
    lp::net::testing::setClientMod(p, true);
    (void)lp::net::testing::takeOutbox(p);
    auto item = enchanted("minecraft:diamond_sword", Enchant::Type::Sharpness, 2);
    auto text = shareText(p, item);
    ctx.info("shared line: " + text);
    ctx.check(text.find(p.getRealName()) != std::string::npos && text.find('[') != std::string::npos, "the line names sender and item");
    ctx.check(shareItem(p, item), "shared");
    ctx.check(!shareItem(p, item), "cooldown");
    auto out   = lp::net::testing::takeOutbox(p);
    bool found = std::any_of(out.begin(), out.end(), [](auto const& m) { return m.first == SharedItemNotice::kId; });
    ctx.check(found, "mod clients get the item notice");
    lp::net::testing::setClientMod(p, false);
    resetShareCooldowns();
});
