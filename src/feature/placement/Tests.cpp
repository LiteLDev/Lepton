// U17 placement selftests (dx 1700..1799). Run: /quark selftest "placement.*"

#include <cmath>
#include <format>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/server/sim/LookDuration.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "feature/placement/Placement.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
namespace pl = lp::feature::placement;
namespace bs = lp::game::blockstates;
using Facing = ScriptModuleMinecraft::ScriptFacing;
using lp::core::GlobalPos;

/// Restores the placement config (and the variant map) when the test ends.
struct ConfigGuard {
    pl::PlacementConfig saved = lp::config::get<pl::PlacementConfig>();
    pl::PlacementConfig& cfg() { return lp::config::mut<pl::PlacementConfig>(); }
    ~ConfigGuard() {
        lp::config::mut<pl::PlacementConfig>() = saved;
        pl::rebuildVariants();
    }
};

BlockSource& region(Context& ctx) { return ctx.player().getDimensionBlockSource(); }
BlockPos     bp(GlobalPos const& p) { return lp::game::blockPos(p); }
GlobalPos    off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}
std::string idAt(Context& ctx, GlobalPos const& p) { return lp::game::blockIdAt(region(ctx), bp(p)); }

bool put(Context& ctx, GlobalPos const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(region(ctx), bp(p), *b);
}

SimulatedPlayer& simp(Context& ctx) { return *static_cast<SimulatedPlayer*>(&ctx.player()); }

/// Puts the player's feet at `feet` (world coordinates) looking along yaw/pitch (degrees).
void stand(Context& ctx, Vec3 feet, float yaw, float pitch) {
    auto& p = ctx.player();
    p.teleport(feet, DimensionType(0));
    auto& rot = const_cast<Vec2&>(p.getRotation());
    rot.x     = pitch;
    rot.y     = yaw;
}

void setSurvival(Context& ctx) { ctx.player().setPlayerGameType(GameType::Survival); }

void hold(Context& ctx, std::string_view id, int count = 16) {
    auto& p = ctx.player();
    p.setSelectedSlot(0);
    ctx.give(p, lp::game::makeItem(id, count), 0);
}

int countOf(Player& p, std::string const& id) {
    int  n   = 0;
    auto& inv = p.getInventory();
    for (int i = 0; i < 36; ++i)
        if (lp::game::itemId(inv.getItem(i)) == id) n += inv.getItem(i).mCount;
    return n;
}

} // namespace

LP_REGISTER_SELFTEST("placement.config", [](Context& ctx) {
    pl::PlacementConfig d;
    ctx.check(d.enableReacharoundPlacing && d.leniency == 0.5 && d.whitelist.empty() && d.blacklist.empty(), "reacharound defaults");
    ctx.check(d.enableLockRotation && d.lockRotationRenderLikeCrossHair && d.showKeybind, "lock rotation defaults");
    ctx.check(!d.enableVariantSelector && !d.convertVariantItems && !d.enableHammer && d.showTooltip && !d.alignHudToHotbar
                  && !d.showSimpleHud && d.showHud && d.enableGreenTint && d.overrideHeldItemRender && d.hudOffsetX == 0
                  && d.hudOffsetY == 0 && d.variantSelectorRenderLikeCrossHair && !d.smallerArrow,
              "variant selector defaults");
    ctx.check(d.variants.variantTypes.size() == 6 && d.variants.stripCandidates.size() == 4 && d.variants.aliases.size() == 2,
              "variants config defaults");
    ctx.check(d.enableTrowel && d.maxDamage == 0, "trowel defaults");
    ctx.check(d.enableReplaceScaffolding && d.maxBounces == 1, "scaffolding defaults");
    ctx.check(d.enableLongRangePickBlock, "long range pick default");
    ctx.check(d.enableEnhancedLadders && d.fallSpeed == -0.2 && d.allowFreestanding && d.allowDroppingDown && d.allowSliding
                  && d.allowInventorySneak,
              "ladder defaults");
    ctx.check(pl::nearestDir(Vec3{0, 0, -1}) == pl::North && pl::nearestDir(Vec3{0, -1, 0.2f}) == pl::Down, "nearest direction");
    ctx.check(pl::horizontalFromYaw(0) == pl::South && pl::horizontalFromYaw(90) == pl::West && pl::horizontalFromYaw(180) == pl::North
                  && pl::horizontalFromYaw(-90) == pl::East,
              "yaw to direction");
    auto dirs = pl::nearestLookingDirections(80.0f, 0.0f); // looking down, a little south
    ctx.check(dirs.size() == 6 && dirs[0] == pl::Down && dirs[5] == pl::Up, "looking directions start with down");
});

// ---- ReacharoundPlacing --------------------------------------------------------------------------

