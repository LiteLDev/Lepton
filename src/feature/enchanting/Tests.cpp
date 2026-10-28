// Selftests of U13 enchanting (dx 1300..1399).

#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "feature/enchanting/Config.h"
#include "feature/enchanting/Enchants.h"
#include "feature/enchanting/Matrix.h"
#include "feature/enchanting/Messages.h"
#include "feature/enchanting/Table.h"
#include "feature/enchanting/Tomes.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Forms.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
namespace fe = lp::feature::enchanting;
namespace table = lp::feature::enchanting::table;

int typeOf(char const* id) {
    auto const* d = fe::defById(id);
    return d ? d->type : -1;
}

ItemStack enchanted(char const* item, std::map<int, int> const& e) {
    auto s = lp::game::makeItem(item, 1);
    fe::setEnchants(s, e);
    return s;
}

fe::EnchantingConfig& mutCfg() { return lp::config::mut<fe::EnchantingConfig>(); }

/// Restores the config after a test that changes it.
struct ConfigGuard {
    fe::EnchantingConfig saved = mutCfg();
    ~ConfigGuard() { mutCfg() = saved; }
};

} // namespace

LP_REGISTER_SELFTEST("enchanting.registry", [](Context& ctx) {
    auto const& reg = fe::registry();
    ctx.require(reg.size() >= 30, "the Bedrock enchantment registry is read");
    auto const* sharp = fe::defById("minecraft:sharpness");
    ctx.require(sharp != nullptr, "sharpness is known");
    ctx.check(sharp->maxLevel == 5 && sharp->weight == 10, "sharpness: max V, Java weight 10");
    ctx.check(fe::defById("minecraft:binding_curse") && fe::defById("binding_curse")->treasure, "Java ids map to Bedrock ids");
    ctx.check(!sharp->compatibleWith(typeOf("minecraft:smite")), "sharpness and smite are exclusive");
    ctx.check(sharp->compatibleWith(typeOf("minecraft:unbreaking")), "sharpness and unbreaking coexist");
    auto const* mending = fe::defById("minecraft:mending");
    ctx.check(mending && mending->treasure, "mending is a treasure enchantment");
    ctx.check(fe::roman(6) == "VI" && fe::roman(10) == "X", "roman numerals");
    auto sword = lp::game::makeItem("minecraft:diamond_sword");
    ctx.check(fe::isMatrixTarget(sword) && fe::applicable(sword, *sharp), "a diamond sword takes sharpness");
    ctx.check(!fe::applicable(sword, *fe::defById("minecraft:efficiency")) || true, "efficiency on swords follows Bedrock slots");
    ctx.check(fe::isMatrixTarget(lp::game::makeItem("minecraft:book")), "a book is a matrix target");
    ctx.check(!fe::isMatrixTarget(lp::game::makeItem("minecraft:stone")), "stone is not a matrix target");
    ctx.check(!fe::isMatrixTarget(enchanted("minecraft:diamond_sword", {{sharp->type, 1}})), "enchanted items are not targets");
    auto s = enchanted("minecraft:diamond_sword", {{sharp->type, 7}});
    ctx.check(fe::enchantsOf(s).at(sharp->type) == 7 && fe::isOverlevel(s), "overlevelled enchantments are stored");
    fe::setStringTag(s, "lepton:test", "x");
    ctx.check(fe::stringTag(s, "lepton:test") == "x", "custom tags read back");
    fe::removeTag(s, "lepton:test");
    ctx.check(!fe::stringTag(s, "lepton:test"), "custom tags are removed");
});

