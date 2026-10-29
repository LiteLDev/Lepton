// U16 hud: config registration and the selftests of the shared logic (both builds).

#include "feature/hud/HudConfig.h"

#include <algorithm>
#include <vector>

#include "ll/api/command/CommandHandle.h"

#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/hud/Logic.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

LP_REGISTER_CONFIG("hud", lp::feature::hud::HudConfig);

namespace lp::feature::hud {

HudConfig const& cfg() { return config::get<HudConfig>(); }

namespace {
using selftest::Context;
using ArmorSlot = SharedTypes::Legacy::ArmorSlot;

/// Empties the test player's inventory, armour and off hand.
void clearPlayer(Player& p) {
    auto& inv = p.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i) inv.setItem(i, ItemStack{});
    for (auto s : {ArmorSlot::Head, ArmorSlot::Torso, ArmorSlot::Legs, ArmorSlot::Feet}) p.setArmor(s, ItemStack{});
    p.setOffhandSlot(ItemStack{});
    p.setSelectedSlot(0);
}
} // namespace

/// The tooltip test kit of `/quark debug hud` (hotbar 0..8).
std::vector<ItemStack> debugKit() {
    return {
        game::makeItem("minecraft:diamond_sword"),
        game::makeItem("minecraft:diamond_helmet"),
        game::makeItem("minecraft:cooked_beef", 16),
        game::itemFromSnbt(
            R"({Name:"minecraft:red_shulker_box",Count:1b,Damage:0s,tag:{Items:[{Name:"minecraft:diamond",Count:12b,Damage:0s,Slot:0b},{Name:"minecraft:golden_apple",Count:3b,Damage:0s,Slot:1b},{Name:"minecraft:oak_log",Count:64b,Damage:0s,Slot:2b},{Name:"minecraft:iron_pickaxe",Count:1b,Damage:40s,Slot:3b},{Name:"minecraft:redstone",Count:30b,Damage:0s,Slot:4b},{Name:"minecraft:torch",Count:48b,Damage:0s,Slot:5b},{Name:"minecraft:bread",Count:9b,Damage:0s,Slot:6b},{Name:"minecraft:emerald",Count:5b,Damage:0s,Slot:7b},{Name:"minecraft:bone",Count:2b,Damage:0s,Slot:8b},{Name:"minecraft:arrow",Count:20b,Damage:0s,Slot:9b}]}})"
        ),
        game::itemFromSnbt(
            R"({Name:"minecraft:enchanted_book",Count:1b,Damage:0s,tag:{ench:[{id:9s,lvl:5s},{id:26s,lvl:1s}]}})"
        ),
        game::makeItem("minecraft:empty_map"),
        game::makeItem("minecraft:rotten_flesh", 4),
        game::makeItem("minecraft:coal", 20),
        game::makeItem("minecraft:cobblestone", 64),
    };
}

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("debug").text("hud").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = commands::playerOf(origin);
        if (!p || !commands::isAdmin(origin)) return output.error("Players with operator permission only.");
        auto  kit = debugKit();
        auto& inv = p->getInventory();
        for (int i = 0; i < static_cast<int>(kit.size()); ++i) inv.setItem(i, kit[static_cast<size_t>(i)]);
        game::inv::sync(*p);
        log().info("hud: debug kit given to {}", p->getRealName());
        output.success("Lepton hud test kit given.");
    });
});

LP_REGISTER_SELFTEST("hud.config", [](Context& ctx) {
    auto const& c = cfg();
    ctx.check(c.version >= 1, "config/hud.json is loaded");
    ctx.check(
        c.foodDivisor == 2 && c.foodCompressionThreshold == 4 && c.fuelTimeDivisor == 200 && c.hudHeight == 10,
        "Quark's numeric defaults"
    );
    ctx.check(
        std::find(c.enchantingStacks.begin(), c.enchantingStacks.end(), "minecraft:diamond_sword")
            != c.enchantingStacks.end(),
        "Quark's enchantingStacks default list"
    );
});