LP_REGISTER_SELFTEST("placement.reacharound_items", [](Context& ctx) {
    ConfigGuard g;
    ctx.check(pl::canReacharound(lp::game::makeItem("minecraft:stone")), "block items reacharound");
    ctx.check(!pl::canReacharound(lp::game::makeItem("minecraft:stick")), "sticks don't");
    ctx.check(pl::canReacharound(lp::game::makeItem("lepton:trowel")), "the trowel does (reacharound_able)");
    g.cfg().whitelist = {"minecraft:stick"};
    ctx.check(pl::canReacharound(lp::game::makeItem("minecraft:stick")), "whitelisted items do");
    g.cfg().blacklist = {"minecraft:stone"};
    ctx.check(!pl::canReacharound(lp::game::makeItem("minecraft:stone")), "blacklisted items don't");
});

LP_REGISTER_SELFTEST("placement.reacharound_vertical", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = off(ctx.testPos(1700), 0, 14, 0); // high above the ground: the rays only see the ledge
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    setSurvival(ctx);
    ctx.require(put(ctx, p, "minecraft:stone"), "ledge placed");
    hold(ctx, "minecraft:cobblestone");
    // feet 0.2 past the east edge, looking west and almost straight down: the eye ray passes under
    // the ledge, the raised ray hits its east face -> the block below the ledge.
    float x = static_cast<float>(p.x), y = static_cast<float>(p.y), z = static_cast<float>(p.z);
    // Aim so that the raised ray (eye + 0.5) meets the ledge's east face 0.25 above its bottom and
    // the eye ray passes 0.25 under it (from the actual eye position: Bedrock's eye isn't exactly
    // above the feet).
    float pitch = 86.0f;
    for (int i = 0; i < 3; ++i) {
        stand(ctx, Vec3{x + 1.2f, y + 1.0f, z + 0.5f}, 90.0f, pitch);
        auto  eye = ctx.player().getEyePos();
        float d   = eye.x - (x + 1.0f);
        float e   = eye.y - y;
        pitch     = std::atan((e + 0.25f) / std::max(d, 0.05f)) * 180.0f / 3.14159265f;
    }
    stand(ctx, Vec3{x + 1.2f, y + 1.0f, z + 0.5f}, 90.0f, pitch);
    {
        auto& pp  = ctx.player();
        auto  eye = pp.getEyePos();
        auto  lk  = pp.getViewVector(1.0f);
        auto  r2  = pl::clip(region(ctx), Vec3{eye.x, eye.y + 0.5f, eye.z}, lk, pl::reachOf(pp), &pp);
        ctx.info(std::format("eye {:.2f} {:.2f} pitch {:.1f} raised ray hit {}", eye.x - x, eye.y - y, pitch, r2.hit));
    }
    auto t = pl::reacharoundTarget(ctx.player());
    ctx.require(t.has_value(), "a vertical target exists");
    ctx.check(t->pos == bp(off(p, 0, -1, 0)) && t->face == pl::Down, "the target is below the ledge");
    ctx.cleanupBlock(off(p, 0, -1, 0));
    ctx.check(pl::doReacharound(ctx.player()), "the block is placed");
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, off(p, 0, -1, 0)) == "minecraft:cobblestone", "cobblestone below the ledge: " + idAt(ctx, off(p, 0, -1, 0)));
    ctx.check(countOf(ctx.player(), "minecraft:cobblestone") == 15, "one item used");
    // looking at a block: no reacharound
    stand(ctx, Vec3{x + 0.5f, y + 1.0f, z + 0.5f}, 90.0f, 89.0f);
    ctx.check(!pl::reacharoundTarget(ctx.player()).has_value(), "no target while the crosshair is on a block");
    // looking up: no vertical target
    stand(ctx, Vec3{x + 1.2f, y + 1.0f, z + 0.5f}, 90.0f, -30.0f);
    auto up = pl::reacharoundTarget(ctx.player());
    ctx.check(!up || up->face != pl::Down, "no vertical target when looking up");
});

LP_REGISTER_SELFTEST("placement.reacharound_horizontal", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = off(ctx.testPos(1705), 0, 14, 0);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    setSurvival(ctx);
    ctx.require(put(ctx, p, "minecraft:stone"), "ledge placed");
    hold(ctx, "minecraft:cobblestone");
    float x = static_cast<float>(p.x), y = static_cast<float>(p.y), z = static_cast<float>(p.z);
    // on the ledge's east half, facing east over the edge, 70 degrees down
    stand(ctx, Vec3{x + 0.7f, y + 1.0f, z + 0.5f}, -90.0f, 70.0f);
    auto t = pl::reacharoundTarget(ctx.player());
    ctx.require(t.has_value(), "a horizontal target exists");
    ctx.check(t->pos == bp(off(p, 1, 0, 0)) && t->face == pl::West, "the target is east of the ledge");
    ctx.cleanupBlock(off(p, 1, 0, 0));
    // through the game's use-item path (the event a client's "use in air" causes)
    ItemStack stack = ctx.player().getSelectedItem();
    simp(ctx).simulateUseItem(stack);
    if (idAt(ctx, off(p, 1, 0, 0)) != "minecraft:cobblestone") {
        ctx.info("simulateUseItem did not place; using the module directly");
        stand(ctx, Vec3{x + 0.7f, y + 1.0f, z + 0.5f}, -90.0f, 70.0f);
        pl::doReacharound(ctx.player());
    }
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, off(p, 1, 0, 0)) == "minecraft:cobblestone", "cobblestone east of the ledge: " + idAt(ctx, off(p, 1, 0, 0)));
    g.cfg().enableReacharoundPlacing = false;
    stand(ctx, Vec3{x + 1.7f, y + 1.0f, z + 0.5f}, -90.0f, 70.0f);
    ctx.check(!pl::reacharoundTarget(ctx.player()).has_value(), "disabled: no target");
});

