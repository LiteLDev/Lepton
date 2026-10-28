// Selftests of U5 palette (docs/spec/palette.md §8). dx range 500..599.

#include <cmath>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/GetCollisionShapeActorProxy.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/GetCollisionShapeInterface.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/levelgen/structure/StructurePieceType.h"
#include "mc/world/phys/AABB.h"

#include "feature/palette/Palette.h"
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
namespace pal    = lp::feature::palette;
using Facing     = ScriptModuleMinecraft::ScriptFacing;
using lp::core::GlobalPos;

constexpr char const* kColors[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                   "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

bool put(Context& ctx, GlobalPos const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}
Block const& at(Context& ctx, GlobalPos const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }
std::string  idAt(Context& ctx, GlobalPos const& p) { return lp::game::blockIdAt(regionOf(ctx), lp::game::blockPos(p)); }
GlobalPos    off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}
/// Moves the test player 3 blocks east of p (out of the way of blocks placed at / above p).
void stepAside(Context& ctx, GlobalPos const& p, int dx = 3) {
    ctx.player().teleport(Vec3{static_cast<float>(p.x) + dx + 0.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, DimensionType(p.dim));
}
Vec3 center(GlobalPos const& p, float dy = 0.f) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + dy, static_cast<float>(p.z) + 0.5f};
}

/// Restores the palette config after a test changed it.
struct ConfigGuard {
    pal::PaletteConfig saved = lp::config::get<pal::PaletteConfig>();
    ~ConfigGuard() { lp::config::mut<pal::PaletteConfig>() = saved; }
};

std::vector<std::string> allBlockIds() {
    std::vector<std::string> ids;
    auto shapeSet = [&](std::string const& base) {
        ids.push_back("lepton:" + base);
        for (auto s : {"_stairs", "_slab", "_vertical_slab"}) ids.push_back("lepton:" + base + s);
    };
    shapeSet("shingles");
    ids.push_back("lepton:framed_glass");
    ids.push_back("lepton:framed_glass_pane");
    for (auto c : kColors) {
        shapeSet(std::string(c) + "_shingles");
        ids.push_back("lepton:" + std::string(c) + "_framed_glass");
        ids.push_back("lepton:" + std::string(c) + "_framed_glass_pane");
    }
    shapeSet("iron_plate");
    shapeSet("rusty_iron_plate");
    shapeSet("thatch");
    for (auto id : {"gold_bars", "iron_pillar", "iron_ladder", "paper_lantern", "paper_lantern_sakura", "paper_wall",
                    "paper_wall_big", "paper_wall_sakura", "bamboo_mat", "bamboo_mat_carpet", "nether_brick_fence_gate", "grate"})
        ids.push_back(std::string("lepton:") + id);
    return ids;
}

Recipe const* findRecipe(Level& level, std::string const& id) {
    for (auto const& [tag, byId] : level.getRecipes().mRecipes.get()) {
        if (auto it = byId.find(id); it != byId.end()) return it->second.get();
    }
    return nullptr;
}

struct FakeProxy { // the layout of GetCollisionShapeActorProxy
    void**       vft;
    Actor const* actor;
};
AABB engineShape(BlockSource& region, BlockPos const& pos, Actor const& actor) {
    FakeProxy proxy{GetCollisionShapeActorProxy::$vftable(), &actor};
    auto const& block = region.getBlock(pos);
    return block.getBlockType().getCollisionShape(block, region, pos, reinterpret_cast<GetCollisionShapeInterface const&>(proxy));
}
} // namespace

