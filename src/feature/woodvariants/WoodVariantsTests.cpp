// Selftests of U6 woodvariants (docs/spec/woodvariants.md §8). U6 owns dx 600..699.

#include <cmath>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/ComposterBlock.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/woodvariants/WoodVariants.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
namespace bs = lp::game::blockstates;
namespace wv = lp::feature::woodvariants;
using Facing = ScriptModuleMinecraft::ScriptFacing;
using GP     = lp::core::GlobalPos;

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

bool put(Context& ctx, GP const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}

void clear(Context& ctx, GP const& p) { lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(p)); }

Block const& at(Context& ctx, GP const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }
std::string  idAt(Context& ctx, GP const& p) { return at(ctx, p).getTypeName(); }
std::string  str(Block const& b, char const* s) { return bs::getString(b, s).value_or("?"); }
bool         flag(Block const& b, char const* s) { return bs::getBool(b, s).value_or(false); }

GP off(GP p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

/// The test player uses `id` on the `face` of the block at `p`.
bool use(Context& ctx, std::string_view id, GP const& p, Facing face, Vec3 facePos) {
    auto* sp = static_cast<SimulatedPlayer*>(&ctx.player());
    auto  s  = lp::game::makeItem(id, 16);
    return sp->simulateUseItemOnBlock(s, lp::game::blockPos(p), face, facePos);
}

/// Moves the test player out of the way of the blocks around `p`.
void stepAside(Context& ctx, GP const& p) {
    ctx.player().teleport(
        Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 4.5f},
        DimensionType(p.dim)
    );
}

bool hasProperty(std::string_view id, BlockProperty prop) {
    auto const* b = bs::of(id);
    if (!b) return false;
    return (static_cast<uint64_t>(b->getBlockType().mProperties) & static_cast<uint64_t>(prop)) != 0;
}
} // namespace

LP_REGISTER_SELFTEST("woodvariants.blocks", [](Context& ctx) {
    int missing = 0;
    for (auto const& id : wv::allBlockIds()) {
        if (!bs::of(id)) {
            ++missing;
            ctx.check(false, "block registered: " + id);
        }
    }
    ctx.check(wv::allBlockIds().size() == 83, "83 blocks, got " + std::to_string(wv::allBlockIds().size()));
    ctx.check(missing == 0, std::to_string(missing) + " blocks missing");
    auto const* post = bs::of("lepton:oak_post");
    ctx.require(post != nullptr, "oak post");
    ctx.check(wv::moduleOf(*post) == "wooden_posts", "post module tag");
    ctx.check(wv::hasTag(*bs::of("lepton:spruce_bookshelf"), "lepton:bookshelf"), "bookshelf tag");
});

LP_REGISTER_SELFTEST("woodvariants.recipes", [](Context& ctx) {
    auto& c     = lp::config::mut<wv::WoodVariantsConfig>();
    auto  saved = c;
    ctx.check(!wv::recipeDisabled("lepton:spruce_bookshelf", "lepton:spruce_bookshelf"), "enabled module keeps recipes");
    c.enableVariantBookshelves = false;
    ctx.check(wv::recipeDisabled("lepton:spruce_bookshelf", "lepton:spruce_bookshelf"), "disabled bookshelves drop their recipe");
    ctx.check(wv::recipeDisabled("lepton:oak_bookshelf", "minecraft:bookshelf"), "and the oak one");
    ctx.check(wv::recipeDisabled("lepton:lectern_from_spruce_bookshelf", "minecraft:lectern"), "and the lectern one");
    ctx.check(!wv::recipeDisabled("lepton:oak_post", "lepton:oak_post"), "other modules keep theirs");
    c.enableHollowLogs = false;
    ctx.check(wv::recipeDisabled("lepton:hollow_oak_log", "lepton:hollow_oak_log"), "disabled hollow logs");
    ctx.check(!wv::recipeDisabled("minecraft:bookshelf", "minecraft:bookshelf"), "vanilla recipes untouched");
    c = saved;
    ctx.check(!wv::recipeDisabled("lepton:azalea_hedge_oak_from_oak_log", "lepton:azalea_hedge") || wv::azaleaWoodEnabled(),
        "azalea hedge from oak logs while AzaleaWood is off");
});

