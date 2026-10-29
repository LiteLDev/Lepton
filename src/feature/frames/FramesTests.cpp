// Selftests of U24 frames (`frames.*`). Test positions: dx 2400..2499.

#include <string>

#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/CauldronBlockActor.h"
#include "mc/world/level/block/actor/ItemFrameBlockActor.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/level/Level.h"
#include "mc/world/phys/AABB.h"

#include "feature/frames/Frames.h"
#include "feature/frames/SignAttachment.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {

using lp::core::GlobalPos;
using lp::selftest::Context;
namespace fr = lp::feature::frames;
namespace bs = lp::game::blockstates;
using Facing = ScriptModuleMinecraft::ScriptFacing;

/// Runs a test with Quark's defaults and restores the configured values afterwards.
struct ConfigGuard {
    fr::FramesConfig saved = lp::config::get<fr::FramesConfig>();
    ConfigGuard() { lp::config::mut<fr::FramesConfig>() = fr::FramesConfig{}; }
    ~ConfigGuard() {
        lp::config::mut<fr::FramesConfig>() = saved;
        fr::glass::applyConfig();
    }
    fr::FramesConfig* operator->() { return &lp::config::mut<fr::FramesConfig>(); }
};

BlockSource& regionOf(Context& ctx) { return ctx.player().getDimensionBlockSource(); }

GlobalPos off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

bool put(Context& ctx, GlobalPos const& p, std::string_view id, bs::Changes const& changes = {}) {
    auto const* b = bs::of(id, changes);
    ctx.cleanupBlock(p);
    return b && lp::game::setBlock(regionOf(ctx), lp::game::blockPos(p), *b);
}

Block const& at(Context& ctx, GlobalPos const& p) { return regionOf(ctx).getBlock(lp::game::blockPos(p)); }
std::string  idAt(Context& ctx, GlobalPos const& p) { return at(ctx, p).getTypeName(); }

/// Puts `stack` into the selected hotbar slot.
void hold(Context& ctx, ItemStack const& stack) {
    auto& p   = ctx.player();
    auto& inv = lp::game::inv::playerInventory(p);
    inv.setItem(p.getSelectedItemSlot(), stack);
}

ItemStack dyed(bool glow, int rgb, int count = 1) {
    auto s = lp::game::makeItem(glow ? fr::kDyedGlowFrame : fr::kDyedFrame, count);
    fr::setItemColor(s, rgb);
    return s;
}

std::vector<Actor*> itemsNear(Context& ctx, GlobalPos const& p) {
    AABB box{Vec3{p.x - 2.f, p.y - 2.f, p.z - 2.f}, Vec3{p.x + 3.f, p.y + 3.f, p.z + 3.f}};
    return lp::game::entities::inBox(regionOf(ctx), box, "minecraft:item");
}
ItemStack const& itemOf(Actor* a) { return static_cast<ItemActor&>(*a).mItem; }
void clearItems(Context& ctx, GlobalPos const& p) {
    for (auto* a : itemsNear(ctx, p)) lp::game::entities::despawn(*a);
}
int countItems(Context& ctx, GlobalPos const& p, std::string_view id) {
    int n = 0;
    for (auto* a : itemsNear(ctx, p))
        if (lp::game::itemId(itemOf(a)) == id) n += itemOf(a).mCount;
    return n;
}

void stepAside(Context& ctx, GlobalPos const& p) {
    ctx.player().teleport(Vec3{p.x + 3.5f, static_cast<float>(p.y), p.z + 3.5f}, DimensionType(p.dim));
}

int nearlyRgb(int a, int b) {
    int d = 0;
    for (int s = 0; s < 24; s += 8) d = std::max(d, std::abs(((a >> s) & 0xFF) - ((b >> s) & 0xFF)));
    return d;
}

} // namespace

LP_REGISTER_SELFTEST("frames.config", [](Context& ctx) {
    fr::FramesConfig c;
    ctx.check(c.enableGlassItemFrame && c.glassItemFramesUpdateMaps && !c.glassItemFramesUpdateMapsEveryTick, "glass frame defaults");
    ctx.check(c.itemRenderScale == 1.5, "itemRenderScale 1.5");
    ctx.check(c.enableDyeableItemFrames && c.enableMoreBannerLayers && c.enableMapWashing, "modules on by default");
    ctx.check(c.layerLimit == 16, "layerLimit 16");
});

