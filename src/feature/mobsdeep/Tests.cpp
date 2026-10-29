// Selftests of U21 mobsdeep (dx 2100..2199). See docs/spec/mobsdeep.md §8.

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/mobsdeep/Mobsdeep.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
namespace ent = lp::game::entities;
namespace md  = lp::feature::mobsdeep;
using lp::game::itemId;
using lp::game::makeItem;

SimulatedPlayer& simOf(Context& ctx) { return static_cast<SimulatedPlayer&>(ctx.player()); }

Vec3 center(lp::core::GlobalPos const& p, float dy = 0.0f) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + dy, static_cast<float>(p.z) + 0.5f};
}

/// Puts the test player in survival (creative players are ignored by stonelings and foxhounds).
void survival(Player& p) { p.setPlayerGameType(GameType::Survival); }

bool dropped(BlockSource& region, Vec3 const& at, std::string const& id) {
    AABB box{Vec3{at.x - 3, at.y - 3, at.z - 3}, Vec3{at.x + 3, at.y + 3, at.z + 3}};
    for (auto* e : ent::inBox(region, box, "minecraft:item"))
        if (itemId(static_cast<ItemActor*>(e)->mItem) == id) return true;
    return false;
}

void clearItems(BlockSource& region, Vec3 const& at) {
    AABB box{Vec3{at.x - 4, at.y - 4, at.z - 4}, Vec3{at.x + 4, at.y + 4, at.z + 4}};
    for (auto* e : ent::inBox(region, box, "minecraft:item")) ent::despawn(*e);
}

Actor* spawnNear(Context& ctx, std::string const& type, float dx) {
    auto& p  = ctx.player();
    auto  at = p.getPosition();
    return ent::spawn(p.getDimensionBlockSource(), type, Vec3{at.x + dx, at.y - 1.62f, at.z});
}
} // namespace

LP_REGISTER_SELFTEST("mobsdeep.config", [](Context& ctx) {
    ctx.check(md::variantOf("minecraft:andesite") == 1 && md::variantOf("minecraft:polished_tuff") == 9, "variant table");
    ctx.check(md::variantOf("minecraft:dirt") == -1 && md::variantOf("") == -1, "non-stone blocks make no stoneling");
    ctx.check(md::bedrockSound("entity.sheep.ambient") == "mob.sheep.say", "java sound ids map to bedrock");
    ctx.check(md::bedrockSound("minecraft:entity.frog.hurt") == "mob.frog.hurt", "minecraft: prefix is accepted");
    ctx.check(md::bedrockSound("random.pop") == "random.pop", "unknown ids pass through");
    auto voices = md::wraithVoices();
    ctx.check(voices.size() == 20, "all 20 default wraith sound sets are valid (got " + std::to_string(voices.size()) + ")");
    ctx.check(!voices.empty() && voices[12].idle.empty() && voices[12].hurt == "mob.creeper.say", "the creeper set has no idle sound");
    ctx.check(md::testing::replacesSkeleton(0.01, -10.0f, false), "deep skeletons are replaced at a low roll");
    ctx.check(!md::testing::replacesSkeleton(0.01, 10.0f, false), "not above maxHeightForSpawn");
    ctx.check(!md::testing::replacesSkeleton(0.2, -10.0f, false), "not above forgottenSpawnRate");
    ctx.check(!md::testing::replacesSkeleton(0.01, -10.0f, true), "never from spawners");
    auto p0 = md::soulBeadPoint(Vec3{0, 64, 0}, 100, 0, 0);
    auto p1 = md::soulBeadPoint(Vec3{0, 64, 0}, 100, 0, 100);
    ctx.check(std::abs(p0.x - 1.5f) < 1e-3 && std::abs(p0.y - 64.0f) < 1e-3, "soul bead starts at cos(0) * 1.5");
    ctx.check(std::abs(p1.y - 66.5f) < 1e-3 && p1.x > 6.0f, "soul bead rises 0.025 and travels 0.08 per tick");
});

