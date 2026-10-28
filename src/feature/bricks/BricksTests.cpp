// Selftests of U4 bricks (dx 400..499). See docs/spec/bricks.md §8.

#include <string>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/monster/EnderDragon.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/GameType.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/AttributeModifier.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/bricks/Bricks.h"
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
namespace bricks = lp::feature::bricks;
using lp::core::GlobalPos;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

bool put(Context& ctx, GlobalPos const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}

GlobalPos off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

std::string idAt(Context& ctx, GlobalPos const& p) { return lp::game::blockIdAt(regionOf(ctx), lp::game::blockPos(p)); }

bool infiniburn(Block const& b) {
    return (static_cast<uint64>(b.getBlockType().mProperties) & static_cast<uint64>(BlockProperty::InfiniBurn)) != 0;
}

bool isShapePath(std::string_view p) {
    return p.ends_with("_stairs") || p.ends_with("_slab") || p.ends_with("_wall");
}
} // namespace

// Every block and item of the content table is registered (the behavior pack loaded them all).
LP_REGISTER_SELFTEST("bricks.content", [](Context& ctx) {
    int blocks = 0;
    for (auto const& [path, group] : bricks::contentItems()) {
        std::string id = "lepton:" + std::string(path);
        if (path == "moss_paste") {
            ctx.check(!lp::game::makeItem(id).isNull(), "moss paste item exists");
            continue;
        }
        auto const* b = bs::of(id);
        ctx.check(b != nullptr, "block " + id + " registered");
        if (b) ++blocks;
        ctx.check(!lp::game::makeItem(id).isNull(), "block item " + id + " exists");
    }
    ctx.info("blocks registered: " + std::to_string(blocks));
    ctx.check(blocks == 71, "71 blocks (Quark's families incl. vertical slabs), got " + std::to_string(blocks));
    // shapes carry the generator tags, so game/Shapes gives them their native behaviour
    auto const* stairs = bs::of("lepton:soul_sandstone_bricks_stairs");
    ctx.check(stairs && lp::game::shapes::kindOf(*stairs) == lp::game::shapes::Kind::Stairs, "brick stairs are lepton:stairs");
    auto const* wall = bs::of("lepton:dirt_bricks_wall");
    ctx.check(wall && lp::game::shapes::kindOf(*wall) == lp::game::shapes::Kind::Wall, "brick walls are lepton:wall");
    auto const* pillar = bs::of("lepton:midori_pillar", {{"minecraft:block_face", "east"}});
    ctx.check(pillar && pillar->getBlockType().hasTag(HashedString("lepton:pillar")), "midori pillar is an axis pillar");
    auto const* mud = bs::of("lepton:mud_pillar", {{"minecraft:block_face", "north"}});
    ctx.check(mud != nullptr, "mud pillar takes an axis");
    for (char const* s : {"lepton:soul_sandstone", "lepton:soul_sandstone_wall", "lepton:soul_sandstone_bricks_wall", "lepton:cut_soul_sandstone_vertical_slab"}) {
        auto const* b = bs::of(s);
        ctx.check(b && b->getBlockType().hasTag(HashedString("lepton:soul_speed")) && b->getBlockType().hasTag(HashedString("lepton:soul_fire_base")), std::string(s) + " is a soul speed / soul fire block");
    }
    for (char const* s : {"lepton:duskbound_block", "lepton:duskbound_lantern", "lepton:duskbound_block_vertical_slab"}) {
        auto const* b = bs::of(s);
        ctx.check(b && b->getBlockType().hasTag(HashedString("lepton:dragon_immune")), std::string(s) + " is dragon immune");
    }
    for (auto const& [path, group] : bricks::contentItems())
        if (isShapePath(path)) ctx.check(!group.empty(), "shape has a group");
});

