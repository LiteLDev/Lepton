// Selftests of U2 stones (docs/spec/stones.md §8). U2 owns dx 200..299.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/registry/CreativeItemEntry.h"
#include "mc/world/item/registry/CreativeItemRegistry.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/stones/Stones.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
namespace bs     = lp::game::blockstates;
namespace shapes = lp::game::shapes;
namespace st     = lp::feature::stones;
using Facing     = ScriptModuleMinecraft::ScriptFacing;
using lp::core::GlobalPos;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

bool put(Context& ctx, GlobalPos const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}
Block const& at(Context& ctx, GlobalPos const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }
std::string idAt(Context& ctx, GlobalPos const& p) { return lp::game::blockIdAt(regionOf(ctx), lp::game::blockPos(p)); }
GlobalPos   off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}
bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

/// Restores the stones config (and the cross-unit view) when a test ends.
struct ConfigGuard {
    st::StonesConfig saved = lp::config::get<st::StonesConfig>();
    st::CrossUnit    cross = st::crossUnit();
    ~ConfigGuard() {
        lp::config::mut<st::StonesConfig>() = saved;
        st::crossUnit()                     = cross;
    }
};

bool recipeLoaded(Level& level, std::string const& id) {
    for (auto const& [tag, byId] : level.getRecipes().mRecipes.get())
        if (byId.contains(id)) return true;
    return false;
}

/// Item entities of `id` in the block at `p` (and around it).
std::vector<Actor*> dropsAt(Context& ctx, GlobalPos const& p, std::string_view id) {
    AABB box{
        Vec3{static_cast<float>(p.x) - 1.5f, static_cast<float>(p.y) - 1.5f, static_cast<float>(p.z) - 1.5f},
        Vec3{static_cast<float>(p.x) + 2.5f, static_cast<float>(p.y) + 2.5f, static_cast<float>(p.z) + 2.5f}
    };
    std::vector<Actor*> out;
    for (auto* a : lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item"))
        if (lp::game::itemId(static_cast<ItemActor*>(a)->item()) == id) out.push_back(a);
    return out;
}
} // namespace

LP_REGISTER_SELFTEST("stones.content", [](Context& ctx) {
    ctx.check(st::blockCount() == 69, "69 blocks, got " + std::to_string(st::blockCount()));
    ctx.check(st::recipeCount() > 200, "recipes gated: " + std::to_string(st::recipeCount()));
    int missing = 0;
    st::forEachBlock([&](std::string_view id, unsigned gate) {
        auto const* b = bs::of(id);
        if (!b) {
            ++missing;
            ctx.check(false, "block registered: " + std::string(id));
            return;
        }
        ctx.check(gate != 0, "gate of " + std::string(id));
        auto k = shapes::kindOf(*b);
        if (endsWith(id, "_stairs")) ctx.check(k == shapes::Kind::Stairs, std::string(id) + " is stairs");
        else if (endsWith(id, "_vertical_slab"))
            ctx.check(k == shapes::Kind::VerticalSlab, std::string(id) + " is a vertical slab");
        else if (endsWith(id, "_slab")) ctx.check(k == shapes::Kind::Slab, std::string(id) + " is a slab");
        else if (endsWith(id, "_wall")) ctx.check(k == shapes::Kind::Wall, std::string(id) + " is a wall");
        else if (endsWith(id, "_pillar"))
            ctx.check(bs::getString(*b, "minecraft:block_face").has_value(), std::string(id) + " has an axis");
        if (endsWith(id, "_vertical_slab"))
            ctx.check((gate & st::G_vslab) != 0, std::string(id) + " gated by vertical slabs");
    });
    ctx.require(missing == 0, "behavior pack loaded");
    auto const* lamp = bs::of("lepton:stone_lamp");
    ctx.check(lamp && bs::getInt(*lamp, "lepton:light") == 15, "lamps default to light 15");
    ctx.check(
        bs::of(
            "lepton:stone_brick_lamp",
            {
                {"lepton:light", 3}
    }
        ) != nullptr,
        "lamp light state 0..15"
    );
    ctx.check(
        st::blockGate("lepton:chiseled_granite_bricks") == (st::G_msv | st::G_bricks | st::G_chiseled),
        "chiseled gate"
    );
    ctx.check(st::recipeGate("lepton:raw_gold_bricks_smelt") == (st::G_rawmetal | st::G_smelt), "smelt recipe gate");
    ctx.check(st::recipeGate("lepton:calcite_pillar") == (st::G_msv | st::G_pillar), "pillar recipe gate");
    ctx.check(
        st::blockGate("minecraft:stone") == 0 && st::blockEnabled("minecraft:stone"),
        "foreign ids are never gated"
    );
});

