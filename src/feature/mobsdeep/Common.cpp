// Shared helpers of the mobsdeep unit: config registration, variant table, wraith sound mapping,
// sounds / particles, entity property reads, soul bead geometry, fortress search.

#include <cmath>
#include <map>
#include <variant>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyContainer.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/WorldGenerator.h"

#include "feature/mobsdeep/Mobsdeep.h"
#include "game/Blocks.h"
#include "mod/Config.h"

LP_REGISTER_CONFIG("mobsdeep", lp::feature::mobsdeep::Config);

namespace lp::feature::mobsdeep {

Config const& config() { return lp::config::get<Config>(); }

int variantOf(std::string_view blockId) {
    if (blockId.empty()) return -1;
    for (int i = 0; i < static_cast<int>(kVariants.size()); ++i)
        for (auto b : kVariants[i].blocks)
            if (!b.empty() && b == blockId) return i;
    return -1;
}

std::string bedrockSound(std::string_view javaId) {
    static std::map<std::string, std::string, std::less<>> const table{
        {"entity.sheep.ambient", "mob.sheep.say"},           {"entity.sheep.hurt", "mob.sheep.say"},
        {"entity.sheep.death", "mob.sheep.say"},             {"entity.cow.ambient", "mob.cow.say"},
        {"entity.cow.hurt", "mob.cow.hurt"},                 {"entity.cow.death", "mob.cow.hurt"},
        {"entity.pig.ambient", "mob.pig.say"},               {"entity.pig.hurt", "mob.pig.say"},
        {"entity.pig.death", "mob.pig.death"},               {"entity.chicken.ambient", "mob.chicken.say"},
        {"entity.chicken.hurt", "mob.chicken.hurt"},         {"entity.chicken.death", "mob.chicken.hurt"},
        {"entity.horse.ambient", "mob.horse.idle"},          {"entity.horse.hurt", "mob.horse.hit"},
        {"entity.horse.death", "mob.horse.death"},           {"entity.cat.ambient", "mob.cat.meow"},
        {"entity.cat.hurt", "mob.cat.hit"},                  {"entity.cat.death", "mob.cat.hit"},
        {"entity.wolf.ambient", "mob.wolf.bark"},            {"entity.wolf.hurt", "mob.wolf.hurt"},
        {"entity.wolf.death", "mob.wolf.death"},             {"entity.villager.ambient", "mob.villager.idle"},
        {"entity.villager.hurt", "mob.villager.hit"},        {"entity.villager.death", "mob.villager.death"},
        {"entity.polar_bear.ambient", "mob.polarbear.idle"}, {"entity.polar_bear.hurt", "mob.polarbear.hurt"},
        {"entity.polar_bear.death", "mob.polarbear.death"},  {"entity.zombie.ambient", "mob.zombie.say"},
        {"entity.zombie.hurt", "mob.zombie.hurt"},           {"entity.zombie.death", "mob.zombie.death"},
        {"entity.skeleton.ambient", "mob.skeleton.say"},     {"entity.skeleton.hurt", "mob.skeleton.hurt"},
        {"entity.skeleton.death", "mob.skeleton.death"},     {"entity.spider.ambient", "mob.spider.say"},
        {"entity.spider.hurt", "mob.spider.say"},            {"entity.spider.death", "mob.spider.death"},
        {"entity.creeper.hurt", "mob.creeper.say"},          {"entity.creeper.death", "mob.creeper.death"},
        {"entity.enderman.ambient", "mob.endermen.idle"},    {"entity.enderman.hurt", "mob.endermen.hit"},
        {"entity.enderman.death", "mob.endermen.death"},     {"entity.zombified_piglin.ambient", "mob.zombiepig.zpig"},
        {"entity.zombified_piglin.hurt", "mob.zombiepig.zpighurt"},
        {"entity.zombified_piglin.death", "mob.zombiepig.zpigdeath"},
        {"entity.witch.ambient", "mob.witch.ambient"},       {"entity.witch.hurt", "mob.witch.hurt"},
        {"entity.witch.death", "mob.witch.death"},           {"entity.blaze.ambient", "mob.blaze.breathe"},
        {"entity.blaze.hurt", "mob.blaze.hit"},              {"entity.blaze.death", "mob.blaze.death"},
        {"entity.llama.ambient", "mob.llama.idle"},          {"entity.llama.hurt", "mob.llama.hurt"},
        {"entity.llama.death", "mob.llama.death"},           {"entity.frog.ambient", "mob.frog.ambient"},
        {"entity.frog.hurt", "mob.frog.hurt"},               {"entity.frog.death", "mob.frog.death"},
        {"quark:entity.stoneling.cry", "lepton.stoneling.cry"},
        {"quark:entity.stoneling.die", "lepton.stoneling.die"},
    };
    std::string_view id = javaId;
    if (id.starts_with("minecraft:")) id.remove_prefix(10);
    if (auto it = table.find(id); it != table.end()) return it->second;
    return std::string(javaId);
}

std::vector<Voice> wraithVoices() {
    std::vector<Voice> out;
    for (auto const& entry : config().wraithSounds) {
        std::vector<std::string> parts;
        std::string_view         rest = entry;
        while (true) {
            auto bar = rest.find('|');
            parts.emplace_back(rest.substr(0, bar));
            if (bar == std::string_view::npos) break;
            rest.remove_prefix(bar + 1);
        }
        if (parts.size() != 3) continue; // Quark: entries with a different count are ignored
        auto map = [](std::string const& s) { return s.empty() ? std::string{} : bedrockSound(s); };
        out.push_back({map(parts[0]), map(parts[1]), map(parts[2])});
    }
    return out;
}

namespace {
// Entity properties are read from the actor's PropertyComponent (Actor::evalMolang does not allow
// query functions such as query.property in its context).
std::optional<std::variant<int, float, bool, std::string>> propValue(Actor& actor, std::string_view prop) {
    auto comp = actor.getEntityContext().tryGetComponent<PropertyComponent>();
    if (!comp) return std::nullopt;
    return comp->mPropertyContainer->getValueVariant(HashedString(std::string(prop)).getHash());
}
} // namespace

bool boolProp(Actor& actor, std::string_view prop) {
    auto v = propValue(actor, prop);
    if (!v) return false;
    if (auto const* b = std::get_if<bool>(&*v)) return *b;
    if (auto const* i = std::get_if<int>(&*v)) return *i != 0;
    return false;
}

int intProp(Actor& actor, std::string_view prop) {
    auto v = propValue(actor, prop);
    if (!v) return 0;
    if (auto const* i = std::get_if<int>(&*v)) return *i;
    if (auto const* f = std::get_if<float>(&*v)) return static_cast<int>(std::lround(*f));
    if (auto const* b = std::get_if<bool>(&*v)) return *b ? 1 : 0;
    return 0;
}

void playSoundAt(Actor& near, std::string const& name, Vec3 const& pos, float volume, float pitch) {
    if (name.empty()) return;
    PlaySoundPacket pkt;
    pkt.mName   = name;
    pkt.mPos    = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    pkt.sendTo(near);
}

void particleAt(Actor& in, std::string const& effect, Vec3 const& pos) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& dim = in.getDimension();
    level->spawnParticleEffect(effect, pos, &dim);
}

