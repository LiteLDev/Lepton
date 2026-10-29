// Selftests of U22 throwables. Test positions: dx 2200..2299.

#include <cmath>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/projectile/Arrow.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/level/Level.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/scripting/modules/minecraft/ScriptFacing.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/monster/Slime.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/phys/AABB.h"

#include "feature/throwables/Throwables.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
namespace tw  = lp::feature::throwables;
namespace ent = lp::game::entities;
namespace inv = lp::game::inv;
using lp::core::GlobalPos;
using lp::game::makeItem;
using Facing = ScriptModuleMinecraft::ScriptFacing;

GlobalPos off(GlobalPos p, int dx, int dy, int dz) {
    p.x += dx;
    p.y += dy;
    p.z += dz;
    return p;
}

Vec3 centerOf(GlobalPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

SimulatedPlayer& bot(Context& ctx) { return *static_cast<SimulatedPlayer*>(&ctx.player()); }

/// Uses the held item in the air the way a client's use packet does (GameMode::useItem).
bool useHeld(Player& p) {
    ItemStack s = p.getSelectedItem();
    return p.mGameMode->useItem(s, HandSlot::Mainhand);
}

/// Interacts with an entity the way a client's interact packet does (GameMode::interact).
bool interactWith(Player& p, Actor& a) { return p.mGameMode->interact(a, a.getPosition(), HandSlot::Mainhand); }

/// Hostile mobs need a difficulty above peaceful while the test runs.
struct Hostile {
    SharedTypes::Legacy::Difficulty prev;
    Hostile() {
        auto level = ll::service::getLevel();
        prev       = level->getDifficulty();
        level->setDifficulty(SharedTypes::Legacy::Difficulty::Easy);
    }
    ~Hostile() {
        if (auto level = ll::service::getLevel()) level->setDifficulty(prev);
    }
};

/// Puts the player at `p` (feet) in `mode` with an empty inventory.
void stand(Context& ctx, GlobalPos const& p, GameType mode) {
    auto& pl = ctx.player();
    pl.setPlayerGameType(mode);
    pl.teleport(Vec3{p.x + 0.5f, static_cast<float>(p.y), p.z + 0.5f}, DimensionType(p.dim));
    inv::restore(inv::playerInventory(pl), inv::kPlayerAll, {});
    pl.setOffhandSlot(ItemStack{});
    inv::sync(pl);
}

int countOf(Player& p, std::string_view id) {
    auto& c = inv::playerInventory(p);
    return inv::count(c, [&](ItemStack const& s) { return lp::game::itemId(s) == id; }, inv::all(c));
}

std::string idAt(Context& ctx, GlobalPos const& p) {
    return lp::game::blockIdAt(ctx.player().getDimensionBlockSource(), lp::game::blockPos(p));
}

bool put(Context& ctx, GlobalPos const& p, std::string_view id, lp::game::blockstates::Changes const& changes = {}) {
    auto const* b = lp::game::blockstates::of(id, changes);
    if (!b) return false;
    ctx.cleanupBlock(p);
    return lp::game::setBlock(ctx.player().getDimensionBlockSource(), lp::game::blockPos(p), *b);
}

/// Throws the item in hand at `target` and waits until it is back (max `maxTicks`).
ll::coro::CoroTask<bool> throwAt(Context& ctx, ItemStack stack, Vec3 target, int maxTicks = 120) {
    auto& sp = bot(ctx);
    sp.simulateSetItem(stack, true, 0);
    sp.simulateLookAt(target, sim::LookDuration::Instant);
    co_await ctx.ticks(2);
    useHeld(sp);
    co_await ctx.ticks(1);
    for (int i = 0; i < maxTicks && tw::pickarang::inFlight() > 0; ++i) co_await ctx.ticks(1);
    co_return tw::pickarang::inFlight() == 0;
}

} // namespace

LP_REGISTER_SELFTEST("throwables.config", [](Context& ctx) {
    tw::ThrowablesConfig d{};
    ctx.check(d.pickarangType.harvestLevel == 3 && d.pickarangType.durability == 800 && d.pickarangType.timeout == 20, "pickarang defaults");
    ctx.check(d.flamerangType.harvestLevel == 4 && d.flamerangType.durability == 1040 && d.flamerangType.attackDamage == 3, "flamerang defaults");
    ctx.check(d.pickarangType.canActAsAxe && d.pickarangType.canActAsShovel && !d.pickarangType.canActAsHoe, "tool kinds");
    ctx.check(d.chance == 0.05 && d.eggTime == 12000 && d.enableKotobirb, "parrot egg defaults");
    ctx.check(d.pikeRange == 5 && d.cloudLevelBottom == 191 && d.cloudLevelTop == 196, "pike / cloud defaults");
    ctx.check(!d.extinguishOnMiss && !d.neverUseHeartOfDiamond, "flags default off");
});