LP_REGISTER_SELFTEST("palette.blocks", [](Context& ctx) {
    int missing = 0;
    for (auto const& id : allBlockIds()) {
        if (!bs::of(id)) {
            ++missing;
            ctx.check(false, "block registered: " + id);
        }
        ctx.check(pal::partOf(id).has_value(), "id table knows " + id);
    }
    ctx.require(missing == 0, "every palette block is registered (behavior pack loaded)");
    ctx.check(bs::of("lepton:thatch")->hasTag(HashedString("lepton:thatch")), "thatch carries lepton:thatch");
    ctx.check(!bs::of("lepton:thatch_slab")->hasTag(HashedString("lepton:thatch")), "thatch slab does not cushion falls");
    ctx.check(shapes::kindOf(*bs::of("lepton:red_shingles_stairs")) == shapes::Kind::Stairs, "shingle stairs are generated stairs");
    ctx.check(shapes::kindOf(*bs::of("lepton:gold_bars")) == shapes::Kind::Pane, "gold bars are a pane");
    ctx.check(shapes::kindOf(*bs::of("lepton:paper_wall")) == shapes::Kind::Pane, "paper walls are panes");
    ctx.check(shapes::kindOf(*bs::of("lepton:nether_brick_fence_gate")) == shapes::Kind::FenceGate, "nether brick fence gate is a gate");
    ctx.check(shapes::kindOf(*bs::of("lepton:iron_pillar")) == shapes::Kind::Pillar, "iron pillar is a pillar");
    ctx.check(bs::of("lepton:bamboo_mat", {{"lepton:facing", "down"}}) != nullptr, "bamboo mat has the facing state");
});

LP_REGISTER_SELFTEST("palette.config", [](Context& ctx) {
    pal::PaletteConfig d{};
    ctx.check(d.enableShingles && d.enableFramedGlass && d.enableGoldBars && d.enableIndustrialPalette, "modules on by default");
    ctx.check(d.enableJapanesePalette && d.enableNetherBrickFenceGate && d.enableThatch && d.enableGrate, "modules on by default (2)");
    ctx.check(d.generateInNetherFortress && d.enableIronPlates && d.enableIronLadder, "industrial / gold bars options");
    ctx.check(d.enablePaperBlocks && d.enableBambooMats, "japanese palette options");
    ctx.check(d.fallDamageMultiplier == 0.5, "thatch multiplier 0.5");
    ConfigGuard guard;
    lp::config::mut<pal::PaletteConfig>().fallDamageMultiplier = 3.0;
    ctx.check(pal::thatchMultiplier() == 1.0, "multiplier clamped to 1");
    lp::config::mut<pal::PaletteConfig>().enableIronPlates = false;
    ctx.check(!pal::enabled(pal::Part::IronPlates) && pal::enabled(pal::Part::IronLadder), "options switch parts separately");
    lp::config::mut<pal::PaletteConfig>().enableIronPlates  = true;
    lp::config::mut<pal::PaletteConfig>().enableIndustrialPalette = false;
    ctx.check(!pal::enabled(pal::Part::IronPlates) && !pal::enabled(pal::Part::IronLadder), "the module switch disables both");
});

LP_REGISTER_SELFTEST("palette.recipes", [](Context& ctx) {
    auto& level = ctx.player().getLevel();
    struct Expect {
        char const* recipe;
        char const* result;
        int         count;
    };
    std::vector<Expect> const expects = {
        {"lepton:red_shingles",                    "lepton:red_shingles",      2 },
        {"lepton:red_shingles_dye",                "lepton:red_shingles",      8 },
        {"lepton:framed_glass",                    "lepton:framed_glass",      8 },
        {"lepton:framed_glass_pane",               "lepton:framed_glass_pane", 16},
        {"lepton:gold_bars",                       "lepton:gold_bars",         16},
        {"lepton:iron_plate",                      "lepton:iron_plate",        24},
        {"lepton:rusty_iron_plate_from_iron_plate", "lepton:rusty_iron_plate", 8 },
        {"lepton:iron_ladder",                     "lepton:iron_ladder",       3 },
        {"lepton:paper_wall",                      "lepton:paper_wall",        6 },
        {"lepton:bamboo_mat_carpet",               "lepton:bamboo_mat_carpet", 4 },
        {"lepton:thatch",                          "lepton:thatch",            4 },
        {"lepton:grate",                           "lepton:grate",             1 },
        {"lepton:nether_brick_fence_gate",         "lepton:nether_brick_fence_gate", 1},
    };
    for (auto const& e : expects) {
        auto const* r = findRecipe(level, e.recipe);
        if (!r) {
            ctx.check(false, std::string("recipe loaded: ") + e.recipe);
            continue;
        }
        auto const& results = r->getResultItems();
        ctx.check(!results.empty() && lp::game::itemId(results.front()) == e.result, std::string("result of ") + e.recipe);
        if (!results.empty())
            ctx.check(results.front().mCount == e.count, std::string("count of ") + e.recipe + " = " + std::to_string(results.front().mCount));
    }
    auto const* thatch = findRecipe(level, "lepton:thatch");
    ctx.require(thatch != nullptr, "thatch recipe loaded");
    auto  netId   = thatch->mRecipeNetId.get();
    auto& recipes = level.getRecipes();
    ctx.check(recipes.getRecipeByNetId(netId) == thatch, "an enabled recipe is served");
    ConfigGuard guard;
    lp::config::mut<pal::PaletteConfig>().enableThatch = false;
    ctx.check(recipes.getRecipeByNetId(netId) == nullptr, "a disabled module's recipe is refused");
    lp::config::mut<pal::PaletteConfig>().enableThatch = true;
    ctx.check(recipes.getRecipeByNetId(netId) == thatch, "re-enabled");
});