LP_REGISTER_SELFTEST("mobsdeep.stoneling.heart", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2100);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:andesite"), "andesite placed");
    auto& sp = simOf(ctx);
    survival(sp);
    sp.simulateSneaking();
    sp.simulateSetItem(makeItem(std::string(md::kDiamondHeart), 2), true, 0);
    co_await ctx.ticks(10);
    sp.simulateUseItemInSlotOnBlock(0, lp::game::blockPos(pos), ScriptModuleMinecraft::ScriptFacing::Up, Vec3{0.5f, 1.0f, 0.5f});
    auto& region = sp.getDimensionBlockSource();
    for (int i = 0; i < 10 && lp::game::blockIdAt(region, lp::game::blockPos(pos)) != "minecraft:air"; ++i) co_await ctx.ticks(1);
    ctx.check(lp::game::blockIdAt(region, lp::game::blockPos(pos)) == "minecraft:air", "the stone became air");
    auto c = center(pos);
    AABB box{Vec3{c.x - 1.5f, c.y - 1.5f, c.z - 1.5f}, Vec3{c.x + 1.5f, c.y + 2.0f, c.z + 1.5f}};
    co_await ctx.ticks(2);
    auto found = ent::inBox(region, box, std::string(md::kStoneling));
    ctx.require(found.size() == 1, "one stoneling appeared");
    auto& st = *found[0];
    ctx.check(md::boolProp(st, "lepton:player_made"), "it is player-made");
    ctx.check(md::intProp(st, "lepton:variant") == 1, "andesite makes an andesite stoneling");
    ctx.check(lp::game::isEmpty(st.getCarriedItem()), "a heart-made stoneling carries nothing");
    ctx.check(sp.getSelectedItem().mCount == 1, "the heart was used");
    ent::despawn(st);
    sp.simulateStopSneaking();
});

LP_REGISTER_SELFTEST("mobsdeep.stoneling.items", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2110);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = simOf(ctx);
    survival(sp);
    auto* st = spawnNear(ctx, std::string(md::kStoneling) + "<lepton:from_heart>", 1.5f);
    ctx.require(st != nullptr, "stoneling spawned");
    auto id = ent::idOf(*st);
    co_await ctx.ticks(3);
    sp.simulateSetItem(makeItem("minecraft:emerald", 3), true, 0);
    sp.simulateInteract(*st);
    co_await ctx.ticks(5);
    st = ent::find(id);
    ctx.require(st != nullptr, "stoneling still there");
    ctx.check(lp::game::isEmpty(st->getCarriedItem()) && itemId(sp.getSelectedItem()) == "minecraft:emerald",
              "a non-stone item without sneaking does nothing");
    sp.simulateSneaking();
    co_await ctx.ticks(2);
    sp.simulateInteract(*st);
    co_await ctx.ticks(5);
    ctx.check(itemId(st->getCarriedItem()) == "minecraft:emerald" && lp::game::isEmpty(sp.getSelectedItem()), "sneaking gives the item");
    sp.simulateStopSneaking();
    co_await ctx.ticks(2);
    sp.simulateInteract(*st);
    co_await ctx.ticks(5);
    ctx.check(lp::game::isEmpty(st->getCarriedItem()) && itemId(sp.getSelectedItem()) == "minecraft:emerald",
              "an empty hand takes it back");
    sp.simulateSetItem(makeItem("minecraft:granite", 2), true, 0);
    co_await ctx.ticks(2);
    sp.simulateInteract(*st);
    co_await ctx.ticks(5);
    ctx.check(md::intProp(*st, "lepton:variant") == 3, "feeding granite makes it a granite stoneling");
    ctx.check(sp.getSelectedItem().mCount == 1, "one granite was eaten");
    ent::despawn(*st);
});

LP_REGISTER_SELFTEST("mobsdeep.stoneling.tame", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2120);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = simOf(ctx);
    survival(sp);
    sp.simulateSneaking();
    co_await ctx.ticks(2);
    auto* st = spawnNear(ctx, std::string(md::kStoneling), 2.0f);
    ctx.require(st != nullptr, "stoneling spawned");
    auto id = ent::idOf(*st);
    co_await ctx.ticks(3);
    ctx.check(!md::boolProp(*st, "lepton:player_made") && !md::boolProp(*st, "lepton:startled"), "wild and calm next to a sneaking player");
    auto food = lp::game::isEmpty(makeItem("lepton:glow_shroom")) ? std::string("minecraft:diamond") : std::string("lepton:glow_shroom");
    sp.simulateSetItem(makeItem(food, 2), true, 0);
    co_await ctx.ticks(2);
    sp.simulateInteract(*st);
    co_await ctx.ticks(3);
    st = ent::find(id);
    ctx.require(st != nullptr, "stoneling still there");
    ctx.check(md::boolProp(*st, "lepton:player_made"), "its food tames it");
    ctx.check(sp.getSelectedItem().mCount == 1, "one food item was used");
    ent::despawn(*st);
    sp.simulateStopSneaking();
});