LP_REGISTER_SELFTEST("throwables.items", [](Context& ctx) {
    for (auto id : {"lepton:pickarang", "lepton:flamerang", "lepton:torch_arrow", "lepton:bottled_cloud", "lepton:slime_in_a_bucket",
                    "lepton:slime_in_a_bucket_excited", "lepton:egg_parrot_red_blue", "lepton:egg_parrot_blue",
                    "lepton:egg_parrot_green", "lepton:egg_parrot_yellow_blue", "lepton:egg_parrot_gray"}) {
        ctx.check(!lp::game::isEmpty(makeItem(id)), std::string("item registered: ") + id);
    }
    ctx.check(makeItem("lepton:pickarang").mItem->getMaxDamage() == 800, "pickarang durability 800");
    ctx.check(makeItem("lepton:egg_parrot_gray", 64).mCount == 16, "parrot eggs stack to 16");
    ctx.check(lp::game::blockWith("lepton:cloud") != nullptr, "cloud block registered");
    ctx.check(tw::testing::recipeOn("lepton:torch_arrow") && tw::testing::recipeOn("lepton:flamerang_smithing"), "recipes on");
    bool heart = !lp::game::isEmpty(makeItem("lepton:diamond_heart"));
    ctx.check(tw::testing::recipeOn("lepton:pickarang_no_heart") != (heart && tw::testing::recipeOn("lepton:pickarang_heart")),
              "exactly one pickarang recipe");
    ctx.check(tw::testing::itemOn("lepton:pickarang") && tw::testing::itemOn("minecraft:stone"), "items enabled");
    auto& c          = lp::config::mut<tw::ThrowablesConfig>();
    c.enableTorchArrow = false;
    ctx.check(!tw::testing::recipeOn("lepton:torch_arrow") && !tw::testing::itemOn("lepton:torch_arrow"), "disabled module gates its content");
    c.enableTorchArrow = true;
});

LP_REGISTER_SELFTEST("throwables.slime_chunk", [](Context& ctx) {
    int hits = 0;
    for (int x = -50; x < 50; ++x)
        for (int z = -50; z < 50; ++z) hits += tw::isSlimeChunk(x, z) ? 1 : 0;
    ctx.check(hits > 800 && hits < 1200, "about one chunk in ten is a slime chunk (" + std::to_string(hits) + ")");
    ctx.check(tw::isSlimeChunk(3, 7) == tw::isSlimeChunk(3, 7), "deterministic");
});

LP_REGISTER_SELFTEST("throwables.pickarang_mine", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2200);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    stand(ctx, base, GameType::Survival);
    auto target = off(base, 4, 1, 0);
    ctx.require(put(ctx, target, "minecraft:stone"), "stone placed");
    bool back = co_await throwAt(ctx, makeItem("lepton:pickarang"), centerOf(target));
    ctx.check(back, "the pickarang came back");
    ctx.check(idAt(ctx, target) == "minecraft:air", "the stone was mined");
    auto& p = ctx.player();
    ctx.check(countOf(p, "lepton:pickarang") == 1, "the pickarang is back in the inventory");
    ctx.check(countOf(p, "minecraft:cobblestone") == 1, "the drop came back with it");
    auto const& slot0 = inv::playerInventory(p).getItem(0);
    ctx.check(lp::game::itemId(slot0) == "lepton:pickarang" && slot0.getDamageValue() == 1, "back in its slot with 1 damage");
});

LP_REGISTER_SELFTEST("throwables.pickarang_immune", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2210);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    stand(ctx, base, GameType::Survival);
    auto deep = off(base, 4, 1, 0);
    ctx.require(put(ctx, deep, "minecraft:reinforced_deepslate"), "reinforced deepslate placed");
    bool back = co_await throwAt(ctx, makeItem("lepton:pickarang"), centerOf(deep));
    ctx.check(back && idAt(ctx, deep) == "minecraft:reinforced_deepslate", "pickarang_immune blocks are not mined");
    auto obsidian = off(base, 4, 2, 0);
    ctx.require(put(ctx, obsidian, "minecraft:obsidian"), "obsidian placed");
    back = co_await throwAt(ctx, makeItem("lepton:pickarang"), centerOf(obsidian));
    ctx.check(back && idAt(ctx, obsidian) == "minecraft:obsidian", "obsidian is harder than maxHardness 20");
    ctx.check(countOf(ctx.player(), "lepton:pickarang") == 1, "the pickarang is back");
});

