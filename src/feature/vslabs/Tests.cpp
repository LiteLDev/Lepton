// U1 vslabs selftests (dx 100..199).

#include <cmath>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "ll/api/service/Bedrock.h"

#include "feature/vslabs/VSlabs.h"
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
namespace vs     = lp::feature::vslabs;
using Facing     = ScriptModuleMinecraft::ScriptFacing;
using GP         = lp::core::GlobalPos;

/// Every vertical slab U1 owns (vanilla materials).
std::vector<std::string> const& allIds() {
    static std::vector<std::string> const ids = [] {
        std::vector<std::string> v;
        for (auto m :
             {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "pale_oak", "bamboo",
              "bamboo_mosaic", "crimson", "warped", "poplar", "stone", "smooth_stone", "cobblestone",
              "mossy_cobblestone", "stone_brick", "mossy_stone_brick", "granite", "polished_granite", "diorite",
              "polished_diorite", "andesite", "polished_andesite", "cobbled_deepslate", "polished_deepslate",
              "deepslate_brick", "deepslate_tile", "tuff", "polished_tuff", "tuff_brick", "brick", "mud_brick",
              "resin_brick", "sandstone", "cut_sandstone", "smooth_sandstone", "red_sandstone", "cut_red_sandstone",
              "smooth_red_sandstone", "quartz", "smooth_quartz", "nether_brick", "red_nether_brick", "blackstone",
              "polished_blackstone", "polished_blackstone_brick", "end_stone_brick", "purpur", "prismarine",
              "prismarine_brick", "dark_prismarine", "cinnabar", "polished_cinnabar", "cinnabar_brick", "sulfur",
              "polished_sulfur", "sulfur_brick"}) {
            v.push_back(std::string("lepton:") + m + "_vertical_slab");
        }
        for (auto kind : {"_concrete", "_wool"}) {
            for (auto c : {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray",
                           "cyan", "purple", "blue", "brown", "green", "red", "black"}) {
                v.push_back(std::string("lepton:") + c + kind + "_vertical_slab");
            }
        }
        for (int age = 0; age < 4; ++age) {
            v.push_back(vs::copperSlabId(age, false));
            v.push_back(vs::copperSlabId(age, true));
        }
        return v;
    }();
    return ids;
}

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }
Block const& at(Context& ctx, GP const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }
std::string  idAt(Context& ctx, GP const& p) { return lp::game::blockIdAt(regionOf(ctx), lp::game::blockPos(p)); }
std::string  str(Block const& b, char const* state) { return bs::getString(b, state).value_or("?"); }

GP off(GP p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

bool put(Context& ctx, GP const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}

bool use(Context& ctx, std::string_view id, GP const& p, Facing face, Vec3 facePos) {
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  s  = lp::game::makeItem(id, 16);
    sp->setSelectedItem(s);
    bool ok = sp->simulateUseItemOnBlock(s, lp::game::blockPos(p), face, facePos);
    ctx.check(ok, "the simulated player could use " + std::string(id));
    return ok;
}

void stepAside(Context& ctx, GP const& p) {
    ctx.player().teleport(
        Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 3.5f},
        DimensionType(p.dim)
    );
}

/// Restores the config after a test that changes it.
struct ConfigGuard {
    vs::Config saved = lp::config::get<vs::Config>();
    ~ConfigGuard() {
        lp::config::mut<vs::Config>() = saved;
        vs::reapplyRecipes();
    }
};
} // namespace