LP_REGISTER_SELFTEST("frames.sign_rule", [](Context& ctx) {
    using fr::SignAttachment;
    using fr::SignInfo;
    using fr::SignKind;
    // a standing sign facing north (rotation 8): a frame facing north is in front of it
    ctx.check(fr::signAttachment(2, SignInfo{SignKind::Standing, 8, 0}) == SignAttachment::StandingInFront, "north frame on a north sign");
    ctx.check(fr::signAttachment(3, SignInfo{SignKind::Standing, 8, 0}) == SignAttachment::StandingBehind, "south frame behind it");
    ctx.check(fr::signAttachment(4, SignInfo{SignKind::Standing, 8, 0}) == SignAttachment::NotAttached, "west frame: side");
    ctx.check(fr::signAttachment(3, SignInfo{SignKind::Standing, 1, 0}) == SignAttachment::StandingInFront, "22.5 degrees off still attaches");
    ctx.check(fr::signAttachment(3, SignInfo{SignKind::Standing, 2, 0}) == SignAttachment::StandingInFront,
              "45 degrees one way attaches (Quark's 0.01 tie-break)");
    ctx.check(fr::signAttachment(3, SignInfo{SignKind::Standing, 14, 0}) == SignAttachment::NotAttached,
              "45 degrees the other way does not");
    ctx.check(fr::signAttachment(1, SignInfo{SignKind::Standing, 0, 0}) == SignAttachment::NotAttached, "floor frames never attach");
    ctx.check(fr::signAttachment(3, SignInfo{SignKind::CeilingHanging, 0, 0}) == SignAttachment::HangingInFront, "ceiling hanging sign");
    ctx.check(fr::signAttachment(3, SignInfo{SignKind::Wall, 0, 3}) == SignAttachment::WallSign, "wall sign facing the frame");
    ctx.check(fr::signAttachment(2, SignInfo{SignKind::Wall, 0, 3}) == SignAttachment::NotAttached, "wall sign facing away");
    ctx.check(fr::signAttachment(2, SignInfo{SignKind::WallHanging, 0, 3}) == SignAttachment::HangingFromWall, "wall hanging sign, either side");
    ctx.check(fr::signAttachment(4, SignInfo{SignKind::WallHanging, 0, 3}) == SignAttachment::NotAttached, "wall hanging sign, sideways");
    // two frames at exactly 90 degrees either side: only one attaches (Quark's 0.01 tweak)
    bool a = fr::signAttachment(4, SignInfo{SignKind::Standing, 2, 0}) != SignAttachment::NotAttached;
    ctx.check(!a, "45 degrees is not < 45");
});

LP_REGISTER_SELFTEST("frames.recipes", [](Context& ctx) {
    ConfigGuard cfg;
    fr::glass::applyConfig();
    ctx.check(fr::hasRecipe("lepton:glass_item_frame"), "8 glass panes + item frame -> 2 glass frames");
    ctx.check(fr::hasRecipe("lepton:glowing_glass_item_frame"), "glow ink sac + glass frame -> glow glass frame");
    cfg->enableGlassItemFrame = false;
    fr::glass::applyConfig();
    ctx.check(!fr::hasRecipe("lepton:glass_item_frame") && !fr::hasRecipe("lepton:glowing_glass_item_frame"),
              "disabled GlassItemFrame removes the recipes");
    cfg->enableGlassItemFrame = true;
    fr::glass::applyConfig();
    ctx.check(fr::hasRecipe("lepton:glass_item_frame"), "re-enabling restores them");
});