LP_REGISTER_SELFTEST("throwables.pickarang_entity", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2220);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    stand(ctx, base, GameType::Survival);
    auto& region = ctx.player().getDimensionBlockSource();
    auto  at     = off(base, 4, 0, 0);
    auto* pig    = ent::spawn(region, "minecraft:pig", Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
    ctx.require(pig != nullptr, "pig spawned");
    auto pigId  = ent::idOf(*pig);
    int  before = pig->getHealth();
    bool back   = co_await throwAt(ctx, makeItem("lepton:pickarang"), Vec3{at.x + 0.5f, at.y + 0.5f, at.z + 0.5f});
    ctx.check(back, "the pickarang came back");
    auto* again = ent::find(pigId);
    int   after = again ? again->getHealth() : 0;
    ctx.info("pig health " + std::to_string(before) + " -> " + std::to_string(after));
    ctx.check(after < before, "the pig was hurt");
    auto const& slot0 = inv::playerInventory(ctx.player()).getItem(0);
    ctx.check(slot0.getDamageValue() == 2, "an entity hit costs 2 durability");
    if (again) ent::despawn(*again);
});

LP_REGISTER_SELFTEST("throwables.pickarang_held", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2280);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    stand(ctx, base, GameType::Survival);
    auto& sp   = bot(ctx);
    auto  dirt = off(base, 1, 0, 0);
    ctx.require(put(ctx, dirt, "minecraft:dirt"), "dirt placed");
    sp.simulateSetItem(makeItem("lepton:pickarang"), true, 0);
    co_await ctx.ticks(2);
    sp.simulateDestroyBlock(lp::game::blockPos(dirt), Facing::West);
    co_await ctx.ticks(40); // bare hands break dirt in 15 ticks
    sp.simulateStopDestroyingBlock();
    // not broken (on the flat world the dirt may turn into grass meanwhile)
    auto left = idAt(ctx, dirt);
    ctx.check(left == "minecraft:dirt" || left == "minecraft:grass_block", "a held pickarang does not mine (" + left + ")");

    auto& region = sp.getDimensionBlockSource();
    auto* pig    = ent::spawn(region, "minecraft:pig", Vec3{dirt.x + 0.5f, float(dirt.y + 1), dirt.z + 0.5f});
    ctx.require(pig != nullptr, "pig spawned");
    auto pigId  = ent::idOf(*pig);
    int  before = pig->getHealth();
    co_await ctx.ticks(2);
    if (auto* p = ent::find(pigId)) sp.simulateAttack(p);
    co_await ctx.ticks(2);
    auto* again = ent::find(pigId);
    ctx.check(!again || again->getHealth() < before, "the melee hit hurt the pig");
    auto const& held = sp.getSelectedItem();
    ctx.info("held pickarang damage after a melee hit: " + std::to_string(held.getDamageValue()));
    ctx.check(lp::game::itemId(held) == "lepton:pickarang" && held.getDamageValue() == 2, "a melee hit costs 2 durability");
    if (again) ent::despawn(*again);
});

