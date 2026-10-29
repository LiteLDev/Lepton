// Selftests of U20 mobsoverworld (dx 2000..2099).

#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/projectile/AbstractArrow.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"

#include "feature/mobsoverworld/Common.h"
#include "feature/mobsoverworld/Config.h"
#include "feature/mobsoverworld/Features.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
namespace mo       = lp::feature::mobsoverworld;
namespace entities = lp::game::entities;
using Cause        = SharedTypes::Legacy::ActorDamageCause;

/// Despawns the test's entities when the test ends (also on failure).
struct Spawned {
    std::vector<int64_t> ids;
    ~Spawned() {
        for (auto id : ids) {
            if (auto* a = entities::find(id)) entities::despawn(*a);
        }
    }
    Actor* add(Actor* a) {
        if (a) ids.push_back(entities::idOf(*a));
        return a;
    }
};

Vec3 at(lp::core::GlobalPos const& p, float dx = 0, float dz = 0) {
    return {static_cast<float>(p.x) + 0.5f + dx, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f + dz};
}

void hold(Context& ctx, Player& player, std::string const& id, int count = 1) {
    player.setSelectedItem(lp::game::makeItem(id, count));
    (void)ctx;
}

void tame(Actor& shiba, Player& owner) {
    entities::triggerEvent(shiba, "lepton:on_tame");
    shiba.setOwner(owner.getOrCreateUniqueID());
}

float knockbackResistance(Actor& a) {
    return a.getAttribute(SharedAttributes::KNOCKBACK_RESISTANCE()).mPtr->mCurrentValue;
}

void removeItems(BlockSource& region, Vec3 const& c) {
    AABB box{Vec3{c.x - 4, c.y - 3, c.z - 4}, Vec3{c.x + 4, c.y + 4, c.z + 4}};
    for (auto* d : entities::inBox(region, box, "minecraft:item")) entities::despawn(*d);
}

} // namespace

LP_REGISTER_SELFTEST("mobsoverworld.crab_variant", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2000);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    std::vector<Actor*> crabs;
    for (int i = 0; i < 6; ++i) crabs.push_back(s.add(entities::spawn(0, mo::kCrab, at(pos, 0, static_cast<float>(i % 3)))));
    ctx.require(crabs[0] != nullptr, "a crab spawns");
    co_await ctx.ticks(2);
    for (auto id : s.ids) {
        auto* c = entities::find(id);
        ctx.require(c != nullptr, "crab still exists");
        ctx.check(c->getVariant() >= 0 && c->getVariant() <= 2, "colour is red, blue or green");
        ctx.check(c->getMarkVariant() == 0 || c->getMarkVariant() == 1, "mould flag is 0 or 1");
        ctx.check(mo::crabs::sizeOf(*c) == 0, "normal size");
    }
});

LP_REGISTER_SELFTEST("mobsoverworld.crab_lightning", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2005);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  crab = s.add(entities::spawn(0, mo::kCrab, at(pos)));
    ctx.require(crab != nullptr, "a crab spawns");
    int64_t id = entities::idOf(*crab);
    co_await ctx.ticks(2);
    crab = entities::find(id);
    int health = crab->getHealth();
    crab->hurtByCause(6, Cause::Lightning);
    co_await ctx.ticks(2);
    crab = entities::find(id);
    ctx.require(crab != nullptr, "crab survives lightning");
    ctx.check(crab->getHealth() >= health, "lightning does no damage");
    ctx.check(mo::crabs::sizeOf(*crab) == 1, "lightning makes it grow (scale 3)");
    ctx.check(!mo::crabs::strikeLightning(*crab, mo::gameTick()), "cooldown blocks a second growth");
    // Contact (cactus) and fire tick are ignored.
    health = crab->getHealth();
    crab->hurtByCause(4, Cause::Contact);
    crab->hurtByCause(4, Cause::FireTick);
    co_await ctx.ticks(1);
    crab = entities::find(id);
    ctx.check(crab && crab->getHealth() == health, "immune to cactus and burning");
});

