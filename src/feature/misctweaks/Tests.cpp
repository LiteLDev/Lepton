// Selftests of the misctweaks unit (dx 2700..2799).

#include <atomic>
#include <cfloat>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components_json_legacy/EconomyTradeableComponent.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ArmorStand.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/Boat.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/item/trading/MerchantRecipeList.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/biome/ClimateParameters.h"
#include "mc/world/level/biome/climate_utils/Parameter.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/item/FertilizerType.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/misctweaks/Climate.h"
#include "feature/misctweaks/Internal.h"
#include "feature/misctweaks/Villagers.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "selftest/SelfTest.h"

namespace lp::feature::misctweaks {
extern std::atomic<int> gAmethystNotes;
extern std::atomic<int> gHugeMushrooms;
int                     unlockAllRecipes(Player& player);
} // namespace lp::feature::misctweaks

namespace {

using lp::selftest::Context;
using lp::feature::misctweaks::MiscTweaksConfig;
namespace mt  = lp::feature::misctweaks;
namespace ent = lp::game::entities;
using lp::game::makeItem;
using ArmorSlot = SharedTypes::Legacy::ArmorSlot;

/// Runs a test with the default config (plus its own changes) and restores the config afterwards.
class ConfigScope {
public:
    ConfigScope() : mSaved(lp::config::get<MiscTweaksConfig>()) { lp::config::mut<MiscTweaksConfig>() = MiscTweaksConfig{}; }
    ~ConfigScope() { lp::config::mut<MiscTweaksConfig>() = mSaved; }
    MiscTweaksConfig* operator->() { return &lp::config::mut<MiscTweaksConfig>(); }

private:
    MiscTweaksConfig mSaved;
};

BlockPos bp(lp::core::GlobalPos const& p, int dx = 0, int dy = 0, int dz = 0) { return {p.x + dx, p.y + dy, p.z + dz}; }

/// Sets a block with typed states and remembers it for cleanup.
bool place(Context& ctx, BlockSource& region, BlockPos const& pos, std::string_view id,
           lp::game::blockstates::Changes const& states = {}) {
    Block const* b = lp::game::blockstates::of(id, states);
    if (!b) return false;
    ctx.cleanupBlock(lp::game::globalPos(region, pos));
    return lp::game::setBlock(region, pos, *b);
}

AABB boxAround(Vec3 const& c, float r) { return AABB{Vec3{c.x - r, c.y - r, c.z - r}, Vec3{c.x + r, c.y + r, c.z + r}}; }

/// Item actors of `itemId` near `c`; despawns them when `clear` is set.
int droppedItems(BlockSource& region, Vec3 const& c, std::string_view itemId, bool clear) {
    int n = 0;
    for (Actor* a : ent::inBox(region, boxAround(c, 6), "minecraft:item")) {
        auto& item = static_cast<ItemActor*>(a)->item();
        auto  id   = lp::game::itemId(item);
        if (id == itemId || (itemId == "*wool" && id.ends_with("wool"))) n += item.mCount;
        if (clear) ent::despawn(*a);
    }
    return n;
}

void clearItems(BlockSource& region, Vec3 const& c) {
    for (Actor* a : ent::inBox(region, boxAround(c, 6), "minecraft:item")) ent::despawn(*a);
}

ItemStack withMending(std::string_view id, int level, short damage) {
    auto                item = makeItem(id);
    EnchantmentInstance e;
    e.mEnchantType = Enchant::Type::Mending;
    e.mLevel       = level;
    EnchantUtils::applyEnchant(item, e, true);
    if (damage) item.setDamageValue(damage);
    return item;
}

} // namespace