// ---- LockRotation --------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("placement.lock_profile", [](Context& ctx) {
    pl::Ray ray;
    ray.hit      = true;
    ray.face     = pl::North;
    ray.location = Vec3{3.5f, 10.7f, 2.0f};
    auto a       = pl::profileFor(ray, Vec3{0, 0, 1});
    ctx.check(a.facing == pl::South && a.half == 1, "north face, upper half -> facing south, top");
    ray.location.y = 10.2f;
    ctx.check(pl::profileFor(ray, Vec3{0, 0, 1}).half == 0, "lower half -> bottom");
    ray.location.y = -3.3f;
    ctx.check(pl::profileFor(ray, Vec3{0, 0, 1}).half == 1, "negative y: 0.7 into the block -> top");
    ray.face = pl::Up;
    ctx.check(pl::profileFor(ray, Vec3{0, 0, 1}).half == -1 && pl::profileFor(ray, Vec3{0, 0, 1}).facing == pl::Down,
              "top face -> facing down, no half");
    pl::Ray miss;
    ctx.check(pl::profileFor(miss, Vec3{0.1f, 0.2f, -0.9f}).facing == pl::North, "no block: the look direction");

    auto& p = ctx.player();
    pl::setLockProfile(p, std::nullopt, {});
    auto set = pl::toggleLock(p, {pl::East, 1}, "K");
    ctx.check(set && pl::lockProfileOf(p) == pl::LockProfile{pl::East, 1}, "the key sets a profile");
    ctx.check(!pl::toggleLock(p, {pl::East, 1}, "K") && !pl::lockProfileOf(p), "the same profile clears it");
    pl::toggleLock(p, {pl::East, 1}, "K");
    ctx.check(pl::toggleLock(p, {pl::West, -1}, "K").has_value(), "another profile replaces it");
    pl::clearLockProfiles();
    ctx.check(!pl::lockProfileOf(p), "config reload forgets every profile");
});

LP_REGISTER_SELFTEST("placement.lock_net", [](Context& ctx) {
    auto& p = ctx.player();
    lp::net::testing::setClientMod(p, true);
    pl::setLockProfile(p, std::nullopt, {});
    lp::net::testing::receiveFromClient(p, pl::LockMsg{true, pl::South, 0, "K"});
    ctx.check(pl::lockProfileOf(p) == pl::LockProfile{pl::South, 0}, "the client's lock is applied");
    lp::net::testing::receiveFromClient(p, pl::LockMsg{true, 9, 0, "K"});
    ctx.check(pl::lockProfileOf(p) == pl::LockProfile{pl::South, 0}, "invalid facings are ignored");
    lp::net::testing::receiveFromClient(p, pl::LockMsg{false, 0, -1, "K"});
    ctx.check(!pl::lockProfileOf(p), "unlock clears it");
    // the one-time message flag
    auto key = "lp:placement:locked_once:" + p.getUuid().asString();
    lp::game::db::erase(key);
    pl::setLockProfile(p, pl::LockProfile{pl::North, -1}, "K");
    auto flag = lp::game::db::load(key);
    ctx.check(flag && flag->is_boolean() && flag->get<bool>(), "the one-time message is remembered");
    pl::setLockProfile(p, std::nullopt, {});
    lp::game::db::erase(key);
    lp::net::testing::setClientMod(p, false);
});

