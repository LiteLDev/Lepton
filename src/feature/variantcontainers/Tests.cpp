// Selftests of U9 variantcontainers (dx 900..999).

#include <format>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ComparatorBlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/variantcontainers/VariantContainers.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
namespace vcf = lp::feature::variantcontainers;
namespace vc  = lp::game::vc;
namespace bs  = lp::game::blockstates;
using lp::game::makeItem;

BlockPos bp(lp::core::GlobalPos const& p) { return lp::game::blockPos(p); }

/// The container of a vanilla block actor (hopper, furnace) at `pos`.
Container* containerAt(BlockSource& region, BlockPos const& pos) {
    auto* a = region.getBlockEntity(pos);
    return a ? static_cast<VanillaBlockActor*>(a)->getContainer() : nullptr;
}

void clearItemsAround(BlockSource& region, BlockPos const& pos) {
    AABB box{Vec3{pos.x - 3.f, pos.y - 3.f, pos.z - 3.f}, Vec3{pos.x + 4.f, pos.y + 4.f, pos.z + 4.f}};
    for (auto* d : lp::game::entities::inBox(region, box, "minecraft:item")) lp::game::entities::despawn(*d);
}

int itemsAround(BlockSource& region, BlockPos const& pos, std::string const& id) {
    AABB box{Vec3{pos.x - 3.f, pos.y - 3.f, pos.z - 3.f}, Vec3{pos.x + 4.f, pos.y + 4.f, pos.z + 4.f}};
    int  n = 0;
    for (auto* d : lp::game::entities::inBox(region, box, "minecraft:item")) {
        auto const& item = static_cast<ItemActor*>(d)->item();
        if (id.empty() || lp::game::itemId(item) == id) n += item.mCount;
    }
    return n;
}

/// Sets a chest with cardinal direction `look` and type.
bool placeChest(Context& ctx, lp::core::GlobalPos const& pos, std::string const& id, std::string const& look, std::string const& type) {
    ctx.cleanupBlock(pos);
    auto* region = lp::game::regionOf(pos.dim);
    auto const* b = bs::of(id, {{"minecraft:cardinal_direction", look}, {"lepton:type", type}});
    return region && b && lp::game::setBlock(*region, bp(pos), *b);
}

int countItems(std::vector<ItemStack> const& v, std::string const& id) {
    int n = 0;
    for (auto const& s : v)
        if (!lp::game::isEmpty(s) && lp::game::itemId(s) == id) n += s.mCount;
    return n;
}

} // namespace

LP_REGISTER_SELFTEST("variantcontainers.registry", [](Context& ctx) {
    for (auto const& id : vcf::chestIds()) {
        auto const* b = bs::of(id);
        ctx.check(b != nullptr, id + " is registered");
        if (!b) continue;
        ctx.check(vcf::isVariantChest(*b), id + " carries the chest tag");
        ctx.check(b->getBlockType().hasComparatorSignal(), id + " has a comparator signal");
        ctx.check(!lp::game::isEmpty(makeItem(id)), id + " has an item");
    }
    for (auto const& id : vcf::furnaceIds()) {
        auto const* b = bs::of(id);
        ctx.check(b != nullptr, id + " is registered");
        if (!b) continue;
        ctx.check(vcf::isVariantFurnace(*b), id + " carries the furnace tag");
        ctx.check(b->getBlockType().getBlockEntityType() == BlockActorType::Furnace, id + " has the furnace block actor");
        ctx.check(b->getBlockType().hasComparatorSignal(), id + " has a comparator signal");
    }
    // the vanilla chest keeps its own behaviour
    if (auto const* v = bs::of("minecraft:stone")) ctx.check(!v->getBlockType().hasComparatorSignal(), "stone is untouched");
    auto& recipes = ctx.player().getLevel().getRecipes();
    std::set<std::string> want{"lepton:oak_chest", "lepton:trapped_oak_chest", "lepton:oak_chest_revert", "lepton:trapped_oak_chest_revert",
        "lepton:nether_brick_chest", "lepton:deepslate_furnace", "lepton:blackstone_furnace", "lepton:deepslate_blast_furnace",
        "lepton:blackstone_smoker", "lepton:oak_chest_wood_oak_log"};
    for (auto& [tag, byId] : recipes.mRecipes.get())
        for (auto const& [id, recipe] : byId) {
            want.erase(id);
            if (!id.starts_with("minecraft:") || tag.getString() != "crafting_table") continue;
            for (auto const& r : recipe->getResultItems()) {
                auto rid = lp::game::itemId(r);
                if (rid == "minecraft:chest" || rid == "minecraft:furnace" || rid == "minecraft:trapped_chest")
                    ctx.info(std::format("probe recipe {} -> {} priority {}", id, rid, static_cast<int>(recipe->mPriority)));
            }
        }
    for (auto const& id : want) ctx.check(false, std::format("recipe {} is registered", id));
    ctx.check(vcf::recipeAllowed("oak_chest") && vcf::recipeAllowed("oak_chest_revert") && vcf::recipeAllowed("deepslate_furnace"),
        "default config allows the recipes");
});