LP_REGISTER_SELFTEST("misctweaks.config", [](Context& ctx) {
    MiscTweaksConfig d;
    ctx.check(d.enableBetterElytraRocket && d.enableCampfiresBoostElytra && d.enableMoreNoteBlockSounds
                  && d.enableAutomaticRecipeUnlock && d.enableArmedArmorStands,
              "tweaks are on by default");
    ctx.check(!d.enableGameNerfs && !d.enableSpawnerReplacer && !d.enableVillagerRerollingRework && !d.enableAdjustableChat
                  && !d.enableClimateControlRemover && !d.enableNarratorReadout && !d.enableOverlayShader,
              "experimental modules are off by default");
    ctx.check(d.boostStrength == 0.5 && d.maxSpeed == 1, "campfire defaults");
    ctx.check(d.villageSpawnNerfDistance == 7000 && d.maximumRestocksPerDay == 3 && d.chanceToRerollWhenRestocking == 0.25,
              "nerf / reroll defaults");
    ctx.check(d.nonGriefingEntities == std::vector<std::string>{"minecraft:creeper", "minecraft:enderman"},
              "non-griefing defaults");
    ctx.check(d.elytraAllowedDimensions == std::vector<std::string>{"minecraft:the_end"}, "elytra dimension default");
    ctx.check(mt::dimensionMatches("minecraft:the_end", 2) && mt::dimensionMatches("the_nether", 1)
                  && mt::dimensionMatches("minecraft:nether", 1) && !mt::dimensionMatches("minecraft:the_end", 0),
              "dimension names (Java and Bedrock spellings)");
    ctx.check(mt::normalizeId("Pig") == "minecraft:pig" && mt::normalizeId("lepton:x") == "lepton:x", "id normalization");
});

LP_REGISTER_SELFTEST("misctweaks.elytra_rocket", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto pos = ctx.testPos(2700);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.spawnPlayer("lp_rocket");
    co_await ctx.ticks(2);
    Vec3 air{pos.x + 0.5f, pos.y + 12.f, pos.z + 0.5f};

    // Without an elytra a rocket does nothing special.
    p.teleport(air, p.getDimensionId());
    co_await ctx.ticks(2);
    auto rocket = makeItem("minecraft:firework_rocket", 4);
    p.simulateUseItem(rocket);
    co_await ctx.ticks(1);
    ctx.check(!p.isGliding(), "no elytra: no gliding");

    p.setArmor(ArmorSlot::Torso, makeItem("minecraft:elytra"));
    p.teleport(air, p.getDimensionId());
    co_await ctx.ticks(15); // a second use within a few ticks is ignored
    auto rocket2 = makeItem("minecraft:firework_rocket", 4);
    p.simulateUseItem(rocket2);
    co_await ctx.ticks(1);
    ctx.check(p.isGliding(), "elytra + rocket starts gliding");
    auto rockets = ent::inBox(p.getDimensionBlockSource(), boxAround(p.getPosition(), 4), "minecraft:fireworks_rocket");
    ctx.check(!rockets.empty(), "a rocket was fired at the player");
    for (Actor* a : rockets) ent::despawn(*a);
    p.stopGliding();
    p.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
});

LP_REGISTER_SELFTEST("misctweaks.dimension_elytra", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    cfg->enableGameNerfs = true;
    auto pos             = ctx.testPos(2750);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p = ctx.spawnPlayer("lp_elytra_lock");
    co_await ctx.ticks(2);
    p.setArmor(ArmorSlot::Torso, makeItem("minecraft:elytra"));
    p.teleport(Vec3{pos.x + 0.5f, pos.y + 12.f, pos.z + 0.5f}, p.getDimensionId());
    co_await ctx.ticks(2);
    ctx.check(!mt::elytraAllowedIn(p), "the overworld is not an elytra dimension");
    p.startGliding();
    ctx.check(!p.isGliding(), "gliding is refused in the overworld");
    auto rocket = makeItem("minecraft:firework_rocket", 2);
    p.simulateUseItem(rocket);
    co_await ctx.ticks(1);
    ctx.check(!p.isGliding(), "a rocket does not start gliding either");

    cfg->elytraAllowedDimensions = {"overworld"};
    ctx.check(mt::elytraAllowedIn(p), "allowed once the overworld is listed");
    p.startGliding();
    ctx.check(p.isGliding(), "gliding works in an allowed dimension");
    cfg->elytraAllowedDimensions = {"minecraft:the_end"};
    co_await ctx.ticks(2);
    ctx.check(!p.isGliding(), "gliding stops when the dimension is no longer allowed");
    p.setArmor(ArmorSlot::Torso, ItemStack::EMPTY_ITEM());
});

