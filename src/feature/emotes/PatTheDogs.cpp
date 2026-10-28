// U19 PatTheDogs (Quark tweaks): sneak + use an empty main hand on a tamed wolf to pet it (a
// heart, a whine), other mobs when allowed by config; with `dogsWantLove` > 0 tamed wolves that
// were not pet for that many ticks leap at and follow their owner. See docs/spec/emotes.md.

#include "feature/emotes/PatTheDogs.h"

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/dimension/Dimension.h"

#include "feature/emotes/EmotesConfig.h"
#include "game/Entities.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::emotes::pat {

namespace {

constexpr char const* kDbKey = "lp:emotes:pet_times";

EmotesConfig const& cfg() { return config::get<EmotesConfig>(); }

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float random01() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

// ---- pet times (Quark: the wolf's persistent "quark:PetTime" tag) --------------------------------

std::unordered_map<std::int64_t, std::int64_t> gPetTimes;
bool                                           gDirty = false;
std::set<std::int64_t>                         gSeenUntamed; ///< wolves seen wild (to notice taming)

std::int64_t now() {
    auto level = ll::service::getLevel();
    return level ? static_cast<std::int64_t>(level->getCurrentTick().tickID) : 0;
}

void loadTimes() {
    gPetTimes.clear();
    gDirty = false;
    auto j = game::db::load(kDbKey);
    if (!j || !j->is_object()) return;
    for (auto const& [k, v] : j->items()) {
        try {
            if (v.is_number_integer()) gPetTimes[std::stoll(k)] = v.get<std::int64_t>();
        } catch (...) {
        }
    }
}

void saveTimes() {
    if (!gDirty || !game::db::available()) return;
    nlohmann::json j = nlohmann::json::object();
    for (auto const& [id, t] : gPetTimes) j[std::to_string(id)] = t;
    game::db::save(kDbKey, j);
    gDirty = false;
}

bool isDog(std::string const& type) { return type == "minecraft:wolf" || type == "lepton:foxhound"; }

bool contains(std::vector<std::string> const& list, std::string const& v) { return std::find(list.begin(), list.end(), v) != list.end(); }

void sound(Actor const& at, Vec3 const& pos, std::string const& name, float volume, float pitch) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!pkt) return;
    auto& ps    = static_cast<PlaySoundPacket&>(*pkt);
    ps.mName    = name;
    ps.mPos     = BlockPos(Vec3(pos.x * 8, pos.y * 8, pos.z * 8)); // network position: 1/8 block
    ps.mVolume  = volume;
    ps.mPitch   = pitch;
    pkt->sendTo(BlockPos(pos), at.getDimensionId());
}

void heart(Actor const& at, Vec3 const& pos) {
    at.getLevel().spawnParticleEffect("minecraft:heart_particle", pos, &at.getDimension());
}

struct PetSound {
    std::string sound;
    float       pitchCenter = 1.f;
};

/// Quark's sound per pettable mob (Java `instanceof` classes mapped to Bedrock ids).
std::optional<PetSound> soundFor(std::string const& type) {
    static std::map<std::string, PetSound> const table{
        {"minecraft:axolotl", {"mob.axolotl.splash"}},
        {"minecraft:cat", {"mob.cat.purreow"}},
        {"minecraft:ocelot", {"mob.cat.purreow"}},
        {"minecraft:chicken", {"mob.chicken.say"}},
        {"minecraft:cow", {"mob.cow.say", 1.2f}},
        {"minecraft:mooshroom", {"mob.cow.say", 1.2f}},
        {"minecraft:horse", {"mob.horse.idle"}},
        {"minecraft:donkey", {"mob.horse.idle"}},
        {"minecraft:mule", {"mob.horse.idle"}},
        {"minecraft:skeleton_horse", {"mob.horse.idle"}},
        {"minecraft:zombie_horse", {"mob.horse.idle"}},
        {"minecraft:llama", {"mob.horse.idle"}},
        {"minecraft:trader_llama", {"mob.horse.idle"}},
        {"minecraft:camel", {"mob.horse.idle"}},
        {"minecraft:cod", {"mob.fish.step"}},
        {"minecraft:salmon", {"mob.fish.step"}},
        {"minecraft:pufferfish", {"mob.fish.step"}},
        {"minecraft:tropicalfish", {"mob.fish.step"}},
        {"minecraft:tadpole", {"mob.fish.step"}},
        {"minecraft:fox", {"mob.fox.sleep"}},
        {"minecraft:squid", {"mob.squid.ink_squirt", 1.2f}},
        {"minecraft:glow_squid", {"mob.glow_squid.ink_squirt", 1.2f}},
        {"minecraft:parrot", {"mob.parrot.idle"}},
        {"minecraft:pig", {"mob.pig.say"}},
        {"minecraft:rabbit", {"mob.rabbit.idle"}},
        {"minecraft:sheep", {"mob.sheep.say"}},
        {"minecraft:strider", {"mob.strider.happy"}},
        {"minecraft:turtle", {"mob.turtle.ambient"}},
    };
    if (auto it = table.find(type); it != table.end()) return it->second;
    return std::nullopt;
}