LP_REGISTER_SELFTEST("frames.glass_use", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto s = off(ctx.testPos(2400), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(s), "test area loaded");
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    stepAside(ctx, s);
    ctx.require(put(ctx, s, "minecraft:stone"), "support placed");
    auto f = off(s, 0, 0, 1);
    ctx.cleanupBlock(f);
    clearItems(ctx, s);

    // place with a real use of the item on the stone's south face
    auto frameItem = lp::game::makeItem(fr::kGlassFrame, 4);
    hold(ctx, frameItem);
    sp.simulateUseItemOnBlock(frameItem, lp::game::blockPos(s), Facing::South, Vec3{0.5f, 0.5f, 1.0f});
    co_await ctx.ticks(3);
    ctx.require(idAt(ctx, f) == fr::kGlassFrame, "the glass frame was placed (" + idAt(ctx, f) + ")");
    ctx.check(bs::getString(at(ctx, f), "minecraft:block_face").value_or("") == "south", "it faces south");
    ctx.require(fr::testing::glassRecord(f).has_value(), "the frame is tracked");

    // put a diamond in
    auto diamond = lp::game::makeItem("minecraft:diamond", 2);
    hold(ctx, diamond);
    co_await ctx.ticks(10);
    sp.simulateUseItemOnBlock(diamond, lp::game::blockPos(f), Facing::South, Vec3{0.5f, 0.5f, 0.1f});
    co_await ctx.ticks(3);
    auto rec = fr::testing::glassRecord(f);
    ctx.require(rec && rec->item.find("diamond") != std::string::npos, "the diamond went in");
    ctx.check(sp.getSelectedItem().mCount == 1, "one diamond was used");
    ctx.check(bs::getBool(at(ctx, f), "lepton:filled").value_or(false), "the frame model is hidden");
    Actor* display = rec->display ? lp::game::entities::find(rec->display) : nullptr;
    ctx.require(display != nullptr, "a display entity shows the item");
    ctx.check(lp::game::itemId(display->getCarriedItem()) == "minecraft:diamond", "the display holds the diamond");
    ctx.check(fr::intProperty(*display, "lepton:facing").value_or(-1) == 3, "display facing south");
    ctx.check(fr::floatProperty(*display, "lepton:scale").value_or(0) == 1.5f, "display scale = itemRenderScale");

    // rotate
    co_await ctx.ticks(10);
    sp.simulateUseItemOnBlock(diamond, lp::game::blockPos(f), Facing::South, Vec3{0.5f, 0.5f, 0.1f});
    co_await ctx.ticks(3);
    rec = fr::testing::glassRecord(f);
    ctx.check(rec && rec->rotation == 1, "the second use rotates the item");
    ctx.check(fr::intProperty(*display, "lepton:rotation").value_or(-1) == 1, "display rotation 1");

    // punch: the item drops, the frame stays
    ctx.check(fr::testing::punchGlass(sp, f), "punching a filled frame only drops its item");
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, f) == fr::kGlassFrame, "the frame stays");
    ctx.check(countItems(ctx, f, "minecraft:diamond") == 1, "the diamond dropped");
    rec = fr::testing::glassRecord(f);
    ctx.check(rec && rec->item.empty() && rec->display == 0, "the frame is empty, no display");
    ctx.check(!bs::getBool(at(ctx, f), "lepton:filled").value_or(true), "the frame model is back");
    ctx.check(!fr::testing::punchGlass(sp, f), "punching an empty frame breaks it");
    clearItems(ctx, s);

    // removing the block drops the held item; the frame forgets it
    hold(ctx, diamond);
    co_await ctx.ticks(10);
    ctx.check(fr::testing::useGlass(sp, f), "the diamond goes in again");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(f));
    co_await ctx.ticks(2);
    ctx.check(!fr::testing::glassRecord(f), "a removed frame is forgotten");
    ctx.check(countItems(ctx, f, "minecraft:diamond") == 1, "its item dropped");
    clearItems(ctx, s);
});

LP_REGISTER_SELFTEST("frames.glass_place_rules", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto s = off(ctx.testPos(2405), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(s), "test area loaded");
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    stepAside(ctx, s);
    ctx.require(put(ctx, s, "minecraft:stone"), "support placed");
    auto item = lp::game::makeItem(fr::kGlowingGlassFrame, 4);

    // on top of the stone: a floor frame
    hold(ctx, item);
    sp.simulateUseItemOnBlock(item, lp::game::blockPos(s), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    co_await ctx.ticks(3);
    auto up = off(s, 0, 1, 0);
    ctx.cleanupBlock(up);
    ctx.check(idAt(ctx, up) == fr::kGlowingGlassFrame, "a glow glass frame lies on the stone");
    ctx.check(bs::getString(at(ctx, up), "minecraft:block_face").value_or("") == "up", "facing up");

    // disabled: nothing is placed
    cfg->enableGlassItemFrame = false;
    co_await ctx.ticks(10);
    auto east = off(s, 1, 0, 0);
    ctx.cleanupBlock(east);
    hold(ctx, item);
    sp.simulateUseItemOnBlock(item, lp::game::blockPos(s), Facing::East, Vec3{1.0f, 0.5f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, east) != fr::kGlowingGlassFrame, "a disabled module places nothing");
    clearItems(ctx, s);
});

LP_REGISTER_SELFTEST("frames.glass_survive", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto s = off(ctx.testPos(2410), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(s), "test area loaded");
    stepAside(ctx, s);
    clearItems(ctx, s);
    ctx.require(put(ctx, s, "minecraft:stone"), "support placed");
    auto f = off(s, 0, 0, 1);
    ctx.require(put(ctx, f, fr::kGlassFrame, {{"minecraft:block_face", std::string("south")}}), "frame placed");
    co_await ctx.ticks(2);
    ctx.require(fr::testing::glassRecord(f).has_value(), "the frame is tracked");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(s));
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, f) != fr::kGlassFrame, "without its support the frame breaks");
    ctx.check(countItems(ctx, f, fr::kGlassFrame) == 1, "and drops itself");
    clearItems(ctx, s);
});