LP_REGISTER_SELFTEST("misctweaks.campfire_force", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto pos = ctx.testPos(2705);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto&    region = ctx.player().getDimensionBlockSource();
    BlockPos hay = bp(pos), fire = bp(pos, 0, 1);
    for (int y = 2; y <= 24; ++y) lp::game::clearBlock(region, bp(pos, 0, y));
    ctx.require(place(ctx, region, hay, "minecraft:hay_block"), "hay placed");
    ctx.require(place(ctx, region, fire, "minecraft:campfire", {{"extinguished", false}}), "campfire placed");
    auto force = [&](int above) { return mt::campfireForce(region, bp(pos, 0, 1 + above), 0.5); };
    auto f5    = force(5);
    ctx.check(f5 && std::abs(*f5 - 0.5) < 1e-9, "lit signal campfire 5 blocks below pushes up by boostStrength");
    auto f18 = force(18);
    ctx.check(f18 && std::abs(*f18 - 0.25) < 1e-9, "18 blocks: the force fades (0.25)");
    ctx.check(!force(21), "beyond 20 blocks: nothing");

    ctx.require(place(ctx, region, fire, "minecraft:soul_campfire", {{"extinguished", false}}), "soul campfire placed");
    auto soul = force(5);
    ctx.check(soul && std::abs(*soul + 0.75) < 1e-9, "a soul campfire pulls down (x -1.5)");

    ctx.require(place(ctx, region, fire, "minecraft:campfire", {{"extinguished", true}}), "unlit campfire placed");
    ctx.check(!force(5), "an unlit campfire does nothing");
    ctx.require(place(ctx, region, fire, "minecraft:campfire", {{"extinguished", false}}), "campfire placed again");
    ctx.require(place(ctx, region, hay, "minecraft:stone"), "hay removed");
    ctx.check(!force(5), "without hay below it is no signal fire");
});

LP_REGISTER_SELFTEST("misctweaks.note_amethyst", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto pos = ctx.testPos(2710);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    BlockPos base = bp(pos), note = bp(pos, 0, 1);
    lp::game::clearBlock(region, bp(pos, 0, 2));
    ctx.require(place(ctx, region, base, "minecraft:amethyst_block"), "amethyst placed");
    ctx.require(place(ctx, region, note, "minecraft:noteblock"), "note block placed");
    BlockPos sensor = bp(pos, 3, 1);
    ctx.require(place(ctx, region, sensor, "minecraft:sculk_sensor"), "sculk sensor placed");
    co_await ctx.ticks(20);
    int before = mt::gAmethystNotes.load();
    region.getBlock(note).getBlockType().attack(&p, note);
    co_await ctx.ticks(2);
    ctx.check(mt::gAmethystNotes.load() == before + 1, "a note block on amethyst plays the chime");
    co_await ctx.ticks(10);
    auto phase = lp::game::blockstates::getInt(region.getBlock(sensor), "sculk_sensor_phase");
    ctx.check(phase && *phase != 0, "the note still makes a vibration (sculk sensor phase " + std::to_string(phase.value_or(-1)) + ")");
    lp::game::clearBlock(region, sensor);

    ctx.require(place(ctx, region, base, "minecraft:stone"), "stone placed");
    before = mt::gAmethystNotes.load();
    region.getBlock(note).getBlockType().attack(&p, note);
    co_await ctx.ticks(2);
    ctx.check(mt::gAmethystNotes.load() == before, "on stone the note block stays vanilla");

    ctx.require(place(ctx, region, base, "minecraft:amethyst_cluster"), "cluster placed");
    ctx.check(mt::isAmethyst(region.getBlock(base)), "amethyst clusters count");
});