LP_REGISTER_SELFTEST("vslabs.blocks", [](Context& ctx) {
    for (auto const& id : allIds()) {
        auto const* b = bs::of(id, {{"lepton:type", "double"}});
        if (!b) {
            ctx.check(false, id + " is registered with lepton:type");
            continue;
        }
        ctx.check(vs::isVerticalSlab(*b) && vs::isVerticalSlabId(id), id + " carries the lepton:vertical_slab tag");
        auto parent = vs::parentSlabOf(id);
        ctx.check(parent.has_value(), id + " has a vanilla parent slab");
        if (parent) ctx.check(!lp::game::makeItem(*parent).isNull(), id + ": parent item " + *parent + " exists");
        ctx.check(!lp::game::makeItem(id).isNull(), id + " has an item");
    }
    ctx.check(vs::parentSlabOf("lepton:stone_vertical_slab") == "minecraft:normal_stone_slab", "stone -> normal_stone_slab");
    ctx.check(!vs::parentSlabOf("lepton:u0_probe_stone_vertical_slab"), "other units' vertical slabs have no vanilla parent");
    ctx.check(vs::copperAge("minecraft:copper_block") == 0 && vs::copperAge("minecraft:weathered_cut_copper") == 2, "vanilla copper ages");
    ctx.check(!vs::copperAge("minecraft:waxed_cut_copper") && !vs::copperAge("minecraft:copper_ore"), "waxed / ore are not weathering");
    ctx.check(vs::copperAge(vs::copperSlabId(3, false)) == 3 && !vs::copperAge(vs::copperSlabId(1, true)), "slab ages");
});

LP_REGISTER_SELFTEST("vslabs.place_and_merge", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(100);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, off(p, 0, 0, 4));
    ctx.require(put(ctx, p, "minecraft:stone"), "stone placed");
    auto v = off(p, 0, 0, -1);
    ctx.cleanupBlock(v);
    co_await ctx.ticks(10);
    use(ctx, "lepton:oak_vertical_slab", p, Facing::North, Vec3{0.5f, 0.5f, 0.f});
    co_await ctx.ticks(3);
    ctx.require(idAt(ctx, v) == "lepton:oak_vertical_slab", "placed against the north face: " + idAt(ctx, v));
    ctx.check(str(at(ctx, v), "lepton:type") == "north", "type = clicked face: " + str(at(ctx, v), "lepton:type"));
    co_await ctx.ticks(10);
    use(ctx, "lepton:oak_vertical_slab", v, Facing::North, Vec3{0.5f, 0.5f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(str(at(ctx, v), "lepton:type") == "double", "two vertical slabs merge: " + str(at(ctx, v), "lepton:type"));
    ctx.cleanupBlock(off(v, 0, 0, -1));
    // top face: the half of the face that was clicked (Quark: atan2 of the hit)
    auto t = off(p, 0, 1, 0);
    ctx.cleanupBlock(t);
    co_await ctx.ticks(10);
    use(ctx, "lepton:stone_brick_vertical_slab", p, Facing::Up, Vec3{0.5f, 1.f, 0.9f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, t) == "lepton:stone_brick_vertical_slab", "placed on the top face");
    ctx.check(str(at(ctx, t), "lepton:type") == "north", "top face, south half clicked: slab on the south half (type north): "
                                                               + str(at(ctx, t), "lepton:type"));
});

LP_REGISTER_SELFTEST("vslabs.connections", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    auto        p = ctx.testPos(110);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, off(p, 0, 0, 5));
    // type north: the slab fills the south half; back face south, sides east/west, open side north
    ctx.require(put(ctx, p, "lepton:andesite_vertical_slab", {{"lepton:type", "north"}}), "vertical slab placed");
    ctx.require(put(ctx, off(p, 1, 0, 0), "lepton:u0_probe_stone_wall"), "lepton wall east");
    ctx.require(put(ctx, off(p, -1, 0, 0), "minecraft:cobblestone_wall"), "vanilla wall west");
    ctx.require(put(ctx, off(p, 0, 0, 1), "lepton:u0_probe_stone_wall"), "lepton wall south (back)");
    ctx.require(put(ctx, off(p, 0, 0, -1), "lepton:u0_probe_stone_wall"), "lepton wall north (open side)");
    ctx.require(put(ctx, off(p, 2, 0, 1), "lepton:u0_probe_stone_fence"), "fence placeholder");
    co_await ctx.ticks(4);
    ctx.check(str(at(ctx, off(p, 1, 0, 0)), "lepton:west") != "none", "lepton wall connects to the side");
    ctx.check(str(at(ctx, off(p, 0, 0, 1)), "lepton:north") != "none", "lepton wall connects to the back");
    ctx.check(str(at(ctx, off(p, 0, 0, -1)), "lepton:south") == "none", "no connection to the open side");
    ctx.check(
        str(at(ctx, off(p, -1, 0, 0)), "wall_connection_type_east") != "none",
        "vanilla wall connects to the side: " + str(at(ctx, off(p, -1, 0, 0)), "wall_connection_type_east")
    );
    // a fence beside the slab: only the full back face counts (Quark changes walls and panes only)
    ctx.require(put(ctx, off(p, 1, 0, 1), "lepton:andesite_vertical_slab", {{"lepton:type", "west"}}), "slab west-open");
    co_await ctx.ticks(4);
    ctx.check(bs::getBool(at(ctx, off(p, 2, 0, 1)), "lepton:west").value_or(false),
              "fence connects to the back face");

    lp::config::mut<vs::Config>().allowSideConnections = false;
    shapes::queueUpdate(regionOf(ctx), lp::game::blockPos(p));
    put(ctx, off(p, -1, 0, 0), "minecraft:stone");
    put(ctx, off(p, -1, 0, 0), "minecraft:cobblestone_wall");
    co_await ctx.ticks(4);
    ctx.check(str(at(ctx, off(p, 1, 0, 0)), "lepton:west") == "none", "allowSideConnections=false: no side connection");
    ctx.check(str(at(ctx, off(p, 0, 0, 1)), "lepton:north") != "none", "allowSideConnections=false: back still connects");
    ctx.check(
        str(at(ctx, off(p, -1, 0, 0)), "wall_connection_type_east") == "none",
        "allowSideConnections=false: vanilla wall leaves the side"
    );
});