LP_REGISTER_SELFTEST("frames.glass_container", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto s = off(ctx.testPos(2415), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(s), "test area loaded");
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    stepAside(ctx, s);
    ctx.require(put(ctx, s, "minecraft:chest"), "chest placed");
    auto f = off(s, 0, 0, 1);
    ctx.require(put(ctx, f, fr::kGlassFrame, {{"minecraft:block_face", std::string("south")}}), "frame on the chest");
    co_await ctx.ticks(2);
    hold(ctx, lp::game::makeItem("minecraft:apple"));
    ctx.check(fr::testing::useGlass(sp, f), "an apple goes into the frame");
    hold(ctx, ItemStack{});
    ctx.check(fr::testing::useGlass(sp, f), "using it again...");
    co_await ctx.ticks(2);
    ctx.check(sp.mContainerManager.get() != nullptr, "...opens the chest behind it");
    auto rec = fr::testing::glassRecord(f);
    ctx.check(rec && rec->rotation == 0, "and does not rotate the apple");
    sp.setSneaking(true);
    ctx.check(fr::testing::useGlass(sp, f), "sneaking...");
    rec = fr::testing::glassRecord(f);
    ctx.check(rec && rec->rotation == 1, "...rotates the item instead");
    sp.setSneaking(false);
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(f));
    co_await ctx.ticks(2);
    clearItems(ctx, s);
});

LP_REGISTER_SELFTEST("frames.glass_sign", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto s = ctx.testPos(2420); // ground level: standing signs stand on the ground
    ctx.require(co_await ctx.ensureLoaded(s), "test area loaded");
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    stepAside(ctx, off(s, 0, 0, 3));
    struct Case {
        std::string_view sign;
        bs::Changes      states;
        int              dx, dz;
        std::string_view face;
        int              expect;
        char const*      what;
    };
    std::vector<Case> cases{
        {"minecraft:standing_sign", {{"ground_sign_direction", 8}}, 0, -1, "north", 1, "in front of a standing sign"},
        {"minecraft:standing_sign", {{"ground_sign_direction", 8}}, 0, 1, "south", 2, "behind a standing sign"},
        {"minecraft:wall_sign", {{"facing_direction", 3}}, 0, 1, "south", 3, "on a wall sign"},
        {"minecraft:oak_hanging_sign", {{"hanging", true}, {"attached_bit", true}, {"ground_sign_direction", 0}}, 0, 1, "south", 5,
         "in front of a ceiling hanging sign"},
        {"minecraft:oak_hanging_sign", {{"hanging", false}, {"facing_direction", 2}}, 0, -1, "north", 4,
         "against a wall hanging sign"},
    };
    int i = 0;
    for (auto const& c : cases) {
        auto sign = off(s, i * 3, 0, 0);
        ++i;
        // what the sign hangs on: the block behind a wall sign, above a ceiling hanging sign, beside a
        // wall hanging sign
        if (c.expect == 3) put(ctx, off(sign, 0, 0, -1), "minecraft:stone");
        if (c.expect == 5) put(ctx, off(sign, 0, 1, 0), "minecraft:stone");
        if (c.expect == 4) {
            put(ctx, off(sign, -1, 0, 0), "minecraft:stone");
            put(ctx, off(sign, 1, 0, 0), "minecraft:stone");
        }
        ctx.require(put(ctx, sign, c.sign, c.states), std::string("placed ") + std::string(c.sign));
        auto f = off(sign, c.dx, 0, c.dz);
        ctx.require(put(ctx, f, fr::kGlassFrame, {{"minecraft:block_face", std::string(c.face)}}), "frame placed");
        co_await ctx.ticks(2);
        hold(ctx, lp::game::makeItem("minecraft:emerald"));
        sp.setSneaking(true); // don't use the sign through the frame
        fr::testing::useGlass(sp, f);
        sp.setSneaking(false);
        fr::testing::glassSweep();
        co_await ctx.ticks(1);
        auto rec = fr::testing::glassRecord(f);
        ctx.check(rec && rec->attachment == c.expect, std::string("frame ") + c.what + " (got " +
                                                        std::to_string(rec ? rec->attachment : -1) + ")");
        ctx.check(idAt(ctx, f) == fr::kGlassFrame, std::string("a frame survives ") + c.what);
        if (rec && rec->display)
            if (auto* d = lp::game::entities::find(rec->display))
                ctx.check(fr::intProperty(*d, "lepton:sign").value_or(-1) == c.expect, "the display knows the attachment");
        lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(f));
    }
    co_await ctx.ticks(2);
    clearItems(ctx, s);
});