bool shouldUseBow(Actor& forgotten) {
    auto level = ll::service::getLevel();
    if (!level) return true;
    ActorUniqueID tid = forgotten.mTargetId;
    if (tid.rawID == 0 || tid.rawID == -1) return true;
    auto* target = level->fetchEntity(tid, false);
    if (!target || !target->isAlive()) return true;
    auto const* blind = target->getEffect(15u); // blindness
    return !blind || blind->mDuration->mValue < 20;
}

Vec3 soulBeadPoint(Vec3 const& start, int targetX, int targetZ, int t) {
    constexpr double scale = 0.08, rotateSpread = 1.5, rise = 0.025;
    double dx = targetX - start.x, dz = targetZ - start.z;
    double len = std::sqrt(dx * dx + dz * dz);
    double vx = len > 1e-9 ? dx / len * scale : 0.0, vz = len > 1e-9 ? dz / len * scale : 0.0;
    double arg = t * 0.32;
    return Vec3{
        static_cast<float>(start.x + vx * t + std::cos(arg) * rotateSpread),
        static_cast<float>(start.y + t * rise),
        static_cast<float>(start.z + vz * t + std::sin(arg) * rotateSpread)};
}

std::optional<std::pair<int, int>> findFortress(int dim, Vec3 const& origin) {
    auto* region = game::regionOf(dim);
    if (!region) return std::nullopt;
    auto& dimension = region->getDimension();
    WorldGenerator* gen = dimension.mWorldGenerator;
    if (!gen) return std::nullopt;
    BlockPos from = game::blockPosOf(origin);
    for (char const* name : {"minecraft:fortress", "fortress"}) {
        BlockPos found{};
        if (gen->findNearestStructureFeature(HashedString(name), from, found, false, std::nullopt)) {
            double dx = found.x - from.x, dz = found.z - from.z;
            if (dx * dx + dz * dz > 1600.0 * 1600.0) return std::nullopt; // Quark: 100 chunks
            return std::pair{found.x, found.z};
        }
    }
    return std::nullopt;
}

} // namespace lp::feature::mobsdeep