LP_REGISTER_SELFTEST("stones.gates", [](Context& ctx) {
    ConfigGuard guard;
    auto&       c   = lp::config::mut<st::StonesConfig>();
    c               = st::StonesConfig{};
    st::crossUnit() = st::CrossUnit{};
    ctx.check(st::blockEnabled("lepton:chiseled_dripstone_bricks"), "defaults: everything on");
    ctx.check(st::recipeEnabled("lepton:raw_copper_bricks_blast"), "defaults: smelting on");
    c.enableChiseledBricks = false;
    ctx.check(!st::blockEnabled("lepton:chiseled_granite_bricks"), "enableChiseledBricks off");
    ctx.check(st::blockEnabled("lepton:granite_bricks"), "bricks stay");
    c              = st::StonesConfig{};
    c.enableBricks = false;
    ctx.check(
        !st::blockEnabled("lepton:chiseled_granite_bricks") && !st::blockEnabled("lepton:granite_bricks_wall"),
        "enableBricks off: bricks, shapes, chiseled"
    );
    ctx.check(
        st::blockEnabled("lepton:granite_pillar") && st::blockEnabled("lepton:polished_calcite"),
        "pillars and polished stay"
    );
    ctx.check(!st::recipeEnabled("lepton:granite_bricks_from_polished_granite_stonecutting"), "brick stonecutting off");
    c              = st::StonesConfig{};
    c.enablePillar = false;
    ctx.check(
        !st::blockEnabled("lepton:andesite_pillar") && !st::recipeEnabled("lepton:andesite_pillar"),
        "enablePillar off"
    );
    c                         = st::StonesConfig{};
    c.enableMoreStoneVariants = false;
    ctx.check(!st::blockEnabled("lepton:polished_dripstone") && !st::blockEnabled("lepton:calcite_wall"), "module off");
    ctx.check(st::blockEnabled("lepton:raw_iron_bricks"), "other modules stay");
    c                      = st::StonesConfig{};
    c.enableRawMetalBricks = false;
    ctx.check(!st::recipeEnabled("lepton:raw_iron_bricks_revert"), "raw metal bricks off");
    c                   = st::StonesConfig{};
    c.enableSturdyStone = false;
    ctx.check(
        !st::recipeEnabled("lepton:sturdy_stone_uncompress") && st::blockEnabled("lepton:stone_lamp"),
        "sturdy stone off"
    );
    c                        = st::StonesConfig{};
    c.enableCelebratoryLamps = false;
    ctx.check(!st::blockEnabled("lepton:stone_brick_lamp") && !st::recipeEnabled("lepton:stone_lamp"), "lamps off");
    c                             = st::StonesConfig{};
    st::crossUnit().verticalSlabs = false;
    ctx.check(!st::blockEnabled("lepton:raw_gold_bricks_vertical_slab"), "U1 vertical slabs off");
    ctx.check(!st::recipeEnabled("lepton:granite_bricks_vertical_slab_revert"), "vertical slab revert off");
    ctx.check(st::blockEnabled("lepton:raw_gold_bricks_slab"), "slabs stay");
    st::crossUnit()                = st::CrossUnit{};
    st::crossUnit().smeltOreBlocks = false;
    ctx.check(
        !st::recipeEnabled("lepton:raw_iron_bricks_smelt") && st::recipeEnabled("lepton:raw_iron_bricks"),
        "U8 smeltRawOreBlocks off"
    );
    c.lightLevel = 99;
    ctx.check(st::lampLight() == 15, "lightLevel is clamped");
    c.lightLevel = -3;
    ctx.check(st::lampLight() == 0, "lightLevel is clamped at 0");
});