LP_REGISTER_SELFTEST("frames.glass_maps", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto s = off(ctx.testPos(2440), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(s), "test area loaded");
    auto& sp = ctx.player();
    stepAside(ctx, s);
    ctx.require(put(ctx, s, "minecraft:stone"), "support placed");
    auto f = off(s, 0, 0, 1);
    ctx.require(put(ctx, f, fr::kGlassFrame, {{"minecraft:block_face", std::string("south")}}), "frame placed");
    co_await ctx.ticks(2);
    hold(ctx, lp::game::makeItem("minecraft:filled_map"));
    ctx.require(fr::testing::useGlass(sp, f), "a map goes into the frame");
    int before = fr::testing::mapUpdates();
    fr::testing::glassSweep();
    ctx.check(fr::testing::mapUpdates() > before, "the sweep updates the framed map");
    cfg->glassItemFramesUpdateMapsEveryTick = true;
    before = fr::testing::mapUpdates();
    co_await ctx.ticks(3);
    ctx.check(fr::testing::mapUpdates() >= before + 2, "every tick with glassItemFramesUpdateMapsEveryTick");
    cfg->glassItemFramesUpdateMapsEveryTick = false;
    cfg->glassItemFramesUpdateMaps          = false;
    before = fr::testing::mapUpdates();
    fr::testing::glassSweep();
    ctx.check(fr::testing::mapUpdates() == before, "no updates with glassItemFramesUpdateMaps off");
    auto rec = fr::testing::glassRecord(f);
    if (rec && rec->display)
        if (auto* d = lp::game::entities::find(rec->display))
            ctx.check(fr::intProperty(*d, "lepton:kind").value_or(-1) == 5, "a map display kind");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(f));
    co_await ctx.ticks(2);
    clearItems(ctx, s);
});

namespace {
/// A water cauldron (level 6) at `p`, dyed `rgb` when >= 0.
bool waterCauldron(Context& ctx, GlobalPos const& p, int rgb) {
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(p));
    if (!put(ctx, p, "minecraft:cauldron", {{"fill_level", 6}, {"cauldron_liquid", std::string("water")}})) return false;
    if (rgb >= 0)
        if (auto* be = regionOf(ctx).getBlockEntity(lp::game::blockPos(p)))
            static_cast<CauldronBlockActor&>(*be).setCustomColor(mce::Color(static_cast<uint>(rgb)));
    return true;
}
int fillLevel(Context& ctx, GlobalPos const& p) { return bs::getInt(at(ctx, p), "fill_level").value_or(-1); }

std::optional<int> colorInInventory(Player& p, std::string_view id) {
    auto& inv = lp::game::inv::playerInventory(p);
    for (int i = 0; i < inv.getContainerSize(); ++i) {
        auto const& s = inv.getItem(i);
        if (lp::game::itemId(s) == id) return fr::itemColor(s);
    }
    return std::nullopt;
}
} // namespace

LP_REGISTER_SELFTEST("frames.dye", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto c = off(ctx.testPos(2450), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(c), "test area loaded");
    auto& sp = ctx.player();
    stepAside(ctx, c);
    lp::game::inv::playerInventory(sp).removeAllItems();
    constexpr int red = 0xB02E26;
    ctx.require(waterCauldron(ctx, c, red), "red water cauldron");
    auto info = fr::cauldronAt(regionOf(ctx), lp::game::blockPos(c));
    ctx.require(info && info->water && info->dyed, "the cauldron holds dyed water");
    ctx.check(nearlyRgb(info->rgb, red) <= 2, "its colour reads back");

    hold(ctx, lp::game::makeItem("minecraft:frame", 2));
    ctx.require(fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "an item frame dips into dyed water");
    ctx.check(sp.getSelectedItem().mCount == 1 && lp::game::itemId(sp.getSelectedItem()) == "minecraft:frame", "one frame used");
    auto col = colorInInventory(sp, fr::kDyedFrame);
    ctx.check(col && nearlyRgb(*col, info->rgb) <= 2, "a red dyed frame came out");
    ctx.check(fillLevel(ctx, c) == 4, "the water dropped one level");

    // re-dye a blue glow frame red
    hold(ctx, dyed(true, 0x3C44AA));
    ctx.check(fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "a dyed glow frame is re-dyed");
    ctx.check(lp::game::itemId(sp.getSelectedItem()) == fr::kDyedGlowFrame
                  && nearlyRgb(fr::itemColor(sp.getSelectedItem()).value_or(0), info->rgb) <= 2,
              "it took the new colour");

    // sneaking: no dyeing (the frame would hang on the cauldron)
    hold(ctx, lp::game::makeItem("minecraft:frame"));
    sp.setSneaking(true);
    ctx.check(!fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "sneaking skips the cauldron");
    sp.setSneaking(false);

    // disabled module
    cfg->enableDyeableItemFrames = false;
    ctx.check(!fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "disabled DyeableItemFrames does not dye");
    lp::game::inv::playerInventory(sp).removeAllItems();
});