LP_REGISTER_SELFTEST("mobsoverworld.crab_armor", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2010);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  crab = s.add(entities::spawn(0, mo::kCrab, at(pos)));
    ctx.require(crab != nullptr, "a crab spawns");
    int64_t id = entities::idOf(*crab);
    co_await ctx.ticks(2);
    crab = entities::find(id);
    crab->hurtByCause(10, Cause::EntityAttack, ctx.player());
    co_await ctx.ticks(1);
    crab = entities::find(id);
    ctx.require(crab != nullptr, "crab survives");
    // 10 * (1 - 0.6 / 25) = 9.76 damage with armour 3, toughness 2.
    ctx.check(crab->getHealth() >= 10 && crab->getHealth() <= 11, "armour reduces 10 damage to 9.76 (health " + std::to_string(crab->getHealth()) + ")");
});

LP_REGISTER_SELFTEST("mobsoverworld.crab_bucket", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2015);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  crab = s.add(entities::spawn(0, mo::kCrab, at(pos)));
    ctx.require(crab != nullptr, "a crab spawns");
    int64_t id = entities::idOf(*crab);
    co_await ctx.ticks(2);
    crab        = entities::find(id);
    int variant = crab->getVariant();
    int mold    = crab->getMarkVariant();
    auto& player = ctx.player();
    hold(ctx, player, "minecraft:water_bucket");
    ctx.check(mo::crabs::interact(player, *crab) == mo::Handled::Yes, "the water bucket catches the crab");
    co_await ctx.ticks(2);
    ctx.check(entities::find(id) == nullptr, "the crab is gone");
    ItemStack bucket = player.getSelectedItem();
    ctx.require(lp::game::itemId(bucket) == "lepton:crab_bucket", "the player holds a Bucket of Crab");
    Actor* back = s.add(mo::crabs::releaseFromBucket(bucket, 0, at(pos, 1)));
    ctx.require(back != nullptr, "the bucket releases a crab");
    int64_t backId = entities::idOf(*back);
    co_await ctx.ticks(2);
    back = entities::find(backId);
    ctx.require(back != nullptr, "the released crab exists");
    ctx.check(back->getVariant() == variant && back->getMarkVariant() == mold, "colour and mould are kept (" + std::to_string(variant) + "/" + std::to_string(mold) + " -> " +
                  std::to_string(back->getVariant()) + "/" + std::to_string(back->getMarkVariant()) + ")");
    ctx.check(mo::boolProperty(*back, "lepton:from_bucket"), "the released crab is marked FromBucket");
    hold(ctx, player, "minecraft:air", 0);
});

LP_REGISTER_SELFTEST("mobsoverworld.crab_rave", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2020);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    ctx.require(ctx.setBlock(pos, "minecraft:jukebox"), "jukebox placed");
    co_await ctx.ticks(2);
    auto& region  = ctx.player().getDimensionBlockSource();
    auto  bp      = lp::game::blockPos(pos);
    auto* be      = region.getBlockEntity(bp);
    ctx.require(be != nullptr, "jukebox block entity");
    Actor* crab = s.add(entities::spawn(0, mo::kCrab, at(pos, 2)));
    ctx.require(crab != nullptr, "a crab spawns");
    int64_t id = entities::idOf(*crab);
    static_cast<JukeboxBlockActor*>(be)->setRecord(lp::game::makeItem("minecraft:music_disc_cat"), true);
    mo::crabs::jukeboxStarted(0, bp);
    bool raving = false;
    for (int i = 0; i < 6 && !raving; ++i) {
        co_await ctx.ticks(10);
        if (auto* c = entities::find(id)) raving = mo::crabs::isRaving(*c);
    }
    ctx.check(raving, "the crab raves next to a playing jukebox");
    lp::game::clearBlock(region, bp);
    bool stopped = false;
    for (int i = 0; i < 6 && !stopped; ++i) {
        co_await ctx.ticks(10);
        if (auto* c = entities::find(id)) stopped = !mo::crabs::isRaving(*c);
    }
    ctx.check(stopped, "the crab stops when the jukebox is gone");
    removeItems(region, at(pos));
});