// The world loaded with the config of this run: loaded recipes and creative items follow the gates.
LP_REGISTER_SELFTEST("stones.recipes", [](Context& ctx) {
    auto& level = ctx.player().getLevel();
    for (auto const* id :
         {"lepton:granite_bricks",
          "lepton:granite_bricks_from_granite_stonecutting",
          "lepton:chiseled_calcite_bricks",
          "lepton:dripstone_pillar",
          "lepton:polished_calcite_vertical_slab_revert",
          "lepton:raw_iron_bricks_revert",
          "lepton:sturdy_stone",
          "lepton:stone_brick_lamp"}) {
        bool want = st::recipeEnabled(id);
        ctx.check(recipeLoaded(level, id) == want, std::string(id) + (want ? " loaded" : " not loaded"));
    }
    auto result = level.getRecipes().getFurnaceRecipeResult(
        lp::game::makeItem("lepton:raw_gold_bricks"),
        HashedString("blast_furnace")
    );
    bool want = st::recipeEnabled("lepton:raw_gold_bricks_blast");
    ctx.check(
        (lp::game::itemId(result) == "minecraft:gold_block") == want,
        "blast furnace: raw gold bricks -> gold block"
    );
    auto creative = level.getItemRegistry().getCreativeItemRegistry();
    ctx.require(creative.get() != nullptr, "creative registry");
    bool lamp = false, bricks = false;
    for (auto const& entry : creative->mCreativeItems.get()) {
        auto id  = lp::game::itemId(entry.mItemInstance.get());
        lamp    |= id == "lepton:stone_lamp";
        bricks  |= id == "lepton:diorite_bricks";
    }
    ctx.check(lamp == st::blockEnabled("lepton:stone_lamp"), "stone lamp in the creative inventory iff enabled");
    ctx.check(
        bricks == st::blockEnabled("lepton:diorite_bricks"),
        "diorite bricks in the creative inventory iff enabled"
    );
});

// A piston pushes stone but not sturdy stone.
LP_REGISTER_SELFTEST("stones.sturdy", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(200);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto stoneCol  = off(base, 0, 1, 2);
    auto sturdyCol = off(base, 3, 1, 2);
    for (auto col : {stoneCol, sturdyCol}) {
        ctx.require(
            put(ctx,
                col,
                "minecraft:piston",
                {
                    {"facing_direction", 1}
        }),
            "piston placed"
        );
        ctx.cleanupBlock(off(col, 0, 1, 0));
        ctx.cleanupBlock(off(col, 0, 2, 0));
        ctx.cleanupBlock(off(col, 0, 3, 0));
    }
    ctx.require(put(ctx, off(stoneCol, 0, 1, 0), "minecraft:stone"), "stone placed");
    ctx.require(put(ctx, off(sturdyCol, 0, 1, 0), "lepton:sturdy_stone"), "sturdy stone placed");
    co_await ctx.ticks(4);
    ctx.require(put(ctx, off(stoneCol, 1, 0, 0), "minecraft:redstone_block"), "power");
    ctx.require(put(ctx, off(sturdyCol, 1, 0, 0), "minecraft:redstone_block"), "power");
    co_await ctx.ticks(20);
    ctx.check(
        idAt(ctx, off(stoneCol, 0, 2, 0)) == "minecraft:stone",
        "the piston pushed stone: " + idAt(ctx, off(stoneCol, 0, 2, 0))
    );
    ctx.check(
        idAt(ctx, off(sturdyCol, 0, 1, 0)) == "lepton:sturdy_stone",
        "sturdy stone stays: " + idAt(ctx, off(sturdyCol, 0, 1, 0))
    );
    ctx.check(idAt(ctx, off(sturdyCol, 0, 2, 0)) == "minecraft:air", "nothing pushed above");
    // remove the power first so no piston arm is left behind
    ctx.check(
        put(ctx, off(stoneCol, 1, 0, 0), "minecraft:air") && put(ctx, off(sturdyCol, 1, 0, 0), "minecraft:air"),
        "power off"
    );
    co_await ctx.ticks(6);
});