LP_REGISTER_SELFTEST("woodvariants.post_axis", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(600);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "minecraft:stone"), "stone placed");
    ctx.cleanupBlock(off(p, 1, 0, 0));
    stepAside(ctx, p);
    co_await ctx.ticks(2);
    ctx.check(use(ctx, "lepton:oak_post", p, Facing::East, Vec3{1.f, 0.5f, 0.5f}), "used a post on the east face");
    co_await ctx.ticks(3);
    auto const& b = at(ctx, off(p, 1, 0, 0));
    ctx.require(b.getTypeName() == "lepton:oak_post", "post placed, got " + b.getTypeName());
    ctx.check(wv::axisOf(b) == "x", "post along x, face " + str(b, "minecraft:block_face"));
});

LP_REGISTER_SELFTEST("woodvariants.post_connections", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(604);
    p.y += 3;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    ctx.require(put(ctx, p, "lepton:oak_post", {{"minecraft:block_face", "up"}}), "y post");
    ctx.require(put(ctx, off(p, 0, -1, 0), "minecraft:chain", {{"pillar_axis", "y"}}), "chain below");
    ctx.require(put(ctx, off(p, 0, 0, -1), "lepton:spruce_post", {{"minecraft:block_face", "north"}}), "z post north");
    ctx.require(put(ctx, off(p, 1, 0, 0), "lepton:birch_post", {{"minecraft:block_face", "up"}}), "y post east (wrong axis)");
    ctx.require(put(ctx, off(p, 0, 1, 0), "lepton:oak_hedge"), "hedge above");
    co_await ctx.ticks(4);
    auto const& b = at(ctx, p);
    ctx.check(str(b, "lepton:connect_down") == "chain", "chain below: " + str(b, "lepton:connect_down"));
    ctx.check(str(b, "lepton:connect_north") == "other_post", "post north: " + str(b, "lepton:connect_north"));
    ctx.check(str(b, "lepton:connect_east") == "none", "post on another axis: " + str(b, "lepton:connect_east"));
    ctx.check(str(b, "lepton:connect_up") == "other_post", "hedge above: " + str(b, "lepton:connect_up"));
    ctx.check(str(b, "lepton:connect_west") == "none", "nothing west");
    // a hanging lantern below instead of the chain
    ctx.require(put(ctx, off(p, 0, -1, 0), "minecraft:lantern", {{"hanging", true}}), "hanging lantern below");
    co_await ctx.ticks(4);
    ctx.check(str(at(ctx, p), "lepton:connect_down") == "chain", "hanging lantern counts as chain");
    clear(ctx, off(p, 0, -1, 0));
    co_await ctx.ticks(4);
    ctx.check(str(at(ctx, p), "lepton:connect_down") == "none", "connection removed");
});

LP_REGISTER_SELFTEST("woodvariants.post_strip", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(610);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    ctx.require(put(ctx, p, "lepton:oak_post", {{"minecraft:block_face", "east"}}), "x post");
    co_await ctx.ticks(2);
    ctx.check(use(ctx, "minecraft:iron_axe", p, Facing::Up, Vec3{0.5f, 0.6f, 0.5f}), "used an axe");
    co_await ctx.ticks(4);
    auto const& b = at(ctx, p);
    ctx.check(b.getTypeName() == "lepton:stripped_oak_post", "stripped: " + b.getTypeName());
    ctx.check(wv::axisOf(b) == "x", "axis kept");
});