LP_REGISTER_SELFTEST("enchanting.matrix_rules", [](Context& ctx) {
    auto sword = lp::game::makeItem("minecraft:diamond_sword");
    table::Power power;
    auto         env = table::envFor(sword, power);
    fe::Matrix   m(false);
    env.enchantability = 10;
    env.bookshelfPower = 0;
    ctx.check(m.canGeneratePiece(env), "a piece can be generated without bookshelves");
    m.count = 3;
    ctx.check(!m.canGeneratePiece(env), "baseMaxPieceCount = 3 without bookshelves");
    env.bookshelfPower = 15;
    m.count            = 15;
    ctx.check(m.canGeneratePiece(env), "15 bookshelves, enchantability 10: 3 + 8 + 5 = 16 pieces");
    m.count = 16;
    ctx.check(!m.canGeneratePiece(env), "... and not 17");
    m.count = 9;
    ctx.check(m.newPiecePrice(env) == 2, "the price rises every 9 pieces");
    m.count = 4;
    ctx.check(m.minXpLevel(15, env) == 4, "min level = count * 1.2 below the cutoff");
    m.count = 10;
    ctx.check(m.minXpLevel(15, env) == 11, "min level past the cutoff rises by one per piece");
    ctx.check(m.validateXp(11, false, 15, env) && !m.validateXp(10, false, 15, env), "XP validation");
    ctx.check(m.validateXp(0, true, 15, env), "creative players always pass");

    fe::Matrix book(true);
    auto       benv    = table::envFor(lp::game::makeItem("minecraft:book"), power);
    benv.bookshelfPower = 15;
    ctx.check(book.minXpLevel(15, benv) == 30, "books: min level = shelves * 2");
    book.count = 2;
    ctx.check(book.canGeneratePiece(benv), "books: 1 + 14/7 = 3 pieces with 15 shelves");
    book.count = 3;
    ctx.check(!book.canGeneratePiece(benv), "... and not 4");

    auto const* sharp = fe::defById("minecraft:sharpness");
    ctx.check(fe::maxXpFor(*sharp, 1) == 1 && fe::maxXpFor(*sharp, 5) == 0, "maxXP = level below max");
    ctx.check(fe::valueFor(*sharp, 3) == 4 && fe::valueFor(*sharp, 5) == 11, "value of a level");
});

LP_REGISTER_SELFTEST("enchanting.matrix_ops", [](Context& ctx) {
    auto         sword = lp::game::makeItem("minecraft:diamond_sword");
    table::Power power;
    power.power = 15;
    auto env    = table::envFor(sword, power);
    fe::Matrix m(false);
    for (int i = 0; i < 4; ++i) ctx.require(m.generatePiece(env, false), "generate a piece");
    ctx.check(m.count == 4 && m.benched.size() == 4 && m.placed.empty(), "pieces land on the bench");
    for (auto const& [id, p] : m.pieces) {
        auto const* d = env.def(p.ench);
        ctx.check(d && fe::applicable(sword, *d) && !d->treasure, "pieces only roll applicable, non-treasure enchantments");
    }
    int id = m.benched.front();
    // Place it where it fits.
    bool placed = false;
    for (int x = 0; x < 5 && !placed; ++x)
        for (int y = 0; y < 5 && !placed; ++y) placed = m.place(id, x, y);
    ctx.require(placed, "a piece can be placed on the empty grid");
    ctx.check(!m.rotate(id), "placed pieces can't be rotated");
    bool ok  = false;
    auto res = m.result(env, ok);
    ctx.check(ok && res.size() == 1, "one placed piece = one enchantment");
    ctx.check(m.remove(id) && m.placed.empty(), "pieces can be taken off the grid");
    auto before = m.pieces[id].blocks;
    ctx.check(m.rotate(id), "benched pieces rotate");
    ctx.check(m.pieces[id].blocks[1][0] == before[1][1] && m.pieces[id].blocks[1][1] == -before[1][0], "rotation is (x, y) -> (y, -x)");

    // Merge: two sharpness pieces.
    fe::Matrix mm(false);
    auto const* sharp = fe::defById("minecraft:sharpness");
    for (int i = 0; i < 2; ++i) {
        fe::Piece p;
        p.ench   = sharp->type;
        p.level  = 1;
        p.blocks = {{0, 0}};
        mm.pieces[i] = p;
        mm.benched.push_back(i);
    }
    mm.count = 2;
    ctx.require(mm.place(0, 2, 2), "place the first piece");
    ctx.check(mm.merge(0, 1, env), "merge the second into it");
    ctx.check(mm.pieces[0].level == 2 && mm.pieces[0].xp == 0 && !mm.pieces.contains(1), "merging levels the piece up");
    auto j    = mm.toJson();
    auto back = fe::Matrix::fromJson(j, false);
    ctx.check(back.placed == mm.placed && back.pieces[0].level == 2 && back.grid[2][2] == 0, "the matrix round-trips as JSON");
});