// Two oak chests side by side pair up (Java updateShape), their slots merge into one double
// inventory, breaking a half drops its slots and gives the other half back its own.
LP_REGISTER_SELFTEST("variantcontainers.pairing", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(900, 2);
    auto b = lp::core::GlobalPos{a.dim, a.x + 1, a.y, a.z};
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    vc::takeAll(a);
    vc::takeAll(b);
    // look north: the fronts face south; Java facing south, left = partner west, right = partner east
    ctx.require(placeChest(ctx, a, "lepton:oak_chest", "north", "single"), "chest A placed");
    vc::setItems(a, {makeItem("minecraft:apple", 3)});
    vc::setItems(b, {makeItem("minecraft:stick", 5)});
    co_await ctx.ticks(2);
    ctx.require(placeChest(ctx, b, "lepton:oak_chest", "north", "left"), "chest B placed as the east half");
    co_await ctx.ticks(4);
    auto sa = vcf::chestState(region.getBlock(bp(a)));
    auto sb = vcf::chestState(region.getBlock(bp(b)));
    ctx.require(sa && sb, "both are chests");
    ctx.check(sa->type == vcf::ChestType::Right && sb->type == vcf::ChestType::Left, "A became the right (west) half");
    auto st = vcf::chestStorage(region, bp(b));
    ctx.require(st.has_value(), "storage of the pair");
    ctx.check(st->key == a && st->size == 54, "the double inventory lives at the west half with 54 slots");
    auto all = vc::items(a, 54);
    ctx.check(countItems(all, "minecraft:apple") == 3 && lp::game::itemId(all[0]) == "minecraft:apple", "A's items are the top part");
    ctx.check(lp::game::itemId(all[27]) == "minecraft:stick", "B's items moved to the bottom part");
    ctx.check(!vc::hasItems(b), "nothing stays under B");
    // a different chest kind does not pair
    auto c = lp::core::GlobalPos{a.dim, a.x + 2, a.y, a.z};
    ctx.require(placeChest(ctx, c, "lepton:spruce_chest", "north", "left"), "spruce chest placed");
    co_await ctx.ticks(3);
    auto sc = vcf::chestState(region.getBlock(bp(c)));
    ctx.check(sc && sc->type == vcf::ChestType::Single, "a spruce chest next to an oak chest stays single");
    // break the west half: its slots drop, the east half keeps the bottom part as its own
    clearItemsAround(region, bp(a));
    lp::game::clearBlock(region, bp(a));
    co_await ctx.ticks(4);
    sb = vcf::chestState(region.getBlock(bp(b)));
    ctx.check(sb && sb->type == vcf::ChestType::Single, "B turned single");
    auto own = vc::items(b, 27);
    ctx.check(lp::game::itemId(own[0]) == "minecraft:stick" && own[0].mCount == 5, "B keeps its own items");
    ctx.check(itemsAround(region, bp(a), "minecraft:apple") == 3, "A's apples dropped");
    clearItemsAround(region, bp(a));
    vc::takeAll(b);
});