// Lamps take lepton:light = lightLevel when placed.
LP_REGISTER_SELFTEST("stones.lamp", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    auto        base = ctx.testPos(210);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    lp::config::mut<st::StonesConfig>().lightLevel = 7;
    auto a                                         = off(base, 0, 1, 0);
    ctx.require(put(ctx, a, "lepton:stone_lamp"), "lamp placed");
    co_await ctx.ticks(3);
    ctx.check(bs::getInt(at(ctx, a), "lepton:light") == 7, "light follows lightLevel 7");
    lp::config::mut<st::StonesConfig>().lightLevel = 15;
    auto b                                         = off(base, 2, 1, 0);
    ctx.require(
        put(ctx,
            b,
            "lepton:stone_brick_lamp",
            {
                {"lepton:light", 4}
    }),
        "lamp placed"
    );
    co_await ctx.ticks(3);
    ctx.check(bs::getInt(at(ctx, b), "lepton:light") == 15, "light follows lightLevel 15");
    ctx.check(bs::getInt(at(ctx, a), "lepton:light") == 7, "earlier lamps keep their light");
});

// Pillars lie along the clicked face's axis.
LP_REGISTER_SELFTEST("stones.pillar", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(220);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto q = off(base, 0, 1, 2);
    ctx.require(put(ctx, q, "minecraft:stone"), "stone placed");
    ctx.cleanupBlock(off(q, 1, 0, 0));
    ctx.cleanupBlock(off(q, 0, 1, 0));
    co_await ctx.ticks(10);
    auto sim  = [&] { return static_cast<SimulatedPlayer*>(&ctx.player()); }; // never kept across ticks
    auto item = lp::game::makeItem("lepton:granite_pillar", 4);
    ctx.check(
        sim()->simulateUseItemOnBlock(item, lp::game::blockPos(q), Facing::East, Vec3{1.f, 0.5f, 0.5f}),
        "used the pillar"
    );
    co_await ctx.ticks(3);
    auto face = bs::getString(at(ctx, off(q, 1, 0, 0)), "minecraft:block_face").value_or("?");
    ctx.check(
        idAt(ctx, off(q, 1, 0, 0)) == "lepton:granite_pillar" && (face == "east" || face == "west"),
        "pillar on the x axis: " + face
    );
    co_await ctx.ticks(10);
    auto item2 = lp::game::makeItem("lepton:granite_pillar", 4);
    ctx.check(
        sim()->simulateUseItemOnBlock(item2, lp::game::blockPos(q), Facing::Up, Vec3{0.5f, 1.f, 0.5f}),
        "used the pillar"
    );
    co_await ctx.ticks(3);
    face = bs::getString(at(ctx, off(q, 0, 1, 0)), "minecraft:block_face").value_or("?");
    ctx.check(face == "up" || face == "down", "pillar upright: " + face);
});

// Raw metal bricks smelt 9x slower than raw iron; XP 6.3 / 9.
LP_REGISTER_SELFTEST("stones.furnace", [](Context& ctx) -> ll::coro::CoroTask<> {
    if (!st::recipeEnabled("lepton:raw_iron_bricks_smelt")) {
        ctx.info("smelting disabled by config: skipped");
        co_return;
    }
    auto xp = [](char const* id) { return FurnaceBlockActor::_getXPRewardMultiplier(lp::game::makeItem(id)); };
    ctx.check(std::abs(xp("lepton:raw_iron_bricks") - 6.3f) < 0.01f, "raw iron bricks XP 6.3");
    ctx.check(std::abs(xp("minecraft:gold_block") - 9.f) < 0.01f, "gold block result XP 9");
    ctx.check(std::abs(xp("minecraft:copper_block") - 6.3f) < 0.01f, "copper block result XP 6.3");
    auto base = ctx.testPos(230);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto bricksFurnace = off(base, 0, 1, 2);
    auto ironFurnace   = off(base, 2, 1, 2);
    ctx.require(
        put(ctx, bricksFurnace, "minecraft:furnace") && put(ctx, ironFurnace, "minecraft:furnace"),
        "furnaces placed"
    );
    co_await ctx.ticks(2);
    auto furnaceAt = [&](GlobalPos const& p) -> FurnaceBlockActor* {
        auto* a = regionOf(ctx).getBlockEntity(lp::game::blockPos(p));
        return a && a->getType() == BlockActorType::Furnace ? static_cast<FurnaceBlockActor*>(a) : nullptr;
    };
    // empty the furnaces however the test ends, so the cleanup drops no items in the test area
    struct EmptyFurnaces {
        std::function<void()> fn;
        ~EmptyFurnaces() { fn(); }
    } emptyFurnaces{[&] {
        for (auto const& p : {bricksFurnace, ironFurnace})
            if (auto* f = furnaceAt(p))
                for (int slot = 0; slot < 3; ++slot) f->setItem(slot, ItemStack{});
    }};
    auto* f1 = furnaceAt(bricksFurnace);
    auto* f2 = furnaceAt(ironFurnace);
    ctx.require(f1 && f2, "furnace block actors");
    f1->setItem(0, lp::game::makeItem("lepton:raw_iron_bricks", 1));
    f1->setItem(1, lp::game::makeItem("minecraft:coal", 4));
    f2->setItem(0, lp::game::makeItem("minecraft:raw_iron", 1));
    f2->setItem(1, lp::game::makeItem("minecraft:coal", 4));
    co_await ctx.ticks(90);
    f1 = furnaceAt(bricksFurnace);
    f2 = furnaceAt(ironFurnace);
    ctx.require(f1 && f2, "furnaces still there");
    int bricks = f1->mCookingProgress;
    int iron   = f2->mCookingProgress;
    ctx.info(
        "cooking progress after 90 ticks: raw iron bricks " + std::to_string(bricks) + ", raw iron "
        + std::to_string(iron)
    );
    ctx.check(iron >= 60, "raw iron cooks at the normal rate");
    ctx.check(bricks >= 5 && bricks <= 16, "raw iron bricks cook 9x slower");
    ctx.check(lp::game::isEmpty(f1->getItem(2)), "no output yet");
});