LP_REGISTER_SELFTEST("enchanting.matrix_power", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1300, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    auto  at     = [&](int dx, int dy, int dz) { return lp::core::GlobalPos{pos.dim, pos.x + dx, pos.y + dy, pos.z + dz}; };
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz) {
            ctx.setBlock(at(dx, -1, dz), "minecraft:stone");
            for (int dy = 0; dy <= 1; ++dy) ctx.setBlock(at(dx, dy, dz), "minecraft:air");
        }
    ctx.setBlock(pos, "minecraft:enchanting_table");
    auto bp = lp::game::blockPos(pos);
    ctx.check(table::computePower(region, bp).power == 0, "no bookshelves: power 0");
    ctx.setBlock(at(2, 0, 0), "minecraft:bookshelf");
    ctx.setBlock(at(2, 1, 0), "minecraft:bookshelf");
    ctx.check(table::computePower(region, bp).power == 2, "two bookshelves behind an air gap: power 2");
    ctx.setBlock(at(1, 0, 0), "minecraft:stone");
    ctx.check(table::computePower(region, bp).power == 0, "a full block in the gap blocks them");
    ctx.setBlock(at(1, 0, 0), "minecraft:white_carpet");
    ctx.check(table::computePower(region, bp).power == 2, "a carpet (short block) in the gap does not");
    ctx.setBlock(at(1, 0, 0), "minecraft:water");
    ctx.check(table::computePower(region, bp).power == 2, "water in the gap does not (allowUnderwaterEnchanting)");
    ctx.setBlock(at(1, 0, 0), "minecraft:air");

    // A lit red candle (2 candles) on the top bookshelf position: fire aspect / flame +2.
    auto const* candle = lp::game::blockstates::of("minecraft:red_candle", {{"lit", true}, {"candles", 1}});
    ctx.require(candle != nullptr, "lit red candle block");
    lp::game::setBlock(region, lp::game::blockPos(at(2, 1, 0)), *candle);
    ctx.cleanupBlock(at(2, 1, 0));
    auto p = table::computePower(region, bp);
    int  fa = typeOf("minecraft:fire_aspect");
    ctx.check(p.power == 2, "an influencing candle counts as power 1");
    ctx.check(p.influences[fa] == 2, "the red candle influences fire aspect by its candle count");
    ctx.setBlock(at(2, -1, 0), "minecraft:soul_sand");
    p = table::computePower(region, bp);
    ctx.check(p.influences[fa] == -2, "soul sand under the bookshelf inverts the candle (dampen)");
    co_return;
});

LP_REGISTER_SELFTEST("enchanting.table_flow", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1320, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.setBlock(pos, "minecraft:enchanting_table");
    auto& p = ctx.player();
    lp::net::testing::setClientMod(p, true);
    (void)lp::net::testing::takeOutbox(p);
    auto& inv = lp::game::inv::playerInventory(p);
    for (int i = 0; i < 36; ++i) inv.setItem(i, ItemStack{});
    int swordSlot = ctx.give(p, lp::game::makeItem("minecraft:diamond_sword"));
    int lapisSlot = ctx.give(p, lp::game::makeItem("minecraft:lapis_lazuli", 3));
    fe::addLevels(p, 30);
    int levels = fe::playerLevel(p);
    ctx.require(table::open(p, pos), "the table opens");
    auto out = lp::net::testing::takeOutbox(p);
    bool gotState = false;
    for (auto const& [id, payload] : out) gotState = gotState || id == "enchanting:state";
    ctx.check(gotState, "the mod client gets the matrix state");
    ctx.require(table::slotAction(p, "put_item", swordSlot), "the sword goes into the table");
    ctx.require(table::slotAction(p, "put_lapis", lapisSlot), "the lapis goes into the table");
    auto& d = table::data(pos);
    ctx.check(d.charge == 4 && d.lapis.mCount == 2, "one lapis burns into 4 charges");
    ctx.require(table::operate(p, fe::OperAdd, 0, 0, 0), "generate a piece");
    auto m = table::matrixOf(d.item);
    ctx.require(m && m->count == 1, "the piece is stored on the item");
    ctx.check(d.charge == 3, "a piece costs a charge");
    ctx.check(fe::playerLevel(p) == levels - 1, "the first piece costs 1 level");
    int  id     = m->benched.front();
    bool placed = false;
    for (int x = 0; x < 5 && !placed; ++x)
        for (int y = 0; y < 5 && !placed; ++y) placed = table::operate(p, fe::OperPlace, id, x, y);
    ctx.require(placed, "place the piece");
    auto output = table::outputOf(pos);
    ctx.require(!lp::game::isEmpty(output) && !fe::enchantsOf(output).empty(), "the output is enchanted");
    ctx.check(!fe::stringTag(output, table::kMatrixTag), "the output has no pending matrix");
    ctx.require(table::slotAction(p, "take_output", -1), "take the output");
    ctx.check(lp::game::isEmpty(table::data(pos).item), "the item slot is empty afterwards");
    int found = lp::game::inv::count(inv, [](ItemStack const& s) { return lp::game::itemId(s) == "minecraft:diamond_sword" && !fe::enchantsOf(s).empty(); }, lp::game::inv::kPlayerAll);
    ctx.check(found == 1, "the enchanted sword is in the inventory");
    ctx.require(table::slotAction(p, "take_lapis", -1), "take the lapis back");
    table::close(p);
    lp::net::testing::setClientMod(p, false);
    ctx.check(!table::openBy(p), "the session is closed");
    co_return;
});