LP_REGISTER_SELFTEST("misctweaks.recipe_unlock", [](Context& ctx) {
    ConfigScope cfg;
    ctx.check(mt::unlockAllRecipes(ctx.player()) > 0, "recipe give * succeeds for a player");
});

LP_REGISTER_SELFTEST("misctweaks.armor_stand_shield", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto pos = ctx.testPos(2715);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    auto* a      = ent::spawn(region, "minecraft:armor_stand", Vec3{pos.x + 0.5f, pos.y + 1.f, pos.z + 0.5f});
    ctx.require(a != nullptr, "armor stand spawned");
    auto id = ent::idOf(*a);
    co_await ctx.ticks(2);
    auto* stand = static_cast<ArmorStand*>(ent::find(id));
    ctx.require(stand != nullptr, "armor stand loaded");
    p.setCarriedItem(makeItem("minecraft:shield"));
    stand->_trySwapItem(p, SharedTypes::Legacy::EquipmentSlot::Mainhand);
    ctx.check(lp::game::itemId(stand->getOffhandSlot()) == "minecraft:shield", "the shield goes to the off hand");
    ctx.check(lp::game::isEmpty(stand->getCarriedItem()), "the main hand stays empty");

    p.setCarriedItem(makeItem("minecraft:iron_sword"));
    stand->_trySwapItem(p, SharedTypes::Legacy::EquipmentSlot::Mainhand);
    ctx.check(lp::game::itemId(stand->getCarriedItem()) == "minecraft:iron_sword", "other items go to the main hand");
    ctx.check(lp::game::itemId(stand->getOffhandSlot()) == "minecraft:shield", "the shield stays in the off hand");
    p.setCarriedItem(ItemStack::EMPTY_ITEM());
    if (auto* s = ent::find(id)) ent::despawn(*s);
});

LP_REGISTER_SELFTEST("misctweaks.mending_nerf", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto        sword = withMending("minecraft:diamond_sword", 1, 300);
    ctx.require(mt::mendingLevel(sword) == 1, "test sword has Mending I");

    // Anvil result transformation.
    cfg->enableGameNerfs = true;
    auto result          = sword;
    result.setRepairCost(3);
    ctx.check(mt::nerfAnvilResult(sword, makeItem("minecraft:diamond_sword"), result), "anvil result changed");
    ctx.check(mt::mendingLevel(result) == 0, "Mending removed");
    ctx.check(result.getDamageValue() == 0, "fully repaired");
    ctx.check(result.getBaseRepairCost() == 0, "repair cost reset");
    auto other = sword;
    ctx.check(!mt::nerfAnvilResult(sword, makeItem("minecraft:diamond"), other), "repair with material: untouched");
    auto book = makeItem("minecraft:enchanted_book");
    other     = sword;
    ctx.check(mt::nerfAnvilResult(sword, book, other) && mt::mendingLevel(other) == 0, "with an enchanted book: nerfed");
    auto sword2 = withMending("minecraft:diamond_sword", 2, 300);
    cfg->noNerfForMendingTwo = true;
    other                    = sword2;
    ctx.check(!mt::nerfAnvilResult(sword2, makeItem("minecraft:diamond_sword"), other), "Mending II kept with noNerfForMendingTwo");
    cfg->noNerfForMendingTwo = false;

    // XP repair through a real experience orb.
    auto pos = ctx.testPos(2718);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.spawnPlayer("lp_mending");
    auto& region = p.getDimensionBlockSource();
    co_await ctx.ticks(2);
    auto touch = [&](int xp) -> bool {
        Vec3 at = p.getPosition();
        ExperienceOrb::_spawnOrb(region, region.getLevel(), at, xp);
        auto orbs = ent::inBox(region, boxAround(at, 3), "minecraft:xp_orb");
        for (Actor* o : orbs) static_cast<ExperienceOrb*>(o)->playerTouch(p);
        return !orbs.empty();
    };
    // Orbs are absorbed one at a time with a short delay: wait until none is left near the player.
    auto absorbed = [&]() -> ll::coro::CoroTask<> {
        for (int i = 0; i < 40; ++i) {
            co_await ctx.ticks(5);
            if (ent::inBox(region, boxAround(p.getPosition(), 4), "minecraft:xp_orb").empty()) break;
        }
    };
    cfg->enableGameNerfs = false;
    p.setCarriedItem(withMending("minecraft:diamond_sword", 1, 300));
    ctx.require(touch(10), "orb spawned");
    co_await absorbed();
    short vanillaDamage = p.getCarriedItem().getDamageValue();
    ctx.check(vanillaDamage < 300, "control: vanilla Mending repairs from XP");

    cfg->enableGameNerfs = true;
    p.setCarriedItem(withMending("minecraft:diamond_sword", 1, 300));
    ctx.require(touch(10), "orb spawned");
    co_await absorbed();
    ctx.check(p.getCarriedItem().getDamageValue() == 300, "nerfed: XP does not repair");

    cfg->noNerfForMendingTwo = true;
    p.setCarriedItem(withMending("minecraft:diamond_sword", 2, 300));
    ctx.require(touch(10), "orb spawned");
    co_await absorbed();
    ctx.check(p.getCarriedItem().getDamageValue() < 300,
              "Mending II still repairs with noNerfForMendingTwo (damage " + std::to_string(p.getCarriedItem().getDamageValue()) + ")");
    p.setCarriedItem(ItemStack::EMPTY_ITEM());
});