LP_REGISTER_SELFTEST("woodvariants.post_lantern", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(614);
    p.y += 3;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    ctx.require(put(ctx, off(p, 0, 1, 0), "lepton:oak_post", {{"minecraft:block_face", "east"}}), "x post above");
    ctx.require(put(ctx, p, "minecraft:lantern", {{"hanging", true}}), "hanging lantern");
    auto& region = regionOf(ctx);
    auto  pos    = lp::game::blockPos(p);
    ctx.check(at(ctx, p).getBlockType().canSurvive(region, pos), "a hanging lantern survives below a post");
    ctx.check(hasProperty("lepton:oak_post", BlockProperty::CanSupportCenterHangingBlock), "post supports hanging blocks");
    clear(ctx, off(p, 0, 1, 0));
    ctx.check(!at(ctx, p).getBlockType().canSurvive(region, pos), "but not below air");
    co_return;
});

LP_REGISTER_SELFTEST("woodvariants.hedge_connections", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(620);
    p.y += 3;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    ctx.require(put(ctx, p, "lepton:oak_hedge"), "hedge");
    ctx.require(put(ctx, off(p, 1, 0, 0), "lepton:cherry_hedge"), "hedge east");
    ctx.require(put(ctx, off(p, 0, 0, -1), "minecraft:oak_fence"), "fence north");
    ctx.require(put(ctx, off(p, -1, 0, 0), "minecraft:stone"), "stone west");
    ctx.require(put(ctx, off(p, 0, 1, 0), "lepton:oak_hedge"), "hedge above");
    co_await ctx.ticks(4);
    auto const& b = at(ctx, p);
    ctx.check(flag(b, "lepton:east"), "connects to a hedge");
    ctx.check(!flag(b, "lepton:north"), "not to a fence");
    ctx.check(!flag(b, "lepton:west"), "not to a solid block");
    ctx.check(!flag(b, "lepton:extend"), "bottom hedge shows its post");
    ctx.check(flag(at(ctx, off(p, 0, 1, 0)), "lepton:extend"), "upper hedge extends");
    ctx.check(flag(at(ctx, off(p, 1, 0, 0)), "lepton:west"), "the other hedge connects back");
});

LP_REGISTER_SELFTEST("woodvariants.hedge_plant", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(626);
    p.y += 2;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    ctx.require(put(ctx, p, "lepton:oak_hedge"), "hedge");
    ctx.cleanupBlock(off(p, 0, 1, 0));
    co_await ctx.ticks(2);
    auto const* poppy = bs::of("minecraft:poppy");
    ctx.require(poppy != nullptr, "poppy");
    ctx.check(poppy->getBlockType().mayPlace(regionOf(ctx), lp::game::blockPos(off(p, 0, 1, 0))), "a flower may be placed on a hedge");
    use(ctx, "minecraft:poppy", p, Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:poppy", "poppy planted: " + idAt(ctx, off(p, 0, 1, 0)));
});

LP_REGISTER_SELFTEST("woodvariants.ladder_place", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(630);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "minecraft:stone"), "wall");
    ctx.cleanupBlock(off(p, 0, 0, -1));
    ctx.cleanupBlock(off(p, 0, 1, 0));
    stepAside(ctx, p);
    co_await ctx.ticks(2);
    use(ctx, "lepton:spruce_ladder", p, Facing::North, Vec3{0.5f, 0.5f, 0.f});
    co_await ctx.ticks(3);
    auto const& b = at(ctx, off(p, 0, 0, -1));
    ctx.require(b.getTypeName() == "lepton:spruce_ladder", "ladder placed: " + b.getTypeName());
    ctx.check(str(b, "minecraft:block_face") == "north", "facing north (away from the wall)");
    co_await ctx.ticks(10);
    use(ctx, "lepton:spruce_ladder", p, Facing::Up, Vec3{0.5f, 1.f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) != "lepton:spruce_ladder", "no ladder on a top face");
    // survival: the wall goes, the ladder pops off
    clear(ctx, p);
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, off(p, 0, 0, -1)) != "lepton:spruce_ladder", "ladder popped: " + idAt(ctx, off(p, 0, 0, -1)));
});