LP_REGISTER_SELFTEST("palette.place_disabled", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(500);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "minecraft:stone"), "stone placed");
    auto above = off(p, 0, 1, 0);
    ctx.cleanupBlock(above);
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(above));
    stepAside(ctx, p);
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    ConfigGuard guard;
    lp::config::mut<pal::PaletteConfig>().enableShingles = false;
    auto stack = lp::game::makeItem("lepton:shingles", 4);
    ctx.give(*sp, stack, 0);
    co_await ctx.ticks(10);
    sp->simulateUseItemOnBlock(stack, lp::game::blockPos(p), Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, above) != "lepton:shingles", "a disabled block is not placed (got " + idAt(ctx, above) + ")");
    lp::config::mut<pal::PaletteConfig>().enableShingles = true;
    co_await ctx.ticks(10);
    sp->simulateUseItemOnBlock(stack, lp::game::blockPos(p), Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, above) == "lepton:shingles", "an enabled block is placed (got " + idAt(ctx, above) + ")");
});

LP_REGISTER_SELFTEST("palette.shapes", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(505);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = regionOf(ctx);
    ctx.require(put(ctx, p, "lepton:framed_glass_pane") && put(ctx, off(p, 1, 0, 0), "lepton:red_framed_glass_pane"), "panes placed");
    ctx.require(put(ctx, off(p, 0, 0, 2), "lepton:gold_bars") && put(ctx, off(p, 1, 0, 2), "minecraft:iron_bars"), "bars placed");
    ctx.require(put(ctx, off(p, 0, 0, 4), "lepton:nether_brick_fence_gate", {{"minecraft:cardinal_direction", "north"}}), "gate placed");
    co_await ctx.ticks(3);
    shapes::updateShape(region, lp::game::blockPos(p));
    shapes::updateShape(region, lp::game::blockPos(off(p, 0, 0, 2)));
    ctx.check(bs::getBool(at(ctx, p), "lepton:east").value_or(false), "framed glass panes connect");
    ctx.check(bs::getBool(at(ctx, off(p, 0, 0, 2)), "lepton:east").value_or(false), "gold bars connect to iron bars");
    ctx.check(shapes::setOpen(region, lp::game::blockPos(off(p, 0, 0, 4)), true), "the nether brick gate opens");
    ctx.check(bs::getBool(at(ctx, off(p, 0, 0, 4)), "lepton:open").value_or(false), "gate is open");
});

LP_REGISTER_SELFTEST("palette.thatch_fall", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(515);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto q = off(p, 3, 0, 0);
    ctx.require(put(ctx, p, "lepton:thatch") && put(ctx, q, "minecraft:stone"), "landing blocks placed");
    for (int dy = 1; dy <= 12; ++dy) {
        ctx.cleanupBlock(off(p, 0, dy, 0));
        ctx.cleanupBlock(off(q, 0, dy, 0));
        lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(off(p, 0, dy, 0)));
        lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(off(q, 0, dy, 0)));
    }
    auto& a = ctx.spawnPlayer("palette_fall_a");
    auto& b = ctx.spawnPlayer("palette_fall_b");
    a.setPlayerGameType(GameType::Survival);
    b.setPlayerGameType(GameType::Survival);
    co_await ctx.ticks(5);
    int ha = a.getHealth(), hb = b.getHealth();
    a.teleport(center(p, 11.f), DimensionType(p.dim));
    b.teleport(center(q, 11.f), DimensionType(q.dim));
    co_await ctx.ticks(60);
    int da = ha - a.getHealth(), db = hb - b.getHealth();
    ctx.info("fall damage: thatch " + std::to_string(da) + ", stone " + std::to_string(db));
    ctx.check(db >= 5, "a 10 block fall onto stone hurts (" + std::to_string(db) + ")");
    ctx.check(da < db, "thatch cushions the fall (" + std::to_string(da) + " < " + std::to_string(db) + ")");
    ctx.check(da <= (db + 1) / 2 + 1, "thatch halves the damage (" + std::to_string(da) + ")");
});