LP_REGISTER_SELFTEST("throwables.torch_arrow", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2230);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    stand(ctx, base, GameType::Creative);
    auto& sp     = bot(ctx);
    auto& region = sp.getDimensionBlockSource();
    // a torch arrow flying from `from` with `velocity` (blocks per tick) hits what is in its way
    auto shoot = [&](Vec3 from, Vec3 velocity) -> bool {
        auto* arrow = ent::spawn(region, "lepton:torch_arrow", from, &sp);
        if (!arrow) return false;
        arrow->mBuiltInComponents->mStateVectorComponent->mPosDelta = velocity;
        return true;
    };
    auto wall = off(base, 5, 1, 0);
    ctx.require(put(ctx, wall, "minecraft:stone"), "wall placed");
    auto torchAt = off(base, 4, 1, 0);
    ctx.cleanupBlock(torchAt);
    ctx.require(shoot(Vec3{base.x + 2.5f, base.y + 1.5f, base.z + 0.5f}, Vec3{1.2f, 0.05f, 0.0f}), "torch arrow shot at the wall");
    for (int i = 0; i < 40 && idAt(ctx, torchAt) != "minecraft:torch"; ++i) co_await ctx.ticks(1);
    // the torch must also survive neighbour updates (a wrong facing would pop it off)
    put(ctx, off(torchAt, 0, 1, 0), "minecraft:dirt");
    co_await ctx.ticks(10);
    ctx.info("in front of the wall: " + idAt(ctx, torchAt));
    ctx.check(idAt(ctx, torchAt) == "minecraft:torch", "a wall torch was placed in front of the hit face");
    auto const& torch = region.getBlock(lp::game::blockPos(torchAt));
    auto facing = lp::game::blockstates::getString(torch, "torch_facing_direction").value_or("");
    ctx.info("wall torch facing: " + facing);
    ctx.check(facing != "top" && facing != "unknown" && !facing.empty(), "the torch hangs on the wall");

    auto floor   = off(base, 3, -1, 2);
    auto floorAt = off(base, 3, 0, 2);
    ctx.cleanupBlock(floorAt);
    ctx.require(lp::game::blockIdAt(region, lp::game::blockPos(floor)) != "minecraft:air", "solid floor");
    ctx.require(shoot(Vec3{floorAt.x + 0.5f, floorAt.y + 2.5f, floorAt.z + 0.5f}, Vec3{0.0f, -1.0f, 0.0f}), "torch arrow shot at the floor");
    for (int i = 0; i < 40 && idAt(ctx, floorAt) != "minecraft:torch"; ++i) co_await ctx.ticks(1);
    ctx.check(idAt(ctx, floorAt) == "minecraft:torch", "a standing torch was placed on the top face");
    ctx.check(lp::game::blockstates::getString(region.getBlock(lp::game::blockPos(floorAt)), "torch_facing_direction").value_or("") == "top", "standing torch");
});
LP_REGISTER_SELFTEST("throwables.parrot_egg", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2240);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    stand(ctx, base, GameType::Survival);
    auto& region = ctx.player().getDimensionBlockSource();
    auto  at     = off(base, 2, 0, 0);
    auto* parrot = ent::spawn(region, "minecraft:parrot", Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
    ctx.require(parrot != nullptr, "parrot spawned");
    auto id = ent::idOf(*parrot);
    // untamed: vanilla behaviour, nothing happens
    auto& sp = bot(ctx);
    sp.simulateSetItem(makeItem("minecraft:beetroot_seeds", 16), true, 0);
    ent::triggerEvent(*parrot, "minecraft:on_tame");
    co_await ctx.ticks(2);
    parrot = ent::find(id);
    ctx.require(parrot && parrot->getStatusFlag(ActorFlags::Tamed), "parrot tamed");
    auto& c     = lp::config::mut<tw::ThrowablesConfig>();
    double keep = c.chance;
    c.chance    = 1.0;
    interactWith(sp, *parrot);
    c.chance = keep;
    co_await ctx.ticks(1);
    ctx.check(tw::parroteggs::eggTimer(id) > c.eggTime - 5, "feeding started the egg timer");
    ctx.check(countOf(ctx.player(), "minecraft:beetroot_seeds") == 15, "one seed was eaten");
    tw::parroteggs::setEggTimer(id, 3);
    co_await ctx.ticks(5);
    ctx.check(tw::parroteggs::eggTimer(id) == 0, "the timer ran out");
    Vec3 pp = parrot->getPosition();
    AABB box{Vec3{pp.x - 2, pp.y - 2, pp.z - 2}, Vec3{pp.x + 2, pp.y + 2, pp.z + 2}};
    bool laid = false;
    for (auto* a : ent::inBox(region, box, "minecraft:item")) {
        if (lp::game::itemId(static_cast<ItemActor*>(a)->mItem).starts_with("lepton:egg_parrot_")) laid = true;
        ent::despawn(*a);
    }
    ctx.check(laid, "the parrot laid an egg");

    // throwing an egg hatches a baby parrot of its colour
    auto dropAt = off(base, 6, 0, 0);
    auto* egg   = ent::spawn(region, "lepton:parrot_egg_green", Vec3{dropAt.x + 0.5f, dropAt.y + 2.5f, dropAt.z + 0.5f});
    ctx.require(egg != nullptr, "parrot egg projectile spawned");
    egg->mBuiltInComponents->mStateVectorComponent->mPosDelta = Vec3{0.0f, -0.5f, 0.0f};
    Actor* baby = nullptr;
    for (int i = 0; i < 60 && !baby; ++i) {
        co_await ctx.ticks(1);
        AABB near{Vec3{dropAt.x - 3.0f, dropAt.y - 2.0f, dropAt.z - 3.0f}, Vec3{dropAt.x + 4.0f, dropAt.y + 4.0f, dropAt.z + 4.0f}};
        for (auto* a : ent::inBox(region, near, "minecraft:parrot"))
            if (tw::parroteggs::isBaby(ent::idOf(*a))) baby = a;
    }
    ctx.require(baby != nullptr, "a baby parrot hatched");
    ctx.check(baby->getVariant() == 2, "it has the egg's colour (green)");
    ctx.check(baby->getStatusFlag(ActorFlags::Baby), "it is a baby");
    ent::despawn(*baby);
    if (auto* p2 = ent::find(id)) ent::despawn(*p2);
});

