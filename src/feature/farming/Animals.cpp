// PigLitters, PoisonPotatoUsage, GrabChickens, HorsesSwim, VillagersFollowEmeralds
// (docs/spec/farming.md §1, §3).
#include "feature/farming/Farming.h"

#include <cmath>
#include <set>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/entity/components/AgeableComponent.h"
#include "mc/entity/components_json_legacy/BreedableComponent.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectIds.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/item/HandSlot.h"

#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

namespace lp::feature::farming {

namespace {

constexpr char const* kTagPoisoned     = "quark:poison_potato_applied";
constexpr char const* kTagGoldenCarrot = "quark:AteGoldenCarrot";

bool isType(Actor const& a, ActorType t) { return a.getEntityTypeId() == t; }

bool isHorseFamily(Actor const& a) {
    switch (a.getEntityTypeId()) {
    case ActorType::Horse:
    case ActorType::Donkey:
    case ActorType::Mule:
    case ActorType::SkeletonHorse:
    case ActorType::ZombieHorse:
    case ActorType::Llama:
    case ActorType::TraderLlama:
    case ActorType::Camel:
        return true;
    default:
        return false;
    }
}

void addHiddenEffect(Actor& actor, MobEffectIds id, int ticks, int amplifier) {
    MobEffectInstance e(static_cast<uint>(id), EffectDuration{ticks});
    e.mAmplifier     = amplifier;
    e.mAmbient       = true;
    e.mEffectVisible = false;
    actor.addEffect(e);
}

void spawnParticle(Actor const& at, std::string const& effect) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto dim = level->getDimension(at.getDimensionId()).lock();
    level->spawnParticleEffect(effect, at.getPosition(), dim.get());
}

bool holds(Player const& p, std::string_view id) {
    auto const& main = p.getSelectedItem();
    if (!main.isNull() && main.getTypeName() == id) return true;
    auto const& off = p.getOffhandSlot();
    return !off.isNull() && off.getTypeName() == id;
}

double distSq(Vec3 const& a, Vec3 const& b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

/// Simulated players host chickens only inside selftests (Quark: real players only).
bool isRealOrTestPlayer(Player const& p) { return !p.isSimulatedPlayer() || selftest::running(); }

} // namespace

// ---- PoisonPotatoUsage -----------------------------------------------------------------------------

bool isPoisoned(Actor const& actor) { return actor.hasTag(kTagPoisoned); }

bool inLove(Actor& actor) {
    if (actor.isInLove()) return true;
    auto breed = actor.getEntityContext().tryGetComponent<BreedableComponent>();
    return breed && breed->mLoveTimer > 0;
}

namespace {

bool canPoison(Actor& target) {
    if (isPoisoned(target)) return false;
    if (isType(target, ActorType::Tadpole)) return true;
    return target.isBaby() && target.getEntityContext().tryGetComponent<AgeableComponent>().has_value();
}

/// Java stunts Animal babies and tadpoles (villagers are AgeableMob but not Animal: they still grow).
void stunt(Actor& actor) {
    if (isType(actor, ActorType::VillagerV2) || isType(actor, ActorType::Villager)) return;
    if (auto age = actor.getEntityContext().tryGetComponent<AgeableComponent>()) age->mGrowthPaused = true;
}

} // namespace

bool usePoisonPotato(Player& player, Actor& target, bool forceSuccess) {
    auto const& c = cfg();
    if (!c.enablePoisonPotatoUsage || !canPoison(target)) return false;
    auto& region = target.getDimensionBlockSource();
    auto  pos    = target.getPosition();
    if (forceSuccess || isCreative(player) || random01() < c.chance) {
        playSoundAt(region, pos.x, pos.y, pos.z, "random.eat", 0.5f, 0.25f);
        spawnParticle(target, "minecraft:mobspell_emitter");
        target.addTag(kTagPoisoned);
        stunt(target);
        if (c.poisonEffect) {
            MobEffectInstance e(static_cast<uint>(MobEffectIds::Poison), EffectDuration{80});
            target.addEffect(e);
        }
        log().info("farming: {} poisoned a baby {}", player.getRealName(), target.getTypeName());
    } else {
        playSoundAt(region, pos.x, pos.y, pos.z, "random.eat", 0.5f, 0.5f + static_cast<float>(random01()) / 2.0f);
        spawnParticle(target, "minecraft:basic_smoke_particle");
    }
    consumeOne(player, player.getSelectedItem());
    return true;
}

// ---- PigLitters --------------------------------------------------------------------------------------

bool feedGoldenCarrot(Player& player, Actor& pig) {
    auto const& c = cfg();
    if (!c.enablePigLitters || !c.pigsEatGoldenCarrots || !isType(pig, ActorType::Pig)) return false;
    if (pig.isBaby() || inLove(pig)) return false;
    auto breed = pig.getEntityContext().tryGetComponent<BreedableComponent>();
    if (!breed || breed->mBreedCooldown > 0) return false;
    breed->mLoveTimer = 600;
    breed->mLoveCause = player.getOrCreateUniqueID();
    pig.setInLove(nullptr);
    BreedableComponent::_spawnLoveParticles(pig);
    pig.addTag(kTagGoldenCarrot);
    consumeOne(player, player.getSelectedItem());
    log().info("farming: {} fed a golden carrot to a pig", player.getRealName());
    return true;
}

int spawnLitter(Actor& parentA, Actor& parentB) {
    auto const& c = cfg();
    if (!c.enablePigLitters) return 0;
    int litter = randomBetween(c.minPigLitterSize, c.maxPigLitterSize);
    if (parentA.hasTag(kTagGoldenCarrot)) litter += randomBetween(c.minGoldenCarrotBoost, c.maxGoldenCarrotBoost);
    if (parentB.hasTag(kTagGoldenCarrot)) litter += randomBetween(c.minGoldenCarrotBoost, c.maxGoldenCarrotBoost);
    int   extra  = 0;
    auto& region = parentA.getDimensionBlockSource();
    for (int i = 1; i < litter; ++i) {
        if (game::entities::spawn(region, "minecraft:pig<minecraft:entity_born>", parentA.getPosition())) ++extra;
    }
    if (extra > 0) log().info("farming: pig litter of {}", extra + 1);
    return extra;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    BreedHook,
    ll::memory::HookPriority::Normal,
    BreedableComponent,
    &BreedableComponent::mate,
    std::vector<WeakEntityRef>,
    Actor& owner,
    Actor& partner
) {
    auto babies = origin(owner, partner);
    if (!babies.empty() && !owner.isClientSide() && isType(owner, ActorType::Pig)) {
        try {
            spawnLitter(owner, partner);
        } catch (std::exception const& e) {
            log().error("farming: litter failed: {}", e.what());
        }
    }
    return babies;
}

} // namespace