LP_REGISTER_SELFTEST("palette.thatch_compost", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(520);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "minecraft:composter"), "composter placed");
    auto* sp    = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  stack = lp::game::makeItem("lepton:thatch", 8);
    ctx.give(*sp, stack, 0);
    sp->simulateSetItem(stack, true, 0);
    co_await ctx.ticks(10);
    sp->simulateUseItemOnBlock(stack, lp::game::blockPos(p), Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(5);
    int level = bs::getInt(at(ctx, p), "composter_fill_level").value_or(-1);
    ctx.check(level == 1, "composting thatch into an empty composter always raises it (level " + std::to_string(level) + ")");
});

LP_REGISTER_SELFTEST("palette.grate_items", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(525);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto g = off(p, 0, 3, 0);
    for (int dy = 0; dy <= 6; ++dy) {
        ctx.cleanupBlock(off(p, 0, dy, 0));
        lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(off(p, 0, dy, 0)));
    }
    ctx.require(put(ctx, p, "minecraft:stone") && put(ctx, g, "lepton:grate"), "grate placed above a gap");
    stepAside(ctx, p, 8);
    auto& region = regionOf(ctx);
    auto* item   = ctx.player().getLevel().getSpawner().spawnItem(region, lp::game::makeItem("minecraft:stick"), nullptr, center(g, 1.5f), 200);
    ctx.require(item != nullptr, "item spawned");
    auto id = lp::game::entities::idOf(*item);
    co_await ctx.ticks(40);
    auto* back = lp::game::entities::find(id);
    ctx.require(back != nullptr, "item still exists");
    float y = back->getPosition().y;
    ctx.check(y < static_cast<float>(g.y) + 0.5f, "the item fell through the grate (y " + std::to_string(y - g.y) + ")");
    lp::game::entities::despawn(*back);
    // a player stands on it
    auto& sp = ctx.spawnPlayer("palette_grate");
    sp.teleport(center(g, 1.2f), DimensionType(g.dim));
    co_await ctx.ticks(30);
    float py = sp.getFeetPos().y;
    ctx.check(py > static_cast<float>(g.y) + 0.9f, "a player stands on the grate (feet " + std::to_string(py - g.y) + ")");
});

LP_REGISTER_SELFTEST("palette.grate_animals", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(535);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "lepton:grate"), "grate placed");
    auto& region = regionOf(ctx);
    auto  pos    = lp::game::blockPos(p);
    auto* pig    = lp::game::entities::spawn(region, "minecraft:pig", center(off(p, 3, 0, 0)));
    ctx.require(pig != nullptr, "pig spawned");
    auto pigId = lp::game::entities::idOf(*pig);
    co_await ctx.ticks(2);
    pig = lp::game::entities::find(pigId);
    ctx.require(pig != nullptr, "pig loaded");
    auto tall = pal::grateCollision(region, pos, pig);
    ctx.check(tall.max.y > static_cast<float>(p.y) + 1.5f, "an unleashed pig gets a tall box (" + std::to_string(tall.max.y - p.y) + ")");
    auto viaEngine = engineShape(region, pos, *pig);
    ctx.check(viaEngine.max.y > static_cast<float>(p.y) + 1.5f, "the engine's collision query sees the tall box (" + std::to_string(viaEngine.max.y - p.y) + ")");
    auto& player = ctx.player();
    auto  plate  = engineShape(region, pos, player);
    ctx.check(std::abs(plate.max.y - (static_cast<float>(p.y) + 1.f)) < 0.01f && std::abs(plate.min.y - (static_cast<float>(p.y) + 15.f / 16.f)) < 0.01f, "players get the top plate");
    pig->setLeashHolder(player.getOrCreateUniqueID());
    if (pig->isLeashed()) {
        auto leashed = pal::grateCollision(region, pos, pig);
        ctx.check(leashed.max.y <= static_cast<float>(p.y) + 1.01f, "a leashed pig may walk on it");
    } else {
        ctx.info("leash holder not accepted without a lead entity; leashed case skipped");
    }
    lp::game::entities::despawn(*pig);
    auto* item = ctx.player().getLevel().getSpawner().spawnItem(region, lp::game::makeItem("minecraft:stick"), nullptr, center(p, 2.f), 0);
    ctx.require(item != nullptr, "item spawned");
    auto none = engineShape(region, pos, *item);
    ctx.check(none.max.y <= none.min.y, "items get no collision");
    lp::game::entities::despawn(*item);
});