LP_REGISTER_SELFTEST("throwables.skull_pike", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2250);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    stand(ctx, off(base, 30, 0, 0), GameType::Creative);
    auto fence = off(base, 5, 0, 2);
    ctx.require(put(ctx, fence, "minecraft:oak_fence"), "fence placed");
    auto skull = off(fence, 0, 1, 0);
    ctx.cleanupBlock(skull);
    stand(ctx, off(base, 5, 0, 4), GameType::Creative);
    int before = tw::skullpikes::pikeCount();
    auto skullItem = makeItem("minecraft:skeleton_skull");
    bot(ctx).simulateUseItemOnBlock(skullItem, lp::game::blockPos(fence), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    co_await ctx.ticks(3);
    ctx.require(idAt(ctx, skull) == "minecraft:skeleton_skull", "the skull is on the fence");
    ctx.check(tw::skullpikes::pikeCount() == before + 1, "a skull pike was made");

    stand(ctx, off(base, 40, 0, 0), GameType::Creative);
    Hostile hostile;
    auto& region = ctx.player().getDimensionBlockSource();
    auto* zombie = ent::spawn(region, "minecraft:husk", Vec3{fence.x + 3.5f, float(fence.y), fence.z + 0.5f});
    ctx.require(zombie != nullptr, "husk spawned");
    auto zid = ent::idOf(*zombie);
    co_await ctx.ticks(2);
    // check the husk itself: other mobs fleeing other pikes (the gallery's) change fleeingCount()
    auto huskId = static_cast<int64_t>(zid);
    for (int i = 0; i < 10 && !tw::skullpikes::isFleeing(huskId); ++i) {
        // A husk busy attacking does not flee: in the full suite it may target other units' mobs.
        if (auto* z = ent::find(zid); z && z->mTargetId->rawID != -1 && z->mTargetId->rawID != 0) {
            ctx.info("the husk had a target: cleared");
            z->setTarget(nullptr);
        }
        tw::skullpikes::scareNow(); // the escape position is random: a few tries
        co_await ctx.ticks(1);
    }
    ctx.check(tw::skullpikes::isFleeing(huskId), "the husk runs from the pike");
    if (auto* z = ent::find(zid)) ent::despawn(*z);

    lp::game::clearBlock(region, lp::game::blockPos(skull));
    co_await ctx.ticks(2);
    ctx.check(tw::skullpikes::pikeCount() == before, "removing the skull removes the pike");
});