LP_REGISTER_SELFTEST("mobsoverworld.brewing", [](Context& ctx) {
    ctx.require(lp::config::get<mo::Config>().enableBrewing, "brewing enabled");
    int  aux = -1;
    auto r   = mo::resilience::brew("minecraft:potion", 4, "lepton:crab_shell", &aux);
    ctx.check(r == "lepton:resilience_potion", "awkward + crab shell = stability (" + r + ")");
    r = mo::resilience::brew("minecraft:potion", 0, "lepton:crab_shell", &aux);
    ctx.check(r == "minecraft:potion" && aux == 1, "water + crab shell = mundane");
    r = mo::resilience::brew("lepton:resilience_potion", 0, "minecraft:redstone", &aux);
    ctx.check(r == "lepton:long_resilience_potion", "+ redstone = long (" + r + ")");
    r = mo::resilience::brew("lepton:resilience_potion", 0, "minecraft:glowstone_dust", &aux);
    ctx.check(r == "lepton:strong_resilience_potion", "+ glowstone = strong (" + r + ")");
    r = mo::resilience::brew("lepton:resilience_potion", 0, "minecraft:gunpowder", &aux);
    ctx.check(r == "lepton:resilience_splash_potion", "+ gunpowder = splash (" + r + ")");
    r = mo::resilience::brew("lepton:resilience_splash_potion", 0, "minecraft:dragon_breath", &aux);
    ctx.check(r == "lepton:resilience_lingering_potion", "+ dragon's breath = lingering (" + r + ")");
    r = mo::resilience::brew("minecraft:splash_potion", 4, "lepton:crab_shell", &aux);
    ctx.check(r == "lepton:resilience_splash_potion", "awkward splash + crab shell = splash stability (" + r + ")");
});

LP_REGISTER_SELFTEST("mobsoverworld.resilience", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& player = ctx.player();
    co_await ctx.ticks(1);
    float base = knockbackResistance(player);
    mo::resilience::onDrink(player, "lepton:resilience_potion");
    int amp = -1;
    ctx.check(mo::resilience::remaining(player, &amp) > 3500 && amp == 0, "potion gives Resilience I for 3:00");
    ctx.check(knockbackResistance(player) > base + 0.49f, "+0.5 knockback resistance");
    mo::resilience::onDrink(player, "lepton:strong_resilience_potion");
    (void)mo::resilience::remaining(player, &amp);
    ctx.check(amp == 1, "the strong potion upgrades to Resilience II");
    ctx.check(knockbackResistance(player) > base + 0.99f, "+1.0 knockback resistance at level II");
    mo::resilience::onDrink(player, "minecraft:milk_bucket");
    ctx.check(mo::resilience::remaining(player) == 0, "milk clears the effect");
    ctx.check(knockbackResistance(player) < base + 0.01f, "the modifier is removed");
});