LP_REGISTER_SELFTEST("palette.grate_water", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(545);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "lepton:grate"), "grate placed");
    auto& region = regionOf(ctx);
    auto const* water = bs::of("minecraft:water");
    ctx.require(water != nullptr, "water block");
    ctx.check(region.setExtraBlock(lp::game::blockPos(p), *water, 3), "water can be put into the grate");
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, p) == "lepton:grate", "the grate stays");
    ctx.check(region.getLiquidBlock(lp::game::blockPos(p)).getTypeName().find("water") != std::string::npos, "the grate holds water");
    region.setExtraBlock(lp::game::blockPos(p), *bs::of("minecraft:air"), 3);
});

LP_REGISTER_SELFTEST("palette.ladder", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(550);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = regionOf(ctx);
    auto  ladder = off(p, 0, 0, -1); // north of the stone, hanging on it
    ctx.require(put(ctx, p, "minecraft:stone"), "stone placed");
    ctx.require(put(ctx, ladder, "lepton:iron_ladder", {{"minecraft:block_face", "north"}}), "ladder placed");
    ctx.check(pal::ladderSupported(region, lp::game::blockPos(ladder)), "the ladder hangs on the stone");
    ctx.check(at(ctx, ladder).hasProperty(BlockProperty::Climbable), "the iron ladder is climbable");
    lp::game::clearBlock(region, lp::game::blockPos(p));
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, ladder) != "lepton:iron_ladder", "the ladder breaks without support (" + idAt(ctx, ladder) + ")");
    // placed by a player against the side of a block
    ctx.require(put(ctx, p, "minecraft:stone"), "stone placed again");
    auto* sp    = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  stack = lp::game::makeItem("lepton:iron_ladder", 4);
    ctx.give(*sp, stack, 0);
    co_await ctx.ticks(10);
    sp->simulateUseItemOnBlock(stack, lp::game::blockPos(p), Facing::North, Vec3{0.5f, 0.5f, 0.f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, ladder) == "lepton:iron_ladder", "a player places the ladder on a side (" + idAt(ctx, ladder) + ")");
    ctx.check(bs::getString(at(ctx, ladder), "minecraft:block_face").value_or("") == "north", "it faces away from the stone");
});