LP_REGISTER_SELFTEST("frames.wash", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto c = off(ctx.testPos(2455), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(c), "test area loaded");
    auto& sp = ctx.player();
    stepAside(ctx, c);
    lp::game::inv::playerInventory(sp).removeAllItems();
    ctx.require(waterCauldron(ctx, c, -1), "water cauldron");

    hold(ctx, dyed(false, 0x5E7C16));
    ctx.require(fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "a dyed frame is washed");
    ctx.check(lp::game::itemId(sp.getSelectedItem()) == "minecraft:frame", "back to an item frame");
    ctx.check(fillLevel(ctx, c) == 4, "one level of water used");

    hold(ctx, lp::game::makeItem("minecraft:frame"));
    ctx.check(!fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "a plain frame on plain water hangs as usual");

    // MapWashing
    hold(ctx, lp::game::makeItem("minecraft:filled_map", 3));
    ctx.require(fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "filled maps are washed");
    ctx.check(lp::game::itemId(sp.getSelectedItem()) == "minecraft:empty_map" && sp.getSelectedItem().mCount == 3,
              "into as many blank maps");
    ctx.check(fillLevel(ctx, c) == 2, "one more level used");
    cfg->enableMapWashing = false;
    hold(ctx, lp::game::makeItem("minecraft:filled_map"));
    ctx.check(!fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "disabled MapWashing does not wash");
    cfg->enableMapWashing = true;
    // dyed water does not wash maps
    ctx.require(waterCauldron(ctx, c, 0xF9801D), "orange water cauldron");
    ctx.check(!fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "dyed water does not wash maps");
    // the last level empties the cauldron
    ctx.require(waterCauldron(ctx, c, -1), "water again");
    for (int i = 0; i < 3; ++i) {
        hold(ctx, lp::game::makeItem("minecraft:filled_map"));
        fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c));
    }
    ctx.check(fillLevel(ctx, c) == 0, "three washes empty a full cauldron");
    hold(ctx, lp::game::makeItem("minecraft:filled_map"));
    ctx.check(!fr::onCauldronUse(sp, regionOf(ctx), lp::game::blockPos(c)), "an empty cauldron washes nothing");
    lp::game::inv::playerInventory(sp).removeAllItems();
});

LP_REGISTER_SELFTEST("frames.dyed_frames", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto s = off(ctx.testPos(2460), 0, 1, 0);
    ctx.require(co_await ctx.ensureLoaded(s), "test area loaded");
    auto& sp = static_cast<SimulatedPlayer&>(ctx.player());
    stepAside(ctx, s);
    clearItems(ctx, s);
    ctx.require(put(ctx, s, "minecraft:stone"), "support placed");
    constexpr int green = 0x5E7C16;

    // a real use of a dyed frame on the stone's south face
    auto item = dyed(false, green, 2);
    hold(ctx, item);
    sp.simulateUseItemOnBlock(item, lp::game::blockPos(s), Facing::South, Vec3{0.5f, 0.5f, 1.0f});
    co_await ctx.ticks(3);
    auto f = off(s, 0, 0, 1);
    ctx.cleanupBlock(f);
    ctx.require(idAt(ctx, f) == "minecraft:frame", "a vanilla item frame was placed (" + idAt(ctx, f) + ")");
    ctx.check(bs::getInt(at(ctx, f), "facing_direction").value_or(-1) == 3, "facing south");
    auto rec = fr::testing::dyedRecord(f);
    ctx.require(rec.has_value(), "the dyed frame is tracked");
    ctx.check(nearlyRgb(rec->rgb, green) <= 2 && !rec->glow, "with its colour");
    ctx.check(sp.getSelectedItem().mCount == 1, "one dyed frame used");
    Actor* overlay = rec->overlay ? lp::game::entities::find(rec->overlay) : nullptr;
    ctx.require(overlay != nullptr, "a tinted overlay is shown");
    co_await ctx.ticks(1);
    ctx.check(nearlyRgb(fr::intProperty(*overlay, "lepton:color").value_or(0), green) <= 2, "the overlay has the colour");
    ctx.check(fr::intProperty(*overlay, "lepton:facing").value_or(-1) == 3, "and the facing");

    // a glow one on top
    auto glowItem = dyed(true, 0x169C9C);
    hold(ctx, glowItem);
    ctx.check(fr::testing::placeDyed(sp, regionOf(ctx), lp::game::blockPos(s), 1), "a dyed glow frame on top");
    co_await ctx.ticks(2);
    auto up = off(s, 0, 1, 0);
    ctx.cleanupBlock(up);
    ctx.check(idAt(ctx, up) == "minecraft:glow_frame", "a glow item frame was placed");
    auto upRec = fr::testing::dyedRecord(up);
    ctx.check(upRec && upRec->glow, "tracked as glow");

    // breaking: the dropped vanilla frame item becomes the dyed one
    clearItems(ctx, s);
    fr::dropItem(regionOf(ctx), lp::game::makeItem("minecraft:frame"), Vec3{f.x + 0.5f, f.y + 0.5f, f.z + 0.5f});
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(f));
    co_await ctx.ticks(3);
    ctx.check(!fr::testing::dyedRecord(f), "the broken frame is forgotten");
    ctx.check(countItems(ctx, f, "minecraft:frame") == 0, "no plain frame item is left");
    bool found = false;
    for (auto* a : itemsNear(ctx, f))
        if (lp::game::itemId(itemOf(a)) == fr::kDyedFrame) found = nearlyRgb(fr::itemColor(itemOf(a)).value_or(0), green) <= 2;
    ctx.check(found, "the dyed frame item dropped, still green");
    co_await ctx.ticks(1);
    ctx.check(lp::game::entities::find(rec->overlay) == nullptr, "the overlay is gone");

    // disabled: dyed frames place nothing
    cfg->enableDyeableItemFrames = false;
    co_await ctx.ticks(10);
    hold(ctx, item);
    auto east = off(s, 1, 0, 0);
    ctx.cleanupBlock(east);
    sp.simulateUseItemOnBlock(item, lp::game::blockPos(s), Facing::East, Vec3{1.0f, 0.5f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, east) != "minecraft:frame", "disabled DyeableItemFrames places nothing");
    lp::game::clearBlock(regionOf(ctx), lp::game::blockPos(up));
    co_await ctx.ticks(3);
    clearItems(ctx, s);
});