LP_REGISTER_SELFTEST("mobsdeep.stoneling.startle", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2130);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = simOf(ctx);
    survival(sp);
    sp.simulateStopSneaking();
    auto* st = spawnNear(ctx, std::string(md::kStoneling), 2.5f);
    ctx.require(st != nullptr, "stoneling spawned");
    auto id = ent::idOf(*st);
    bool startled = false;
    for (int i = 0; i < 10 && !startled; ++i) {
        co_await ctx.ticks(1);
        if (auto* a = ent::find(id)) startled = md::boolProp(*a, "lepton:startled");
    }
    ctx.check(startled, "a walking player startles it");
    bool gone = false;
    for (int i = 0; i < 160 && !gone; ++i) {
        co_await ctx.ticks(1);
        gone = ent::find(id) == nullptr;
    }
    ctx.check(gone, "the startled stoneling ran and poofed");
    if (auto* a = ent::find(id)) ent::despawn(*a);
});

LP_REGISTER_SELFTEST("mobsdeep.stoneling.drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2140);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = simOf(ctx);
    sp.setPlayerGameType(GameType::Creative); // creative players don't startle it
    auto* st = spawnNear(ctx, std::string(md::kStoneling), 2.0f);
    ctx.require(st != nullptr, "stoneling spawned");
    auto  id     = ent::idOf(*st);
    auto& region = sp.getDimensionBlockSource();
    co_await ctx.ticks(2);
    clearItems(region, st->getPosition());
    st = ent::find(id);
    ctx.require(st != nullptr, "stoneling still there");
    st->setCarriedItem(makeItem("minecraft:emerald", 3));
    auto at = st->getPosition();
    st->kill();
    co_await ctx.ticks(5);
    ctx.check(dropped(region, at, "minecraft:emerald"), "the carried item dropped");
    ctx.check(dropped(region, at, std::string(md::kDiamondHeart)), "the Heart of Diamond dropped");
    clearItems(region, at);
});

LP_REGISTER_SELFTEST("mobsdeep.forgotten.swap", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2150);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = simOf(ctx);
    sp.setPlayerGameType(GameType::Creative);
    auto* f = spawnNear(ctx, std::string(md::kForgotten), 4.0f);
    ctx.require(f != nullptr, "forgotten spawned");
    auto id = ent::idOf(*f);
    co_await ctx.ticks(3);
    f = ent::find(id);
    ctx.require(f != nullptr, "forgotten still there");
    ctx.check(itemId(f->getCarriedItem()) == "minecraft:bow", "it holds a bow");
    ctx.check(itemId(f->getOffhandSlot()) == "minecraft:iron_sword", "the sword is sheathed");
    ctx.check(md::shouldUseBow(*f), "no target: bow");
    auto* pig = spawnNear(ctx, "minecraft:pig", 6.0f);
    ctx.require(pig != nullptr, "pig spawned");
    MobEffectInstance blind(15u, EffectDuration{400});
    pig->addEffect(blind);
    f->setTarget(pig);
    ctx.check(!md::shouldUseBow(*f), "blind target: sword");
    bool sword = false;
    for (int i = 0; i < 5 && !sword; ++i) {
        co_await ctx.ticks(1);
        if (auto* a = ent::find(id)) {
            a->setTarget(ent::find(ent::idOf(*pig)));
            sword = itemId(a->getCarriedItem()) == "minecraft:iron_sword";
        }
    }
    ctx.check(sword, "it draws the sword");
    if (auto* p = ent::find(ent::idOf(*pig))) ent::despawn(*p);
    if (auto* a = ent::find(id)) ent::despawn(*a);
});