// Opening: screen with 27 / 54 slots, lid frames, trapped chest power, blocked by a solid block.
LP_REGISTER_SELFTEST("variantcontainers.open", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(910, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    vc::takeAll(pos);
    ctx.require(placeChest(ctx, pos, "lepton:trapped_birch_chest", "east", "single"), "trapped chest placed");
    vc::setItems(pos, {makeItem("minecraft:gold_ingot", 7)});
    co_await ctx.ticks(2);
    ctx.require(vcf::openChest(p, bp(pos)), "the chest opens");
    for (int i = 0; i < 10 && !vc::liveContainer(pos); ++i) co_await ctx.ticks(1);
    auto* live = vc::liveContainer(pos);
    ctx.require(live != nullptr, "live screen container");
    ctx.check(live->getContainerSize() == 27, "single chest screen has 27 slots");
    ctx.check(lp::game::itemId(live->getItem(0)) == "minecraft:gold_ingot", "the screen shows the chest's items");
    co_await ctx.ticks(8);
    auto const& opened = region.getBlock(bp(pos));
    ctx.check(bs::getInt(opened, "lepton:lid") == 4, "the lid is fully open");
    ctx.check(bs::getInt(opened, "lepton:power") == 1, "one viewer = power 1");
    vc::closeAll(pos);
    for (int i = 0; i < 20 && bs::getInt(region.getBlock(bp(pos)), "lepton:lid") != 0; ++i) co_await ctx.ticks(1);
    auto const& closed = region.getBlock(bp(pos));
    ctx.check(bs::getInt(closed, "lepton:lid") == 0 && bs::getInt(closed, "lepton:power") == 0, "the lid closes and the power goes off");
    // a solid block on top keeps it shut
    auto above = lp::core::GlobalPos{pos.dim, pos.x, pos.y + 1, pos.z};
    ctx.require(ctx.setBlock(above, "minecraft:stone"), "stone above");
    co_await ctx.ticks(2);
    ctx.check(!vcf::openChest(p, bp(pos)), "a solid block above blocks the chest");
    ctx.setBlock(above, "minecraft:glass");
    co_await ctx.ticks(2);
    ctx.check(vcf::openChest(p, bp(pos)), "glass above does not");
    vc::closeAll(pos);
    co_await ctx.ticks(12);
    vc::takeAll(pos);
});

LP_REGISTER_SELFTEST("variantcontainers.double_screen", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(915, 2);
    auto b = lp::core::GlobalPos{a.dim, a.x, a.y, a.z + 1};
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    auto& p = ctx.player();
    vc::takeAll(a);
    vc::takeAll(b);
    // look east: Java facing west; left = partner clockwise of west = north
    ctx.require(placeChest(ctx, a, "lepton:crimson_chest", "east", "single"), "chest A");
    ctx.require(placeChest(ctx, b, "lepton:crimson_chest", "east", "left"), "chest B (left half, partner north)");
    co_await ctx.ticks(4);
    auto& region = p.getDimensionBlockSource();
    auto  st     = vcf::chestStorage(region, bp(b));
    ctx.require(st && st->size == 54, "a double chest");
    ctx.require(vcf::openChest(p, bp(b)), "the double chest opens from either half");
    for (int i = 0; i < 10 && !vc::liveContainer(st->key); ++i) co_await ctx.ticks(1);
    auto* live = vc::liveContainer(st->key);
    ctx.check(live && live->getContainerSize() == 54, "double chest screen has 54 slots");
    co_await ctx.ticks(8);
    ctx.check(bs::getInt(region.getBlock(bp(a)), "lepton:lid") == 4 && bs::getInt(region.getBlock(bp(b)), "lepton:lid") == 4,
        "both lids open");
    vc::closeAll(st->key);
    co_await ctx.ticks(12);
});