LP_REGISTER_SELFTEST("palette.bamboo_mat", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(560);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& region = regionOf(ctx);
    auto  pos    = lp::game::blockPos(p);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            ctx.cleanupBlock(off(p, dx, 0, dz));
            lp::game::clearBlock(region, lp::game::blockPos(off(p, dx, 0, dz)));
        }
    ctx.check(pal::bambooMatFacing(region, pos, "lepton:bamboo_mat", 180.f, 0.f) == "north", "looking north faces north");
    ctx.check(pal::bambooMatFacing(region, pos, "lepton:bamboo_mat", 90.f, 0.f) == "west", "looking west faces west");
    ctx.check(pal::bambooMatFacing(region, pos, "lepton:bamboo_mat", 0.f, 80.f) == "down", "looking down faces down");
    ctx.require(put(ctx, off(p, 0, 0, -1), "lepton:bamboo_mat", {{"lepton:facing", "north"}}), "mat in front");
    ctx.check(pal::bambooMatFacing(region, pos, "lepton:bamboo_mat", 180.f, 0.f) == "south", "a mat in front facing the same way flips it");
    ctx.require(put(ctx, off(p, 0, 0, 1), "lepton:bamboo_mat", {{"lepton:facing", "south"}}), "mat behind");
    ctx.check(pal::bambooMatFacing(region, pos, "lepton:bamboo_mat", 180.f, 0.f) == "north", "a mat behind facing back keeps it");
    // carpet support
    auto q = off(p, 3, 0, 0);
    ctx.require(put(ctx, q, "minecraft:stone") && put(ctx, off(q, 0, 1, 0), "lepton:bamboo_mat_carpet"), "carpet on stone");
    lp::game::clearBlock(region, lp::game::blockPos(q));
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, off(q, 0, 1, 0)) != "lepton:bamboo_mat_carpet", "the carpet breaks without a block below");
    // placed by a player: facing fixed up on the next tick
    ctx.require(put(ctx, q, "minecraft:stone"), "stone again");
    auto* sp    = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  stack = lp::game::makeItem("lepton:bamboo_mat", 4);
    ctx.give(*sp, stack, 0);
    co_await ctx.ticks(10);
    sp->simulateUseItemOnBlock(stack, lp::game::blockPos(q), Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(4);
    auto const& placed = at(ctx, off(q, 0, 1, 0));
    if (lp::game::blockId(placed) == "lepton:bamboo_mat") {
        auto rot    = sp->getRotation();
        auto expect = pal::bambooMatFacing(region, lp::game::blockPos(off(q, 0, 1, 0)), "lepton:bamboo_mat", rot.y, rot.x);
        ctx.check(bs::getString(placed, "lepton:facing").value_or("") == expect, "placement facing follows the player's look (" + expect + ")");
    } else {
        ctx.check(false, "a player places a bamboo mat (" + lp::game::blockId(placed) + ")");
    }
});

LP_REGISTER_SELFTEST("palette.fortress_bars", [](Context& ctx) {
    auto const* fence = bs::of("minecraft:nether_brick_fence");
    auto const* bars  = bs::of("lepton:gold_bars");
    ctx.require(fence && bars, "blocks exist");
    auto straight = static_cast<int>(StructurePieceType::NetherFortressStraight);
    auto corridor = static_cast<int>(StructurePieceType::NetherCastleSmallCorridor);
    auto const* r = pal::testing::fortressReplacement(straight, *fence);
    ctx.check(r && lp::game::blockId(*r) == "lepton:gold_bars", "fortress fences become gold bars");
    ctx.check(pal::testing::fortressReplacement(corridor, *fence) != nullptr, "in every fortress piece");
    ctx.check(pal::testing::fortressReplacement(0, *fence) == nullptr, "other structures keep their fences");
    ctx.check(pal::testing::fortressReplacement(straight, *bs::of("minecraft:nether_brick")) == nullptr, "other blocks are kept");
    ConfigGuard guard;
    lp::config::mut<pal::PaletteConfig>().generateInNetherFortress = false;
    ctx.check(pal::testing::fortressReplacement(straight, *fence) == nullptr, "generateInNetherFortress=false keeps fences");
    lp::config::mut<pal::PaletteConfig>().generateInNetherFortress = true;
    lp::config::mut<pal::PaletteConfig>().enableGoldBars           = false;
    ctx.check(pal::testing::fortressReplacement(straight, *fence) == nullptr, "a disabled module keeps fences");
});