LP_REGISTER_SELFTEST("mobsoverworld.shiba_interact", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2030);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  shiba = s.add(entities::spawn(0, mo::kShiba, at(pos)));
    ctx.require(shiba != nullptr, "a shiba spawns");
    int64_t id = entities::idOf(*shiba);
    auto&   player = ctx.player();
    co_await ctx.ticks(2);
    shiba = entities::find(id);
    ctx.check(shiba->getMaxHealth() == 8, "a wild shiba has 8 health");
    tame(*shiba, player);
    co_await ctx.ticks(2);
    shiba = entities::find(id);
    ctx.require(shiba->isTame(), "tamed");
    ctx.check(shiba->getMaxHealth() == 20, "a tame shiba has 20 health");
    ctx.check(mo::intProperty(*shiba, "lepton:collar") == 14, "the collar is red by default");
    hold(ctx, player, "minecraft:blue_dye", 2);
    ctx.check(mo::shiba::interact(player, *shiba) == mo::Handled::Yes, "dye is used");
    co_await ctx.ticks(2);
    shiba = entities::find(id);
    ctx.check(mo::intProperty(*shiba, "lepton:collar") == 11, "the collar turns blue");
    hold(ctx, player, "minecraft:iron_sword");
    ctx.check(mo::shiba::interact(player, *shiba) == mo::Handled::Yes, "the sword is taken");
    ctx.check(lp::game::itemId(shiba->getCarriedItem()) == "minecraft:iron_sword", "the shiba holds the sword");
    ctx.check(lp::game::isEmpty(player.getSelectedItem()), "the player's sword is gone");
    hold(ctx, player, "minecraft:stick");
    ctx.check(mo::shiba::interact(player, *shiba) == mo::Handled::Yes, "using it again takes the item back");
    ctx.check(lp::game::isEmpty(shiba->getCarriedItem()), "the mouth is empty");
    hold(ctx, player, "minecraft:wolf_armor");
    ctx.check(mo::shiba::interact(player, *shiba) == mo::Handled::Yes, "the owner puts wolf armor on");
    co_await ctx.ticks(2);
    shiba = entities::find(id);
    ctx.check(mo::boolProperty(*shiba, "lepton:armor"), "armour is shown");
    int health = shiba->getHealth();
    shiba->hurtByCause(4, Cause::EntityAttack, player);
    co_await ctx.ticks(1);
    shiba = entities::find(id);
    ctx.check(shiba->getHealth() == health, "the armour absorbs the hit");
    ctx.check(shiba->getArmor(SharedTypes::Legacy::ArmorSlot::Body).getDamageValue() > 0, "the armour takes the damage");
    hold(ctx, player, "minecraft:shears");
    ctx.check(mo::shiba::interact(player, *shiba) == mo::Handled::Yes, "shears remove the armour");
    hold(ctx, player, "minecraft:air", 0);
    removeItems(player.getDimensionBlockSource(), at(pos));
});

LP_REGISTER_SELFTEST("mobsoverworld.shiba_fetch", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2040);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    auto&   player = ctx.player();
    Actor*  shiba  = s.add(entities::spawn(0, mo::kShiba, at(pos)));
    ctx.require(shiba != nullptr, "a shiba spawns");
    int64_t id = entities::idOf(*shiba);
    co_await ctx.ticks(2);
    tame(*entities::find(id), player);
    co_await ctx.ticks(2);
    Actor* arrow = s.add(entities::spawn(0, "minecraft:arrow", player.getPosition(), &player));
    ctx.require(arrow != nullptr, "an arrow spawns");
    int64_t arrowId = entities::idOf(*arrow);
    static_cast<AbstractArrow*>(arrow)->mIsPlayerOwned = true;
    static_cast<AbstractArrow*>(arrow)->mIsCreative    = false;
    arrow->setOwner(player.getOrCreateUniqueID());
    co_await ctx.ticks(3);
    shiba = entities::find(id);
    ctx.require(shiba != nullptr, "shiba exists");
    ctx.check(mo::shiba::fetching(*shiba) == arrowId, "the shiba goes for its owner's arrow");
    // Bring the arrow next to it: it picks it up.
    if (auto* a = entities::find(arrowId)) a->teleport(shiba->getPosition(), 0);
    bool got = false;
    for (int i = 0; i < 20 && !got; ++i) {
        co_await ctx.ticks(2);
        shiba = entities::find(id);
        got   = shiba && lp::game::itemId(shiba->getCarriedItem()) == "minecraft:arrow";
    }
    ctx.check(got, "the shiba carries the arrow");
});