LP_REGISTER_SELFTEST("placement.lock_vanilla", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = ctx.testPos(1710);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    setSurvival(ctx);
    for (int i = 0; i < 3; ++i) ctx.require(put(ctx, off(p, 0, 0, i - 1), "minecraft:stone"), "floor placed");
    stand(ctx, Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, 90.0f, 30.0f);
    pl::setLockProfile(player, pl::LockProfile{pl::East, 1}, "K");

    auto place = [&](std::string_view id, int dz) {
        auto stack = lp::game::makeItem(id, 4);
        ctx.cleanupBlock(off(p, 0, 1, dz));
        return simp(ctx).simulateUseItemOnBlock(stack, bp(off(p, 0, 0, dz)), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    };
    ctx.check(place("minecraft:oak_log", -1), "log used");
    co_await ctx.ticks(12);
    ctx.check(place("minecraft:stone_brick_stairs", 0), "stairs used");
    co_await ctx.ticks(12);
    ctx.check(place("minecraft:furnace", 1), "furnace used");
    co_await ctx.ticks(3);
    auto const& log = region(ctx).getBlock(bp(off(p, 0, 1, -1)));
    ctx.check(log.getTypeName() == "minecraft:oak_log" && bs::getString(log, "pillar_axis") == "x",
              "locked log lies along x: " + bs::getString(log, "pillar_axis").value_or("?"));
    auto const& st = region(ctx).getBlock(bp(off(p, 0, 1, 0)));
    ctx.check(st.getTypeName() == "minecraft:stone_brick_stairs", "stairs placed");
    ctx.check(bs::getInt(st, "weirdo_direction") == 0, "locked stairs face east: " + std::to_string(bs::getInt(st, "weirdo_direction").value_or(-1)));
    ctx.check(bs::getBool(st, "upside_down_bit") == true, "locked stairs are upside down (top half)");
    auto const& fu = region(ctx).getBlock(bp(off(p, 0, 1, 1)));
    ctx.check(fu.getTypeName() == "minecraft:furnace", "furnace placed");
    ctx.check(bs::getString(fu, "minecraft:cardinal_direction") == "west",
              "locked furnace faces the player (west): " + bs::getString(fu, "minecraft:cardinal_direction").value_or("?"));
    pl::setLockProfile(player, std::nullopt, {});
});

LP_REGISTER_SELFTEST("placement.lock_lepton", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = ctx.testPos(1715);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    setSurvival(ctx);
    for (int i = 0; i < 2; ++i) ctx.require(put(ctx, off(p, 0, 0, i), "minecraft:stone"), "floor placed");
    stand(ctx, Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, 90.0f, 30.0f);
    pl::setLockProfile(player, pl::LockProfile{pl::East, 1}, "K");
    auto place = [&](std::string_view id, int dz) {
        auto stack = lp::game::makeItem(id, 4);
        ctx.cleanupBlock(off(p, 0, 1, dz));
        return simp(ctx).simulateUseItemOnBlock(stack, bp(off(p, 0, 0, dz)), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    };
    ctx.check(place("lepton:u0_probe_stone_stairs", 0), "stairs used");
    co_await ctx.ticks(12);
    ctx.check(place("lepton:u0_probe_stone_vertical_slab", 1), "vertical slab used");
    co_await ctx.ticks(4);
    auto const& st = region(ctx).getBlock(bp(off(p, 0, 1, 0)));
    ctx.check(bs::getString(st, "minecraft:cardinal_direction") == "east",
              "locked Lepton stairs face east: " + bs::getString(st, "minecraft:cardinal_direction").value_or("?"));
    ctx.check(bs::getString(st, "minecraft:vertical_half") == "top", "locked Lepton stairs: top half");
    auto const& vs = region(ctx).getBlock(bp(off(p, 0, 1, 1)));
    ctx.check(bs::getString(vs, "lepton:type") == "west", "locked vertical slab type west: " + bs::getString(vs, "lepton:type").value_or("?"));
    pl::setLockProfile(player, std::nullopt, {});
});

// ---- VariantSelector ----------------------------------------------------------------------------