LP_REGISTER_SELFTEST("vslabs.waterlog", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(120);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, off(p, 0, 0, 4));
    ctx.require(put(ctx, p, "minecraft:stone"), "stone placed");
    auto v = off(p, 0, 0, -1);
    ctx.require(put(ctx, v, "minecraft:water"), "water source north of it");
    for (auto d : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, -1}}) put(ctx, off(v, d.first, 0, d.second), "minecraft:glass");
    put(ctx, off(v, 0, -1, 0), "minecraft:glass");
    co_await ctx.ticks(10);
    use(ctx, "lepton:prismarine_vertical_slab", p, Facing::North, Vec3{0.5f, 0.5f, 0.f});
    co_await ctx.ticks(3);
    ctx.require(idAt(ctx, v) == "lepton:prismarine_vertical_slab", "placed into water: " + idAt(ctx, v));
    auto& region = regionOf(ctx);
    auto  extra  = region.getExtraBlock(lp::game::blockPos(v)).getTypeName();
    ctx.check(extra.find("water") != std::string::npos, "the slab holds the water (waterlogged): " + extra);
    co_await ctx.ticks(10);
    use(ctx, "lepton:prismarine_vertical_slab", v, Facing::North, Vec3{0.5f, 0.5f, 0.5f});
    co_await ctx.ticks(4);
    ctx.check(str(at(ctx, v), "lepton:type") == "double", "merged");
    extra = region.getExtraBlock(lp::game::blockPos(v)).getTypeName();
    ctx.check(extra.find("water") == std::string::npos, "a double vertical slab holds no water: " + extra);
});

LP_REGISTER_SELFTEST("vslabs.copper_tools", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(130);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, off(p, 0, 0, 4));
    ctx.require(put(ctx, p, vs::copperSlabId(1, false), {{"lepton:type", "east"}}), "exposed copper slab placed");
    co_await ctx.ticks(10);
    use(ctx, "minecraft:honeycomb", p, Facing::Up, Vec3{0.25f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, p) == vs::copperSlabId(1, true), "honeycomb waxes: " + idAt(ctx, p));
    ctx.check(str(at(ctx, p), "lepton:type") == "east", "the type is kept");
    co_await ctx.ticks(10);
    use(ctx, "minecraft:iron_axe", p, Facing::Up, Vec3{0.25f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, p) == vs::copperSlabId(1, false), "an axe removes the wax: " + idAt(ctx, p));
    co_await ctx.ticks(10);
    use(ctx, "minecraft:iron_axe", p, Facing::Up, Vec3{0.25f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, p) == vs::copperSlabId(0, false), "an axe scrapes one stage: " + idAt(ctx, p));
});

