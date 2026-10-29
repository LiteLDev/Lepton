// Selftests of U18 cosmetics (dx 1800..1899). docs/spec/cosmetics.md §8.
#include "feature/cosmetics/Cosmetics.h"

#include <cmath>

#include "ll/api/coro/CoroTask.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/EconomyTradeableComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/item/trading/MerchantRecipeList.h"

#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
namespace cos = lp::feature::cosmetics;
namespace ent = lp::game::entities;

Vec3 at(lp::core::GlobalPos const& p, float dy = 1) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + dy, static_cast<float>(p.z) + 0.5f};
}

/// Spawned actors removed at the end of a test.
struct Spawned {
    std::vector<int64_t> ids;
    Actor*               add(Actor* a) {
        if (a) ids.push_back(ent::idOf(*a));
        return a;
    }
    ~Spawned() {
        for (auto id : ids)
            if (auto* a = ent::find(id)) ent::despawn(*a);
    }
};

/// Restores the config after a test that changes it.
struct ConfigGuard {
    cos::CosmeticsConfig saved = lp::config::get<cos::CosmeticsConfig>();
    ~ConfigGuard() { lp::config::mut<cos::CosmeticsConfig>() = saved; }
    cos::CosmeticsConfig& cfg() { return lp::config::mut<cos::CosmeticsConfig>(); }
};

/// GrabChickens (U25) lets players carry chickens; the vanilla player only takes parrots. The test
/// lets a chicken through while it seats one, like U25 does.
bool gSeatChicken = false;
LL_TYPE_INSTANCE_HOOK(TestPassengerHook, ll::memory::HookPriority::Low, Player, &Player::$canAddPassenger, bool, Actor& passenger) {
    if (gSeatChicken && passenger.getEntityTypeId() == ActorType::Chicken) return true;
    return origin(passenger);
}

} // namespace

LP_REGISTER_SELFTEST("cosmetics.config", [](Context& ctx) {
    cos::CosmeticsConfig d{};
    ctx.check(d.enableGreenerGrass && d.affectLeaves && !d.affectWater, "GreenerGrass defaults");
    ctx.check(d.blockList.size() == 11 && d.blockList[2] == "minecraft:grass_block", "Quark's block list");
    ctx.check(d.leavesList.size() == 14 && d.leavesList[12] == "minecraft:vine", "Quark's leaves list");
    ctx.check(d.colorMatrix.r == std::vector<double>{0.89, 0, 0} && d.colorMatrix.g == std::vector<double>{0, 1.11, 0}, "grass matrix");
    ctx.check(d.waterMatrix.g == std::vector<double>{0, 1, 0.22} && d.waterMatrix.b == std::vector<double>{0, 0, 1.22}, "water matrix");
    ctx.check(d.shinyAnimalChance == 2048 && d.shinySparkles && !d.everyBeeIsLGBT, "variant defaults");
    ctx.check(d.dateFormat == "MM/dd/yyyy" && d.enableCamera, "camera defaults");
    ctx.check(d.oceanVillager && d.beachVillager && d.vanishPumpkinOverlay && d.bindArmorStandsWithPlayerHeads, "other defaults");
});