LP_REGISTER_SELFTEST("hud.ticker_anim", [](Context& ctx) {
    TickerAnim a;
    a.tick(false, true);
    ctx.check(a.liveTicks == 60, "a change starts the animation at 60 ticks");
    ctx.check(a.offset(0) == 0.f, "starts below the screen");
    for (int i = 0; i < 5; ++i) a.tick(false, false);
    ctx.check(a.liveTicks == 55 && a.offset(0) == 20.f, "fully up after 5 ticks (20 px)");
    a.tick(false, true);
    ctx.check(a.liveTicks == 55, "a change while visible keeps it up (55)");
    for (int i = 0; i < 52; ++i) a.tick(false, false);
    ctx.check(a.liveTicks == 3, "animating out");
    a.tick(false, true);
    ctx.check(a.liveTicks == 57, "a change while animating out reverses it (60 - 3)");
    a.tick(true, false);
    ctx.check(a.liveTicks == 0 && !a.visible(), "nothing to show hides it at once");
    TickerAnim b;
    b.liveTicks = 2;
    float mid   = b.offset(0.5f);
    ctx.check(mid > 0 && mid < 20, "eased offset between 0 and 20 while animating");
});

LP_REGISTER_SELFTEST("hud.ticker_layout", [](Context& ctx) {
    float w = 400;
    // Armour on the right: feet (index 3) closest to... Quark: x = w/2 + 95 + (4 - idx - 1) * 20.
    ctx.check(tickerX(w, TickerSlot::Head, false, true, 0, 0) == 295, "head at w/2 + 95");
    ctx.check(tickerX(w, TickerSlot::Feet, false, true, 0, 0) == 355, "feet at w/2 + 155");
    // Hands on the left: x = w/2 - 95 + idx*20 - 40.
    ctx.check(tickerX(w, TickerSlot::MainHand, false, true, 0, 0) == 85, "main hand at w/2 - 115");
    ctx.check(tickerX(w, TickerSlot::OffHand, false, true, 0, 0) == 65, "off hand at w/2 - 135");
    ctx.check(tickerX(w, TickerSlot::MainHand, false, false, 0, 0) == 56, "an off-hand item widens the left bar by 58");
    ctx.check(tickerX(w, TickerSlot::MainHand, false, true, 7, 0) == 92, "shiftLeft");
    ctx.check(tickerX(w, TickerSlot::Head, true, true, 0, -3) == 25, "invert puts armour left");
    ctx.check(
        tickerX(w, TickerSlot::MainHand, true, true, 0, 4) == 200 + 95 + 20 + 4,
        "invert puts hands right (+shiftRight)"
    );
});