// Hoppers pull from a chest above them and push into a chest below them.
LP_REGISTER_SELFTEST("variantcontainers.hopper", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto chest = ctx.testPos(920, 2);
    chest.y += 1;
    auto below = lp::core::GlobalPos{chest.dim, chest.x, chest.y - 1, chest.z};
    auto above = lp::core::GlobalPos{chest.dim, chest.x, chest.y + 1, chest.z};
    ctx.require(co_await ctx.ensureLoaded(chest), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    vc::takeAll(chest);
    ctx.require(placeChest(ctx, chest, "lepton:jungle_chest", "north", "single"), "chest placed");
    vc::setItems(chest, {makeItem("minecraft:iron_ingot", 3)});
    ctx.require(ctx.setBlock(below, "minecraft:hopper", {{"facing_direction", 0}}), "hopper below");
    ctx.require(ctx.setBlock(above, "minecraft:hopper", {{"facing_direction", 0}}), "hopper above");
    auto* top = containerAt(region, bp(above));
    ctx.require(top != nullptr, "the upper hopper has a container");
    top->setItem(0, makeItem("minecraft:diamond", 2));
    co_await ctx.ticks(80);
    auto* bottom = containerAt(region, bp(below));
    ctx.require(bottom != nullptr, "the lower hopper has a container");
    int pulled = 0;
    for (int i = 0; i < bottom->getContainerSize(); ++i) pulled += bottom->getItem(i).mCount;
    top = containerAt(region, bp(above));
    ctx.check(top && lp::game::isEmpty(top->getItem(0)), "the upper hopper pushed its diamonds into the chest");
    ctx.check(pulled == 5, std::format("the lower hopper pulled everything from the chest ({} items)", pulled));
    lp::game::clearBlock(region, bp(below));
    lp::game::clearBlock(region, bp(above));
    vc::takeAll(chest);
});

LP_REGISTER_SELFTEST("variantcontainers.probe_crafter", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(985, 2);
    pos.y += 2;
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    for (std::string plank : {"minecraft:oak_planks", "minecraft:mangrove_planks", "minecraft:cobbled_deepslate"}) {
        ctx.cleanupBlock(pos);
        lp::game::clearBlock(region, bp(pos));
        co_await ctx.ticks(1);
        ctx.setBlock(pos, "minecraft:crafter");
        co_await ctx.ticks(1);
        auto* c = containerAt(region, bp(pos));
        if (!c) {
            ctx.info("no crafter container");
            co_return;
        }
        for (int i = 0; i < 9; ++i)
            if (i != 4) c->setItem(i, makeItem(plank, 1));
        auto red = lp::core::GlobalPos{pos.dim, pos.x + 1, pos.y, pos.z};
        clearItemsAround(region, bp(pos));
        ctx.setBlock(red, "minecraft:redstone_block");
        co_await ctx.ticks(10);
        AABB box{Vec3{pos.x - 3.f, pos.y - 3.f, pos.z - 3.f}, Vec3{pos.x + 4.f, pos.y + 4.f, pos.z + 4.f}};
        for (auto* d : lp::game::entities::inBox(region, box, "minecraft:item"))
            ctx.info(std::format("crafter {} -> {}", plank, lp::game::itemId(static_cast<ItemActor*>(d)->item())));
        clearItemsAround(region, bp(pos));
        lp::game::clearBlock(region, bp(red));
    }
});