LP_REGISTER_SELFTEST("woodvariants.ladder_climb", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(638);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, off(p, 0, 0, 1), "minecraft:stone"), "wall");
    ctx.require(put(ctx, p, "lepton:birch_ladder", {{"minecraft:block_face", "north"}}), "ladder");
    ctx.require(put(ctx, off(p, 0, 1, 0), "lepton:birch_ladder", {{"minecraft:block_face", "north"}}), "ladder above");
    ctx.require(put(ctx, off(p, 0, 1, 1), "minecraft:stone"), "wall above");
    co_await ctx.ticks(2);
    ctx.check(hasProperty("lepton:birch_ladder", BlockProperty::Climbable), "ladder block type is climbable");
    ctx.check(hasProperty("lepton:hollow_oak_log", BlockProperty::Climbable), "hollow log block type is climbable");
    ctx.player().teleport(Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, DimensionType(p.dim));
    co_await ctx.ticks(3);
    ctx.check(ctx.player().onClimbableBlock(), "a player in the ladder is on a climbable block");
});

LP_REGISTER_SELFTEST("woodvariants.carpet_survive", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(642);
    p.y += 2;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    ctx.require(put(ctx, p, "minecraft:stone"), "ground");
    ctx.require(put(ctx, off(p, 0, 1, 0), "lepton:oak_leaf_carpet"), "carpet");
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "lepton:oak_leaf_carpet", "carpet stays on stone");
    clear(ctx, p);
    co_await ctx.ticks(4);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) != "lepton:oak_leaf_carpet", "carpet popped over air");
});

LP_REGISTER_SELFTEST("woodvariants.carpet_compost", [](Context& ctx) {
    auto carpet = lp::game::makeItem("lepton:oak_leaf_carpet");
    ctx.require(!carpet.isNull(), "carpet item");
    ctx.check(ComposterBlock::getFillChance(carpet) == 20, "leaf carpet composts at 20%: " + std::to_string(ComposterBlock::getFillChance(carpet)));
    ctx.check(ComposterBlock::getFillChance(lp::game::makeItem("lepton:oak_post")) == 0, "posts don't compost");
});

LP_REGISTER_SELFTEST("woodvariants.fuel", [](Context& ctx) {
    float ladder = FurnaceBlockActor::getBurnDuration(lp::game::makeItem("lepton:spruce_ladder"), 200.f);
    float planks = FurnaceBlockActor::getBurnDuration(lp::game::makeItem("minecraft:oak_planks"), 200.f);
    ctx.check(ladder > 0.f && ladder == planks, "variant ladders burn like planks: " + std::to_string(ladder) + " vs " + std::to_string(planks));
});

LP_REGISTER_SELFTEST("woodvariants.bookshelf_power", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(650);
    p.y += 1;
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    stepAside(ctx, p);
    ctx.require(put(ctx, p, "minecraft:enchanting_table"), "table");
    ctx.require(put(ctx, off(p, 2, 0, 0), "lepton:spruce_bookshelf"), "shelf 1");
    ctx.require(put(ctx, off(p, 2, 1, 1), "lepton:cherry_bookshelf"), "shelf 2");
    ctx.require(put(ctx, off(p, -2, 0, 0), "lepton:crimson_bookshelf"), "shelf 3 (blocked)");
    ctx.require(put(ctx, off(p, -1, 0, 0), "minecraft:stone"), "blocker");
    ctx.require(put(ctx, off(p, 0, 0, 2), "minecraft:bookshelf"), "vanilla shelf");
    co_await ctx.ticks(1);
    auto& region = regionOf(ctx);
    auto  pos    = lp::game::blockPos(p);
    ctx.check(wv::variantBookshelfCount(region, pos) == 2, "two variant shelves count: " + std::to_string(wv::variantBookshelfCount(region, pos)));
    auto list = EnchantUtils::getBookCasePositions(region, Vec3{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
    int  ours = 0;
    for (auto const& v : list) {
        if (std::lround(v.x) == p.x + 2 && std::lround(v.z) == p.z) ++ours;
    }
    ctx.check(ours >= 1, "the game's bookcase list includes the variant shelf (" + std::to_string(list.size()) + " positions)");
});

LP_REGISTER_SELFTEST("woodvariants.hollow_axis", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(660);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, p, "minecraft:stone"), "stone");
    ctx.cleanupBlock(off(p, 0, 0, 1));
    stepAside(ctx, off(p, 3, 0, 0));
    co_await ctx.ticks(2);
    use(ctx, "lepton:hollow_spruce_log", p, Facing::South, Vec3{0.5f, 0.5f, 1.f});
    co_await ctx.ticks(3);
    auto const& b = at(ctx, off(p, 0, 0, 1));
    ctx.require(b.getTypeName() == "lepton:hollow_spruce_log", "hollow log placed: " + b.getTypeName());
    ctx.check(wv::axisOf(b) == "z", "along z");
});