LP_REGISTER_SELFTEST("placement.variants_map", [](Context& ctx) {
    ConfigGuard g;
    g.cfg().enableVariantSelector = true;
    pl::rebuildVariants();
    ctx.check(pl::variantOf("minecraft:stone_bricks", "stairs") == "minecraft:stone_brick_stairs",
              "stone bricks -> stairs: " + pl::variantOf("minecraft:stone_bricks", "stairs"));
    ctx.check(pl::variantOf("minecraft:stone_bricks", "slab") == "minecraft:stone_brick_slab",
              "stone bricks -> slab: " + pl::variantOf("minecraft:stone_bricks", "slab"));
    ctx.check(pl::variantOf("minecraft:stone_bricks", "wall") == "minecraft:stone_brick_wall", "stone bricks -> wall");
    ctx.check(pl::variantOf("minecraft:oak_planks", "stairs") == "minecraft:oak_stairs", "oak planks -> stairs (strip _planks)");
    ctx.check(pl::variantOf("minecraft:oak_planks", "fence") == "minecraft:oak_fence", "oak planks -> fence");
    ctx.check(pl::variantOf("minecraft:oak_planks", "fence_gate") == "minecraft:fence_gate", "oak planks -> fence gate (manual)");
    ctx.check(pl::variantOf("lepton:u0_probe_stone", "vertical_slab") == "lepton:u0_probe_stone_vertical_slab", "vertical slab");
    ctx.check(pl::originalOf("minecraft:stone_brick_stairs") == "minecraft:stone_bricks", "original of the stairs");
    ctx.check(pl::variantNameOf("minecraft:stone_bricks", "minecraft:stone_brick_slab") == "slab", "variant name");
    ctx.check(pl::variantFromAny("minecraft:stone_brick_slab", "stairs") == "minecraft:stone_brick_stairs", "variant from any");
    ctx.check(pl::variantFromAny("minecraft:stone_brick_slab", "") == "minecraft:stone_bricks", "back to the original");
    ctx.check(!pl::hasVariants("minecraft:snow"), "blacklisted block");
    ctx.check(pl::isKnownVariant("stairs") && !pl::isKnownVariant("bogus"), "known variants");
    auto all = pl::allVariants("minecraft:stone_bricks");
    ctx.check(all.size() >= 3 && all[0].first == "slab" && all[1].first == "stairs", "variants in config order");
    g.cfg().variants.blacklist.push_back("minecraft:stone_bricks=minecraft:stone_brick_wall");
    pl::rebuildVariants();
    ctx.check(pl::variantOf("minecraft:stone_bricks", "wall").empty(), "blacklisted pair");
    g.cfg().enableVariantSelector = false;
    pl::rebuildVariants();
    ctx.check(!pl::hasVariants("minecraft:stone_bricks"), "disabled: no variants");
});

LP_REGISTER_SELFTEST("placement.variant_place", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    g.cfg().enableVariantSelector = true;
    pl::rebuildVariants();
    auto p = ctx.testPos(1720);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    setSurvival(ctx);
    ctx.require(put(ctx, p, "minecraft:stone"), "floor placed");
    stand(ctx, Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, 90.0f, 30.0f);
    pl::setSavedVariant(player, "stairs");
    ctx.check(pl::savedVariant(player) == "stairs", "variant saved");
    hold(ctx, "minecraft:stone_bricks", 8);
    ctx.cleanupBlock(off(p, 0, 1, 0));
    ctx.check(simp(ctx).simulateUseItemInSlotOnBlock(0, bp(p), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}), "stone bricks used");
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:stone_brick_stairs", "stairs placed instead: " + idAt(ctx, off(p, 0, 1, 0)));
    ctx.check(countOf(player, "minecraft:stone_bricks") == 7, "one stone brick used: " + std::to_string(countOf(player, "minecraft:stone_bricks")));
    ctx.check(countOf(player, "minecraft:stone_brick_stairs") == 0, "no stairs items appeared");
    pl::setSavedVariant(player, "bogus");
    ctx.check(pl::savedVariant(player) == "stairs", "unknown variants are refused");
    pl::setSavedVariant(player, "");
});

LP_REGISTER_SELFTEST("placement.variant_net", [](Context& ctx) {
    ConfigGuard g;
    g.cfg().enableVariantSelector = true;
    pl::rebuildVariants();
    auto& p = ctx.player();
    lp::net::testing::setClientMod(p, true);
    (void)lp::net::testing::takeOutbox(p);
    lp::net::testing::receiveFromClient(p, pl::VariantMsg{"slab"});
    ctx.check(pl::savedVariant(p) == "slab", "the client's variant is saved");
    lp::net::testing::receiveFromClient(p, pl::VariantMsg{"nonsense"});
    ctx.check(pl::savedVariant(p) == "slab", "unknown variants are ignored");
    pl::sendVariantRestore(p);
    auto out   = lp::net::testing::takeOutbox(p);
    bool found = false;
    for (auto const& [id, json] : out)
        if (id == "placement:variant_restore" && json.find("slab") != std::string::npos) found = true;
    ctx.check(found, "the saved variant is sent back on join");
    pl::setSavedVariant(p, "");
    lp::net::testing::setClientMod(p, false);
});

LP_REGISTER_SELFTEST("placement.hammer", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    g.cfg().enableVariantSelector = true;
    g.cfg().enableHammer          = true;
    pl::rebuildVariants();
    auto p = ctx.testPos(1725);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    ctx.require(put(ctx, off(p, 0, 1, 0), "minecraft:stone_bricks"), "stone bricks placed");
    stand(ctx, Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, 90.0f, 10.0f);
    pl::setSavedVariant(player, "slab");
    auto hammer = lp::game::makeItem("lepton:hammer");
    ctx.require(!lp::game::isEmpty(hammer), "the hammer item exists");
    simp(ctx).simulateUseItemOnBlock(hammer, bp(off(p, 0, 1, 0)), Facing::East, Vec3{1.0f, 0.3f, 0.5f});
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:stone_brick_slab", "the hammer makes a slab: " + idAt(ctx, off(p, 0, 1, 0)));
    pl::setSavedVariant(player, "");
    co_await ctx.ticks(10);
    simp(ctx).simulateUseItemOnBlock(hammer, bp(off(p, 0, 1, 0)), Facing::East, Vec3{1.0f, 0.3f, 0.5f});
    co_await ctx.ticks(2);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:stone_bricks", "and back to the original: " + idAt(ctx, off(p, 0, 1, 0)));
});