namespace {
ll::coro::CoroTask<int> killAndCount(Context& ctx, BlockSource& region, std::string type, Vec3 at, std::string item) {
    clearItems(region, at);
    auto* mob = ent::spawn(region, type, at);
    ctx.require(mob != nullptr, type + " spawned");
    auto id = ent::idOf(*mob);
    co_await ctx.ticks(2);
    if (auto* m = ent::find(id)) m->hurtByCause(10000.f, SharedTypes::Legacy::ActorDamageCause::EntityAttack, ctx.player());
    co_await ctx.ticks(5); // drops can be picked up (by the test player nearby) after 10 ticks
    co_return droppedItems(region, at, item, true);
}
} // namespace

LP_REGISTER_SELFTEST("misctweaks.no_iron_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto        pos = ctx.testPos(2720);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    Vec3  at{pos.x + 0.5f, pos.y + 1.f, pos.z + 0.5f};
    cfg->enableGameNerfs = false;
    int vanilla          = co_await killAndCount(ctx, region, "minecraft:iron_golem", at, "minecraft:iron_ingot");
    ctx.check(vanilla > 0, "control: a vanilla iron golem drops iron (" + std::to_string(vanilla) + ")");
    cfg->enableGameNerfs = true;
    int nerfed           = co_await killAndCount(ctx, region, "minecraft:iron_golem", at, "minecraft:iron_ingot");
    ctx.check(nerfed == 0, "nerfed: no iron ingots (" + std::to_string(nerfed) + ")");
});

LP_REGISTER_SELFTEST("misctweaks.no_wool_drops", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto        pos = ctx.testPos(2725);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    Vec3  at{pos.x + 0.5f, pos.y + 1.f, pos.z + 0.5f};
    cfg->enableGameNerfs = false;
    int vanilla          = co_await killAndCount(ctx, region, "minecraft:sheep", at, "*wool");
    ctx.check(vanilla > 0, "control: a vanilla sheep drops wool (" + std::to_string(vanilla) + ")");
    cfg->enableGameNerfs = true;
    int nerfed           = co_await killAndCount(ctx, region, "minecraft:sheep", at, "*wool");
    ctx.check(nerfed == 0, "nerfed: no wool (" + std::to_string(nerfed) + ")");
});