LP_REGISTER_SELFTEST("woodvariants.hollow_crawl", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto p = ctx.testPos(664);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    ctx.require(put(ctx, off(p, 0, -1, 0), "minecraft:stone"), "floor under the log");
    ctx.require(put(ctx, off(p, -1, -1, 0), "minecraft:stone"), "floor under the player");
    ctx.require(put(ctx, p, "lepton:hollow_oak_log", {{"minecraft:block_face", "east"}}), "horizontal hollow log");
    ctx.cleanupBlock(off(p, -1, 0, 0));
    ctx.cleanupBlock(off(p, -1, 1, 0));
    auto& player = ctx.player();
    player.setSneaking(false);
    // stand west of the log, looking east (yaw -90)
    player.teleport(
        Vec3{static_cast<float>(p.x) - 0.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f},
        DimensionType(p.dim),
        Vec2{0.f, -90.f}
    );
    co_await ctx.ticks(3);
    player.setSneaking(true);
    co_await ctx.ticks(3);
    auto f = player.getFeetPos();
    ctx.check(
        static_cast<int>(std::floor(f.x)) == p.x && static_cast<int>(std::floor(f.z)) == p.z,
        "player moved into the log: " + std::to_string(f.x) + " " + std::to_string(f.y) + " " + std::to_string(f.z)
    );
    ctx.check(player.isSwimming(), "player crawls (swimming pose)");
    player.setSneaking(false);
    stepAside(ctx, p);
    co_await ctx.ticks(2);
});