LP_REGISTER_SELFTEST("throwables.cloud", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2260);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    GlobalPos high{base.dim, base.x, 193, base.z};
    ctx.require(put(ctx, off(high, 0, -1, 0), "minecraft:glass"), "platform at cloud height");
    stand(ctx, high, GameType::Survival);
    auto& sp = bot(ctx);
    sp.simulateSetItem(makeItem("minecraft:glass_bottle", 3), true, 0);
    co_await ctx.ticks(2);
    useHeld(sp);
    co_await ctx.ticks(3);
    ctx.check(countOf(ctx.player(), "lepton:bottled_cloud") == 1, "a cloud was bottled at cloud height");
    ctx.check(countOf(ctx.player(), "minecraft:glass_bottle") == 2, "one bottle used");

    inv::restore(inv::playerInventory(ctx.player()), inv::kPlayerAll, {});
    sp.simulateSetItem(makeItem("lepton:bottled_cloud", 2), true, 0);
    sp.simulateLookAt(Vec3{high.x + 20.5f, high.y + 1.62f, high.z + 0.5f}, sim::LookDuration::Instant);
    co_await ctx.ticks(2);
    int clouds = tw::cloud::cloudCount();
    useHeld(sp);
    co_await ctx.ticks(3);
    ctx.check(tw::cloud::cloudCount() == clouds + 1, "a cloud was placed in mid-air");
    GlobalPos cloudAt{high.dim, high.x + 4, high.y + 1, high.z};
    for (int dx = 3; dx <= 5; ++dx)
        if (idAt(ctx, GlobalPos{high.dim, high.x + dx, high.y + 1, high.z}) == "lepton:cloud") cloudAt.x = high.x + dx;
    ctx.cleanupBlock(cloudAt);
    ctx.check(idAt(ctx, cloudAt) == "lepton:cloud", "the cloud is at the end of the reach");
    ctx.check(countOf(ctx.player(), "minecraft:glass_bottle") == 1, "the bottle came back");

    // build on it
    co_await ctx.ticks(10);
    sp.simulateSetItem(makeItem("minecraft:stone", 4), true, 0);
    co_await ctx.ticks(2);
    auto stone = sp.getSelectedItem();
    sp.simulateUseItemOnBlock(stone, lp::game::blockPos(cloudAt), Facing::West, Vec3{0.0f, 0.5f, 0.5f});
    co_await ctx.ticks(3);
    ctx.check(idAt(ctx, cloudAt) == "minecraft:stone", "the block replaced the cloud");
    ctx.check(countOf(ctx.player(), "lepton:bottled_cloud") >= 1, "the cloud went back into the bottle");

    // expiry: place another cloud (the stone gave a bottled cloud back) and let it run out
    inv::restore(inv::playerInventory(ctx.player()), inv::kPlayerAll, {});
    sp.simulateSetItem(makeItem("lepton:bottled_cloud"), true, 0);
    sp.simulateLookAt(Vec3{high.x + 0.5f, high.y + 30.0f, high.z + 0.5f}, sim::LookDuration::Instant);
    co_await ctx.ticks(12);
    useHeld(sp);
    co_await ctx.ticks(3);
    ctx.check(tw::cloud::cloudCount() == clouds + 1, "a second cloud was placed");
    for (int dy = 4; dy <= 7; ++dy) ctx.cleanupBlock(GlobalPos{high.dim, high.x, high.y + dy, high.z});
    tw::cloud::ageAll(2);
    co_await ctx.ticks(4);
    ctx.check(tw::cloud::cloudCount() == clouds, "clouds expire");
});