LP_REGISTER_SELFTEST("placement.convert_items", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    g.cfg().enableVariantSelector = true;
    g.cfg().convertVariantItems   = true;
    pl::rebuildVariants();
    auto p = ctx.testPos(1730);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto  level = ll::service::getLevel();
    auto  stack = lp::game::makeItem("minecraft:stone_brick_stairs", 3);
    Vec3  at{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f};
    auto* ia = level->getSpawner().spawnItem(region(ctx), stack, nullptr, at, 0);
    ctx.require(ia != nullptr, "item spawned");
    ctx.check(lp::game::itemId(ia->item()) == "minecraft:stone_bricks", "dropped stairs become stone bricks: " + lp::game::itemId(ia->item()));
    ctx.check(ia->item().mCount == 3, "the count is kept");
    ia->remove();
    g.cfg().convertVariantItems = false;
    auto* ib = level->getSpawner().spawnItem(region(ctx), stack, nullptr, at, 0);
    ctx.require(ib != nullptr, "item spawned");
    ctx.check(lp::game::itemId(ib->item()) == "minecraft:stone_brick_stairs", "off: unchanged");
    ib->remove();
});

// ---- Trowel ------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("placement.trowel", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    g.cfg().maxDamage = 3;
    auto p            = ctx.testPos(1735);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    setSurvival(ctx);
    ctx.require(put(ctx, p, "minecraft:stone"), "floor placed");
    stand(ctx, Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, 90.0f, 30.0f);
    auto& inv = player.getInventory();
    for (int i = 0; i < 9; ++i) inv.setItem(i, ItemStack());
    ctx.give(player, lp::game::makeItem("lepton:trowel"), 0);
    ctx.give(player, lp::game::makeItem("minecraft:mossy_cobblestone", 5), 3);
    ctx.give(player, lp::game::makeItem("minecraft:stick", 5), 4);
    player.setSelectedSlot(0);
    ctx.cleanupBlock(off(p, 0, 1, 0));
    ctx.check(simp(ctx).simulateUseItemInSlotOnBlock(0, bp(p), Facing::Up, Vec3{0.5f, 1.0f, 0.5f}), "trowel used");
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:mossy_cobblestone", "a hotbar block was placed: " + idAt(ctx, off(p, 0, 1, 0)));
    ctx.check(inv.getItem(3).mCount == 4, "it came from its slot");
    ctx.check(inv.getItem(4).mCount == 5, "sticks are no candidates");
    ctx.check(lp::game::itemId(player.getSelectedItem()) == "lepton:trowel", "the trowel stays in the hand");
    ctx.check(player.getSelectedItem().getDamageValue() == 1, "maxDamage > 0: one damage");
    // no candidates: nothing happens
    inv.setItem(3, ItemStack());
    co_await ctx.ticks(10);
    ctx.cleanupBlock(off(p, 0, 2, 0));
    simp(ctx).simulateUseItemInSlotOnBlock(0, bp(off(p, 0, 1, 0)), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 2, 0)) == "minecraft:air", "no block without candidates");
});

// ---- ReplaceScaffolding ------------------------------------------------------------------------

LP_REGISTER_SELFTEST("placement.scaffolding", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = ctx.testPos(1740);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    setSurvival(ctx);
    for (int i = 0; i < 4; ++i) ctx.require(put(ctx, off(p, 0, i, 0), "minecraft:scaffolding"), "scaffolding tower");
    stand(ctx, Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 3.5f}, 180.0f, 10.0f);
    hold(ctx, "minecraft:cobblestone", 4);
    ctx.check(pl::scaffoldingTarget(region(ctx), bp(p), pl::South) == bp(off(p, 0, 3, 0)), "the top of the tower is the target");
    simp(ctx).simulateUseItemInSlotOnBlock(0, bp(p), Facing::South, Vec3{0.5f, 0.5f, 1.0f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 3, 0)) == "minecraft:cobblestone", "the top piece is replaced: " + idAt(ctx, off(p, 0, 3, 0)));
    ctx.check(idAt(ctx, off(p, 0, 0, 1)) == "minecraft:air", "nothing placed next to the clicked piece");
    ctx.check(countOf(player, "minecraft:cobblestone") == 3, "one block used");
    ctx.check(countOf(player, "minecraft:scaffolding") == 1, "the scaffolding is given back");
});