LP_REGISTER_SELFTEST("cosmetics.variant_rules", [](Context& ctx) {
    ctx.check(std::abs(cos::javaRandomDouble(0) - 0.730967787376657) < 1e-12, "Java Random(0).nextDouble()");
    ctx.check(std::abs(cos::javaRandomDouble(42) - 0.7275636800328681) < 1e-12, "Java Random(42).nextDouble()");
    ctx.check(cos::isShiny(4096, 2048) && !cos::isShiny(4097, 2048) && !cos::isShiny(0, 0), "shiny rule");
    ctx.check(cos::variantSkin(0, 4) == 1 && cos::variantSkin(4, 4) == 0 && cos::variantSkin(-7, 4) == 3, "variant pick |m % (n+1)|");
    ctx.check(cos::mostBits(1) != cos::mostBits(2) && cos::mostBits(5) == cos::mostBits(5), "id mixing is stable");
    ctx.check(cos::beeSkin(1, "GayBee", true, false) == 8, "named bee (case-insensitive)");
    ctx.check(cos::beeSkin(1, "wirebeegal", true, false) == 7, "wirebeegal is the enbee");
    ctx.check(cos::beeSkin(1, "Helen", true, false) == 14, "helen by name");
    ctx.check(cos::beeSkin(1, "GayBee", false, true) == 0, "disabled pride bees");
    ctx.check(cos::beeSkin(16 * 3 + 5, "", true, true) == 6, "every bee: |m % 16| + 1");
    ctx.check(cos::beeSkin(-5, "", true, true) == 6, "negative m uses the absolute remainder");
    int lgbt = 0;
    for (int64_t i = 0; i < 20000; ++i)
        if (cos::beeSkin(cos::mostBits(i), "", true, false) != 0) ++lgbt;
    ctx.check(lgbt > 20000 * 0.045 && lgbt < 20000 * 0.067, "about 5.6% of bees are pride bees (" + std::to_string(lgbt) + ")");
    int shiny = 0;
    for (int64_t i = 0; i < 200000; ++i)
        if (cos::isShiny(cos::mostBits(i), 2048)) ++shiny;
    ctx.check(shiny > 50 && shiny < 150, "about 1 in 2048 animals is shiny (" + std::to_string(shiny) + ")");
    ctx.check(cos::chickenOffsetCode(0) == 100 && cos::chickenOffsetCode(-1) == 0 && cos::chickenOffsetCode(5) == 200, "offset code");
    ctx.check(cos::villagerTypeForBiome("beach") == "beach" && cos::villagerTypeForBiome("deep_frozen_ocean") == "ocean", "biome rule");
    ctx.check(cos::villagerTypeForBiome("snowy_beach").empty() && cos::villagerTypeForBiome("plains").empty(), "other biomes keep their type");
});

LP_REGISTER_SELFTEST("cosmetics.tint_matrix", [](Context& ctx) {
    cos::CosmeticsConfig d{};
    ctx.check(cos::convolve(d.colorMatrix, 0xff91bd59u) == 0xff81d14fu, "plains grass through the default matrix");
    ctx.check(cos::convolve(d.waterMatrix, 0xff3f76e4u) == 0xff36a8ffu, "water matrix (blue clamps to 255)");
    cos::ColorMatrix id{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    ctx.check(cos::convolve(id, 0x12345678u) == 0x12345678u, "identity keeps the colour and alpha");
});

LP_REGISTER_SELFTEST("cosmetics.animal_skins", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    auto        pos = ctx.testPos(1800);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    for (auto type : {"minecraft:cow", "minecraft:pig", "minecraft:chicken", "minecraft:allay", "minecraft:rabbit", "minecraft:frog", "minecraft:bee"}) {
        Actor* a = s.add(ent::spawn(pos.dim, type, at(pos)));
        ctx.require(a != nullptr, std::string("spawned ") + type);
        co_await ctx.ticks(1);
        a = ent::find(ent::idOf(*a));
        ctx.require(a != nullptr, std::string("still there: ") + type);
        int want = cos::animalSkin(type, ent::idOf(*a), a->getNameTag(), a->isBaby());
        ctx.check((cos::skinIdOf(*a) & cos::kSkinMask) == want, std::string(type) + " has its skin " + std::to_string(want));
    }
    // A forced pride bee and shiny cow.
    guard.cfg().everyBeeIsLGBT    = true;
    guard.cfg().shinyAnimalChance = 1;
    Actor* bee                    = s.add(ent::spawn(pos.dim, "minecraft:bee", at(pos)));
    Actor* cow                    = s.add(ent::spawn(pos.dim, "minecraft:cow", at(pos)));
    ctx.require(bee && cow, "spawned bee and cow");
    cos::applyTo(*bee);
    cos::applyTo(*cow);
    ctx.check(cos::skinIdOf(*bee) >= 1 && cos::skinIdOf(*bee) <= 16, "every bee is a pride bee");
    ctx.check(cos::skinIdOf(*cow) == cos::kShinySkin, "chance 1 makes every cow shiny");
    bee->setNameTag("transbee");
    cos::applyTo(*bee);
    ctx.check(cos::skinIdOf(*bee) == 13, "a name tag picks the bee skin");
    guard.cfg().enableCow = false;
    cos::applyTo(*cow);
    ctx.check(cos::skinIdOf(*cow) == 0, "enableCow=false writes the vanilla skin");
    guard.cfg().enableVariantAnimalTextures = false;
    cos::applyTo(*bee);
    ctx.check(cos::skinIdOf(*bee) == 0, "a disabled module writes the vanilla skin");
});