std::int64_t idOf(Actor const& a) { return game::entities::idOf(a); }

void igniteForFoxhound(Player& player) {
    constexpr unsigned kFireResistance = 12;
    if (player.isInWater() || player.getEffect(kFireResistance) || player.isCreative() || player.isSpectator()) return;
    player.setOnFire(1, true);
}

} // namespace

std::int64_t timeSinceLastPet(Actor const& dog) {
    if (!dog.isTame()) return 0;
    auto it = gPetTimes.find(idOf(dog));
    return now() - (it == gPetTimes.end() ? 0 : it->second);
}

bool canPet(Actor const& dog) { return timeSinceLastPet(dog) > 20; }

bool needsPets(Actor const& dog) {
    int want = cfg().dogsWantLove;
    if (want <= 0) return false;
    return timeSinceLastPet(dog) > want;
}

void setPetTime(Actor const& dog) {
    if (cfg().dogsWantLove <= 0) return;
    gPetTimes[idOf(dog)] = now();
    gDirty               = true;
}

PetResult tryPet(Player& player, Actor& target) {
    if (!cfg().enablePatTheDogs) return PetResult::None;
    if (!player.isSneaking() || !player.getSelectedItem().isNull()) return PetResult::None;
    std::string type = game::entities::typeOf(target);

    if (isDog(type)) {
        if (!canPet(target)) return PetResult::Refused; // Quark cancels the interaction anyway
        auto pos = target.getPosition();
        heart(target, Vec3(pos.x, pos.y + 0.5f, pos.z));
        sound(target, pos, "mob.wolf.whine", 1.f, 0.5f + random01() * 0.5f);
        setPetTime(target);
        if (type == "lepton:foxhound") igniteForFoxhound(player);
        log().info("pat: {} pets a {}", player.getRealName(), type);
        return PetResult::Pet;
    }

    bool living = target.isPlayer() || target.hasCategory(ActorCategory::Mob);
    if (!living) return PetResult::None;
    auto const& c = cfg();
    if (!(c.petAllMobs || target.isPlayer() || contains(c.pettableAllowlist, type)) || contains(c.pettableDenylist, type))
        return PetResult::None;

    std::optional<PetSound> s;
    bool                    pettable = false;
    if (target.isPlayer()) {
        // Quark only has sounds for a few of its contributors' Java accounts; an allowlisted
        // "minecraft:player" is pettable (Quark #5538), without a sound on Bedrock.
        pettable = contains(c.pettableAllowlist, "minecraft:player");
    } else if ((s = soundFor(type))) {
        pettable = true;
    } else {
        pettable = c.petAllMobs || contains(c.pettableAllowlist, type);
    }
    if (!pettable) return PetResult::None;

    auto eye = target.getEyePos();
    heart(target, Vec3(eye.x, eye.y + 0.5f, eye.z));
    if (s) sound(target, target.getPosition(), s->sound, 1.f, s->pitchCenter + (random01() - 0.5f) * 0.5f);
    log().info("pat: {} pets a {}", player.getRealName(), type);
    return PetResult::Pet;
}

namespace {

LL_TYPE_INSTANCE_HOOK(PetHook, ll::memory::HookPriority::Normal, Player, &Player::interact, InteractionResult, Actor& actor, Vec3 const& location) {
    if (!isClientSide()) {
        auto r = tryPet(*this, actor);
        if (r != PetResult::None) {
            InteractionResult res{};
            res.mSuccess = r == PetResult::Pet;
            res.mSwing   = r == PetResult::Pet;
            return res;
        }
    }
    return origin(actor, location);
}

// ---- dogs want love (Quark's WantLoveGoal + NuzzleGoal) -------------------------------------------

struct DogState {
    bool leaping       = false;
    int  rebuildPath   = 0;
    int  whineCooldown = 10;
    bool nuzzling      = false;
};
std::unordered_map<std::int64_t, DogState> gDogs;

Player* ownerOf(Actor const& dog) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto* a = level->fetchEntity(dog.getOwnerId(), false);
    return (a && a->isPlayer()) ? static_cast<Player*>(a) : nullptr;
}

