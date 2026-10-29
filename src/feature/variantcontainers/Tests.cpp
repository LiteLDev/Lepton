// Selftests of U9 variantcontainers (dx 900..999).

#include <format>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"
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
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
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

// Crafting (resolved on the server by a crafter, like the crafting table on the client): eight
// equal planks / stones make the variant, mixed ones the vanilla chest / furnace.
LP_REGISTER_SELFTEST("variantcontainers.recipes", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(960, 4);
    base.y += 2;
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    struct Case {
        std::string a, b, want;
    };
    std::vector<Case> cases{
        {"minecraft:oak_planks", "minecraft:oak_planks", "lepton:oak_chest"},
        {"minecraft:mangrove_planks", "minecraft:mangrove_planks", "lepton:mangrove_chest"},
        {"minecraft:crimson_planks", "minecraft:crimson_planks", "lepton:crimson_chest"},
        {"minecraft:nether_brick", "minecraft:nether_brick", "lepton:nether_brick_chest"},
        {"minecraft:oak_planks", "minecraft:spruce_planks", "minecraft:chest"},
        {"minecraft:cobbled_deepslate", "minecraft:cobbled_deepslate", "lepton:deepslate_furnace"},
        {"minecraft:blackstone", "minecraft:blackstone", "lepton:blackstone_furnace"},
        {"minecraft:cobblestone", "minecraft:blackstone", "minecraft:furnace"},
    };
    for (size_t i = 0; i < cases.size(); ++i) {
        auto const& k   = cases[i];
        auto        pos = lp::core::GlobalPos{base.dim, base.x + static_cast<int>(i) * 3, base.y, base.z};
        auto        red = lp::core::GlobalPos{pos.dim, pos.x, pos.y + 1, pos.z};
        ctx.require(ctx.setBlock(pos, "minecraft:crafter"), "crafter placed");
        co_await ctx.ticks(1);
        auto* c = containerAt(region, bp(pos));
        ctx.require(c != nullptr, "crafter container");
        for (int s = 0; s < 9; ++s)
            if (s != 4) c->setItem(s, makeItem(s == 0 ? k.b : k.a, 1));
        clearItemsAround(region, bp(pos));
        ctx.setBlock(red, "minecraft:redstone_block");
        std::string got;
        for (int t = 0; t < 20 && got.empty(); ++t) {
            co_await ctx.ticks(1);
            AABB box{Vec3{pos.x - 2.f, pos.y - 2.f, pos.z - 2.f}, Vec3{pos.x + 3.f, pos.y + 3.f, pos.z + 3.f}};
            for (auto* d : lp::game::entities::inBox(region, box, "minecraft:item")) got = lp::game::itemId(static_cast<ItemActor*>(d)->item());
        }
        ctx.check(got == k.want, std::format("{} + {} crafts {} (got {})", k.a, k.b, k.want, got));
        clearItemsAround(region, bp(pos));
        lp::game::clearBlock(region, bp(red));
        lp::game::clearBlock(region, bp(pos));
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

// A player places chests (Java ChestBlock#getStateForPlacement): next to an equal chest facing the
// same way they pair up, sneaking keeps the new chest single.
LP_REGISTER_SELFTEST("variantcontainers.placement", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto a = ctx.testPos(983, 6);
    auto b = lp::core::GlobalPos{a.dim, a.x + 1, a.y, a.z};
    auto c = lp::core::GlobalPos{a.dim, a.x + 2, a.y, a.z};
    ctx.require(co_await ctx.ensureLoaded(a), "test area loaded");
    auto* sp     = static_cast<SimulatedPlayer*>(&ctx.player());
    auto& region = sp->getDimensionBlockSource();
    for (auto const& p : {a, b, c}) {
        ctx.cleanupBlock(p);
        lp::game::clearBlock(region, bp(p));
        vc::takeAll(p);
    }
    auto place = [&](lp::core::GlobalPos const& p, bool sneak) {
        sp->teleport(Vec3{p.x + 0.5f, static_cast<float>(p.y), p.z + 3.5f}, DimensionType(p.dim));
        sp->simulateLookAt(Vec3{p.x + 0.5f, p.y - 0.5f, p.z + 0.5f}, sim::LookDuration::Instant);
        sp->setSneaking(sneak);
        auto chest = makeItem("lepton:spruce_chest", 1);
        bool ok = sp->simulateUseItemOnBlock(chest, BlockPos{p.x, p.y - 1, p.z}, ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.f, 0.5f});
        sp->setSneaking(false);
        return ok;
    };
    ctx.require(place(a, false), "first chest placed");
    co_await ctx.ticks(12);
    auto sa = vcf::chestState(region.getBlock(bp(a)));
    ctx.require(sa.has_value(), "the first chest is there");
    ctx.check(sa->facing == 0, std::format("it faces the player (south), got {}", sa->facing));
    ctx.require(place(b, false), "second chest placed");
    co_await ctx.ticks(12);
    sa      = vcf::chestState(region.getBlock(bp(a)));
    auto sb = vcf::chestState(region.getBlock(bp(b)));
    ctx.require(sa && sb, "both chests are there");
    ctx.check(sb->type == vcf::ChestType::Left && sa->type == vcf::ChestType::Right, "placed next to it, the chests pair");
    ctx.require(place(c, true), "third chest placed while sneaking");
    co_await ctx.ticks(12);
    auto sc = vcf::chestState(region.getBlock(bp(c)));
    ctx.check(sc && sc->type == vcf::ChestType::Single, "a sneaking placement stays single");
    sb = vcf::chestState(region.getBlock(bp(b)));
    ctx.check(sb && sb->type == vcf::ChestType::Left, "the pair is unchanged");
    // a vanilla chest placed the same way: its cardinal direction is its front (structure chests
    // turn vanilla fronts into ours)
    auto d = lp::core::GlobalPos{a.dim, a.x + 4, a.y, a.z};
    ctx.cleanupBlock(d);
    sp->teleport(Vec3{d.x + 0.5f, static_cast<float>(d.y), d.z + 3.5f}, DimensionType(d.dim));
    sp->simulateLookAt(Vec3{d.x + 0.5f, d.y - 0.5f, d.z + 0.5f}, sim::LookDuration::Instant);
    auto vchest = makeItem("minecraft:chest", 1);
    sp->simulateUseItemOnBlock(vchest, BlockPos{d.x, d.y - 1, d.z}, ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    auto vcard = bs::getString(region.getBlock(bp(d)), "minecraft:cardinal_direction").value_or("?");
    ctx.check(vcf::dirIndex(vcard) == sa->facing, std::format("a vanilla chest placed the same way has cardinal direction {} (our front {})", vcard, sa->facing));
});

// Trapped chests power redstone while open (weak power around, strong power below).
LP_REGISTER_SELFTEST("variantcontainers.trapped_redstone", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto chest = ctx.testPos(993, 2);
    chest.y += 1;
    auto lamp = lp::core::GlobalPos{chest.dim, chest.x + 1, chest.y, chest.z};
    ctx.require(co_await ctx.ensureLoaded(chest), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    vc::takeAll(chest);
    ctx.require(placeChest(ctx, chest, "lepton:trapped_warped_chest", "north", "single"), "trapped chest placed");
    ctx.require(ctx.setBlock(lamp, "minecraft:redstone_lamp"), "lamp next to it");
    co_await ctx.ticks(4);
    ctx.check(region.getBlock(bp(lamp)).getTypeName() == "minecraft:redstone_lamp", "the lamp is off while the chest is closed");
    ctx.require(vcf::openChest(p, bp(chest)), "the chest opens");
    co_await ctx.ticks(10);
    ctx.check(region.getBlock(bp(lamp)).getTypeName() == "minecraft:lit_redstone_lamp", "the open trapped chest lights the lamp");
    vc::closeAll(chest);
    co_await ctx.ticks(16);
    ctx.check(region.getBlock(bp(lamp)).getTypeName() == "minecraft:redstone_lamp", "closing turns the lamp off");
});

// Donk chest: a wooden variant chest goes onto a tamed donkey and drops back as itself.
LP_REGISTER_SELFTEST("variantcontainers.donk", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(987, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto* sp     = static_cast<SimulatedPlayer*>(&ctx.player());
    auto& region = sp->getDimensionBlockSource();
    clearItemsAround(region, bp(pos));
    Vec3  at{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f};
    auto* donkey = lp::game::entities::spawn(region, "minecraft:donkey", at);
    ctx.require(donkey != nullptr, "donkey spawned");
    auto id = lp::game::entities::idOf(*donkey);
    if (donkey->isBaby()) lp::game::entities::triggerEvent(*donkey, "minecraft:ageable_grow_up");
    lp::game::entities::triggerEvent(*donkey, "minecraft:on_tame");
    co_await ctx.ticks(3);
    donkey = lp::game::entities::find(id);
    ctx.require(donkey != nullptr, "donkey still there");
    ctx.check(donkey->getStatusFlag(ActorFlags::Tamed), "the donkey is tamed");
    sp->teleport(Vec3{at.x + 1.5f, at.y, at.z}, donkey->getDimensionId());
    sp->simulateSetItem(makeItem("lepton:birch_chest", 2), true, 0);
    co_await ctx.ticks(2);
    donkey = lp::game::entities::find(id);
    ctx.require(donkey != nullptr, "donkey still there");
    ctx.check(sp->simulateInteract(*donkey), "interacted with the donkey");
    co_await ctx.ticks(3);
    donkey = lp::game::entities::find(id);
    ctx.require(donkey != nullptr, "donkey still there");
    ctx.check(donkey->getStatusFlag(ActorFlags::Chested), "the donkey carries a chest");
    ctx.check(sp->isCreative() || sp->getSelectedItem().mCount == 1, "one chest was used");
    donkey->kill();
    co_await ctx.ticks(10);
    auto dropAt = bp(pos);
    ctx.check(itemsAround(region, dropAt, "lepton:birch_chest") == 1, "the donkey dropped the birch chest");
    ctx.check(itemsAround(region, dropAt, "minecraft:chest") == 0, "and no vanilla chest");
    clearItemsAround(region, dropAt);
});

// Structure chests: a vanilla chest that gets a structure loot table becomes the structure's
// variant (desert pyramid: birch) and rolls the loot when first opened.
LP_REGISTER_SELFTEST("variantcontainers.worldgen", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(990, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    vc::takeAll(pos);
    auto const* v = bs::of("minecraft:chest", {{"minecraft:cardinal_direction", std::string("west")}});
    ctx.require(v != nullptr, "vanilla chest state");
    ctx.cleanupBlock(pos);
    ctx.require(lp::game::setBlock(region, bp(pos), *v), "vanilla chest placed");
    co_await ctx.ticks(1);
    auto* actor = region.getBlockEntity(bp(pos));
    ctx.require(actor && actor->mType == BlockActorType::Chest, "chest block actor");
    static_cast<RandomizableBlockActorContainerBase*>(static_cast<ChestBlockActor*>(actor))->setLootTable("loot_tables/chests/desert_pyramid.json", 1234);
    co_await ctx.ticks(3);
    auto const& b = region.getBlock(bp(pos));
    ctx.check(b.getTypeName() == "lepton:birch_chest", std::format("the desert pyramid chest became a birch chest ({})", b.getTypeName()));
    auto s = vcf::chestState(b);
    ctx.check(s && s->facing == 1, std::format("it keeps the vanilla chest's facing (front west = 1, got {})", s ? s->facing : -1));
    ctx.check(!vc::hasItems(pos), "the loot is not rolled yet");
    ctx.require(vcf::openChest(p, bp(pos)), "the chest opens");
    co_await ctx.ticks(2);
    ctx.check(vc::hasItems(pos), "opening rolled the loot");
    vc::closeAll(pos);
    co_await ctx.ticks(12);
    vc::takeAll(pos);
});

// Piglins: Bedrock's piglin AI turns on players that have a container screen open (the
// `has_container_open` filter). A variant chest's screen is a container screen like the vanilla
// chest's, so the player counts as having a container open while it shows.
LP_REGISTER_SELFTEST("variantcontainers.piglins", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(996, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    vc::takeAll(pos);
    ctx.require(placeChest(ctx, pos, "lepton:dark_oak_chest", "north", "single"), "chest placed");
    ctx.require(vcf::openChest(p, bp(pos)), "the chest opens");
    bool open = false;
    for (int i = 0; i < 10 && !open; ++i) {
        co_await ctx.ticks(1);
        open = p.mContainerManager.get() != nullptr;
    }
    ctx.check(open, "the player has a container open while the chest screen shows");
    vc::closeAll(pos);
    co_await ctx.ticks(12);
});
LP_REGISTER_SELFTEST("variantcontainers.gating", [](Context& ctx) {
    auto& c     = lp::config::mut<vcf::Config>();
    auto  saved = c;
    c.enableRevertingWoodenChests = false;
    ctx.check(!vcf::recipeAllowed("oak_chest_revert") && vcf::recipeAllowed("oak_chest"), "reverting off removes only the revert recipes");
    c.enableVariantChests = false;
    ctx.check(!vcf::recipeAllowed("oak_chest") && !vcf::recipeAllowed("trapped_dark_oak_chest") && !vcf::itemAllowed("lepton:oak_chest"),
        "chests off removes their recipes and creative entries");
    ctx.check(vcf::recipeAllowed("deepslate_furnace") && vcf::itemAllowed("lepton:deepslate_furnace"), "furnaces stay");
    c.enableVariantFurnaces = false;
    ctx.check(!vcf::recipeAllowed("deepslate_furnace") && !vcf::recipeAllowed("blackstone_smoker") && !vcf::itemAllowed("lepton:blackstone_furnace"),
        "furnaces off removes theirs");
    ctx.check(vcf::recipeAllowed("some_other_recipe") && vcf::itemAllowed("lepton:something_else"), "other units' content is untouched");
    c = saved;
});

// A chest renamed in an anvil shows its name as the screen title.
LP_REGISTER_SELFTEST("variantcontainers.name", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(998, 2);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.player();
    vc::takeAll(pos);
    ctx.require(placeChest(ctx, pos, "lepton:cherry_chest", "north", "single"), "chest placed");
    lp::game::db::save("lp:variantcontainers:name:" + pos.key(), {{"name", "Loot"}});
    ctx.require(vcf::openChest(p, bp(pos)), "the chest opens");
    std::string title;
    for (int i = 0; i < 10 && title.empty(); ++i) {
        co_await ctx.ticks(1);
        for (auto* h : lp::game::entities::ofType(vc::kHelperEntity, pos.dim)) title = h->getNameTag();
    }
    ctx.check(title == "Loot", std::format("the screen title is the chest's name ({})", title));
    vc::closeAll(pos);
    co_await ctx.ticks(12);
    lp::game::db::erase("lp:variantcontainers:name:" + pos.key());
});