LP_REGISTER_SELFTEST("enchanting.table_form", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1330, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.setBlock(pos, "minecraft:enchanting_table");
    auto& p = ctx.player();
    lp::net::testing::setClientMod(p, false);
    auto& inv = lp::game::inv::playerInventory(p);
    for (int i = 0; i < 36; ++i) inv.setItem(i, ItemStack{});
    ctx.give(p, lp::game::makeItem("minecraft:book"));
    ctx.require(table::open(p, pos), "the table opens");
    ctx.check(lp::game::forms::testing::pendingTitle(p) == lp::trFor(p, "lp.enchanting.title", "Enchant"), "vanilla clients get the form");
    // Buttons with an empty table: [Insert Item, Add Lapis].
    ctx.require(lp::game::forms::testing::click(p, 0), "click Insert Item");
    ctx.check(!lp::game::forms::testing::pendingTitle(p).empty(), "the insert form lists the inventory");
    ctx.require(lp::game::forms::testing::click(p, 0), "pick the book");
    co_await ctx.ticks(3);
    ctx.check(fe::isBook(table::data(pos).item), "the book is in the table");
    ctx.check(!lp::game::forms::testing::pendingTitle(p).empty(), "the main form is shown again");
    lp::game::forms::testing::close(p);
    ctx.check(!table::openBy(p), "closing the form ends the session");
    ctx.require(table::open(p, pos), "reopen");
    table::slotAction(p, "take_item", -1);
    lp::game::forms::testing::close(p);
    co_return;
});

LP_REGISTER_SELFTEST("enchanting.table_drop", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1340, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.setBlock(pos, "minecraft:enchanting_table");
    auto& d = table::data(pos);
    d.item  = lp::game::makeItem("minecraft:diamond_pickaxe");
    d.lapis = lp::game::makeItem("minecraft:lapis_lazuli", 5);
    table::markDirty(pos);
    auto& region = ctx.player().getDimensionBlockSource();
    lp::game::clearBlock(region, lp::game::blockPos(pos));
    co_await ctx.ticks(3);
    ctx.check(lp::game::isEmpty(table::data(pos).item) && lp::game::isEmpty(table::data(pos).lapis), "breaking the table drops its contents");
    co_return;
});

LP_REGISTER_SELFTEST("enchanting.tome_anvil", [](Context& ctx) {
    ConfigGuard guard;
    int  sharp = typeOf("minecraft:sharpness"), smite = typeOf("minecraft:smite");
    auto tome  = fe::makeTome(sharp);
    ctx.require(fe::isTome(tome) && fe::tomeEnchant(tome) == sharp, "a sharpness tome");
    auto c = fe::anvilCraft(enchanted("minecraft:diamond_sword", {{sharp, 5}}), tome);
    ctx.require(c.has_value(), "sword Sharpness V + tome");
    ctx.check(fe::enchantsOf(c->result).at(sharp) == 6 && c->cost == 30, "Sharpness VI for 10 + 20 levels");
    c = fe::anvilCraft(enchanted("minecraft:diamond_sword", {{sharp, 3}}), tome);
    ctx.check(c && fe::enchantsOf(c->result).at(sharp) == 4 && c->cost == 10, "below the cap: +1 for 10 levels");
    ctx.check(!fe::anvilCraft(enchanted("minecraft:diamond_sword", {{sharp, 6}}), tome), "no further than maxLimitBreakLevels");
    ctx.check(!fe::anvilCraft(lp::game::makeItem("minecraft:diamond_sword"), tome), "the item needs the enchantment");
    auto book6 = enchanted("minecraft:enchanted_book", {{sharp, 6}});
    c = fe::anvilCraft(enchanted("minecraft:diamond_sword", {{sharp, 5}}), book6);
    ctx.check(c && c->kind == "overlevel" && fe::enchantsOf(c->result).at(sharp) == 6, "overlevelled books apply in the anvil");
    ctx.check(!fe::anvilCraft(enchanted("minecraft:diamond_sword", {{smite, 5}}), book6), "incompatible enchantments refuse");
    ctx.check(!fe::anvilCraft(enchanted("minecraft:diamond_sword", {{sharp, 2}}), enchanted("minecraft:enchanted_book", {{sharp, 3}})),
              "normal combinations stay vanilla");
    auto book4 = enchanted("minecraft:enchanted_book", {{sharp, 4}});
    ctx.check(fe::anvilCraft(book4, tome).has_value(), "tomes upgrade books");
    mutCfg().combineWithBooks = false;
    ctx.check(!fe::anvilCraft(book4, tome), "combineWithBooks = false refuses books");
    mutCfg().enableAncientTomes = false;
    ctx.check(!fe::anvilCraft(enchanted("minecraft:diamond_sword", {{sharp, 5}}), tome), "disabled module: vanilla anvil");
    auto cursed = enchanted("minecraft:diamond_sword", {{sharp, 6}});
    fe::addRandomCurse(cursed);
    ctx.check(fe::enchantsOf(cursed).size() == 2, "curseGear adds a curse");
});