LP_REGISTER_SELFTEST("cosmetics.sparkles", [](Context& ctx) {
    ConfigGuard guard;
    ctx.check(cos::sparkles("minecraft:cow") && cos::sparkles("minecraft:frog") && !cos::sparkles("minecraft:slime"), "sparkly types");
    guard.cfg().enableShinyFrog = false;
    ctx.check(!cos::sparkles("minecraft:frog"), "a disabled key stops the sparkles");
    guard.cfg().shinySparkles = false;
    ctx.check(!cos::sparkles("minecraft:cow"), "shinySparkles=false");
});

LP_REGISTER_SELFTEST("cosmetics.armor_stand_binding", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    auto        pos = ctx.testPos(1810);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  stand = s.add(ent::spawn(pos.dim, "minecraft:armor_stand", at(pos)));
    ctx.require(stand != nullptr, "armor stand spawned");
    auto head = lp::game::makeItem("minecraft:player_head");
    ctx.require(!lp::game::isEmpty(head), "player head item exists");
    stand->setArmor(SharedTypes::Legacy::ArmorSlot::Head, head);
    cos::applyTo(*stand);
    ctx.check(cos::skinIdOf(*stand) == 0, "a plain player head keeps the armor stand");
    EnchantmentInstance binding;
    binding.mEnchantType = Enchant::Type::CurseOfBinding;
    binding.mLevel       = 1;
    EnchantUtils::applyEnchant(head, binding, false);
    ctx.check(cos::isBoundPlayerHead(head), "the head has Curse of Binding");
    stand->setArmor(SharedTypes::Legacy::ArmorSlot::Head, head);
    cos::applyTo(*stand);
    ctx.check(cos::skinIdOf(*stand) == 1, "a bound player head shows the player body");
    guard.cfg().bindArmorStandsWithPlayerHeads = false;
    cos::applyTo(*stand);
    ctx.check(cos::skinIdOf(*stand) == 0, "bindArmorStandsWithPlayerHeads=false");
    co_return;
});

LP_REGISTER_SELFTEST("cosmetics.villager_types", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard guard;
    auto        pos = ctx.testPos(1820);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  plain = s.add(ent::spawn(pos.dim, "minecraft:villager_v2", at(pos)));
    ctx.require(plain != nullptr, "villager spawned");
    co_await ctx.ticks(2);
    plain = ent::find(s.ids.back());
    ctx.require(plain != nullptr, "villager still there");
    ctx.check(!plain->hasTag(std::string(cos::kBeachTag)) && !plain->hasTag(std::string(cos::kOceanTag)), "a plains villager is not retyped");
    ctx.check(cos::markVariantOf(*plain) != cos::kBeachMarkVariant && cos::markVariantOf(*plain) != cos::kOceanMarkVariant, "plains look kept");

    Actor* beach = s.add(ent::spawn(pos.dim, "minecraft:villager_v2", at(pos)));
    Actor* ocean = s.add(ent::spawn(pos.dim, "minecraft:villager_v2", at(pos)));
    ctx.require(beach && ocean, "spawned");
    beach->addTag(std::string(cos::kBeachTag));
    ocean->addTag(std::string(cos::kOceanTag));
    cos::applyTo(*beach);
    cos::applyTo(*ocean);
    ctx.check(cos::markVariantOf(*beach) == cos::kBeachMarkVariant, "beach villager mark variant 7");
    ctx.check(cos::markVariantOf(*ocean) == cos::kOceanMarkVariant, "ocean villager mark variant 8");
    // Zombie villagers (hostile: not spawnable on peaceful worlds).
    if (Actor* zombie = s.add(ent::spawn(pos.dim, "minecraft:zombie_villager_v2", at(pos)))) {
        zombie->addTag(std::string(cos::kOceanTag));
        cos::applyTo(*zombie);
        ctx.check(cos::markVariantOf(*zombie) == cos::kOceanMarkVariant, "ocean zombie villager mark variant 8");
    } else {
        ctx.info("zombie villagers cannot spawn here (peaceful); skipped");
    }
    ent::triggerEvent(*beach, "minecraft:become_fisherman");
    co_await ctx.ticks(2);
    beach = ent::find(s.ids[1]);
    ctx.require(beach != nullptr, "beach villager still there");
    ctx.check(cos::markVariantOf(*beach) == cos::kBeachMarkVariant, "the type survives a profession change");
    guard.cfg().beachVillager = false;
    cos::applyTo(*beach);
    ctx.check(cos::markVariantOf(*beach) == 0, "beachVillager=false shows the plains look");
});