LP_REGISTER_SELFTEST("vslabs.weathering", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(140);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = regionOf(ctx);
    ctx.require(put(ctx, p, vs::copperSlabId(0, false)), "copper slab placed");
    ctx.check(vs::weatherOnce(region, lp::game::blockPos(p), true), "an isolated slab ages");
    ctx.check(idAt(ctx, p) == vs::copperSlabId(1, false), "unaffected -> exposed: " + idAt(ctx, p));
    ctx.require(put(ctx, off(p, 0, 2, 0), "minecraft:copper_block"), "younger copper 2 blocks away");
    ctx.check(!vs::weatherOnce(region, lp::game::blockPos(p), true), "a younger neighbour holds it back");
    ctx.require(put(ctx, p, vs::copperSlabId(1, true)), "waxed slab");
    ctx.check(!vs::weatherOnce(region, lp::game::blockPos(p), true), "waxed copper never ages");
    ctx.require(put(ctx, p, vs::copperSlabId(3, false)), "oxidized slab");
    ctx.check(!vs::weatherOnce(region, lp::game::blockPos(p), true), "oxidized is the last stage");
    // the random walk of a lightning strike resets the attached slab
    ctx.require(put(ctx, off(p, 0, 2, 0), "minecraft:air"), "neighbour removed");
    ctx.require(put(ctx, p, vs::copperSlabId(2, false)), "weathered slab");
    ctx.require(put(ctx, off(p, 0, 1, 0), "minecraft:lightning_rod", {{"facing_direction", 1}}), "lightning rod on top");
    vs::clearCopperOnLightning(region, lp::game::blockPos(off(p, 0, 1, 0)));
    ctx.check(idAt(ctx, p) == vs::copperSlabId(0, false), "lightning on the rod clears the slab: " + idAt(ctx, p));
});

LP_REGISTER_SELFTEST("vslabs.copper_ticks", [](Context& ctx) -> ll::coro::CoroTask<> {
    // weathering slabs carry their own tick chain (custom blocks get no random ticks): placing one
    // queues a tick, every tick queues the next (one chain per block); waxed slabs don't tick.
    // Aging is off here (module disabled) so the slab keeps its stage.
    ConfigGuard guard;
    lp::config::mut<vs::Config>().enableVerticalSlabs = false;
    auto p = ctx.testPos(150, 10);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    vs::testing::setTickDelayMax(10);
    int before = vs::testing::copperTicks();
    put(ctx, p, vs::copperSlabId(1, false));
    put(ctx, off(p, 2, 0, 0), vs::copperSlabId(1, true));
    co_await ctx.ticks(200);
    int seen = vs::testing::copperTicks() - before;
    vs::testing::setTickDelayMax(8191);
    ctx.info("ticks in 200 game ticks (mean delay 5.5): " + std::to_string(seen));
    ctx.check(seen >= 20 && seen <= 60, "one tick chain for one slab (" + std::to_string(seen) + " ticks, ~36 expected)");
});

LP_REGISTER_SELFTEST("vslabs.fuel", [](Context& ctx) {
    auto burn = [](std::string_view id) {
        auto s = lp::game::makeItem(id);
        return s.isNull() || !s.mItem.get() ? -1.f : FurnaceBlockActor::getItemBurnDuration(*s.mItem.get(), 200.f);
    };
    float slab = burn("minecraft:oak_slab");
    ctx.check(slab > 0.f, "oak slabs burn");
    ctx.check(burn("lepton:oak_vertical_slab") == slab, "a wooden vertical slab burns like its slab: " + std::to_string(burn("lepton:oak_vertical_slab")));
    ctx.check(burn("lepton:crimson_vertical_slab") <= 0.f, "nether wood vertical slabs don't burn");
    ctx.check(burn("lepton:red_wool_vertical_slab") == burn("minecraft:red_wool_slab"), "wool vertical slabs burn like wool slabs");
    ctx.check(burn("lepton:stone_vertical_slab") <= 0.f, "stone vertical slabs don't burn");
});