// ---- GrabChickens ------------------------------------------------------------------------------------

bool canHostChicken(Player const& player) {
    if (!isRealOrTestPlayer(player)) return false;
    auto const& c = cfg();
    if (c.needsNoHelmet && !player.getArmor(SharedTypes::Legacy::ArmorSlot::Head).isNull()) return false;
    return !player.isImmersedInWater();
}

std::vector<Actor*> carriedChickens(Player& player) {
    std::vector<Actor*> out;
    if (!player.hasPassenger()) return out;
    auto& region = player.getDimensionBlockSource();
    auto  p      = player.getPosition();
    AABB  box{Vec3{p.x - 3, p.y - 3, p.z - 3}, Vec3{p.x + 3, p.y + 4, p.z + 3}};
    for (auto* a : region.getEntities(box, 0.0f)) {
        if (a && isType(*a, ActorType::Chicken) && a->getVehicle() == &player) out.push_back(a);
    }
    return out;
}

namespace {

/// Set while GrabChickens seats a chicken on a player: the vanilla player only accepts tame
/// parrots as passengers (its rideable seats are the shoulder seats).
bool gSeatingChicken = false;

LL_TYPE_INSTANCE_HOOK(
    PlayerCanAddPassengerHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$canAddPassenger,
    bool,
    Actor& passenger
) {
    if (gSeatingChicken && passenger.getEntityTypeId() == ActorType::Chicken) return true;
    return origin(passenger);
}

bool grabChicken(Player& player, Actor& target) {
    auto const& c = cfg();
    if (!c.enableGrabChickens || player.isSneaking() || !isRealOrTestPlayer(player)) return false;
    auto const& hand = player.getSelectedItem();
    if (!hand.isNull() && hand.mCount > 0) return false;
    auto carried = carriedChickens(player);
    if (!carried.empty()) {
        for (auto* ch : carried) ch->stopRiding(true, false, false, false);
        log().info("farming: {} put down {} chicken(s)", player.getRealName(), carried.size());
        return true;
    }
    if (canHostChicken(player) && isType(target, ActorType::Chicken) && !target.isBaby()) {
        gSeatingChicken = true;
        bool ok         = false;
        try {
            ok = target.startRiding(player, true);
        } catch (...) {
            gSeatingChicken = false;
            throw;
        }
        gSeatingChicken = false;
        if (ok) log().info("farming: {} picked up a chicken", player.getRealName());
        return ok;
    }
    return false;
}

void tickChickens(Player& player) {
    if (!player.hasPassenger()) return;
    auto carried = carriedChickens(player);
    if (carried.empty()) return;
    if (!canHostChicken(player)) {
        for (auto* ch : carried) ch->stopRiding(true, false, false, false);
        return;
    }
    addHiddenEffect(player, MobEffectIds::SlowFalling, 5, 0);
    int slow = cfg().slownessLevel;
    if (slow > 0) addHiddenEffect(player, MobEffectIds::MovementSlowdown, 5, slow - 1);
}

} // namespace