LP_REGISTER_SELFTEST("mobsdeep.foxhound.tame", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2160);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = simOf(ctx);
    survival(sp);
    auto* fox = spawnNear(ctx, std::string(md::kFoxhound), 2.0f);
    ctx.require(fox != nullptr, "foxhound spawned");
    auto id = ent::idOf(*fox);
    md::testing::forceTameChance(1.0);
    sp.simulateSetItem(makeItem("minecraft:coal", 5), true, 0);
    co_await ctx.ticks(3);
    sp.removeEffect(12);
    sp.simulateInteract(*fox);
    co_await ctx.ticks(3);
    fox = ent::find(id);
    ctx.require(fox != nullptr, "foxhound still there");
    bool peaceful = sp.getLevel().getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful;
    if (!peaceful) ctx.check(!fox->isTame() && sp.getSelectedItem().mCount == 5, "coal without fire resistance does nothing");
    MobEffectInstance fireRes(12u, EffectDuration{600});
    sp.addEffect(fireRes);
    sp.simulateInteract(*fox);
    co_await ctx.ticks(5);
    fox = ent::find(id);
    ctx.require(fox != nullptr, "foxhound still there");
    ctx.check(fox->isTame(), "coal under fire resistance tames it");
    ctx.check(fox->getMaxHealth() == 30, "a tame foxhound has 30 HP (got " + std::to_string(fox->getMaxHealth()) + ")");
    md::testing::forceTameChance(std::nullopt);
    sp.removeEffect(12);
    ent::despawn(*fox);
});

LP_REGISTER_SELFTEST("mobsdeep.foxhound.furnace", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2170, 3);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:furnace"), "furnace placed");
    auto& sp     = simOf(ctx);
    auto& region = sp.getDimensionBlockSource();
    co_await ctx.ticks(2);
    auto* be = region.getBlockEntity(lp::game::blockPos(pos));
    ctx.require(be != nullptr, "the furnace has a block entity");
    auto& furnace = static_cast<FurnaceBlockActor&>(*be);
    furnace.setItem(0, makeItem("minecraft:raw_iron", 4));
    furnace.setItem(1, makeItem("minecraft:coal", 4));
    auto* fox = ent::spawn(region, std::string(md::kFoxhound), center(pos, 1.0f));
    ctx.require(fox != nullptr, "foxhound spawned");
    auto id = ent::idOf(*fox);
    ent::triggerEvent(*fox, "lepton:on_tame");
    fox->setSitting(true);
    co_await ctx.ticks(60);
    int progress = furnace.mCookingProgress;
    auto* f2     = ent::find(id);
    ctx.check(f2 && f2->isTame(), "the foxhound is tame");
    // 60 ticks of plain cooking reach ~59; a foxhound adds one tick in three
    ctx.check(progress >= 70 || !lp::game::isEmpty(furnace.getItem(2)), "the furnace cooks faster (progress " + std::to_string(progress) + ")");
    if (f2) ent::despawn(*f2);
});

LP_REGISTER_SELFTEST("mobsdeep.foxhound.ignite", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2175, 6);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = simOf(ctx);
    sp.setPlayerGameType(GameType::Creative);
    auto& region = sp.getDimensionBlockSource();
    auto* fox    = ent::spawn(region, std::string(md::kFoxhound), center(pos, 1.0f));
    auto* sheep  = ent::spawn(region, "minecraft:sheep", Vec3{pos.x + 1.5f, pos.y + 1.0f, pos.z + 0.5f});
    ctx.require(fox && sheep, "foxhound and sheep spawned");
    auto fid = ent::idOf(*fox), sid = ent::idOf(*sheep);
    fox->setTarget(sheep);
    bool burning = false;
    for (int i = 0; i < 200 && !burning; ++i) {
        co_await ctx.ticks(1);
        auto* s = ent::find(sid);
        if (!s) break;
        burning = s->isOnFire();
    }
    ctx.check(burning, "the foxhound's bite sets its prey on fire");
    if (auto* a = ent::find(fid)) ent::despawn(*a);
    if (auto* a = ent::find(sid)) ent::despawn(*a);
});