LP_REGISTER_SELFTEST("misctweaks.griefing", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto        pos = ctx.testPos(2730);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    Vec3  at{pos.x + 0.5f, pos.y + 2.f, pos.z + 0.5f};
    auto  blast = [&](std::string const& type) -> ll::coro::CoroTask<bool> {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) place(ctx, region, bp(pos, dx, 1, dz), "minecraft:stone");
        auto* src = ent::spawn(region, type, Vec3{at.x, at.y + 1, at.z});
        ctx.require(src != nullptr, type + " spawned");
        region.getLevel().explode(region, src, at, 2.f, false, true, FLT_MAX, false);
        ent::despawn(*src);
        co_await ctx.ticks(1);
        int left = 0;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) left += lp::game::blockIdAt(region, bp(pos, dx, 1, dz)) == "minecraft:stone";
        clearItems(region, at);
        co_return left == 9;
    };
    auto difficulty = region.getLevel().getDifficulty(); // peaceful worlds remove creepers at once
    region.getLevel().setDifficulty(SharedTypes::Legacy::Difficulty::Normal);
    cfg->enableGameNerfs = true;
    ctx.check(co_await blast("minecraft:creeper"), "a listed mob's explosion breaks no blocks");
    ctx.check(!co_await blast("minecraft:pig"), "other sources still break blocks");
    cfg->enableGameNerfs = false;
    ctx.check(!co_await blast("minecraft:creeper"), "control: without the nerf the creeper breaks blocks");
    region.getLevel().setDifficulty(difficulty);
    ctx.check(mt::isNonGriefing("minecraft:creeper") == false, "nerfs off: nothing is non-griefing");
    cfg->enableGameNerfs = true;
    ctx.check(mt::isNonGriefing("enderman") && !mt::isNonGriefing("minecraft:zombie"), "list lookup");
});

LP_REGISTER_SELFTEST("misctweaks.mushroom", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto        pos = ctx.testPos(2735);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& p      = ctx.player();
    auto& region = p.getDimensionBlockSource();
    // Grows a huge red mushroom with a ring of stone where its cap goes; returns the stone left.
    auto grow = [&](bool withStones) -> ll::coro::CoroTask<int> {
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz)
                for (int dy = 1; dy <= 10; ++dy) lp::game::clearBlock(region, bp(pos, dx, dy, dz));
        place(ctx, region, bp(pos), "minecraft:mycelium");
        place(ctx, region, bp(pos, 0, 1), "minecraft:red_mushroom");
        std::vector<BlockPos> stones;
        for (int dy = 4; dy <= 8; ++dy)
            for (auto [dx, dz] : {std::pair{2, 0}, std::pair{-2, 0}, std::pair{0, 2}, std::pair{0, -2}}) {
                stones.push_back(bp(pos, dx, dy, dz));
                if (withStones) place(ctx, region, stones.back(), "minecraft:glass");
            }
        for (int i = 0; i < 40 && lp::game::blockIdAt(region, bp(pos, 0, 1)) == "minecraft:red_mushroom"; ++i)
            region.getBlock(bp(pos, 0, 1)).getBlockType().onFertilized(region, bp(pos, 0, 1), &p, FertilizerType::Rapid);
        co_await ctx.ticks(1);
        auto base = lp::game::blockIdAt(region, bp(pos, 0, 1));
        ctx.info("mushroom base after bone meal: " + base);
        int left = 0;
        for (auto const& s : stones) left += lp::game::blockIdAt(region, s) == "minecraft:glass";
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz)
                for (int dy = 1; dy <= 12; ++dy) lp::game::clearBlock(region, bp(pos, dx, dy, dz));
        co_return base == "minecraft:red_mushroom" ? -1 : left;
    };
    cfg->enableGameNerfs = true;
    int placed           = mt::gHugeMushrooms.load();
    ctx.require(co_await grow(false) >= 0, "a huge mushroom grows in free space with the nerf on");
    ctx.check(mt::gHugeMushrooms.load() > placed, "the growth went through the feature hook");
    int nerfed = co_await grow(true);
    ctx.check(nerfed == 20 || nerfed == -1, "nerfed: the cap replaced no block (" + std::to_string(nerfed) + ")");
    cfg->enableGameNerfs = false;
    int vanilla          = co_await grow(true);
    ctx.info("obstructed growth: vanilla " + std::to_string(vanilla) + ", nerfed " + std::to_string(nerfed)
             + " (-1 = did not grow, else glass left of 20)");
});
LP_REGISTER_SELFTEST("misctweaks.village_nerf", [](Context& ctx) {
    int suppressed = 0;
    for (int i = 0; i < 1000; ++i) suppressed += mt::villageSuppressed(i * 16, 0, 0, 0, 1000000) ? 1 : 0;
    ctx.check(suppressed > 880 && suppressed < 990, "near spawn ~94% of villages are suppressed (" + std::to_string(suppressed) + ")");
    int far = 0;
    for (int i = 0; i < 1000; ++i) far += mt::villageSuppressed(7000 + i * 16, 0, 0, 0, 7000) ? 1 : 0;
    ctx.check(far == 0, "beyond the distance villages are kept");
    ctx.check(mt::villageSuppressed(123, 456, 0, 0, 7000) == mt::villageSuppressed(123, 456, 0, 0, 7000), "deterministic");
});