bool onInteractEntity(Player& player, Actor& target) {
    if (player.isClientSide()) return false;
    auto const& hand = player.getSelectedItem();
    if (cfg().enableGrabChickens && (hand.isNull() || hand.mCount == 0)) {
        if (grabChicken(player, target)) return true;
    }
    if (hand.isNull()) return false;
    auto id = hand.getTypeName();
    if (id == "minecraft:poisonous_potato") return usePoisonPotato(player, target);
    if (id == "minecraft:golden_carrot") return feedGoldenCarrot(player, target);
    return false;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    InteractHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$interact,
    bool,
    Actor&      entity,
    Vec3 const& location,
    HandSlot    handSlot
) {
    try {
        if (onInteractEntity(mPlayer, entity)) return true;
    } catch (std::exception const& e) {
        log().error("farming: interact failed: {}", e.what());
    }
    return origin(entity, location, handSlot);
}

} // namespace

// ---- HorsesSwim ----------------------------------------------------------------------------------

bool horseSwimTick(Actor& horse) {
    if (!cfg().enableHorsesSwim || !isHorseFamily(horse) || !horse.hasPassenger() || !horse.isInWater()) return false;
    auto&    region = horse.getDimensionBlockSource();
    auto     pos    = horse.getPosition();
    BlockPos below  = game::blockPosOf(Vec3{pos.x, pos.y - 1.0f, pos.z});
    auto isWater = [](Block const& b) {
        auto const& id = b.getTypeName();
        return id == "minecraft:water" || id == "minecraft:flowing_water";
    };
    if (!isWater(region.getBlock(below)) && !isWater(region.getLiquidBlock(below))) return false;
    horse.move(Vec3{0.0f, 0.1f, 0.0f});
    auto delta = horse.getPosDelta();
    if (delta.y < 0.1f) {
        if (auto base = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion)) {
            auto& pkt      = static_cast<SetActorMotionPacket&>(*base);
            pkt.mRuntimeId = horse.getRuntimeID();
            pkt.mMotion    = Vec3{delta.x, 0.1f, delta.z};
            pkt.sendTo(horse);
        }
    }
    return true;
}