LP_REGISTER_SELFTEST("mobsoverworld.shiba_darkness", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2050);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    auto&   player = ctx.player();
    Actor*  shiba  = s.add(entities::spawn(0, mo::kShiba, at(pos)));
    ctx.require(shiba != nullptr, "a shiba spawns");
    int64_t id = entities::idOf(*shiba);
    co_await ctx.ticks(2);
    tame(*entities::find(id), player);
    co_await ctx.ticks(2);
    if (auto* sh = entities::find(id)) sh->setSitting(false);
    hold(ctx, player, "minecraft:torch");
    BlockPos spot;
    bool     found = false;
    for (int i = 0; i < 40 && !found; ++i) {
        co_await ctx.ticks(2);
        if (auto* sh = entities::find(id)) found = mo::shiba::hyperfocus(*sh, spot);
    }
    ctx.require(found, "with a torch in hand the shiba points at a dark spot");
    auto& region = player.getDimensionBlockSource();
    ctx.check(region.getBrightnessPair(spot).block->mValue == 0, "the spot has no block light");
    ctx.setBlock(lp::game::globalPos(0, spot), "minecraft:torch");
    bool cleared = false;
    for (int i = 0; i < 20 && !cleared; ++i) {
        co_await ctx.ticks(2);
        BlockPos p;
        if (auto* sh = entities::find(id)) cleared = !mo::shiba::hyperfocus(*sh, p) || !(p == spot);
    }
    ctx.check(cleared, "lighting the spot clears it");
    hold(ctx, player, "minecraft:air", 0);
});

LP_REGISTER_SELFTEST("mobsoverworld.toretoise_ore", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2060);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  t = s.add(entities::spawn(0, mo::kToretoise, at(pos)));
    ctx.require(t != nullptr, "an oretoise spawns");
    int64_t id = entities::idOf(*t);
    co_await ctx.ticks(2);
    t = entities::find(id);
    int ore = mo::toretoise::oreOf(*t);
    ctx.check(ore >= 1 && ore <= 5, "it spawns with an ore (" + std::to_string(ore) + ")");
    ctx.check(t->getMaxHealth() == 60, "60 health");
    // Mining with a pickaxe: drops, no damage.
    auto& player = ctx.player();
    hold(ctx, player, "minecraft:iron_pickaxe");
    int health = t->getHealth();
    t->hurtByCause(5, Cause::EntityAttack, player);
    co_await ctx.ticks(2);
    t = entities::find(id);
    ctx.check(t && t->getHealth() == health, "a pickaxe hit does no damage");
    ctx.check(t && mo::toretoise::oreOf(*t) == 0, "the ore is mined");
    auto& region = player.getDimensionBlockSource();
    AABB  box{Vec3{at(pos).x - 4, at(pos).y - 2, at(pos).z - 4}, Vec3{at(pos).x + 4, at(pos).y + 4, at(pos).z + 4}};
    ctx.check(!entities::inBox(region, box, "minecraft:item").empty(), "the ore drops");
    removeItems(region, at(pos));
    hold(ctx, player, "minecraft:air", 0);
});

LP_REGISTER_SELFTEST("mobsoverworld.toretoise_feed", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2070);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  t = s.add(entities::spawn(0, mo::kToretoise, at(pos)));
    ctx.require(t != nullptr, "an oretoise spawns");
    int64_t id = entities::idOf(*t);
    co_await ctx.ticks(2);
    mo::toretoise::setOre(*entities::find(id), 0);
    co_await ctx.ticks(2);
    auto& cfg    = lp::config::mut<mo::Config>();
    int   chance = cfg.regrowChance;
    cfg.regrowChance = 1;
    auto& player = ctx.player();
    hold(ctx, player, "minecraft:glow_berries", 8);
    ctx.check(mo::toretoise::interact(player, *entities::find(id)) == mo::Handled::Yes, "glow berries are eaten");
    co_await ctx.ticks(2);
    t = entities::find(id);
    ctx.check(mo::boolProperty(*t, "lepton:fed"), "the first feeding tames it");
    ctx.check(mo::toretoise::oreOf(*t) == 0, "no ore yet");
    mo::toretoise::interact(player, *t);
    co_await ctx.ticks(2);
    t = entities::find(id);
    ctx.check(mo::toretoise::oreOf(*t) > 0, "feeding again regrows an ore");
    int regrowAt = mo::intProperty(*t, "lepton:regrow_at");
    ctx.check(regrowAt > mo::gameTick(), "the cooldown starts");
    mo::toretoise::setOre(*t, 0);
    co_await ctx.ticks(2);
    mo::toretoise::interact(player, *entities::find(id));
    co_await ctx.ticks(2);
    ctx.check(mo::toretoise::oreOf(*entities::find(id)) == 0, "no regrowth during the cooldown");
    cfg.regrowChance = chance;
    hold(ctx, player, "minecraft:air", 0);
});