LP_REGISTER_SELFTEST("frames.banner_limit", [](Context& ctx) {
    ConfigGuard cfg;
    ctx.check(fr::banners::effectiveLimit() == 6, "default 16 is capped at Bedrock's 6 drawn layers");
    cfg->layerLimit = 3;
    ctx.check(fr::banners::effectiveLimit() == 3, "a lower limit applies");
    ctx.check(fr::banners::mayAddPattern(2) && !fr::banners::mayAddPattern(3), "3 layers is full");
    cfg->layerLimit = 0;
    ctx.check(fr::banners::effectiveLimit() == 1, "the limit is at least 1 (Quark's @Config.Min)");
    cfg->layerLimit             = 3;
    cfg->enableMoreBannerLayers = false;
    ctx.check(fr::banners::effectiveLimit() == 6, "disabled: vanilla 6");
});

// ---- gallery (client e2e screenshots) --------------------------------------------------------------
// Builds a wall of glass frames (an item, a tool, a block, a banner, a shield, a map, rotated items,
// glow frames, empty frames, floor frames), frames on signs and dyed frames, then moves the real
// player (if any) in front of each part: "FRAMES view <part>" lines for the client scenario.
LP_REGISTER_SELFTEST("frames.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    namespace game = lp::game;
    ConfigGuard cfg;
    auto base = ctx.testPos(2470);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    ctx.require(co_await ctx.ensureLoaded(off(base, 40, 0, 0)), "test area loaded (east)");
    auto& sp     = ctx.player();
    auto& region = regionOf(ctx);
    stepAside(ctx, off(base, 0, 0, 6));
    std::vector<GlobalPos> glass;

    auto glassFrame = [&](GlobalPos const& f, std::string_view id, std::string_view face, ItemStack const& item,
                          int rotations) {
        if (!put(ctx, f, id, {{"minecraft:block_face", std::string(face)}})) return;
        glass.push_back(f);
        if (game::isEmpty(item)) return;
        hold(ctx, item);
        fr::testing::useGlass(sp, f);
        hold(ctx, ItemStack{});
        sp.setSneaking(true);
        for (int i = 0; i < rotations; ++i) fr::testing::useGlass(sp, f);
        sp.setSneaking(false);
    };

    // the glass wall: x 0..13, z 0, y 0..3; frames on its south side
    for (int x = 0; x < 14; ++x)
        for (int y = 0; y < 4; ++y) put(ctx, off(base, x, y, 0), "minecraft:stone");
    co_await ctx.ticks(2);
    std::vector<std::pair<std::string, int>> items{
        {"minecraft:apple", 0},  {"minecraft:diamond_sword", 0}, {"minecraft:grass_block", 0}, {"minecraft:banner", 0},
        {"minecraft:shield", 0}, {"minecraft:filled_map", 0},    {"minecraft:arrow", 1},       {"minecraft:arrow", 2},
        {"minecraft:torch", 0},  {"minecraft:bow", 0},           {"minecraft:lantern", 0},     {"minecraft:compass", 3},
    };
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        auto const& [id, rot] = items[i];
        glassFrame(off(base, i, 1, 1), fr::kGlassFrame, "south", game::makeItem(id), rot);
        glassFrame(off(base, i, 2, 1), fr::kGlowingGlassFrame, "south", game::makeItem(id), rot);
    }
    glassFrame(off(base, 0, 3, 1), fr::kGlassFrame, "south", ItemStack{}, 0);
    glassFrame(off(base, 1, 3, 1), fr::kGlowingGlassFrame, "south", ItemStack{}, 0);
    glassFrame(off(base, 3, 0, 2), fr::kGlassFrame, "up", game::makeItem("minecraft:diamond"), 0);
    glassFrame(off(base, 5, 0, 2), fr::kGlassFrame, "up", ItemStack{}, 0);

    // frames on signs (x 18..)
    auto sx = off(base, 18, 0, 1);
    put(ctx, sx, "minecraft:standing_sign", {{"ground_sign_direction", 8}});
    glassFrame(off(sx, 0, 0, 1), fr::kGlassFrame, "south", game::makeItem("minecraft:emerald"), 0);
    put(ctx, off(sx, 3, 0, -1), "minecraft:stone");
    put(ctx, off(sx, 3, 0, 0), "minecraft:wall_sign", {{"facing_direction", 3}});
    glassFrame(off(sx, 3, 0, 1), fr::kGlassFrame, "south", game::makeItem("minecraft:emerald"), 0);
    put(ctx, off(sx, 6, 2, 0), "minecraft:stone");
    put(ctx, off(sx, 6, 1, 0), "minecraft:oak_hanging_sign",
        {{"hanging", true}, {"attached_bit", true}, {"ground_sign_direction", 0}});
    glassFrame(off(sx, 6, 1, 1), fr::kGlassFrame, "south", game::makeItem("minecraft:emerald"), 0);
    put(ctx, off(sx, 9, 0, 0), "minecraft:standing_sign", {{"ground_sign_direction", 0}});
    glassFrame(off(sx, 9, 0, 1), fr::kGlassFrame, "south", game::makeItem("minecraft:emerald"), 0);

    // dyed frames (x 30..37) on a wall
    auto dx = off(base, 30, 0, 0);
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 3; ++y) put(ctx, off(dx, x, y, 0), "minecraft:stone");
    std::vector<std::pair<int, bool>> colors{
        {0xB02E26, false}, {0x3C44AA, false}, {0x5E7C16, false}, {0xFED83D, false},
        {0xF38BAA, true},  {0x169C9C, true},  {0x1D1D21, false}, {0xF9FFFE, false},
    };
    for (int i = 0; i < static_cast<int>(colors.size()); ++i) {
        hold(ctx, dyed(colors[i].second, colors[i].first));
        fr::testing::placeDyed(sp, region, game::blockPos(off(dx, i, 1, 0)), 3);
        ctx.cleanupBlock(off(dx, i, 1, 1));
    }
    hold(ctx, dyed(false, 0xF9801D));
    fr::testing::placeDyed(sp, region, game::blockPos(off(dx, 2, 0, 1)), 1);
    ctx.cleanupBlock(off(dx, 2, 1, 1));
    co_await ctx.ticks(3);
    // a map in the second and an apple in the third dyed frame
    if (auto* be = region.getBlockEntity(game::blockPos(off(dx, 1, 1, 1))))
        static_cast<ItemFrameBlockActor&>(*be).setItem(region, ItemInstance(game::makeItem("minecraft:filled_map")), nullptr);
    if (auto* be = region.getBlockEntity(game::blockPos(off(dx, 2, 1, 1))))
        static_cast<ItemFrameBlockActor&>(*be).setItem(region, ItemInstance(game::makeItem("minecraft:apple")), nullptr);
    co_await ctx.ticks(12);
    auto mapRec = fr::testing::dyedRecord(off(dx, 1, 1, 1));
    ctx.check(mapRec.has_value(), "the dyed frames are tracked");
    if (mapRec && mapRec->overlay)
        if (auto* o = game::entities::find(mapRec->overlay))
            ctx.check(fr::intProperty(*o, "lepton:map").value_or(0) == 1, "the overlay switches to the map frame shape");
    fr::testing::glassSweep();
    co_await ctx.ticks(2);

    Player* viewer = nullptr;
    sp.getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (viewer) {
        auto uid  = viewer->getOrCreateUniqueID();
        auto view = [&](GlobalPos const& at, float dz, float pitch) {
            if (auto* v = sp.getLevel().getPlayer(uid))
                v->teleport(Vec3{at.x + 0.5f, static_cast<float>(at.y), at.z + dz}, DimensionType(at.dim), Vec2{pitch, 180.f});
        };
        ctx.info("FRAMES view glass");
        view(off(base, 6, 0, 0), 6.f, 15.f);
        co_await ctx.ticks(160);
        ctx.info("FRAMES view glass-close");
        view(off(base, 2, 1, 0), 3.5f, 10.f);
        co_await ctx.ticks(120);
        ctx.info("FRAMES view signs");
        view(off(sx, 4, 0, -1), 5.f, 15.f);
        co_await ctx.ticks(120);
        ctx.info("FRAMES view dyed");
        view(off(dx, 3, 0, 0), 5.5f, 15.f);
        co_await ctx.ticks(120);
        ctx.info("FRAMES done");
    }
    for (auto const& g : glass) fr::testing::emptyGlass(g);
    co_await ctx.ticks(1);
});