// Drops need a pickaxe; raw metal bricks a stone (raw gold: iron) tier pickaxe.
LP_REGISTER_SELFTEST("stones.loot", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(234);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    // the test's own simulated player (removed when the test ends) mines in survival
    ctx.player().setPlayerGameType(GameType::Survival);

    struct Case {
        char const* block;
        char const* tool;
        bool        drops;
    };
    std::vector<Case> const cases = {
        {"lepton:granite_bricks",       "minecraft:stick",          false},
        {"lepton:granite_bricks",       "minecraft:wooden_pickaxe", true },
        {"lepton:raw_iron_bricks",      "minecraft:wooden_pickaxe", false},
        {"lepton:raw_iron_bricks",      "minecraft:stone_pickaxe",  true },
        {"lepton:raw_gold_bricks",      "minecraft:stone_pickaxe",  false},
        {"lepton:raw_gold_bricks",      "minecraft:iron_pickaxe",   true },
        {"lepton:raw_gold_bricks_wall", "minecraft:wooden_pickaxe", true },
        {"lepton:sturdy_stone",         "minecraft:wooden_pickaxe", true },
    };
    int i = 0;
    for (auto const& c : cases) {
        auto p = off(base, 2 * i++, 1, 2);
        ctx.require(put(ctx, p, c.block), std::string("placed ") + c.block);
        ctx.player().setSelectedItem(lp::game::makeItem(c.tool));
        co_await ctx.ticks(2);
        // what survival mining runs at the end: drops from the loot table with the held tool, then air
        at(ctx, p).playerDestroy(ctx.player(), lp::game::blockPos(p));
        bool destroyed = lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(p));
        co_await ctx.ticks(3);
        auto drops = dropsAt(ctx, p, c.block);
        ctx.check(destroyed && idAt(ctx, p) == "minecraft:air", std::string(c.block) + " broken with " + c.tool);
        ctx.check(
            drops.empty() != c.drops,
            std::string(c.block) + (c.drops ? " drops" : " drops nothing") + " with " + c.tool
        );
        for (auto* a : drops) lp::game::entities::despawn(*a);
    }
});