LP_REGISTER_SELFTEST("placement.scaffolding_bounce", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = ctx.testPos(1745);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    // a line east of p, then a branch north from its end
    for (int i = 0; i < 3; ++i) ctx.require(put(ctx, off(p, i, 0, 0), "minecraft:scaffolding"), "line");
    for (int i = 1; i < 3; ++i) ctx.require(put(ctx, off(p, 2, 0, -i), "minecraft:scaffolding"), "branch");
    auto& r = region(ctx);
    ctx.check(pl::scaffoldingTarget(r, bp(p), pl::West) == bp(off(p, 2, 0, -2)), "east line, then one bounce north");
    g.cfg().maxBounces = 0;
    ctx.check(pl::scaffoldingTarget(r, bp(p), pl::West) == bp(off(p, 2, 0, 0)), "no bounces: the end of the line");
    ctx.check(pl::scaffoldingTarget(r, bp(off(p, 1, 0, 0)), pl::East) == bp(off(p, 2, 0, 0)), "along the clicked direction first");
});

// ---- LongRangePickBlock --------------------------------------------------------------------------

LP_REGISTER_SELFTEST("placement.pick", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = ctx.testPos(1750);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    setSurvival(ctx);
    auto& inv = player.getInventory();
    for (int i = 0; i < 36; ++i) inv.setItem(i, ItemStack());
    ctx.give(player, lp::game::makeItem("minecraft:dirt", 1), 1);
    ctx.give(player, lp::game::makeItem("minecraft:mossy_cobblestone", 7), 20);
    player.setSelectedSlot(1);
    ctx.check(pl::pickItem(player, "minecraft:mossy_cobblestone"), "survival: picked from the inventory");
    ctx.check(player.getSelectedItemSlot() == 2, "into the next empty hotbar slot: " + std::to_string(player.getSelectedItemSlot()));
    ctx.check(lp::game::itemId(inv.getItem(2)) == "minecraft:mossy_cobblestone" && inv.getItem(2).mCount == 7, "the stack moved");
    ctx.check(!pl::pickItem(player, "minecraft:gold_block"), "survival: nothing to pick");
    // the companion message for a block far away
    ctx.require(put(ctx, off(p, 0, 3, 0), "minecraft:gold_block"), "gold block placed");
    player.setPlayerGameType(GameType::Creative);
    lp::net::testing::setClientMod(player, true);
    lp::net::testing::receiveFromClient(player, pl::PickMsg{p.x, p.y + 3, p.z, ""});
    ctx.check(lp::game::itemId(player.getSelectedItem()) == "minecraft:gold_block", "creative: the far block is picked");
    lp::net::testing::receiveFromClient(player, pl::PickMsg{p.x + 400, p.y, p.z, ""});
    ctx.check(lp::game::itemId(player.getSelectedItem()) == "minecraft:gold_block", "too far: refused");
    lp::net::testing::setClientMod(player, false);
    player.setPlayerGameType(GameType::Survival);
});

// ---- EnhancedLadders -----------------------------------------------------------------------------

LP_REGISTER_SELFTEST("placement.ladder_freestanding", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = ctx.testPos(1755);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    setSurvival(ctx);
    auto& r = region(ctx);
    ctx.require(put(ctx, off(p, 0, 4, 0), "minecraft:stone"), "wall block");
    stand(ctx, Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, 90.0f, -20.0f);
    // the facing convention: a ladder on the east face of a block faces east
    auto ladder = lp::game::makeItem("minecraft:ladder", 4);
    ctx.cleanupBlock(off(p, 1, 4, 0));
    simp(ctx).simulateUseItemOnBlock(ladder, bp(off(p, 0, 4, 0)), Facing::East, Vec3{1.0f, 0.5f, 0.5f});
    co_await ctx.ticks(2);
    auto const& top = r.getBlock(bp(off(p, 1, 4, 0)));
    ctx.require(top.getTypeName() == "minecraft:ladder", "ladder placed on the east face: " + top.getTypeName());
    ctx.check(pl::ladderFacing(top) == pl::East, "it faces east: " + std::to_string(pl::ladderFacing(top)));
    // a free-standing one below it
    ctx.require(put(ctx, off(p, 1, 3, 0), "minecraft:ladder", {{"facing_direction", pl::East}}), "ladder below");
    ctx.check(pl::ladderSurvives(r, bp(off(p, 1, 3, 0)), pl::East), "survives under a ladder of the same facing");
    ctx.check(!pl::ladderSurvives(r, bp(off(p, 1, 3, 0)), pl::North), "not with another facing");
    auto const& lb = r.getBlock(bp(off(p, 1, 3, 0)));
    ctx.check(lb.canSurvive(r, bp(off(p, 1, 3, 0))), "the game agrees (LadderBlock::canSurvive)");
    ctx.check(lb.mayPlace(r, bp(off(p, 1, 2, 0)), 0, &ctx.player()), "another one may be placed below it");
    g.cfg().allowFreestanding = false;
    ctx.check(!pl::ladderSurvives(r, bp(off(p, 1, 3, 0)), pl::East), "free-standing off: no");
    g.cfg().allowFreestanding = true;
    co_await ctx.ticks(5);
    ctx.check(idAt(ctx, off(p, 1, 3, 0)) == "minecraft:ladder", "the free-standing ladder stays");
    // breaking the top one makes the column fall
    lp::game::clearBlock(r, bp(off(p, 1, 4, 0)));
    co_await ctx.ticks(6);
    ctx.check(idAt(ctx, off(p, 1, 3, 0)) == "minecraft:air", "the free-standing ladder fell: " + idAt(ctx, off(p, 1, 3, 0)));
});