LP_REGISTER_SELFTEST("mobsoverworld.toretoise_stomp", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(2080);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  t   = s.add(entities::spawn(0, mo::kToretoise, at(pos)));
    Actor*  pig = s.add(entities::spawn(0, "minecraft:pig", at(pos, 2.0f)));
    ctx.require(t && pig, "an oretoise and a pig spawn");
    int64_t id = entities::idOf(*t), pigId = entities::idOf(*pig);
    co_await ctx.ticks(10);
    auto& player = ctx.player();
    hold(ctx, player, "minecraft:stick");
    entities::find(id)->hurtByCause(1, Cause::EntityAttack, player);
    co_await ctx.ticks(1);
    ctx.check(mo::toretoise::angerOf(*entities::find(id)) > 0, "a hit makes it angry");
    int health = entities::find(pigId) ? entities::find(pigId)->getHealth() : 0;
    co_await ctx.ticks(25);
    auto* p = entities::find(pigId);
    ctx.check(!p || p->getHealth() < health, "the stomp hurts mobs around it");
    hold(ctx, player, "minecraft:air", 0);
});

LP_REGISTER_SELFTEST("mobsoverworld.spawn_rules", [](Context& ctx) {
    auto& cfg = lp::config::mut<mo::Config>();
    ctx.check(!mo::spawning::allowed(std::string(mo::kToretoise), 0, BlockPos{0, cfg.maxYLevel + 10, 0}, true),
              "no natural oretoise above maxYLevel");
    ctx.check(mo::spawning::allowed(std::string(mo::kToretoise), 0, BlockPos{0, 70, 0}, false), "spawn eggs work anywhere");
    bool crabs = cfg.enableCrabs;
    cfg.enableCrabs = false;
    ctx.check(!mo::spawning::allowed(std::string(mo::kCrab), 0, BlockPos{0, 64, 0}, false), "disabled crabs never spawn");
    cfg.enableCrabs = crabs;
});