LP_REGISTER_SELFTEST("mobsdeep.wraith.aggro", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2180);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp = simOf(ctx);
    survival(sp);
    auto* w = spawnNear(ctx, std::string(md::kWraith), 2.0f);
    ctx.require(w != nullptr, "wraith spawned");
    auto id = ent::idOf(*w);
    co_await ctx.ticks(30);
    w = ent::find(id);
    ctx.require(w != nullptr, "wraith still there");
    ctx.check(md::testing::voiceOf(id).has_value(), "the wraith picked a voice");
    ctx.check(w->mTargetId->rawID == 0 || w->mTargetId->rawID == -1, "it ignores players until hit");
    sp.simulateAttack(w);
    co_await ctx.ticks(3);
    // its hits slow the victim (Slowness II): a villager victim works on Peaceful too
    auto* villager = spawnNear(ctx, "minecraft:villager_v2", 3.0f);
    ctx.require(villager != nullptr, "villager spawned");
    auto vid    = ent::idOf(*villager);
    bool slowed = false;
    for (int i = 0; i < 200 && !slowed; ++i) {
        auto* a = ent::find(id);
        auto* v = ent::find(vid);
        if (!a || !v) break;
        a->setTarget(v);
        co_await ctx.ticks(1);
        if ((v = ent::find(vid))) {
            auto const* eff = v->getEffect(2u);
            slowed          = eff && eff->mAmplifier == 1;
        }
    }
    ctx.check(slowed, "its hit slows the victim (Slowness II)");
    if (auto* v = ent::find(vid)) ent::despawn(*v);
    if (auto* a = ent::find(id)) ent::despawn(*a);
});

LP_REGISTER_SELFTEST("mobsdeep.soulbead", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2190);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& sp    = simOf(ctx);
    auto  start = center(pos, 1.25f);
    auto  id    = md::spawnSoulBead(sp, start, pos.x + 200, pos.z);
    ctx.require(id != 0, "soul bead spawned");
    co_await ctx.ticks(20);
    int age = md::testing::soulBeadAge(id);
    ctx.check(age >= 18 && age <= 22, "the bead ages one per tick (" + std::to_string(age) + ")");
    if (auto* b = ent::find(id)) {
        auto expect = md::soulBeadPoint(start, pos.x + 200, pos.z, age - 1);
        auto got    = b->getPosition();
        ctx.check(std::abs(got.x - expect.x) < 1.0f && std::abs(got.z - expect.z) < 1.0f, "the bead follows the spiral trail");
    } else {
        ctx.fail("the bead entity is loaded");
    }
    md::testing::setSoulBeadAge(id, 5998);
    co_await ctx.ticks(6);
    ctx.check(md::testing::soulBeadAge(id) == -1, "its state is gone after 6000 ticks");
    ctx.check(ent::find(id) == nullptr, "the bead vanishes after 6000 ticks");
});