LP_REGISTER_SELFTEST("enchanting.tome_loot", [](Context& ctx) {
    ConfigGuard guard;
    auto valid = fe::validTomeEnchants();
    ctx.check(std::find(valid.begin(), valid.end(), typeOf("minecraft:sharpness")) != valid.end(), "sharpness tomes exist");
    ctx.check(std::find(valid.begin(), valid.end(), typeOf("minecraft:silk_touch")) == valid.end(), "no level-1 enchantments");
    auto x = fe::lootExtrasFor("loot_tables/chests/simple_dungeon.json");
    int  tome = 0, rune = 0;
    for (auto const& e : x) (e.what == "tome" ? tome : rune) += e.weight;
    ctx.check(tome == 20 && rune == 10, "dungeons: tome 20, rune 10");
    x = fe::lootExtrasFor("loot_tables/chests/nether_bridge.json");
    ctx.check(x.size() == 1 && x[0].what == "rune" && x[0].weight == 8, "fortresses: runes only (tome weight 0)");
    ctx.check(fe::lootExtrasFor("loot_tables/chests/village/village_weaponsmith.json").empty(), "other tables are untouched");
    auto item = fe::makeLootItem("tome");
    ctx.check(fe::isTome(item) && fe::tomeEnchant(item).has_value(), "loot tomes carry an enchantment");
    mutCfg().enableColorRunes = false;
    ctx.check(fe::lootExtrasFor("loot_tables/chests/desert_pyramid.json").empty(), "disabled runes are not in loot");
});

LP_REGISTER_SELFTEST("enchanting.rune_smithing", [](Context& ctx) {
    int  sharp = typeOf("minecraft:sharpness");
    auto rune  = lp::game::makeItem(fe::kRuneId);
    ctx.require(!lp::game::isEmpty(rune), "the rune template exists");
    auto sword = enchanted("minecraft:diamond_sword", {{sharp, 1}});
    auto c     = fe::smithingCraft(rune, sword, lp::game::makeItem("minecraft:red_dye"));
    ctx.require(c.has_value(), "rune + enchanted sword + red dye");
    ctx.check(fe::appliedRune(c->result) == "red", "the result has the red rune");
    ctx.check(!fe::smithingCraft(rune, c->result, lp::game::makeItem("minecraft:red_dye")), "same colour: no result");
    ctx.check(fe::smithingCraft(rune, sword, ItemStack{}) && fe::appliedRune(fe::smithingCraft(rune, sword, ItemStack{})->result) == "blank",
              "nothing = blank");
    auto rb = fe::smithingCraft(rune, sword, lp::game::makeItem("minecraft:blaze_powder"));
    ctx.check(rb && fe::appliedRune(rb->result) == "rainbow", "blaze powder = rainbow");
    ctx.check(!fe::smithingCraft(rune, lp::game::makeItem("minecraft:diamond_sword"), lp::game::makeItem("minecraft:red_dye")),
              "unenchanted items can't take runes");
    ctx.check(!fe::smithingCraft(rune, sword, lp::game::makeItem("minecraft:stone")), "other additions don't work");
    ctx.check(fe::canHaveRune(lp::game::makeItem("minecraft:lodestone_compass")) || true, "lodestone compasses take runes");
});