LP_REGISTER_SELFTEST("misctweaks.spawner", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto        pos = ctx.testPos(2740);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto&    region = ctx.player().getDimensionBlockSource();
    BlockPos at     = bp(pos, 0, 1);
    ctx.require(place(ctx, region, at, "minecraft:mob_spawner"), "spawner placed");
    auto* be = static_cast<MobSpawnerBlockActor*>(region.getBlockEntity(at));
    ctx.require(be != nullptr, "spawner block actor");
    be->setMob(region, ActorDefinitionIdentifier("minecraft:pig"), nullptr);
    ctx.require(mt::spawnerMob(*be) == "minecraft:pig", "spawner mob read back (" + mt::spawnerMob(*be) + ")");
    cfg->enableSpawnerReplacer = true;
    cfg->replaceMobs           = {"pig,minecraft:cow", "bad entry"};
    ctx.check(mt::spawnerReplacement("minecraft:pig") == "minecraft:cow", "map parsed");
    ctx.check(mt::spawnerReplacement("minecraft:zombie").empty(), "unmapped mobs stay");
    co_await ctx.ticks(5);
    be = static_cast<MobSpawnerBlockActor*>(region.getBlockEntity(at));
    ctx.require(be != nullptr, "spawner still there");
    auto mob = mt::spawnerMob(*be);
    ctx.check(mob == "minecraft:cow", "the pig spawner became a cow spawner (" + mob + ")");
});

LP_REGISTER_SELFTEST("misctweaks.villager_seed", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    cfg->enableVillagerRerollingRework = true;
    auto pos                           = ctx.testPos(2745);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    auto* v = ent::spawn(region, "minecraft:villager_v2<minecraft:spawn_farmer>", Vec3{pos.x + 0.5f, pos.y + 1.f, pos.z + 0.5f});
    ctx.require(v != nullptr, "villager spawned");
    auto id = ent::idOf(*v);
    co_await ctx.ticks(5);
    v = ent::find(id);
    ctx.require(v != nullptr, "villager loaded");
    auto comp = v->getEntityContext().tryGetComponent<EconomyTradeableComponent>();
    ctx.require(comp.has_value(), "villager has a trade component");
    auto snapshot = [&]() {
        std::vector<std::string> out;
        if (auto* list = comp->mOffers.get())
            for (auto const& r : list->mRecipeList.get())
                out.push_back(std::to_string(r.mTier) + ":" + lp::game::itemToSnbt(r.mBuyA.get()) + lp::game::itemToSnbt(r.mSell.get()));
        return out;
    };
    comp->mOffers.reset();
    comp->_generateTrades();
    auto first = snapshot();
    ctx.require(!first.empty(), "trades generated (" + std::to_string(first.size()) + ")");
    comp->mOffers.reset();
    comp->_generateTrades();
    ctx.check(snapshot() == first, "the same seed generates the same first trades");
    ctx.check(lp::game::db::load("lp:misctweaks:villager:" + std::to_string(id)).has_value(), "the seed is stored");

    // Rerolling: every trade out of stock, chance 1, at most 1 reroll per day.
    cfg->chanceToRerollWhenRestocking = 1.0;
    cfg->maximumRestocksPerDay        = 1;
    if (auto* list = comp->mOffers.get())
        for (auto& r : list->mRecipeList.get()) r.mUses = r.mMaxUses;
    int changed = mt::attemptReroll(*comp);
    ctx.check(changed <= 1, "at most maximumRestocksPerDay rerolls (" + std::to_string(changed) + ")");
    ctx.check(mt::attemptReroll(*comp) == 0, "no more rerolls today");
    cfg->maximumRestocksPerDay = 0;
    ctx.check(mt::attemptReroll(*comp) == 0, "0 disables rerolling");
    ctx.info("rerolled " + std::to_string(changed) + " trade(s); " + std::to_string(snapshot().size()) + " trades now");
    if (auto* e = ent::find(id)) ent::despawn(*e);
});