// Every block of the unit in rows, for screenshots (client scenario `stones`). With a real player
// online, the player is moved in front of the rows (views logged as "STONES GALLERY view <name>").
LP_REGISTER_SELFTEST("stones.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(250, 0);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    std::vector<std::vector<std::string>> rows;
    for (auto const* s : {"granite", "diorite", "andesite", "calcite", "dripstone"}) {
        std::string b = std::string(s) + "_bricks";
        rows.push_back(
            {"lepton:" + b,
             "lepton:" + b + "_stairs",
             "lepton:" + b + "_slab",
             "lepton:" + b + "_vertical_slab",
             "lepton:" + b + "_wall",
             "lepton:chiseled_" + b,
             std::string("lepton:") + s + "_pillar"}
        );
    }
    for (auto const* s : {"calcite", "dripstone"}) {
        std::string raw = std::string(s) == "calcite" ? "calcite" : "dripstone_block";
        std::string pol = std::string("polished_") + s;
        rows.push_back(
            {"lepton:" + pol,
             "lepton:" + pol + "_stairs",
             "lepton:" + pol + "_slab",
             "lepton:" + pol + "_vertical_slab",
             "lepton:" + raw + "_stairs",
             "lepton:" + raw + "_slab",
             "lepton:" + raw + "_vertical_slab",
             "lepton:" + raw + "_wall"}
        );
    }
    for (auto const* m : {"iron", "gold", "copper"}) {
        std::string b = std::string("raw_") + m + "_bricks";
        rows.push_back(
            {"lepton:" + b,
             "lepton:" + b + "_stairs",
             "lepton:" + b + "_slab",
             "lepton:" + b + "_vertical_slab",
             "lepton:" + b + "_wall"}
        );
    }
    rows.push_back({"lepton:sturdy_stone", "lepton:stone_lamp", "lepton:stone_brick_lamp"});
    // U2 owns x = dx 200..299 and one 5-block z row: up to 3 lines (z +4, +2, +0) of up to 40 blocks,
    // one block gap between materials.
    std::vector<std::string> flat;
    for (auto const& row : rows) {
        flat.insert(flat.end(), row.begin(), row.end());
        flat.emplace_back();
    }
    int placed = 0, x = 0, line = 0;
    for (std::size_t i = 0; i < flat.size(); ++i) {
        auto const& id = flat[i];
        if (id.empty()) {
            if (x > 0) ++x;
            continue;
        }
        std::size_t len = 0; // keep a material on one line
        while (i + len < flat.size() && !flat[i + len].empty()) ++len;
        if (x > 0 && (i == 0 || flat[i - 1].empty()) && x + static_cast<int>(len) > 40) {
            ++line;
            x = 0;
        }
        bs::Changes ch;
        if (endsWith(id, "_stairs"))
            ch = {
                {"minecraft:cardinal_direction", "north"}
            };
        if (endsWith(id, "_vertical_slab"))
            ch = {
                {"lepton:type", "north"}
            };
        auto p = off(base, x++, 0, 4 - 2 * line);
        if (put(ctx, p, id, ch)) ++placed;
        else ctx.check(false, "placed " + id);
    }
    ctx.check(line <= 2, "the gallery fits in 3 lines");
    ctx.check(placed == st::blockCount(), "every block placed: " + std::to_string(placed));

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](float dx, float dy, float dz, float pitch, float yaw) {
        if (auto* v = ctx.player().getLevel().getPlayer(uid)) {
            v->teleport(
                Vec3{static_cast<float>(base.x) + dx, static_cast<float>(base.y) + dy, static_cast<float>(base.z) + dz},
                DimensionType(base.dim),
                Vec2{pitch, yaw}
            );
        }
    };
    // Close views: line 0 (stone bricks, z +4) from the south, line 1 (polished, raw metal, sturdy
    // stone, lamps; z +2) from the north, each in three parts of 13 blocks; then the lamps at night.
    auto& level    = ctx.player().getLevel();
    int   timeWas  = level.getTime();
    auto  showView = [&](std::string const& name) -> ll::coro::CoroTask<> {
        ctx.info("STONES GALLERY view " + name);
        co_await ctx.ticks(100);
    };
    for (int part = 0; part < 3; ++part) {
        view(6.5f + 13.f * part, 1.f, 10.5f, 20.f, 180.f); // looking north
        co_await showView("bricks-" + std::to_string(part + 1));
    }
    for (int part = 0; part < 3; ++part) {
        view(6.5f + 13.f * part, 1.f, -4.5f, 20.f, 0.f); // looking south
        co_await showView("line2-" + std::to_string(part + 1));
    }
    level.setTime(18000); // midnight: the lamps (x 37, 38) light up the grass around them
    view(37.5f, 2.f, -2.5f, 35.f, 0.f);
    co_await showView("lamps-night");
    level.setTime(timeWas);
    ctx.info("STONES GALLERY done");
});