// Every block of the unit in front of the (real) player, for the client scenario's screenshots:
// all chests, all trapped chests, the lid frames, double chests from the front and the back,
// the furnaces (unlit, lit, soul) and both screens. On a dedicated server it is only built.
LP_REGISTER_SELFTEST("variantcontainers.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(900, -8);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    auto  at     = [&](int dx, int dz) { return lp::core::GlobalPos{base.dim, base.x + dx, base.y, base.z + dz}; };
    auto  put    = [&](lp::core::GlobalPos const& p, std::string const& id, bs::Changes const& ch) {
        ctx.cleanupBlock(p);
        auto const* b = bs::of(id, ch);
        return b && lp::game::setBlock(region, bp(p), *b);
    };
    auto const& mats = vcf::chestMaterials();
    for (size_t i = 0; i < mats.size(); ++i) {
        int dx = static_cast<int>(i) * 2;
        ctx.check(put(at(dx, 0), "lepton:" + mats[i] + "_chest", {{"minecraft:cardinal_direction", std::string("north")}}), mats[i] + " chest");
        ctx.check(put(at(dx, -3), "lepton:trapped_" + mats[i] + "_chest", {{"minecraft:cardinal_direction", std::string("north")}}),
            mats[i] + " trapped chest");
    }
    // lid frames 0..4, a double oak chest, a double nether brick chest, an open trapped chest
    for (int f = 0; f <= 4; ++f)
        put(at(f * 2, -6), "lepton:oak_chest", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:lid", f}});
    put(at(11, -6), "lepton:oak_chest", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:type", std::string("right")}});
    put(at(12, -6), "lepton:oak_chest", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:type", std::string("left")}});
    put(at(14, -6), "lepton:nether_brick_chest", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:type", std::string("right")}, {"lepton:lid", 4}});
    put(at(15, -6), "lepton:nether_brick_chest", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:type", std::string("left")}, {"lepton:lid", 4}});
    put(at(17, -6), "lepton:trapped_oak_chest", {{"minecraft:cardinal_direction", std::string("east")}, {"lepton:lid", 2}});
    // furnaces: deepslate unlit / lit, blackstone unlit / lit / soul lit (on soul soil)
    put(at(0, -9), "lepton:deepslate_furnace", {{"minecraft:cardinal_direction", std::string("north")}});
    put(at(2, -9), "lepton:deepslate_furnace", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:lit", true}});
    put(at(4, -9), "lepton:blackstone_furnace", {{"minecraft:cardinal_direction", std::string("north")}});
    put(at(6, -9), "lepton:blackstone_furnace", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:lit", true}});
    auto soulBase = lp::core::GlobalPos{base.dim, base.x + 8, base.y - 1, base.z - 9};
    ctx.setBlock(soulBase, "minecraft:soul_soil");
    put(at(8, -9), "lepton:blackstone_furnace", {{"minecraft:cardinal_direction", std::string("north")}, {"lepton:lit", true}, {"lepton:soul", true}});
    // the lit furnaces burn for real (an idle furnace goes out at once)
    for (int dx : {2, 6, 8}) {
        if (auto* c = containerAt(region, bp(at(dx, -9)))) {
            c->setItem(0, makeItem("minecraft:raw_iron", 64));
            c->setItem(1, makeItem("minecraft:coal", 64));
        }
    }
    co_await ctx.ticks(4);
    // nothing re-paired or changed what was placed
    auto s = vcf::chestState(region.getBlock(bp(at(11, -6))));
    ctx.check(s && s->type == vcf::ChestType::Right, "the gallery's double chest stays paired");

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) {
        for (int dx : {0, 2, 4, 6, 8})
            if (auto* c = containerAt(region, bp(at(dx, -9)))) c->removeAllItems();
        co_return;
    }
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](float dx, float dz, float yaw, float pitch) {
        if (auto* v = ctx.player().getLevel().getPlayer(uid))
            v->teleport(Vec3{base.x + dx, base.y + 1.f, base.z + dz}, DimensionType(base.dim), Vec2{pitch, yaw});
    };
    // the fronts face south (+z): look north from the south
    ctx.info("VCGALLERY chests-left");
    view(6.5f, 5.f, 180.f, 25.f);
    co_await ctx.ticks(100);
    ctx.info("VCGALLERY chests-right");
    view(20.5f, 5.f, 180.f, 25.f);
    co_await ctx.ticks(100);
    ctx.info("VCGALLERY trapped-left");
    view(6.5f, 2.f, 180.f, 25.f);
    co_await ctx.ticks(100);
    ctx.info("VCGALLERY trapped-right");
    view(20.5f, 2.f, 180.f, 25.f);
    co_await ctx.ticks(100);
    ctx.info("VCGALLERY lids");
    view(9.f, -1.5f, 180.f, 30.f);
    co_await ctx.ticks(100);
    ctx.info("VCGALLERY lids-back");
    view(9.f, -10.5f, 0.f, 30.f);
    co_await ctx.ticks(100);
    ctx.info("VCGALLERY furnaces");
    view(4.5f, -5.5f, 180.f, 20.f);
    co_await ctx.ticks(100);
    // the screens open with a real click of the player (the client scenario right-clicks)
    // the screens open with real clicks of the player: the client scenario right-clicks after each
    // "aim" line and waits for the module's "opened" log line
    auto aim = [&](std::string const& what, lp::core::GlobalPos const& target) -> ll::coro::CoroTask<> {
        if (auto* v = ctx.player().getLevel().getPlayer(uid))
            v->teleport(Vec3{target.x + 0.5f, static_cast<float>(target.y), target.z + 1.8f}, DimensionType(base.dim), Vec2{55.f, 180.f});
        co_await ctx.ticks(10);
        ctx.info("VCGALLERY aim " + what);
        co_await ctx.ticks(160);
        if (auto* v = ctx.player().getLevel().getPlayer(uid)) static_cast<ServerPlayer*>(v)->initiateContainerClose();
        co_await ctx.ticks(20);
    };
    vc::setItems(at(0, 0), {makeItem("minecraft:diamond", 5), makeItem("minecraft:oak_log", 32)});
    co_await aim("chest", at(0, 0));
    {
        std::vector<ItemStack> both(54);
        both[0]  = makeItem("minecraft:emerald", 7);  // top half (the west chest)
        both[30] = makeItem("minecraft:gold_ingot", 9); // bottom half (the east chest)
        vc::setItems(at(11, -6), both);
    }
    co_await aim("double", at(12, -6));
    if (auto* c = containerAt(region, bp(at(2, -9)))) {
        c->setItem(0, makeItem("minecraft:raw_copper", 8));
        c->setItem(1, makeItem("minecraft:coal", 4));
    }
    co_await aim("furnace", at(2, -9));    vc::takeAll(at(0, 0));
    vc::takeAll(at(11, -6));
    ctx.info("VCGALLERY done");
    for (int dx : {0, 2, 4, 6, 8})
        if (auto* c = containerAt(region, bp(at(dx, -9)))) c->removeAllItems();
});

// e2e aid: the (real) player's hotbar, so the client scenario can check what it crafted.
LP_REGISTER_SELFTEST("variantcontainers.report", [](Context& ctx) {
    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) return;
    auto& inv = lp::game::inv::playerInventory(*viewer);
    for (int i = 0; i < 9; ++i) {
        auto const& s = inv.getItem(i);
        if (!lp::game::isEmpty(s)) ctx.info(std::format("VCINV {} {} {}", i, lp::game::itemId(s), static_cast<int>(s.mCount)));
    }
    ctx.info("VCINV done");
});