// A gallery of every block for the client screenshots (scenario `palette`). With a real player
// online, the player is moved in front of the rows (three views, 7.5 s each).
LP_REGISTER_SELFTEST("palette.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(580, -3);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    struct Item {
        int         dx, dy, dz;
        std::string id;
        bs::Changes states;
    };
    std::vector<Item> items;
    // row 0: shingles (block + stairs on top)
    std::vector<std::string> shingles = {"shingles"};
    for (auto c : kColors) shingles.push_back(std::string(c) + "_shingles");
    for (int i = 0; i < static_cast<int>(shingles.size()); ++i) {
        items.push_back({i, 0, 0, "lepton:" + shingles[i], {}});
        items.push_back({i, 1, 0, "lepton:" + shingles[i] + "_stairs", {{"minecraft:cardinal_direction", "north"}}});
    }
    // row 1 (z+2): framed glass + panes
    std::vector<std::string> glass = {"framed_glass"};
    for (auto c : kColors) glass.push_back(std::string(c) + "_framed_glass");
    for (int i = 0; i < static_cast<int>(glass.size()); ++i) {
        items.push_back({i, 0, 2, "lepton:" + glass[i], {}});
        items.push_back({i, 1, 2, "lepton:" + glass[i] + "_pane", {}});
    }
    // row 2 (z+4): metal and the rest
    std::vector<Item> misc = {
        {0,  0, 4, "lepton:iron_plate",               {}                                          },
        {0,  1, 4, "lepton:iron_plate_slab",          {}                                          },
        {1,  0, 4, "lepton:rusty_iron_plate",         {}                                          },
        {1,  1, 4, "lepton:rusty_iron_plate_vertical_slab", {{"lepton:type", "north"}}            },
        {2,  0, 4, "lepton:iron_pillar",              {{"minecraft:block_face", "up"}}            },
        {2,  1, 4, "lepton:iron_pillar",              {{"minecraft:block_face", "east"}}          },
        {3,  0, 4, "lepton:gold_bars",                {}                                          },
        {4,  0, 4, "lepton:gold_bars",                {}                                          },
        {5,  0, 4, "minecraft:stone",                 {}                                          },
        {5,  0, 5, "lepton:iron_ladder",              {{"minecraft:block_face", "south"}}         },
        {5,  1, 4, "minecraft:stone",                 {}                                          },
        {5,  1, 5, "lepton:iron_ladder",              {{"minecraft:block_face", "south"}}         },
        {6,  0, 4, "lepton:paper_lantern",            {}                                          },
        {7,  0, 4, "lepton:paper_lantern_sakura",     {}                                          },
        {8,  0, 4, "lepton:paper_wall",               {}                                          },
        {9,  0, 4, "lepton:paper_wall_big",           {}                                          },
        {10, 0, 4, "lepton:paper_wall_sakura",        {}                                          },
        {11, 0, 4, "lepton:bamboo_mat",               {{"lepton:facing", "north"}}                },
        {12, 0, 4, "lepton:bamboo_mat",               {{"lepton:facing", "east"}}                 },
        {13, 0, 4, "lepton:bamboo_mat",               {{"lepton:facing", "down"}}                 },
        {14, 0, 4, "lepton:bamboo_mat_carpet",        {{"lepton:facing", "north"}}                },
        {15, 0, 4, "lepton:nether_brick_fence_gate",  {{"minecraft:cardinal_direction", "north"}} },
        {16, 0, 4, "lepton:thatch",                   {}                                          },
        {16, 1, 4, "lepton:thatch_stairs",            {{"minecraft:cardinal_direction", "north"}} },
        {17, 1, 4, "lepton:grate",                    {}                                          },
        {17, 0, 4, "minecraft:water",                 {}                                          },
    };
    items.insert(items.end(), misc.begin(), misc.end());
    // a floor under the gallery so carpets and ladders have support
    for (int x = -1; x <= 18; ++x)
        for (int z = -1; z <= 6; ++z) put(ctx, off(base, x, -1, z), "minecraft:smooth_stone");
    for (auto const& it : items) ctx.check(put(ctx, off(base, it.dx, it.dy, it.dz), it.id, it.states), "placed " + it.id);
    co_await ctx.ticks(4);
    auto& region = regionOf(ctx);
    for (auto const& it : items) shapes::updateShape(region, lp::game::blockPos(off(base, it.dx, it.dy, it.dz)));

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& pl) {
        if (!pl.isSimulated()) viewer = &pl;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](float dx, float dy, float dz, float yaw, float pitch) {
        if (auto* v = ctx.player().getLevel().getPlayer(uid)) {
            v->teleport(Vec3{static_cast<float>(base.x) + dx, static_cast<float>(base.y) + dy, static_cast<float>(base.z) + dz},
                        DimensionType(base.dim), Vec2{pitch, yaw});
        }
    };
    ctx.info("PALETTE view shingles");
    view(8.5f, 1.f, -5.f, 0.f, 15.f); // north of row 0, looking south
    co_await ctx.ticks(150);
    ctx.info("PALETTE view glass");
    view(8.5f, 3.f, -3.f, 0.f, 35.f);
    co_await ctx.ticks(150);
    ctx.info("PALETTE view misc");
    view(9.f, 2.f, 10.f, 180.f, 20.f); // south of row 2, looking north
    co_await ctx.ticks(150);
    ctx.info("PALETTE view misc-close");
    view(13.f, 1.5f, 7.f, 180.f, 25.f);
    co_await ctx.ticks(150);
    ctx.info("PALETTE done");
});