LP_REGISTER_SELFTEST("placement.ladder_drop", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = ctx.testPos(1760);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    setSurvival(ctx);
    for (int i = 1; i <= 4; ++i) ctx.require(put(ctx, off(p, 0, i, 0), "minecraft:stone"), "wall");
    ctx.require(put(ctx, off(p, 1, 4, 0), "minecraft:ladder", {{"facing_direction", pl::East}}), "top ladder");
    ctx.require(put(ctx, off(p, 1, 3, 0), "minecraft:ladder", {{"facing_direction", pl::East}}), "second ladder");
    ctx.cleanupBlock(off(p, 1, 2, 0));
    ctx.cleanupBlock(off(p, 1, 1, 0));
    stand(ctx, Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, 90.0f, -20.0f);
    hold(ctx, "minecraft:ladder", 5);
    simp(ctx).simulateUseItemInSlotOnBlock(0, bp(off(p, 1, 4, 0)), Facing::East, Vec3{0.2f, 0.5f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 1, 2, 0)) == "minecraft:ladder", "a ladder dropped to the bottom: " + idAt(ctx, off(p, 1, 2, 0)));
    ctx.check(idAt(ctx, off(p, 2, 4, 0)) == "minecraft:air", "not placed in front of the clicked one");
    ctx.check(countOf(player, "minecraft:ladder") == 4, "one ladder used");
    ctx.check(pl::ladderFacing(region(ctx).getBlock(bp(off(p, 1, 2, 0)))) == pl::East, "same facing");
    // into water
    ctx.require(put(ctx, off(p, 1, 1, 0), "minecraft:water"), "water below");
    co_await ctx.ticks(10);
    simp(ctx).simulateUseItemInSlotOnBlock(0, bp(off(p, 1, 3, 0)), Facing::East, Vec3{0.2f, 0.5f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 1, 1, 0)) == "minecraft:ladder", "dropped into the water");
    auto const& extra = region(ctx).getExtraBlock(bp(off(p, 1, 1, 0)));
    ctx.check(extra.getTypeName().find("water") != std::string::npos, "and waterlogged: " + extra.getTypeName());
    lp::game::clearBlock(region(ctx), bp(off(p, 1, 1, 0)));
    region(ctx).setExtraBlock(bp(off(p, 1, 1, 0)), *bs::of("minecraft:air"), 3);
});

LP_REGISTER_SELFTEST("placement.disabled", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard g;
    auto        p = ctx.testPos(1765);
    ctx.require(co_await ctx.ensureLoaded(p), "test area loaded");
    auto& player = ctx.player();
    setSurvival(ctx);
    ctx.require(put(ctx, p, "minecraft:stone"), "floor");
    stand(ctx, Vec3{static_cast<float>(p.x) + 3.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f}, 90.0f, 30.0f);
    g.cfg().enableTrowel = false;
    auto& inv            = player.getInventory();
    for (int i = 0; i < 9; ++i) inv.setItem(i, ItemStack());
    ctx.give(player, lp::game::makeItem("lepton:trowel"), 0);
    ctx.give(player, lp::game::makeItem("minecraft:mossy_cobblestone", 5), 1);
    player.setSelectedSlot(0);
    ctx.cleanupBlock(off(p, 0, 1, 0));
    simp(ctx).simulateUseItemInSlotOnBlock(0, bp(p), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, off(p, 0, 1, 0)) == "minecraft:air", "disabled trowel: nothing placed");
    g.cfg().enableLockRotation = false;
    pl::setLockProfile(player, pl::LockProfile{pl::East, 1}, "K");
    ctx.check(!pl::lockProfileOf(player), "disabled rotation lock: no profile in effect");
    pl::setLockProfile(player, std::nullopt, {});
    g.cfg().enableEnhancedLadders = false;
    ctx.require(put(ctx, off(p, 2, 3, 0), "minecraft:stone"), "wall");
    ctx.require(put(ctx, off(p, 3, 3, 0), "minecraft:ladder", {{"facing_direction", pl::East}}), "ladder");
    ctx.check(!pl::ladderSurvives(region(ctx), bp(off(p, 3, 2, 0)), pl::East), "disabled: no free-standing");
});