// ---- tempting (VillagersFollowEmeralds, golden carrot pigs) ----------------------------------------

namespace {

struct TemptState {
    long long player    = 0;
    long long calmUntil = 0;
};

std::unordered_map<long long, TemptState>& tempts() {
    static std::unordered_map<long long, TemptState> m;
    return m;
}

void tempt(std::vector<Player*> const& holders, ActorType type, float speed) {
    if (holders.empty()) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    std::set<long long> seen;
    for (auto* p : holders) {
        auto& region = p->getDimensionBlockSource();
        auto  pp     = p->getPosition();
        AABB  box{Vec3{pp.x - 10, pp.y - 10, pp.z - 10}, Vec3{pp.x + 10, pp.y + 10, pp.z + 10}};
        for (auto* a : region.getEntities(box, 0.0f)) {
            if (!a || !isType(*a, type) || a->isRiding(nullptr) || a->getVehicle()) continue;
            auto  id = game::entities::idOf(*a);
            auto& s  = tempts()[id];
            if (seen.contains(id)) continue;
            if (s.player == 0 && s.calmUntil > now()) continue;
            if (distSq(a->getPosition(), pp) > 100.0) continue;
            seen.insert(id);
            s.player = p->getOrCreateUniqueID().rawID;
            auto nav = a->getEntityContext().tryGetComponent<NavigationComponent>();
            if (!nav || !nav->mNavigation) continue;
            auto& mob = static_cast<Mob&>(*a);
            if (distSq(a->getPosition(), pp) < 6.25) nav->mNavigation->stop(*nav, mob);
            else nav->mNavigation->moveTo(*nav, mob, static_cast<Actor const&>(*p), speed);
        }
    }
    // tempted mobs whose player went away calm down for 100 ticks (Java TemptGoal)
    for (auto& [id, s] : tempts()) {
        if (s.player != 0 && !seen.contains(id)) {
            if (auto* a = game::entities::find(id); a && isType(*a, type)) {
                s.player    = 0;
                s.calmUntil = now() + 100;
            }
        }
    }
}

} // namespace

void tickAnimals() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto const& c = cfg();
    long long   t = now();

    std::vector<Player*> emerald, carrot;
    level->forEachPlayer([&](Player& p) {
        if (c.enableGrabChickens) tickChickens(p);
        if (c.enableVillagersFollowEmeralds && holds(p, "minecraft:emerald_block")) emerald.push_back(&p);
        if (c.enablePigLitters && c.pigsEatGoldenCarrots && holds(p, "minecraft:golden_carrot")) carrot.push_back(&p);
        return true;
    });
    if (t % 5 == 0) {
        tempt(emerald, ActorType::VillagerV2, 0.6f);
        tempt(carrot, ActorType::Pig, 1.2f);
        if (emerald.empty() && carrot.empty() && t % 100 == 0) {
            std::erase_if(tempts(), [t](auto const& kv) { return kv.second.player == 0 && kv.second.calmUntil <= t; });
        }
    }

    bool swim = c.enableHorsesSwim;
    bool slow = t % 20 == 0;
    if (!swim && !slow) return;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a) continue;
        if (swim && isHorseFamily(*a)) horseSwimTick(*a);
        if (slow) {
            if (a->hasTags() && isPoisoned(*a)) {
                if (c.enablePoisonPotatoUsage) stunt(*a);
                else if (auto age = a->getEntityContext().tryGetComponent<AgeableComponent>()) age->mGrowthPaused = false;
            }
            if (a->hasTags() && a->hasTag(kTagGoldenCarrot) && !inLove(*a)) a->removeTag(kTagGoldenCarrot);
        }
    }
}

void installAnimals() {
    PlayerCanAddPassengerHook::hook();
    InteractHook::hook();
    BreedHook::hook();
}

void uninstallAnimals() {
    PlayerCanAddPassengerHook::unhook();
    InteractHook::unhook();
    BreedHook::unhook();
    tempts().clear();
}

} // namespace lp::feature::farming