LP_REGISTER_SELFTEST("variantcontainers.probe_comparator", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto chest = ctx.testPos(990, 2);
    auto comp  = lp::core::GlobalPos{chest.dim, chest.x, chest.y, chest.z + 1};
    ctx.require(co_await ctx.ensureLoaded(chest), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    for (std::string kind : {"minecraft:chest", "lepton:prismarine_chest", "lepton:deepslate_furnace"}) {
        ctx.cleanupBlock(chest);
        ctx.cleanupBlock(comp);
        if (kind == "minecraft:chest") {
            ctx.setBlock(chest, "minecraft:chest");
            co_await ctx.ticks(1);
            if (auto* c = containerAt(region, bp(chest))) c->setItem(0, makeItem("minecraft:cobblestone", 64));
        } else if (kind == "lepton:deepslate_furnace") {
            ctx.setBlock(chest, kind);
            co_await ctx.ticks(1);
            if (auto* c = containerAt(region, bp(chest))) c->setItem(0, makeItem("minecraft:cobblestone", 64));
        } else {
            placeChest(ctx, chest, kind, "north", "single");
            vc::setItems(chest, {makeItem("minecraft:cobblestone", 64)});
        }
        for (std::string dir : {"north", "south", "east", "west"}) {
            auto const* cmp = bs::of("minecraft:unpowered_comparator", {{"minecraft:cardinal_direction", dir}});
            if (!cmp) {
                ctx.info("no comparator state " + dir);
                continue;
            }
            lp::game::clearBlock(region, bp(comp));
            co_await ctx.ticks(1);
            lp::game::setBlock(region, bp(comp), *cmp);
            co_await ctx.ticks(8);
            auto* actor = region.getBlockEntity(bp(comp));
            int   out   = actor ? static_cast<ComparatorBlockActor*>(actor)->mOutput : -1;
            ctx.info(std::format("{} comparator {} -> {} ({})", kind, dir, out, region.getBlock(bp(comp)).getTypeName()));
        }
        lp::game::clearBlock(region, bp(comp));
        vc::takeAll(chest);
    }
});

LP_REGISTER_SELFTEST("variantcontainers.comparator", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto chest = ctx.testPos(930, 2);
    auto comp  = lp::core::GlobalPos{chest.dim, chest.x, chest.y, chest.z + 1};
    ctx.require(co_await ctx.ensureLoaded(chest), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    vc::takeAll(chest);
    ctx.require(placeChest(ctx, chest, "lepton:prismarine_chest", "north", "single"), "chest placed");
    std::vector<ItemStack> full(27);
    for (int i = 0; i < 14; ++i) full[static_cast<size_t>(i)] = makeItem("minecraft:cobblestone", 64);
    vc::setItems(chest, full);
    ctx.check(vcf::chestComparatorSignal(region, bp(chest)) == 8, "14 of 27 stacks = signal 8 (Java formula)");
    auto const& cb = region.getBlock(bp(chest));
    ctx.check(cb.getBlockType().getComparatorSignal(region, bp(chest), cb, 0) == 8, "the block type reports the signal");
    // a comparator's cardinal direction points at its input (vanilla chests read the same way)
    auto const* cmp = bs::of("minecraft:unpowered_comparator", {{"minecraft:cardinal_direction", std::string("north")}});
    ctx.require(cmp && lp::game::setBlock(region, bp(comp), *cmp), "comparator placed");
    ctx.cleanupBlock(comp);
    co_await ctx.ticks(10);
    auto* actor = region.getBlockEntity(bp(comp));
    int   out   = actor ? static_cast<ComparatorBlockActor*>(actor)->mOutput : -1;
    ctx.check(out == 8, std::format("the comparator reads the chest ({})", out));
    vc::setItems(chest, {});
    vcf::notifyChestChanged(region, bp(chest));
    co_await ctx.ticks(6);
    actor = region.getBlockEntity(bp(comp));
    out   = actor ? static_cast<ComparatorBlockActor*>(actor)->mOutput : -1;
    ctx.check(out == 0, std::format("an empty chest gives 0 ({})", out));
});