LP_REGISTER_SELFTEST("cosmetics.fisherman_boat", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1830);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    for (auto [tag, boat] : {std::pair{cos::kOceanTag, "minecraft:oak_boat"}, std::pair{cos::kBeachTag, "minecraft:jungle_boat"}}) {
        Actor* v = s.add(ent::spawn(pos.dim, "minecraft:villager_v2", at(pos)));
        ctx.require(v != nullptr, "villager spawned");
        v->addTag(std::string(tag));
        cos::applyTo(*v);
        ent::triggerEvent(*v, "minecraft:become_fisherman");
        co_await ctx.ticks(2);
        v = ent::find(s.ids.back());
        ctx.require(v != nullptr, "villager still there");
        cos::applyTo(*v);
        auto trade = v->getEntityContext().tryGetComponent<EconomyTradeableComponent>();
        ctx.require(trade.has_value(), "the fisherman trades");
        trade->_generateTrades();
        auto* offers = trade->getOffers();
        ctx.require(offers != nullptr, "offers generated");
        bool found = false;
        for (auto const& r : *offers->mRecipeList)
            if (!r.mBuyA->isNull() && r.mBuyA->getTypeName() == boat && r.mSell->getTypeName() == "minecraft:emerald") found = true;
        ctx.check(found, std::string("the fisherman buys a ") + boat);
    }
});

LP_REGISTER_SELFTEST("cosmetics.carried_chicken", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(1840);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    Spawned s;
    Actor*  chicken = s.add(ent::spawn(pos.dim, "minecraft:chicken", at(pos)));
    Actor*  cart    = s.add(ent::spawn(pos.dim, "minecraft:minecart", at(pos)));
    ctx.require(chicken && cart, "spawned");
    cos::applyTo(*chicken);
    ctx.check((cos::skinIdOf(*chicken) & cos::kCarriedFlag) == 0, "a free chicken is not carried");
    cos::setFarmingViewForTests(cos::FarmingView{true, 0.5});
    // Riding something that is not a player does not count.
    chicken->startRiding(*cart, true);
    co_await ctx.ticks(1);
    chicken = ent::find(s.ids[0]);
    ctx.require(chicken != nullptr, "chicken still there");
    cos::applyTo(*chicken);
    ctx.check((cos::skinIdOf(*chicken) & cos::kCarriedFlag) == 0, "a chicken in a minecart is not carried");
    chicken->stopRiding(true, false, false, false);
    // A player vehicle.
    auto& p = ctx.player();
    TestPassengerHook::hook();
    gSeatChicken = true;
    bool rode    = chicken->startRiding(p, true);
    gSeatChicken = false;
    TestPassengerHook::unhook();
    ctx.require(rode, "the chicken rides the player");
    co_await ctx.ticks(1);
    chicken = ent::find(s.ids[0]);
    ctx.require(chicken != nullptr && chicken->getVehicle() == &p, "chicken still on the player");
    cos::applyTo(*chicken);
    int skin = cos::skinIdOf(*chicken);
    ctx.check((skin & cos::kCarriedFlag) != 0, "a chicken on a player is carried");
    ctx.check((skin & cos::kSecondSeat) == 0, "first seat");
    ctx.check(skin / cos::kOffsetUnit == 150, "offset 0.5 is code 150");
    cos::setFarmingViewForTests(cos::FarmingView{false, 0.0});
    cos::applyTo(*chicken);
    ctx.check((cos::skinIdOf(*chicken) & ~cos::kSkinMask) == 0, "enableGrabChickens=false clears the flags");
    chicken->stopRiding(true, false, false, false);
    cos::setFarmingViewForTests(std::nullopt);
});