// The config gate (pure logic): Quark's option dependencies and recipe flags.
LP_REGISTER_SELFTEST("bricks.gate", [](Context& ctx) {
    bricks::Config    c;
    bricks::Externals e;
    e.duskyMyalite = false;
    ctx.check(bricks::itemEnabled(c, e, "blackstone_bricks_wall"), "defaults: everything enabled");
    ctx.check(bricks::recipeEnabled(c, e, "duskbound_block_without_myalite"), "no myalite: the obsidian recipe");
    ctx.check(!bricks::recipeEnabled(c, e, "duskbound_block"), "no myalite: no myalite recipe");
    e.duskyMyalite = true;
    ctx.check(bricks::recipeEnabled(c, e, "duskbound_block") && !bricks::recipeEnabled(c, e, "duskbound_block_without_myalite"), "myalite present: the myalite recipe only");
    e.spiralSpires = false;
    ctx.check(!bricks::recipeEnabled(c, e, "duskbound_block") && bricks::recipeEnabled(c, e, "duskbound_block_without_myalite"), "SpiralSpires off: obsidian recipe");

    c.enableCobblestoneBricks = false;
    for (char const* p : {"cobblestone_bricks", "mossy_cobblestone_bricks_slab", "blackstone_bricks", "dirt_bricks_stairs", "netherrack_bricks_wall"})
        ctx.check(!bricks::itemEnabled(c, e, p), std::string(p) + " needs cobblestone bricks");
    ctx.check(bricks::itemEnabled(c, e, "blue_nether_bricks"), "blue nether bricks do not need cobblestone bricks");
    c = {};
    c.enableSandstoneBricks = false;
    ctx.check(!bricks::itemEnabled(c, e, "soul_sandstone_bricks"), "soul sandstone bricks need sandstone bricks");
    ctx.check(bricks::itemEnabled(c, e, "soul_sandstone_wall"), "soul sandstone wall block stays");
    ctx.check(!bricks::recipeEnabled(c, e, "soul_sandstone_wall"), "Quark's soul sandstone wall recipe carries the sandstone_bricks flag");
    ctx.check(bricks::recipeEnabled(c, e, "soul_sandstone_slab"), "other soul sandstone recipes stay");
    c = {};
    c.enableSoulSandstone = false;
    ctx.check(!bricks::itemEnabled(c, e, "soul_sandstone_bricks") && !bricks::itemEnabled(c, e, "smooth_soul_sandstone"), "SoulSandstone off");
    ctx.check(!bricks::recipeEnabled(c, e, "smooth_soul_sandstone"), "no smelting recipe without the module");
    c = {};
    c.enableMoreBrickTypes = false;
    ctx.check(!bricks::itemEnabled(c, e, "sandstone_bricks") && bricks::itemEnabled(c, e, "soul_sandstone"), "MoreBrickTypes off");
    c = {};
    c.enableMidori = false;
    ctx.check(!bricks::itemEnabled(c, e, "moss_paste") && !bricks::recipeEnabled(c, e, "moss_paste"), "Midori off: no moss paste");
    c = {};
    e.verticalSlabs = false;
    ctx.check(!bricks::itemEnabled(c, e, "midori_block_vertical_slab") && !bricks::recipeEnabled(c, e, "midori_block_vertical_slab"), "vertical slabs follow U1's module");
    ctx.check(bricks::itemEnabled(c, e, "midori_block_slab"), "normal slabs stay");
    ctx.check(bricks::recipeEnabled(c, e, "not_ours"), "foreign recipes untouched");
});

// The recipes of the default config are loaded; the disabled duskbound variant is not.
LP_REGISTER_SELFTEST("bricks.recipes", [](Context& ctx) {
    auto& recipes = ctx.player().getLevel().getRecipes();
    auto  has     = [&](char const* tag, std::string const& id) {
        auto const& byTag = recipes.mRecipes.get();
        auto        it    = byTag.find(HashedString(tag));
        return it != byTag.end() && it->second.contains(id);
    };
    auto const& cfg = lp::config::get<bricks::Config>();
    auto        ext = bricks::currentExternals();
    int         checked = 0;
    for (auto const& [name, result] : bricks::contentRecipes()) {
        std::string id = "lepton:" + std::string(name);
        bool want = bricks::recipeEnabled(cfg, ext, name);
        bool got  = has("crafting_table", id) || has("stonecutter", id) || has("furnace", id);
        ctx.check(want == got, id + (want ? " should be loaded" : " should be skipped"));
        ++checked;
    }
    ctx.info("recipes checked: " + std::to_string(checked) + ", skipped by the gate: " + std::to_string(bricks::gateStats().recipesSkipped));
});