LP_REGISTER_SELFTEST("throwables.slime_bucket", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2270);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    stand(ctx, base, GameType::Survival);
    Hostile hostile;
    auto& region = ctx.player().getDimensionBlockSource();
    auto  at     = off(base, 2, 0, 0);
    auto* slime  = ent::spawn(region, "minecraft:slime", Vec3{at.x + 0.5f, float(at.y), at.z + 0.5f});
    ctx.require(slime != nullptr, "slime spawned");
    static_cast<Slime*>(slime)->_setSlimeSize(1);
    slime->setNameTag("Bob");
    auto sid = ent::idOf(*slime);
    co_await ctx.ticks(2);
    auto& sp = bot(ctx);
    sp.simulateSetItem(makeItem("minecraft:bucket"), true, 0);
    co_await ctx.ticks(1);
    if (auto* s = ent::find(sid)) interactWith(sp, *s);
    co_await ctx.ticks(2);
    auto bucket = ctx.player().getSelectedItem();
    auto bid    = lp::game::itemId(bucket);
    ctx.check(bid == "lepton:slime_in_a_bucket" || bid == "lepton:slime_in_a_bucket_excited", "the slime is in the bucket");
    ctx.check(ent::find(sid) == nullptr, "the slime left the world");
    ctx.check(bucket.mUserData && bucket.mUserData->contains("slime_nbt"), "the slime's data is stored");
    ctx.check(bucket.getCustomName().find("Bob") != std::string::npos, "named slimes name the bucket");

    co_await ctx.ticks(10);
    auto ground = off(base, 3, -1, 0);
    bucket      = ctx.player().getSelectedItem();
    sp.simulateUseItemOnBlock(bucket, lp::game::blockPos(ground), Facing::Up, Vec3{0.5f, 1.0f, 0.5f});
    co_await ctx.ticks(2);
    ctx.check(lp::game::itemId(ctx.player().getSelectedItem()) == "minecraft:bucket", "the bucket is empty again");
    AABB box{Vec3{ground.x - 2.0f, ground.y - 1.0f, ground.z - 2.0f}, Vec3{ground.x + 3.0f, ground.y + 4.0f, ground.z + 3.0f}};
    Actor* released = nullptr;
    for (auto* a : ent::inBox(region, box, "minecraft:slime")) released = a;
    ctx.check(released != nullptr && released->getNameTag() == "Bob", "the slime was released with its name");
    if (released) ent::despawn(*released);

    // excited in a slime chunk
    int cx = 0, cz = 0;
    for (int r = 0; r < 20 && !tw::isSlimeChunk(cx, cz); ++r) {
        for (int x = -r; x <= r && !tw::isSlimeChunk(cx, cz); ++x)
            for (int z = -r; z <= r; ++z)
                if (tw::isSlimeChunk((base.x >> 4) + x, (base.z >> 4) + z)) {
                    cx = (base.x >> 4) + x;
                    cz = (base.z >> 4) + z;
                    break;
                }
    }
    sp.simulateSetItem(makeItem("lepton:slime_in_a_bucket"), true, 0);
    bool inside = tw::isSlimeChunk(base.x >> 4, base.z >> 4);
    co_await ctx.ticks(12);
    auto want = inside ? "lepton:slime_in_a_bucket_excited" : "lepton:slime_in_a_bucket";
    ctx.check(lp::game::itemId(ctx.player().getSelectedItem()) == want, std::string("the bucket is ") + (inside ? "excited" : "calm") + " here");
    ctx.info("nearest slime chunk " + std::to_string(cx) + "," + std::to_string(cz));
    inv::restore(inv::playerInventory(ctx.player()), inv::kPlayerAll, {});
});