LP_REGISTER_SELFTEST("vslabs.recipes", [](Context& ctx) {
    ConfigGuard guard;
    auto        level = ll::service::getLevel();
    ctx.require(level.has_value(), "level");
    auto& recipes = level->getRecipes();
    vs::reapplyRecipes();
    ctx.check(vs::hasRecipe(recipes, "lepton:oak_vertical_slab"), "3 slabs -> 3 vertical slabs");
    ctx.check(vs::hasRecipe(recipes, "lepton:oak_vertical_slab_revert"), "vertical slab -> slab");
    ctx.check(vs::hasRecipe(recipes, "lepton:stone_brick_vertical_slab_from_stone_stonecutting"), "stonecutter");
    auto table = vs::slabsToBlocksTable();
    auto has   = [&](std::string const& slab, std::string const& block) {
        for (auto const& [s, b] : table)
            if (s == slab && b == block) return true;
        return false;
    };
    ctx.check(has("minecraft:oak_slab", "minecraft:oak_planks"), "oak slabs -> oak planks");
    ctx.check(has("minecraft:normal_stone_slab", "minecraft:stone"), "stone slabs -> stone");
    ctx.check(has("minecraft:stone_brick_slab", "minecraft:stone_bricks"), "stone brick slabs -> stone bricks");
    ctx.check(has("minecraft:purpur_slab", "minecraft:purpur_block"), "purpur slabs -> purpur block (not the pillar)");
    ctx.check(has("minecraft:quartz_slab", "minecraft:quartz_block"), "quartz slabs -> quartz block");
    ctx.check(has("minecraft:light_gray_wool_slab", "minecraft:light_gray_wool"), "wool slabs -> wool");
    ctx.check(has("minecraft:white_concrete_slab", "minecraft:white_concrete"), "concrete slabs -> concrete");
    ctx.info("slabs-to-blocks recipes: " + std::to_string(table.size()));
    ctx.check(table.size() >= 100, "a recipe for every vanilla slab");
    // every vanilla slab with a crafting recipe has a vertical slab (Quark: one per vanilla slab)
    for (auto const& [s, b] : table) {
        if (!s.starts_with("minecraft:")) continue;
        auto path = s.substr(10, s.size() - 10 - 5); // minecraft:<path>_slab
        auto vid  = "lepton:" + (path == "normal_stone" ? std::string("stone") : path) + "_vertical_slab";
        ctx.check(vs::isVerticalSlabId(vid), s + " has a vertical slab " + vid);
    }
    ctx.check(vs::hasRecipe(recipes, "lepton:slabs_to_blocks_oak_slab"), "oak recipe present");

    lp::config::mut<vs::Config>().bannedSlabs = {"minecraft:oak_slab"};
    vs::applyRecipes(recipes);
    ctx.check(!vs::hasRecipe(recipes, "lepton:slabs_to_blocks_oak_slab"), "bannedSlabs removes a recipe");
    ctx.check(vs::hasRecipe(recipes, "lepton:slabs_to_blocks_spruce_slab"), "others stay");
    lp::config::mut<vs::Config>().enableSlabsToBlocks = false;
    vs::applyRecipes(recipes);
    ctx.check(vs::slabsToBlocksRecipes().empty(), "SlabsToBlocks off: no recipes");
    lp::config::mut<vs::Config>().enableVerticalSlabs = false;
    vs::applyRecipes(recipes);
    ctx.check(!vs::hasRecipe(recipes, "lepton:oak_vertical_slab"), "VerticalSlabs off: no vertical slab recipe");
    ctx.check(!vs::hasRecipe(recipes, "lepton:oak_vertical_slab_revert"), "VerticalSlabs off: no revert recipe");
    lp::config::mut<vs::Config>() = guard.saved;
    vs::applyRecipes(recipes);
    ctx.check(vs::hasRecipe(recipes, "lepton:oak_vertical_slab"), "re-enabled: recipe back");
    ctx.check(vs::hasRecipe(recipes, "lepton:slabs_to_blocks_oak_slab"), "re-enabled: slabs to blocks back");
});