// Fire lit on soul sandstone becomes soul fire and stays (soul_fire_base_blocks).
LP_REGISTER_SELFTEST("bricks.soul_fire", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(400);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "lepton:soul_sandstone_bricks"), "placed soul sandstone bricks");
    ctx.cleanupBlock(off(p, 0, 1, 0));
    ctx.require(put(ctx, off(p, 0, 1, 0), "minecraft:fire"), "lit a fire on top");
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:soul_fire", "the fire turned into soul fire, got " + idAt(ctx, off(p, 0, 1, 0)));
    co_await ctx.ticks(40);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:soul_fire", "the soul fire survives on soul sandstone");
    lp::game::setBlock(regionOf(ctx), lp::game::blockPos(off(p, 0, 1, 0)), *bs::of("minecraft:air"));
});

// Netherrack bricks keep fire burning forever (infiniburn_overworld).
LP_REGISTER_SELFTEST("bricks.infiniburn", [](Context& ctx) {
    for (char const* id : {"lepton:netherrack_bricks", "lepton:netherrack_bricks_slab", "lepton:netherrack_bricks_wall"}) {
        auto const* b = bs::of(id);
        ctx.check(b && infiniburn(*b), std::string(id) + " is infiniburn");
    }
    auto const* other = bs::of("lepton:blackstone_bricks");
    ctx.check(other && !infiniburn(*other), "other bricks are not");
});

// Soul Speed boots speed the wearer up on soul sandstone, and stop doing so off it.
LP_REGISTER_SELFTEST("bricks.soul_speed", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(410);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& pl = ctx.player();
    ctx.require(put(ctx, p, "lepton:soul_sandstone"), "placed soul sandstone");
    ctx.require(put(ctx, off(p, 1, 0, 0), "minecraft:stone"), "placed stone");
    auto boots = lp::game::makeItem("minecraft:iron_boots");
    EnchantmentInstance ench{};
    ench.mEnchantType = Enchant::Type::SoulSpeed;
    ench.mLevel       = 3;
    EnchantUtils::applyEnchant(boots, ench, true);
    ctx.require(EnchantUtils::getEnchantLevel(Enchant::Type::SoulSpeed, boots) == 3, "boots enchanted");
    pl.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, boots);
    auto ours = [&] {
        auto attr = pl.getMutableAttribute(SharedAttributes::MOVEMENT_SPEED());
        auto* inst = attr.mInstance->mPtr;
        if (!inst) return 0.f;
        for (auto const& m : inst->mModifierList.get())
            if (m.mName.get() == bricks::kSoulSpeedModifier) return m.mAmount;
        return 0.f;
    };
    pl.teleport(Vec3{p.x + 0.5f, p.y + 1.f, p.z + 0.5f}, DimensionType(p.dim));
    co_await ctx.ticks(20);
    float bonus = ours();
    ctx.check(bonus > 0.06f && bonus < 0.065f, "soul speed III bonus 0.0615 on soul sandstone, got " + std::to_string(bonus));
    pl.teleport(Vec3{p.x + 1.5f, p.y + 1.f, p.z + 0.5f}, DimensionType(p.dim));
    co_await ctx.ticks(20);
    ctx.check(ours() == 0.f, "no bonus on stone");
    pl.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, ItemStack{});
});

// The ender dragon does not break duskbound blocks (dragon_immune).
LP_REGISTER_SELFTEST("bricks.dragon_immune", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(420);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto q = off(p, 0, 8, 0);
    ctx.require(put(ctx, q, "lepton:duskbound_block"), "placed a duskbound block");
    ctx.require(put(ctx, off(q, 1, 0, 0), "lepton:duskbound_lantern"), "placed a duskbound lantern");
    auto* actor = lp::game::entities::spawn(regionOf(ctx), "minecraft:ender_dragon", Vec3{p.x + 0.5f, p.y + 40.f, p.z + 0.5f});
    ctx.require(actor != nullptr, "spawned an ender dragon");
    auto& dragon = static_cast<EnderDragon&>(*actor);
    AABB  box{Vec3{(float)q.x, (float)q.y, (float)q.z}, Vec3{q.x + 1.9f, q.y + 0.9f, q.z + 0.9f}};
    bool  blocked = dragon._checkWalls(box);
    actor->despawn();
    ctx.check(blocked, "the dragon is blocked by duskbound blocks");
    ctx.check(idAt(ctx, q) == "lepton:duskbound_block", "duskbound block survives, got " + idAt(ctx, q));
    ctx.check(idAt(ctx, off(q, 1, 0, 0)) == "lepton:duskbound_lantern", "duskbound lantern survives");
});