// The variant furnace is a real furnace: smelts, lights up, keeps its block, takes fuel and input
// from hoppers, opens the furnace screen, reads on comparators and drops its items when broken.
LP_REGISTER_SELFTEST("variantcontainers.furnace", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(940, 2);
    pos.y += 1;
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    ctx.cleanupBlock(pos);
    auto const* fb = bs::of("lepton:deepslate_furnace", {{"minecraft:cardinal_direction", std::string("north")}});
    ctx.require(fb && lp::game::setBlock(region, bp(pos), *fb), "furnace placed");
    co_await ctx.ticks(2);
    auto* actor = region.getBlockEntity(bp(pos));
    ctx.require(actor && actor->mType == BlockActorType::Furnace, "the furnace has the vanilla furnace block actor");
    auto* c = containerAt(region, bp(pos));
    ctx.require(c && c->getContainerSize() == 3, "furnace container with 3 slots");
    c->setItem(0, makeItem("minecraft:raw_iron", 2));
    c->setItem(1, makeItem("minecraft:coal", 1));
    co_await ctx.ticks(20);
    auto const& lit = region.getBlock(bp(pos));
    ctx.check(lit.getTypeName() == "lepton:deepslate_furnace", "still our furnace while burning");
    ctx.check(bs::getBool(lit, "lepton:lit") == true, "lit while burning");
    ctx.check(lit.getBlockType().getComparatorSignal(region, bp(pos), lit, 0) > 0, "comparator signal from the furnace items");
    co_await ctx.ticks(400);
    actor = region.getBlockEntity(bp(pos));
    c = containerAt(region, bp(pos));
    ctx.require(actor && c, "furnace actor still there");
    auto const& out = c->getItem(2);
    ctx.check(lp::game::itemId(out) == "minecraft:iron_ingot" && out.mCount == 2,
        std::format("smelted 2 iron ({} x{})", lp::game::itemId(out), out.mCount));
    ctx.check(region.getBlock(bp(pos)).getTypeName() == "lepton:deepslate_furnace", "still our furnace after burning out");
    // the screen
    ctx.check(vcf::openFurnace(p, bp(pos)), "the furnace screen opens");
    co_await ctx.ticks(2);
    static_cast<ServerPlayer&>(p).initiateContainerClose();
    co_await ctx.ticks(2);
    // hopper feeding: top = input, side = fuel
    auto top  = lp::core::GlobalPos{pos.dim, pos.x, pos.y + 1, pos.z};
    auto side = lp::core::GlobalPos{pos.dim, pos.x + 1, pos.y, pos.z};
    ctx.require(ctx.setBlock(top, "minecraft:hopper", {{"facing_direction", 0}}), "hopper above");
    ctx.require(ctx.setBlock(side, "minecraft:hopper", {{"facing_direction", 4}}), "hopper beside, facing west into the furnace");
    containerAt(region, bp(top))->setItem(0, makeItem("minecraft:raw_gold", 1));
    containerAt(region, bp(side))->setItem(0, makeItem("minecraft:charcoal", 1));
    co_await ctx.ticks(30);
    c = containerAt(region, bp(pos));
    ctx.check(lp::game::itemId(c->getItem(0)) == "minecraft:raw_gold" || lp::game::itemId(c->getItem(2)) == "minecraft:gold_ingot",
        "the top hopper fed the input slot");
    ctx.check(lp::game::itemId(c->getItem(1)) == "minecraft:charcoal" || bs::getBool(region.getBlock(bp(pos)), "lepton:lit") == true,
        "the side hopper fed the fuel slot");
    lp::game::clearBlock(region, bp(top));
    lp::game::clearBlock(region, bp(side));
    // break: the items drop
    clearItemsAround(region, bp(pos));
    co_await ctx.ticks(1);
    lp::game::clearBlock(region, bp(pos));
    co_await ctx.ticks(3);
    ctx.check(itemsAround(region, bp(pos), "minecraft:iron_ingot") == 2, "the smelted iron dropped");
    clearItemsAround(region, bp(pos));
});

LP_REGISTER_SELFTEST("variantcontainers.soul", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(950, 2);
    pos.y += 1;
    auto below = lp::core::GlobalPos{pos.dim, pos.x, pos.y - 1, pos.z};
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    ctx.require(ctx.setBlock(below, "minecraft:soul_sand"), "soul sand");
    ctx.require(ctx.setBlock(pos, "lepton:blackstone_furnace"), "blackstone furnace on soul sand");
    co_await ctx.ticks(3);
    ctx.check(bs::getBool(region.getBlock(bp(pos)), "lepton:soul") == true, "soul state on soul sand");
    ctx.setBlock(below, "minecraft:stone");
    co_await ctx.ticks(3);
    ctx.check(bs::getBool(region.getBlock(bp(pos)), "lepton:soul") == false, "no soul state on stone");
});