// Client e2e gallery (tools/e2e/client/scenarios/vslabs.ps1): every vertical slab in rows of 18,
// alternating north / east / double, plus walls, panes, bars and fences against them and one in
// water. Logs "VSLABS view <n>" before each camera position.
LP_REGISTER_SELFTEST("vslabs.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(170, 0);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto const& ids = allIds();
    static constexpr char const* kTypes[3] = {"north", "east", "double"};
    for (size_t i = 0; i < ids.size(); ++i) {
        int col = static_cast<int>(i % 18), row = static_cast<int>(i / 18);
        ctx.check(put(ctx, off(base, col, 0, -row * 3), ids[i], {{"lepton:type", kTypes[i % 3]}}), "placed " + ids[i]);
    }
    // connections: a north-type slab with things on its back, sides and open side
    auto c = off(base, 4, 0, 4);
    put(ctx, c, "lepton:stone_brick_vertical_slab", {{"lepton:type", "north"}});
    put(ctx, off(c, -1, 0, 0), "minecraft:cobblestone_wall");
    put(ctx, off(c, 1, 0, 0), "minecraft:glass_pane");
    put(ctx, off(c, 0, 0, 1), "minecraft:oak_fence");
    put(ctx, off(c, 0, 0, -1), "minecraft:iron_bars");
    put(ctx, off(c, 3, 0, 0), "lepton:oak_vertical_slab", {{"lepton:type", "west"}});
    put(ctx, off(c, 4, 0, 0), "lepton:u0_probe_stone_wall");
    put(ctx, off(c, 3, 0, 1), "lepton:u0_probe_stone_pane");
    // waterlogged: a basin with a vertical slab in a source block
    auto w = off(base, 10, 0, 4);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) put(ctx, off(w, dx, -1, dz), "minecraft:stone");
    }
    put(ctx, off(w, -1, 0, 0), "minecraft:stone");
    put(ctx, off(w, 1, 0, 0), "minecraft:stone");
    put(ctx, off(w, 0, 0, -1), "minecraft:stone");
    put(ctx, off(w, 0, 0, 1), "minecraft:glass");
    put(ctx, w, "minecraft:water");
    co_await ctx.ticks(2);
    if (auto const* slab = bs::of("lepton:prismarine_vertical_slab", {{"lepton:type", "south"}})) {
        auto& region = regionOf(ctx);
        auto  pos    = lp::game::blockPos(w);
        auto  water  = &region.getBlock(pos);
        lp::game::setBlock(region, pos, *slab);
        if (water->getTypeName().find("water") != std::string::npos) region.setExtraBlock(pos, *water, 3);
    }
    co_await ctx.ticks(4);

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](float dx, float dy, float dz, float pitch, float yaw) {
        // an invisible floor, so the viewer doesn't fall from the viewpoint while the screenshot waits
        put(ctx,
            off(base,
                static_cast<int>(std::floor(dx)),
                static_cast<int>(std::floor(dy)) - 1,
                static_cast<int>(std::floor(dz))),
            "minecraft:barrier");
        if (auto* v = ctx.player().getLevel().getPlayer(uid)) {
            v->teleport(
                Vec3{static_cast<float>(base.x) + dx, static_cast<float>(base.y) + dy, static_cast<float>(base.z) + dz},
                DimensionType(base.dim),
                Vec2{pitch, yaw}
            );
        }
    };
    ctx.info("VSLABS view rows");
    view(9.f, 9.f, 8.f, 30.f, 180.f); // south, above, looking north over the six rows
    co_await ctx.ticks(150);
    ctx.info("VSLABS view connections");
    view(5.5f, 2.5f, 9.f, 25.f, 180.f);
    co_await ctx.ticks(150);
    ctx.info("VSLABS view water");
    view(10.5f, 2.5f, 8.f, 30.f, 180.f);
    co_await ctx.ticks(150);
    ctx.info("VSLABS view done");
});

LP_REGISTER_SELFTEST("vslabs.disabled", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    auto        p = ctx.testPos(160);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, off(p, 0, 0, 4));
    ctx.require(put(ctx, p, "minecraft:stone"), "stone placed");
    auto v = off(p, 0, 1, 0);
    ctx.cleanupBlock(v);
    lp::config::mut<vs::Config>().enableVerticalSlabs = false;
    co_await ctx.ticks(10);
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  s  = lp::game::makeItem("lepton:oak_vertical_slab", 4);
    sp->setSelectedItem(s);
    sp->simulateUseItemOnBlock(s, lp::game::blockPos(p), Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, v) != "lepton:oak_vertical_slab", "VerticalSlabs off: vertical slabs can't be placed");
});