// A row of every full block and some shapes for the client screenshots (scenario "bricks").
LP_REGISTER_SELFTEST("bricks.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(440, -2);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    int x = 0;
    for (auto const& [path, group] : bricks::contentItems()) {
        if (path == "moss_paste" || isShapePath(path)) continue;
        ctx.check(put(ctx, off(base, x, 0, 0), "lepton:" + std::string(path)), "placed " + std::string(path));
        x += 1;
    }
    int y = 1;
    for (auto const& [id, st] : std::vector<std::pair<char const*, bs::Changes>>{
             {"lepton:cobblestone_bricks_stairs",      {{"minecraft:cardinal_direction", "north"}}},
             {"lepton:sandstone_bricks_slab",          {}                                          },
             {"lepton:soul_sandstone_wall",            {{"lepton:post", true}}                     },
             {"lepton:midori_block_vertical_slab",     {{"lepton:type", "north"}}                  },
             {"lepton:duskbound_block_stairs",         {{"minecraft:cardinal_direction", "north"}}},
             {"lepton:blue_nether_bricks_wall",        {{"lepton:post", true}}                     },
             {"lepton:mud_pillar",                     {{"minecraft:block_face", "east"}}          },
             {"lepton:midori_pillar",                  {{"minecraft:block_face", "up"}}            },
         }) {
        ctx.check(put(ctx, off(base, 2 * y, 1, 0), id, st), std::string("placed ") + id);
        ++y;
    }
    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& pl) {
        if (!pl.isSimulated()) viewer = &pl;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](float dx, float dz, float yaw) {
        if (auto* v = ctx.player().getLevel().getPlayer(uid))
            v->teleport(Vec3{base.x + dx, base.y + 1.f, base.z + dz}, DimensionType(base.dim), Vec2{20.f, yaw});
    };
    ctx.info("BRICKS view west");
    view(5.f, 7.f, 180.f);
    co_await ctx.ticks(150);
    ctx.info("BRICKS view middle");
    view(14.f, 7.f, 180.f);
    co_await ctx.ticks(150);
    ctx.info("BRICKS done");
});

// requiresCorrectToolForDrops: the blocks drop only when mined with their tool (match_tool in the loot).
LP_REGISTER_SELFTEST("bricks.tool_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(430);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& pl = ctx.player();
    pl.setPlayerGameType(GameType::Survival);
    auto drops = [&](GlobalPos const& at) {
        AABB box{Vec3{at.x - 1.f, at.y - 1.f, at.z - 1.f}, Vec3{at.x + 2.f, at.y + 2.f, at.z + 2.f}};
        return lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item");
    };
    struct Case {
        int         dx;
        char const* block;
        char const* tool;
        bool        drops;
    };
    for (auto const& c : std::vector<Case>{
             {0, "lepton:cobblestone_bricks", "minecraft:iron_pickaxe", true },
             {3, "lepton:cobblestone_bricks", "minecraft:stick",        false},
             {6, "lepton:dirt_bricks",        "minecraft:iron_shovel",  true },
             {9, "lepton:dirt_bricks",        "minecraft:iron_pickaxe", false},
         }) {
        auto at = off(p, c.dx, 0, 0);
        ctx.require(put(ctx, at, c.block), std::string("placed ") + c.block);
        pl.setSelectedItem(lp::game::makeItem(c.tool));
        pl.teleport(Vec3{at.x + 0.5f, at.y + 1.f, at.z + 1.5f}, DimensionType(at.dim));
        // what a survival break does with the drops: the block's loot with the player's tool
        regionOf(ctx).getBlock(lp::game::blockPos(at)).playerDestroy(pl, lp::game::blockPos(at));
        lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(at));
        co_await ctx.ticks(3);
        auto items = drops(at);
        ctx.check(idAt(ctx, at) == "minecraft:air", std::string(c.block) + " broken");
        ctx.check(items.empty() != c.drops, std::string(c.block) + " with " + c.tool + (c.drops ? " drops" : " drops nothing"));
        for (auto* a : items) a->despawn();
    }
});