// A row of every mob and variant for screenshots (client scenario `mobsdeep`): stonelings of several
// variants holding items (one with lichen), the Forgotten with its bow and sheathed sword, black / blue /
// red foxhounds and a resting tame one, a wraith, a soul bead and the test player wearing the Forgotten
// Hat. With a real player online, that player gets the three items in the hotbar and is moved in front
// of the row (two views, mobs pinned in place); on a dedicated server the row is only built and checked.
LP_REGISTER_SELFTEST("mobsdeep.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2195, -4);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& sp = simOf(ctx);
    sp.setPlayerGameType(GameType::Creative); // nothing startles, nothing attacks
    auto& region = sp.getDimensionBlockSource();

    struct Slot {
        float                    dx;
        std::string              type;
        std::vector<std::string> events;
        std::string              carried;
    };
    std::string const       heartMade = std::string(md::kStoneling) + "<lepton:from_heart>";
    std::vector<Slot> const slots     = {
        {0.0f,  heartMade,                  {"lepton:variant_0"},                  "minecraft:diamond"    },
        {1.5f,  heartMade,                  {"lepton:variant_1"},                  "minecraft:ender_pearl"},
        {3.0f,  heartMade,                  {"lepton:variant_3"},                  "minecraft:gold_ingot" },
        {4.5f,  heartMade,                  {"lepton:variant_8", "lepton:lichen"}, "minecraft:emerald"    },
        {6.0f,  heartMade,                  {"lepton:variant_10"},                 ""                     },
        {8.0f,  std::string(md::kForgotten), {},                                   ""                     },
        {10.5f, std::string(md::kFoxhound),  {"lepton:set_black"},                 ""                     },
        {12.0f, std::string(md::kFoxhound),  {"lepton:set_blue"},                  ""                     },
        {13.5f, std::string(md::kFoxhound),  {},                                   ""                     },
        {15.0f, std::string(md::kFoxhound),  {"lepton:on_tame", "lepton:rest"},    ""                     },
        {17.0f, std::string(md::kWraith),    {},                                   ""                     },
    };
    float const y  = static_cast<float>(base.y);
    float const z  = static_cast<float>(base.z) + 0.5f;
    auto        at = [&](float dx) { return Vec3{static_cast<float>(base.x) + 0.5f + dx, y, z}; };
    std::vector<int64_t> ids;
    for (auto const& s : slots) {
        auto* a = ent::spawn(region, s.type, at(s.dx));
        ctx.require(a != nullptr, "spawned " + s.type);
        for (auto const& e : s.events) ent::triggerEvent(*a, e);
        if (!s.carried.empty()) a->setCarriedItem(makeItem(s.carried));
        ids.push_back(ent::idOf(*a));
    }
    int64_t bead = md::spawnSoulBead(sp, at(19.0f), base.x + 219, base.z);
    ids.push_back(bead);
    auto cleanup = [&] {
        for (auto id : ids)
            if (auto* a = ent::find(id)) ent::despawn(*a);
    };
    co_await ctx.ticks(4);
    auto* lichen = ent::find(ids[3]);
    ctx.check(lichen && md::intProp(*lichen, "lepton:variant") == 8 && md::boolProp(*lichen, "lepton:lichen"), "a deepslate stoneling with lichen");
    auto* forgotten = ent::find(ids[5]);
    ctx.check(forgotten && itemId(forgotten->getOffhandSlot()) == "minecraft:iron_sword", "the Forgotten carries its sword");
    auto* blue = ent::find(ids[7]);
    ctx.check(blue && blue->getVariant() == 1, "a blue soulhound");
    auto* black = ent::find(ids[6]);
    ctx.check(black && black->getVariant() == 2, "a black foxhound");
    // a plain spawn outside a Soul Sand Valley is red (1 in 256 is black: spawn again then)
    auto* red = ent::find(ids[8]);
    for (int retry = 0; retry < 3 && red && red->getVariant() == 2; ++retry) {
        ent::despawn(*red);
        red     = ent::spawn(region, slots[8].type, at(slots[8].dx));
        ids[8]  = red ? ent::idOf(*red) : 0;
        co_await ctx.ticks(2);
        red = ent::find(ids[8]);
    }
    ctx.check(red && red->getVariant() == 0, "a red foxhound (got " + std::to_string(red ? red->getVariant() : -1) + ")");
    auto* resting = ent::find(ids[9]);
    ctx.check(resting && md::boolProp(*resting, "lepton:resting"), "the tame foxhound rests");
    ctx.check(bead != 0, "a soul bead flies");

    Player* viewer = nullptr;
    sp.getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (!viewer) {
        cleanup();
        co_return;
    }
    auto uid = viewer->getOrCreateUniqueID();
    ctx.give(*viewer, makeItem(std::string(md::kDiamondHeart)), 0);
    ctx.give(*viewer, makeItem(std::string(md::kForgottenHat)), 1);
    ctx.give(*viewer, makeItem(std::string(md::kSoulBeadItem), 16), 2);
    sp.setArmor(SharedTypes::Legacy::ArmorSlot::Head, makeItem(std::string(md::kForgottenHat)));
    auto view = [&](float dx, float dist) {
        if (auto* v = sp.getLevel().getPlayer(uid))
            v->teleport(Vec3{static_cast<float>(base.x) + 0.5f + dx, y + 1.0f, z + dist}, DimensionType(base.dim), Vec2{15.f, 180.f});
    };
    for (int phase = 0; phase < 2; ++phase) {
        ctx.info(phase == 0 ? "GALLERY view stonelings" : "GALLERY view mobs");
        if (phase == 0) view(3.0f, 4.0f);
        else view(13.0f, 7.0f);
        for (int i = 0; i < 150; ++i) {
            for (size_t k = 0; k < slots.size(); ++k)
                if (auto* a = ent::find(ids[k])) a->teleport(at(slots[k].dx), DimensionType(base.dim), Vec2{0.f, 0.f});
            if (auto* s = ent::find(ent::idOf(sp))) s->teleport(at(19.0f), DimensionType(base.dim), Vec2{0.f, 0.f});
            co_await ctx.ticks(1);
        }
    }
    ctx.info("GALLERY done");
    sp.setArmor(SharedTypes::Legacy::ArmorSlot::Head, ItemStack{});
    cleanup();
});