void tickDog(Actor& dog) {
    auto  id    = idOf(dog);
    auto& state = gDogs[id];
    if (!dog.isTame()) {
        gSeenUntamed.insert(id);
        return;
    }
    if (gSeenUntamed.erase(id)) setPetTime(dog); // Quark: AnimalTameEvent
    if (!needsPets(dog)) {
        state = {};
        return;
    }
    Player* owner = ownerOf(dog);
    if (!owner || owner->getDimensionId() != dog.getDimensionId()) {
        state = {};
        return;
    }
    float dist2 = dog.distanceToSqr(*owner);

    // WantLoveGoal (priority 20): between 2 and 4 blocks, on the ground, 1 in 5 chance: a hop
    // towards the owner; runs until the dog lands.
    if (state.leaping) {
        if (dog.isOnGround()) state.leaping = false;
        else return;
    }
    if (dist2 >= 4 && dist2 <= 16 && dog.isOnGround() && std::uniform_int_distribution<int>(0, 4)(rng()) == 0) {
        auto  op = owner->getPosition();
        auto  dp = dog.getPosition();
        float dx = op.x - dp.x, dz = op.z - dp.z;
        float mag    = std::sqrt(dx * dx + dz * dz);
        Vec3  motion = dog.getPosDelta();
        if (mag >= 0.0001f) {
            motion.x += dx / mag * 0.4f + motion.x * 0.2f;
            motion.z += dz / mag * 0.4f + motion.z * 0.2f;
        }
        motion.y += 0.2f;
        dog.mBuiltInComponents->mStateVectorComponent->mPosDelta = motion;
        state.leaping                                            = true;
        log().debug("pat: a dog leaps at {}", owner->getRealName());
        return;
    }

    // NuzzleGoal (priority 21): walk to the owner at speed 0.5 and whine next to them.
    if (owner->isSpectator() || dog.isSitting()) {
        state.nuzzling = false;
        return;
    }
    if (!state.nuzzling) {
        state.nuzzling      = true;
        state.rebuildPath   = 0;
        state.whineCooldown = 10;
    }
    if (--state.rebuildPath <= 0) {
        state.rebuildPath = 10;
        if (auto nav = dog.getEntityContext().tryGetComponent<NavigationComponent>(); nav && nav->mNavigation.get())
            nav->mNavigation.get()->moveTo(*nav, static_cast<Mob&>(dog), static_cast<Actor const&>(*owner), 0.5f);
    }
    if (dist2 < 2 && --state.whineCooldown <= 0) {
        state.whineCooldown = 80 + std::uniform_int_distribution<int>(0, 39)(rng());
        sound(dog, dog.getPosition(), "mob.wolf.whine", 1.f, 0.5f + random01() * 0.5f);
    }
}

void onTick(ll::event::ServerLevelTickEvent&) {
    if (!cfg().enablePatTheDogs || cfg().dogsWantLove <= 0) return;
    for (char const* type : {"minecraft:wolf", "lepton:foxhound"})
        for (auto* dog : game::entities::ofType(type))
            if (dog && dog->isAlive()) tickDog(*dog);
}

} // namespace

class PatTheDogsModule {
public:
    void install() {
        PetHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(onTick);
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldReady([] { loadTimes(); });
            game::db::onWorldSave([] { saveTimes(); });
            game::db::onWorldClosed([] {
                gPetTimes.clear();
                gDogs.clear();
                gSeenUntamed.clear();
                gDirty = false;
            });
        }
        if (game::db::available()) loadTimes();
    }
    void uninstall() {
        PetHook::unhook();
        ll::event::EventBus::getInstance().removeListener(mTick);
        saveTimes();
    }

private:
    ll::event::ListenerPtr mTick;
};

namespace testing {
void setLastPet(Actor const& dog, std::int64_t tick) {
    gPetTimes[idOf(dog)] = tick;
    gDirty               = true;
}
void clearLastPet(Actor const& dog) { gPetTimes.erase(idOf(dog)); }
std::int64_t currentTick() { return now(); }
} // namespace testing

} // namespace lp::feature::emotes::pat

LP_REGISTER_MODULE(lp::feature::emotes::pat::PatTheDogsModule);