// A gallery of every block of the unit for screenshots (client scenario `woodvariants`), dx 670..699
// inside U6's z band (dz -2..2). The rows are stacked (ground, +6, +12, +16; a smooth stone floor
// under the raised ones), each seen from the south:
//   A  vertical planks / bookshelf / vertical planks columns, a ladder on the top planks
//   B  posts along x with a chain + lantern or a lantern below, stripped posts standing in front
//   C  a connected hedge row (every other hedge extended by one above), a post under a hedge,
//      a leaf carpet row in front
//   D  hollow logs along z (open towards the viewer), x and y; a crawl log for the real player
// With a real player online the player is moved in front of each row (and into the crawl spot); on
// a dedicated server the gallery is only built and checked.
LP_REGISTER_SELFTEST("woodvariants.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    static constexpr char const* kAll[] = {
        "oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "bamboo", "crimson", "warped"};
    static constexpr char const* kLeaves[] = {
        "oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "azalea", "flowering_azalea"};
    static constexpr char const* kHollow[] = {
        "hollow_oak_log", "hollow_spruce_log", "hollow_birch_log", "hollow_jungle_log", "hollow_acacia_log",
        "hollow_dark_oak_log", "hollow_mangrove_log", "hollow_cherry_log", "hollow_crimson_stem", "hollow_warped_stem"};
    auto base = ctx.testPos(670);
    GP   rowA = base, rowB = off(base, 0, 6, 0), rowC = off(base, 0, 12, 0), rowD = off(base, 0, 16, 0);
    ctx.require(co_await ctx.ensureLoaded(base), "gallery loaded");
    ctx.require(co_await ctx.ensureLoaded(off(base, 28, 0, 0)), "gallery end loaded");
    int  failed = 0;
    auto place  = [&](GP const& p, std::string const& id, bs::Changes const& states = {}) {
        if (!put(ctx, p, id, states)) {
            ++failed;
            ctx.check(false, "placed " + id);
        }
    };
    std::string const ns = "lepton:";
    for (auto const& r : {rowB, rowC, rowD}) {
        for (int x = -1; x <= 28; ++x) {
            for (int z = -2; z <= 2; ++z) place(off(r, x, -1, z), "minecraft:smooth_stone");
        }
    }
    // the ground row stands on whatever is there: make sure carpets and ladders have support
    for (int x = -1; x <= 28; ++x) {
        for (int z = -2; z <= 2; ++z) {
            if (regionOf(ctx).getBlock(lp::game::blockPos(off(rowA, x, -1, z))).isAir())
                place(off(rowA, x, -1, z), "minecraft:smooth_stone");
        }
    }
    // A: planks, bookshelves, ladders
    for (int i = 0; i < 11; ++i) {
        std::string w = kAll[i];
        auto        c = off(rowA, 2 * i, 0, 0);
        place(c, ns + "vertical_" + w + "_planks");
        place(off(c, 0, 1, 0), w == "oak" ? std::string("minecraft:bookshelf") : ns + w + "_bookshelf");
        place(off(c, 0, 2, 0), ns + "vertical_" + w + "_planks");
        if (w == "oak") place(off(c, 0, 2, 1), "minecraft:ladder", {{"facing_direction", 3}});
        else place(off(c, 0, 2, 1), ns + w + "_ladder", {{"minecraft:block_face", "south"}});
    }
    // B: posts with chains and lanterns, stripped posts
    for (int i = 0; i < 11; ++i) {
        std::string w = kAll[i];
        auto        c = off(rowB, 2 * i, 0, 0);
        place(off(c, 0, 3, 0), ns + w + "_post", {{"minecraft:block_face", "east"}});
        if (i % 2 == 0) {
            place(off(c, 0, 2, 0), "minecraft:chain", {{"pillar_axis", "y"}});
            place(off(c, 0, 1, 0), "minecraft:lantern", {{"hanging", true}});
        } else {
            place(off(c, 0, 2, 0), i % 4 == 3 ? "minecraft:soul_lantern" : "minecraft:lantern", {{"hanging", true}});
        }
        place(off(c, 0, 0, 2), ns + "stripped_" + w + "_post", {{"minecraft:block_face", "up"}});
        place(off(c, 0, 1, 2), ns + "stripped_" + w + "_post", {{"minecraft:block_face", "up"}});
    }
    // C: hedges, a post below a hedge, leaf carpets
    for (int i = 0; i < 10; ++i) {
        std::string w = kLeaves[i];
        place(off(rowC, i, 0, 0), ns + w + "_hedge");
        if (i % 2 == 0) place(off(rowC, i, 1, 0), ns + w + "_hedge");
        place(off(rowC, i, 0, 2), ns + w + "_leaf_carpet");
    }
    place(off(rowC, 13, 0, 0), "lepton:dark_oak_post", {{"minecraft:block_face", "up"}});
    place(off(rowC, 13, 1, 0), "lepton:oak_hedge");
    // D: hollow logs on each axis, the crawl log (x axis) with free space west of it
    for (int i = 0; i < 10; ++i) {
        auto c = off(rowD, 2 * i, 0, 0);
        place(c, ns + kHollow[i], {{"minecraft:block_face", "south"}});
        place(off(c, 0, 1, 0), ns + kHollow[i], {{"minecraft:block_face", "east"}});
        place(off(c, 0, 2, 0), ns + kHollow[i], {{"minecraft:block_face", "up"}});
    }
    auto crawl = off(rowD, 25, 0, 0);
    place(crawl, "lepton:hollow_birch_log", {{"minecraft:block_face", "east"}});
    for (int dy = 0; dy < 2; ++dy) ctx.cleanupBlock(off(crawl, -1, dy, 0));
    co_await ctx.ticks(6);
    ctx.check(failed == 0, std::to_string(failed) + " gallery blocks not placed");
    ctx.check(str(at(ctx, off(rowB, 0, 3, 0)), "lepton:connect_down") == "chain", "chain below the first post");
    ctx.check(str(at(ctx, off(rowB, 2, 3, 0)), "lepton:connect_down") == "chain", "lantern below the second post");
    ctx.check(idAt(ctx, off(rowB, 2, 2, 0)) == "minecraft:lantern", "the lantern hangs below the post");
    ctx.check(flag(at(ctx, off(rowC, 0, 0, 0)), "lepton:east"), "hedges connect");
    ctx.check(flag(at(ctx, off(rowC, 0, 1, 0)), "lepton:extend"), "a hedge above a hedge extends");
    ctx.check(str(at(ctx, off(rowC, 13, 0, 0)), "lepton:connect_up") == "other_post", "post below a hedge");
    ctx.check(idAt(ctx, off(rowA, 2, 2, 1)) == "lepton:spruce_ladder", "ladders stay on the planks");
    ctx.check(idAt(ctx, off(rowC, 0, 0, 2)) == "lepton:oak_leaf_carpet", "leaf carpets stay on the floor");

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) co_return;
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](GP const& row, float dx, float dz, float pitch, float yaw) {
        if (auto* v = ctx.player().getLevel().getPlayer(uid)) {
            v->teleport(
                Vec3{static_cast<float>(row.x) + dx, static_cast<float>(row.y), static_cast<float>(row.z) + dz},
                DimensionType(row.dim),
                Vec2{pitch, yaw}
            );
        }
    };
    struct View {
        char const* name;
        GP          row;
        float       dx, dz, pitch;
    };
    View const views[] = {
        {"a-left",  rowA, 5.5f,  8.5f, 12.f},
        {"a-right", rowA, 15.5f, 8.5f, 12.f},
        {"b-left",  rowB, 5.5f,  9.5f, 8.f },
        {"b-right", rowB, 15.5f, 9.5f, 8.f },
        {"c",       rowC, 6.5f,  9.5f, 22.f},
        {"d-left",  rowD, 5.5f,  7.5f, 10.f},
        {"d-right", rowD, 14.5f, 7.5f, 10.f},
    };
    for (auto const& v : views) {
        ctx.info(std::string("GALLERY view ") + v.name);
        // south of the row, looking north; the raised rows have no floor there: hold the player
        for (int t = 0; t < 120; ++t) {
            view(v.row, v.dx, v.dz, v.pitch, 180.f);
            co_await ctx.ticks(1);
        }
    }
    // the real player crawls into the crawl log: west of it looking east; the scenario holds sneak
    ctx.info("GALLERY crawl");
    view(crawl, -0.5f, 0.5f, 10.f, -90.f);
    bool crawled = false;
    for (int t = 0; t < 20 && !crawled; ++t) {
        co_await ctx.ticks(10);
        auto* v = ctx.player().getLevel().getPlayer(uid);
        if (!v) break;
        auto f  = v->getFeetPos();
        crawled = static_cast<int>(std::floor(f.x)) == crawl.x && static_cast<int>(std::floor(f.z)) == crawl.z
               && v->isSwimming();
    }
    // not a check: in the `full` scenarios nobody sneaks; the woodvariants client scenario asserts this line
    ctx.info(std::string("GALLERY crawled ") + (crawled ? "yes" : "no"));
    co_await ctx.ticks(100);
    view(rowA, 5.5f, 8.5f, 12.f, 180.f); // back on the ground
    co_await ctx.ticks(10);
    ctx.info("GALLERY done");
});