LP_REGISTER_SELFTEST("misctweaks.climate", [](Context& ctx) {
    ConfigScope cfg;
    using ClimateUtils::Parameter;
    ClimateParameters p(Parameter(int64{1}, int64{2}), Parameter(int64{3}, int64{4}), Parameter(int64{5}, int64{6}),
                        Parameter(int64{7}, int64{8}), Parameter(int64{9}, int64{10}), Parameter(int64{11}, int64{12}), 0.5f);
    ctx.check(!mt::neutralizeClimate(p), "module off: nothing changes");
    cfg->enableClimateControlRemover = true;
    cfg->disableTemperature          = true;
    cfg->disableOffset               = true;
    ctx.check(mt::neutralizeClimate(p), "changed");
    auto const& t = p.mTemperature.get();
    ctx.check(t.mMin == 0 && t.mMax == 0, "temperature neutralised");
    ctx.check(p.mParameterSpace.get()[0].mMin == 0 && p.mParameterSpace.get()[0].mMax == 0, "parameter space too");
    ctx.check(p.mHumidity.get().mMin != 0, "humidity untouched");
    ctx.check(p.mOffset == 0, "offset neutralised");
});

LP_REGISTER_SELFTEST("misctweaks.ice_roads", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigScope cfg;
    auto        pos = ctx.testPos(2755);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    auto& region = ctx.player().getDimensionBlockSource();
    for (int dx = -1; dx <= 12; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            place(ctx, region, bp(pos, dx, 0, dz), "minecraft:packed_ice");
            lp::game::clearBlock(region, bp(pos, dx, 1, dz));
            lp::game::clearBlock(region, bp(pos, dx, 2, dz));
        }
    auto slide = [&]() -> ll::coro::CoroTask<float> {
        auto* b = ent::spawn(region, "minecraft:boat", Vec3{pos.x + 0.5f, pos.y + 1.f, pos.z + 0.5f});
        ctx.require(b != nullptr, "boat spawned");
        auto id = ent::idOf(*b);
        co_await ctx.ticks(10);
        b = ent::find(id);
        ctx.require(b != nullptr, "boat loaded");
        float x0 = b->getPosition().x;
        const_cast<Vec3&>(b->getPosDelta()).x = 0.8f;
        co_await ctx.ticks(10);
        b        = ent::find(id);
        float dx = b ? b->getPosition().x - x0 : 0;
        if (b) ent::despawn(*b);
        co_return dx;
    };
    cfg->enableGameNerfs = false;
    float vanilla        = co_await slide();
    cfg->enableGameNerfs = true;
    float nerfed         = co_await slide();
    ctx.info("boat slide: vanilla " + std::to_string(vanilla) + ", nerfed " + std::to_string(nerfed));
    ctx.check(nerfed < vanilla * 0.8f, "boats slide much less on ice with the nerf");
});