// Everything visual of the unit in one row, for the client scenario `throwables`: skull pikes,
// clouds of every few sizes, hanging pickarang / flamerang models, a stuck torch arrow, parrots
// (adult, hatched baby, kotobirb). With a real player online it also fills their hotbar with the
// unit's items and moves them in front of the row for two views; on a dedicated server the row is
// only built and checked.
LP_REGISTER_SELFTEST("throwables.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2290, -3);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    int   dim    = base.dim;

    // floor and backdrop
    for (int dx = -1; dx <= 24; ++dx) {
        ctx.check(put(ctx, off(base, dx, -1, 0), "minecraft:smooth_stone"), "floor");
        ctx.check(put(ctx, off(base, dx, 3, -1), "minecraft:white_concrete"), "backdrop");
        for (int dy = 0; dy < 3; ++dy) ctx.check(put(ctx, off(base, dx, dy, -1), "minecraft:white_concrete"), "backdrop");
    }
    // skull pikes
    char const* trophies[] = {"minecraft:skeleton_skull", "minecraft:zombie_head", "minecraft:creeper_head"};
    int         pikes0     = tw::skullpikes::pikeCount();
    for (int i = 0; i < 3; ++i) {
        auto fence = off(base, i * 2, 0, 0);
        ctx.check(put(ctx, fence, "minecraft:oak_fence"), "pike fence");
        ctx.check(put(ctx, off(fence, 0, 1, 0), trophies[i], {{"facing_direction", 1}}), "pike trophy");
        tw::skullpikes::onPlaced(ctx.player(), lp::game::blockPos(off(fence, 0, 1, 0)));
    }
    co_await ctx.ticks(2);
    ctx.check(tw::skullpikes::pikeCount() == pikes0 + 3, "three skull pikes");
    // clouds, shrinking from left to right (not tracked: they stay until cleanup)
    int sizes[] = {0, 3, 6, 9};
    for (int i = 0; i < 4; ++i)
        ctx.check(put(ctx, off(base, 7 + i, 1, 0), "lepton:cloud", {{"lepton:size", sizes[i]}}), "cloud");
    // pickarangs hanging in the air
    ctx.check(tw::testing::showRang(dim, Vec3{base.x + 12.5f, base.y + 1.2f, base.z + 0.5f}, false, 1200), "pickarang model");
    ctx.check(tw::testing::showRang(dim, Vec3{base.x + 14.5f, base.y + 1.2f, base.z + 0.5f}, true, 1200), "flamerang model");
    // a torch arrow stuck under an overhang (bottom faces never take torches)
    ctx.check(put(ctx, off(base, 16, 2, 0), "minecraft:stone"), "overhang");
    auto* arrow = ent::spawn(region, "lepton:torch_arrow", Vec3{base.x + 16.5f, base.y + 0.3f, base.z + 0.5f}, &ctx.player());
    ctx.check(arrow != nullptr, "torch arrow spawned");
    int64_t arrowId = arrow ? ent::idOf(*arrow) : 0;
    if (arrow) arrow->mBuiltInComponents->mStateVectorComponent->mPosDelta = Vec3{0.0f, 0.9f, 0.0f};
    // parrots: an adult, a hatched baby and the kotobirb, kept still
    std::vector<int64_t> parrots;
    auto still = [&](Actor* p) {
        if (!p) return;
        tw::setActorFlag(*p, static_cast<int>(ActorFlags::Noai), true);
        parrots.push_back(ent::idOf(*p));
    };
    auto* adult = ent::spawn(region, "minecraft:parrot", Vec3{base.x + 18.5f, float(base.y), base.z + 0.5f});
    if (adult) tw::setActorInt(*adult, 2, 0);
    still(adult);
    tw::parroteggs::onEggHit("lepton:parrot_egg_green", Vec3{base.x + 20.5f, float(base.y), base.z + 0.5f}, dim);
    AABB near{Vec3{base.x + 19.0f, base.y - 1.0f, base.z - 1.0f}, Vec3{base.x + 22.0f, base.y + 3.0f, base.z + 2.0f}};
    for (auto* a : ent::inBox(region, near, "minecraft:parrot"))
        if (tw::parroteggs::isBaby(ent::idOf(*a))) still(a);
    auto* koto = ent::spawn(region, "minecraft:parrot", Vec3{base.x + 22.5f, float(base.y), base.z + 0.5f});
    if (koto) {
        tw::setActorInt(*koto, 2, 4);
        tw::testing::forceKotobirb(ent::idOf(*koto));
    }
    still(koto);
    ctx.check(parrots.size() == 3, "three parrots");
    co_await ctx.ticks(10);
    ctx.check(idAt(ctx, off(base, 16, 1, 0)) != "minecraft:torch" && idAt(ctx, off(base, 16, 0, 0)) != "minecraft:torch",
              "no torch under an overhang");

    Player* viewer = nullptr;
    ctx.player().getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    if (viewer) {
        auto uid = viewer->getOrCreateUniqueID();
        auto& pinv = inv::playerInventory(*viewer);
        char const* items[] = {"lepton:pickarang", "lepton:flamerang", "lepton:torch_arrow", "lepton:egg_parrot_red_blue",
                               "lepton:egg_parrot_yellow_blue", "lepton:egg_parrot_gray", "lepton:bottled_cloud",
                               "lepton:slime_in_a_bucket", "lepton:slime_in_a_bucket_excited"};
        auto saved = inv::snapshot(pinv, inv::kPlayerAll);
        for (int i = 0; i < 9; ++i) pinv.setItem(i, makeItem(items[i], 8));
        char const* more[] = {"lepton:egg_parrot_blue", "lepton:egg_parrot_green"};
        for (int i = 0; i < 2; ++i) pinv.setItem(9 + i, makeItem(more[i], 8));
        inv::sync(*viewer);
        auto view = [&](float dx, float dz, float yaw, float pitch) {
            if (auto* v = ctx.player().getLevel().getPlayer(uid))
                v->teleport(Vec3{base.x + dx, base.y + 0.f, base.z + dz}, DimensionType(dim), Vec2{pitch, yaw});
        };
        ctx.info("THROWABLES view left");
        view(5.f, 5.5f, 180.f, 15.f);
        co_await ctx.ticks(160);
        ctx.info("THROWABLES view right");
        view(17.f, 5.5f, 180.f, 15.f);
        co_await ctx.ticks(160);
        ctx.info("THROWABLES done");
        if (auto* v = ctx.player().getLevel().getPlayer(uid)) {
            inv::restore(inv::playerInventory(*v), inv::kPlayerAll, saved);
            inv::sync(*v);
        }
    }
    for (auto id : parrots)
        if (auto* p = ent::find(id)) ent::despawn(*p);
    if (auto* a = arrowId ? ent::find(arrowId) : nullptr) ent::despawn(*a);
});