LP_REGISTER_SELFTEST("hud.ticker_count", [](Context& ctx) {
    auto& p = ctx.player();
    clearPlayer(p);
    ctx.give(p, game::makeItem("minecraft:cobblestone", 10), 0);
    ctx.give(p, game::makeItem("minecraft:cobblestone", 64), 5);
    ctx.give(p, game::makeItem("minecraft:cobblestone", 3), 20);
    p.setOffhandSlot(game::makeItem("minecraft:cobblestone", 2));
    auto t = tickerStack(p, TickerSlot::MainHand);
    ctx.check(
        game::itemId(t.stack) == "minecraft:cobblestone" && t.count == 79,
        "main hand total counts inventory + off hand"
    );
    clearPlayer(p);
    ctx.give(p, game::makeItem("minecraft:cobblestone", 10), 0);
    ctx.check(
        game::isEmpty(tickerStack(p, TickerSlot::MainHand).stack),
        "nothing when the slot already shows the total"
    );
    clearPlayer(p);
    ctx.give(p, game::makeItem("minecraft:diamond_sword"), 0);
    ctx.check(game::isEmpty(tickerStack(p, TickerSlot::MainHand).stack), "non-stackable hand items show nothing");
    p.setArmor(ArmorSlot::Head, game::makeItem("minecraft:iron_helmet"));
    auto h = tickerStack(p, TickerSlot::Head);
    ctx.check(game::itemId(h.stack) == "minecraft:iron_helmet" && h.count == 1, "armour always shows");
    auto now  = game::makeItem("minecraft:iron_helmet");
    auto then = game::makeItem("minecraft:iron_helmet");
    now.setDamageValue(5);
    ctx.check(tickerChanged(now, then, 1, 1), "damage changes trigger the ticker");
    ctx.check(!tickerChanged(then, game::makeItem("minecraft:iron_helmet"), 1, 1), "no change, no trigger");
    ctx.check(tickerChanged(then, then, 2, 1), "count changes trigger the ticker");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("hud.ticker_bow", [](Context& ctx) {
    auto& p = ctx.player();
    clearPlayer(p);
    ctx.give(p, game::makeItem("minecraft:bow"), 0);
    ctx.check(game::isEmpty(tickerStack(p, TickerSlot::MainHand).stack), "a bow without arrows shows nothing");
    ctx.give(p, game::makeItem("minecraft:arrow", 20), 9);
    ctx.give(p, game::makeItem("minecraft:arrow", 5), 30);
    auto t = tickerStack(p, TickerSlot::MainHand);
    ctx.check(game::itemId(t.stack) == "minecraft:arrow" && t.count == 25, "a bow shows its arrows and their total");
    auto inf = game::itemFromSnbt(R"({Name:"minecraft:bow",Count:1b,Damage:0s,tag:{ench:[{id:22s,lvl:1s}]}})");
    ctx.require(!game::isEmpty(inf), "infinity bow created");
    p.getInventory().setItem(0, inf);
    ctx.check(!projectileFor(p, inf).has_value(), "Infinity bows are not projectile weapons");
    ctx.check(game::isEmpty(tickerStack(p, TickerSlot::MainHand).stack), "an Infinity bow shows nothing");
    clearPlayer(p);
});

LP_REGISTER_SELFTEST("hud.elytra", [](Context& ctx) {
    auto e = game::makeItem("minecraft:elytra");
    ctx.check(canElytraFly(e), "a new elytra can fly");
    e.setDamageValue(431);
    ctx.check(!canElytraFly(e), "a broken elytra cannot");
    ctx.check(!canElytraFly(game::makeItem("minecraft:diamond_chestplate")), "a chestplate is no elytra");
});

LP_REGISTER_SELFTEST("hud.food", [](Context& ctx) {
    ctx.check(
        saturationTier(19.2f) == 5 && saturationTier(12) == 0 && saturationTier(9) == 1 && saturationTier(7) == 2
            && saturationTier(3) == 3 && saturationTier(1) == 4,
        "saturation tiers"
    );
    auto steak = foodInfo(game::makeItem("minecraft:cooked_beef"));
    ctx.require(steak.has_value(), "steak is food");
    ctx.check(steak->nutrition == 8 && !steak->poison, "steak: 8 nutrition, not poisonous");
    ctx.check(saturationTier(steak->saturation) == 0, "steak: Fulfilling Meal (12.8 saturation)");
    auto flesh = foodInfo(game::makeItem("minecraft:rotten_flesh"));
    ctx.check(flesh && flesh->poison, "rotten flesh is poisonous");
    ctx.check(!foodInfo(game::makeItem("minecraft:stone")).has_value(), "stone is no food");
    auto r = foodRow(8, 2, 4);
    ctx.check(r.shanks == 4 && !r.halfLast && r.text.empty() && r.width == 36, "8 nutrition: 4 shanks");
    auto bread = foodRow(5, 2, 4);
    ctx.check(bread.shanks == 3 && bread.halfLast, "5 nutrition: 2.5 shanks");
    auto big = foodRow(11, 2, 4);
    ctx.check(big.shanks == 1 && big.text == "x5.5", "compressed above the threshold");
    auto stew = foodRow(10, 2, 4);
    ctx.check(stew.shanks == 1 && stew.text == "x5", "compressed even count");
});

LP_REGISTER_SELFTEST("hud.fuel", [](Context& ctx) {
    int coal = fuelTicks(game::makeItem("minecraft:coal"));
    ctx.check(coal == 1600, "coal burns 1600 ticks (got " + std::to_string(coal) + ")");
    ctx.check(fuelText(coal, 200) == "x8", "coal smelts x8");
    ctx.check(fuelText(300, 200) == "x1.5", "planks smelt x1.5");
    ctx.check(fuelTicks(game::makeItem("minecraft:stone")) == 0, "stone is no fuel");
});

LP_REGISTER_SELFTEST("hud.shulker", [](Context& ctx) {
    ctx.check(
        shulkerDims(27) == std::pair{9, 3} && shulkerDims(1) == std::pair{1, 1} && shulkerDims(4) == std::pair{4, 1}
            && shulkerDims(12) == std::pair{9, 2} && shulkerDims(108) == std::pair{12, 9},
        "grid dimensions"
    );
    auto box = game::itemFromSnbt(
        R"({Name:"minecraft:red_shulker_box",Count:1b,Damage:0s,tag:{Items:[{Name:"minecraft:diamond",Count:5b,Damage:0s,Slot:3b},{Name:"minecraft:stone",Count:64b,Damage:0s,Slot:1b}]}})"
    );
    ctx.require(!game::isEmpty(box), "shulker box created");
    ctx.check(isShulkerBox(box), "is a shulker box");
    auto items = shulkerContents(box);
    ctx.check(
        items.size() == 2 && game::itemId(items[0]) == "minecraft:stone" && items[1].mCount == 5,
        "contents in slot order"
    );
    ctx.check(shulkerColor(box) == 0xB02E26, "red tint");
    ctx.check(!shulkerColor(game::makeItem("minecraft:undyed_shulker_box")).has_value(), "the undyed box has no tint");
    ctx.check(shulkerContents(game::makeItem("minecraft:undyed_shulker_box")).empty(), "an empty box has no contents");
});

LP_REGISTER_SELFTEST("hud.enchanting", [](Context& ctx) {
    auto book =
        game::itemFromSnbt(R"({Name:"minecraft:enchanted_book",Count:1b,Damage:0s,tag:{ench:[{id:9s,lvl:5s}]}})");
    ctx.require(!game::isEmpty(book), "book created");
    auto e = bookEnchants(book);
    ctx.require(e.size() == 1 && e[0].type == 9 && e[0].level == 5, "the book holds Sharpness V");
    ctx.info("enchant line: " + e[0].line);
    auto items = enchantItems(9);
    auto has   = [&](std::string const& id) { return std::find(items.begin(), items.end(), id) != items.end(); };
    ctx.check(has("minecraft:diamond_sword"), "sharpness goes on swords");
    ctx.check(
        !has("minecraft:diamond_axe") && !has("minecraft:diamond_helmet") && !has("minecraft:bow"),
        "not on axes (secondary), helmets or bows"
    );
    auto prot = enchantItems(0);
    ctx.check(
        std::find(prot.begin(), prot.end(), "minecraft:diamond_chestplate") != prot.end(),
        "protection goes on armour"
    );
});

LP_REGISTER_SELFTEST("hud.attributes", [](Context& ctx) {
    auto sword = attributesOf(game::makeItem("minecraft:diamond_sword"));
    ctx.require(sword.size() == 1 && sword[0].id == "attack_damage", "a sword has attack damage");
    ctx.info("diamond sword: " + formatAttribute(sword[0]).first);
    ctx.check(formatAttribute(sword[0]).first == formatNumber(sword[0].amount + 1), "shown as base 1 + bonus");
    auto chest = attributesOf(game::makeItem("minecraft:diamond_chestplate"));
    bool armor = false, tough = false;
    for (auto const& a : chest) {
        armor = armor || (a.id == "armor" && a.amount == 8 && a.group == SlotGroup::Chest);
        tough = tough || (a.id == "armor_toughness" && a.amount == 2);
    }
    ctx.check(armor && tough, "diamond chestplate: 8 armour, 2 toughness in the chest group");
    auto neth = attributesOf(game::makeItem("minecraft:netherite_boots"));
    ctx.check(
        std::any_of(neth.begin(), neth.end(), [](auto const& a) { return a.id == "knockback_resistance"; }),
        "netherite: knockback resistance"
    );
    ctx.check(formatNumber(1.6) == "1.6" && formatNumber(7) == "7" && formatNumber(0.125) == "0.12", "#.## format");
    ctx.check(
        equipmentSlotFor(game::makeItem("minecraft:iron_boots")) == TickerSlot::Feet,
        "boots compare with the feet"
    );
    ctx.check(attributesOf(game::makeItem("minecraft:stone")).empty(), "stone has no attributes");
});

} // namespace lp::feature::hud