// Gallery for the client e2e screenshots: every crab colour (+ mould), a raving crab, a giant
// crab, shibas (coats, collars, armour, a sword in the mouth) and oretoises with each ore. Without
// a real player (BDS runs) it only checks that everything spawns.
LP_REGISTER_SELFTEST("mobsoverworld.gallery", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto base = ctx.testPos(2045, -2);
    ctx.require(co_await ctx.ensureLoaded(base), "test area loaded");
    Spawned s;
    auto&   sim = ctx.player();
    Player* viewer = nullptr;
    sim.getLevel().forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) viewer = &p;
        return viewer == nullptr;
    });
    auto place = [&](std::string_view type, float dx, float dz) {
        return s.add(entities::spawn(0, type, at(base, dx, dz)));
    };
    // Crabs: colours 0..2 without / with mould, one giant.
    std::vector<int64_t> crabs;
    for (int i = 0; i < 6; ++i) {
        if (Actor* c = place(mo::kCrab, static_cast<float>(i) * 1.5f, 0)) crabs.push_back(entities::idOf(*c));
    }
    Actor* giant = place(mo::kCrab, 13, -3);
    Actor* raver = place(mo::kCrab, 10, 0);
    // Shibas: three coats + the rare one, two tamed with collars, one armoured, one with a sword.
    std::vector<int64_t> shibas;
    for (int i = 0; i < 5; ++i) {
        if (Actor* a = place(mo::kShiba, 20.f + static_cast<float>(i) * 1.6f, 0)) shibas.push_back(entities::idOf(*a));
    }
    // Oretoises: ores 1..5 and a bare shell.
    std::vector<int64_t> tortoises;
    for (int i = 0; i < 6; ++i) {
        if (Actor* a = place(mo::kToretoise, 30.f + static_cast<float>(i) * 2.6f, 0)) tortoises.push_back(entities::idOf(*a));
    }
    ctx.require(crabs.size() == 6 && shibas.size() == 5 && tortoises.size() == 6 && giant && raver, "gallery mobs spawn");
    int64_t giantId = entities::idOf(*giant), raverId = entities::idOf(*raver);
    co_await ctx.ticks(2);
    for (int i = 0; i < 6; ++i) {
        if (Actor* c = entities::find(crabs[i])) {
            entities::triggerEvent(*c, "lepton:color_" + std::to_string(i % 3));
            entities::triggerEvent(*c, i >= 3 ? "lepton:mold" : "lepton:no_mold");
            entities::triggerEvent(*c, "lepton:freeze");
        }
    }
    if (Actor* g = entities::find(giantId)) {
        entities::triggerEvent(*g, "lepton:size_1");
        entities::triggerEvent(*g, "lepton:freeze");
    }
    if (Actor* r = entities::find(raverId)) {
        entities::triggerEvent(*r, "lepton:rave_start");
        entities::triggerEvent(*r, "lepton:freeze");
    }
    for (int i = 0; i < 5; ++i) {
        Actor* a = entities::find(shibas[i]);
        if (!a) continue;
        entities::triggerEvent(*a, "lepton:coat_" + std::to_string(std::min(i, 3)));
        entities::triggerEvent(*a, "lepton:freeze");
        if (i >= 1) {
            tame(*a, viewer ? *viewer : sim);
            a->setSitting(i == 4);
        }
        if (i == 1) mo::setIntProperty(*a, "lepton:collar", 11);
        if (i == 2) {
            a->setArmor(SharedTypes::Legacy::ArmorSlot::Body, lp::game::makeItem("minecraft:wolf_armor"));
            entities::triggerEvent(*a, "lepton:armor_on");
        }
        if (i == 3) a->setCarriedItem(lp::game::makeItem("minecraft:iron_sword"));
    }
    for (int i = 0; i < 6; ++i) {
        if (Actor* a = entities::find(tortoises[i])) {
            mo::toretoise::setOre(*a, i < 5 ? i + 1 : 0);
            entities::triggerEvent(*a, "lepton:freeze");
        }
    }
    co_await ctx.ticks(4);
    if (Actor* g = entities::find(giantId)) ctx.check(mo::crabs::sizeOf(*g) == 1, "the giant crab is scale 3");
    if (Actor* a = entities::find(tortoises[2])) ctx.check(mo::toretoise::oreOf(*a) == 3, "the third oretoise carries redstone");
    if (!viewer) co_return;

    for (auto id : {"lepton:crab_leg", "lepton:cooked_crab_leg", "lepton:crab_shell", "lepton:crab_bucket",
                    "lepton:resilience_potion", "lepton:resilience_splash_potion", "lepton:resilience_lingering_potion"}) {
        ItemStack stack = lp::game::makeItem(id);
        viewer->add(stack);
    }
    viewer->refreshInventory();
    auto uid  = viewer->getOrCreateUniqueID();
    auto view = [&](float dx, float dz, float pitch, float yaw) {
        if (auto* v = sim.getLevel().getPlayer(uid)) {
            v->teleport(Vec3{static_cast<float>(base.x) + dx, static_cast<float>(base.y) + 1.f, static_cast<float>(base.z) + dz},
                        DimensionType(base.dim), Vec2{pitch, yaw});
        }
    };
    ctx.info("GALLERY view crabs");
    view(7.f, 7.f, 30.f, 180.f);
    co_await ctx.ticks(150);
    ctx.info("GALLERY view shibas");
    view(23.5f, 5.f, 30.f, 180.f);
    co_await ctx.ticks(150);
    ctx.info("GALLERY view oretoises");
    view(37.f, 9.f, 25.f, 180.f);
    co_await ctx.ticks(150);
    ctx.info("GALLERY done");
});
