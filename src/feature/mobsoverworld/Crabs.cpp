// Crabs (Quark CrabsModule / Crab): lightning growth, raving, bucket, armour, immunities, sounds.
// The vanilla goals, breeding, spikes and variants are in bp/entities/crab.json.

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"

#include "feature/mobsoverworld/Common.h"
#include "feature/mobsoverworld/Config.h"
#include "feature/mobsoverworld/Features.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::mobsoverworld::crabs {

namespace {

using Cause = SharedTypes::Legacy::ActorDamageCause;

constexpr float kJukeboxRadius = 10.0f; // GameEvent.JUKEBOX_PLAY notification radius
constexpr int   kLightningCooldown = 150;

using JukeboxKey = std::tuple<int, int, int, int>; // dim, x, y, z
std::set<JukeboxKey>                       gJukeboxes;
std::unordered_map<int64_t, JukeboxKey>    gRaveJukebox; // crab -> jukebox it dances to
std::unordered_map<int64_t, int>           gAmbient;

struct PendingRelease {
    int64_t   player;
    int       dim;
    BlockPos  pos;
    ItemStack bucket;
};
std::vector<PendingRelease> gPending;

JukeboxKey keyOf(int dim, BlockPos const& p) { return {dim, p.x, p.y, p.z}; }
BlockPos   posOf(JukeboxKey const& k) { return {std::get<1>(k), std::get<2>(k), std::get<3>(k)}; }
Vec3       centerOf(BlockPos const& p) {
    return {static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f};
}

bool jukeboxPlaying(JukeboxKey const& key) {
    auto* region = game::regionOf(std::get<0>(key));
    if (!region) return false;
    BlockPos pos = posOf(key);
    if (game::blockIdAt(*region, pos) != "minecraft:jukebox") return false;
    auto* be = region->getBlockEntity(pos);
    if (!be) return false;
    auto* jukebox = static_cast<JukeboxBlockActor*>(be);
    return !jukebox->mRecord->isNull() && !jukebox->mRecordingFinished;
}

void startRave(Actor& crab, JukeboxKey const& key) {
    gRaveJukebox[game::entities::idOf(crab)] = key;
    if (!isRaving(crab)) {
        game::entities::triggerEvent(crab, "lepton:rave_start");
        stopNavigation(crab);
        log().info("mobsoverworld: crab starts raving");
    }
}

void stopRave(Actor& crab) {
    gRaveJukebox.erase(game::entities::idOf(crab));
    if (isRaving(crab)) {
        game::entities::triggerEvent(crab, "lepton:rave_stop");
        log().info("mobsoverworld: crab stops raving");
    }
}

/// Quark's Crab.shouldStopRaving.
bool shouldStopRaving(Actor& crab) {
    auto it = gRaveJukebox.find(game::entities::idOf(crab));
    if (it == gRaveJukebox.end()) return true;
    auto const& key = it->second;
    if (std::get<0>(key) != static_cast<int>(crab.getDimensionId())) return true;
    Vec3 c = centerOf(posOf(key));
    if (crab.getPosition().distanceTo(c) > kJukeboxRadius) return true;
    auto* region = game::regionOf(std::get<0>(key));
    return !region || game::blockIdAt(*region, posOf(key)) != "minecraft:jukebox";
}

void ambient(Actor& crab) {
    int& t = gAmbient[game::entities::idOf(crab)];
    if (randomInt(1000) < t++) {
        t = -80;
        playSound(crab, "mob.turtle.ambient", crab.getPosition(), 1.0f, (randomFloat() - randomFloat()) * 0.2f + 1.4f);
    }
}

} // namespace

int  sizeOf(Actor& crab) { return std::clamp(intProperty(crab, "lepton:size"), 0, 8); }
bool isRaving(Actor& crab) { return boolProperty(crab, "lepton:raving"); }

bool strikeLightning(Actor& crab, int64_t now) {
    int64_t until = intProperty(crab, "lepton:lightning_until");
    if (now < until) return false;
    int size = sizeOf(crab);
    if (size >= 8) return false; // scale 17 > 15: no more growth
    game::entities::triggerEvent(crab, "lepton:size_" + std::to_string(size + 1));
    setIntProperty(crab, "lepton:lightning_until", static_cast<int>(now + kLightningCooldown));
    crab.stopFire();
    log().info("mobsoverworld: crab struck by lightning grows to scale {}", 1 + 2 * (size + 1));
    return true;
}

void tick(Actor& crab, int64_t now) {
    int64_t uid   = game::entities::idOf(crab);
    int     phase = static_cast<int>((now + uid) % 20);
    if (phase % 10 == 0) {
        if (intProperty(crab, "lepton:lightning_until") > now && crab.isOnFire()) crab.stopFire();
        if (isRaving(crab)) {
            // Quark spawns block-break particles of the sand below a dancing crab every 10 ticks.
            auto&    region = crab.getDimensionBlockSource();
            BlockPos under  = below(crab);
            auto     id     = game::blockIdAt(region, under);
            if (id == "minecraft:sand" || id == "minecraft:red_sand" || id == "minecraft:suspicious_sand") {
                blockBreakParticles(region, under);
            }
            if (phase == 0 && shouldStopRaving(crab)) stopRave(crab);
        }
    }
    ambient(crab);
}

void tickWorld(int64_t now) {
    // Bucket releases deferred from the (cancelled) interaction.
    if (!gPending.empty()) {
        auto pending = std::move(gPending);
        gPending.clear();
        for (auto& p : pending) {
            auto* region = game::regionOf(p.dim);
            if (!region) continue;
            if (game::replaceable(*region, p.pos)) {
                if (auto const* water = game::blockWith("minecraft:water")) game::setBlock(*region, p.pos, *water);
            }
            Vec3 at{static_cast<float>(p.pos.x) + 0.5f, static_cast<float>(p.pos.y), static_cast<float>(p.pos.z) + 0.5f};
            if (Actor* crab = releaseFromBucket(p.bucket, p.dim, at)) {
                playSound(*crab, "bucket.empty_fish", at);
            }
            if (Player* player = playerById(p.player); player && !isCreative(*player)) {
                if (game::itemId(player->getSelectedItem()) == "lepton:crab_bucket") {
                    setSelected(*player, game::makeItem("minecraft:bucket"));
                }
            }
        }
    }
    if (now % 20 != 0) return;
    // Jukeboxes notify crabs within 10 blocks every second while they play (JUKEBOX_PLAY).
    for (auto it = gJukeboxes.begin(); it != gJukeboxes.end();) {
        if (!jukeboxPlaying(*it)) {
            jukeboxStopped(std::get<0>(*it), posOf(*it));
            it = gJukeboxes.begin(); // jukeboxStopped erased it
            continue;
        }
        Vec3 c = centerOf(posOf(*it));
        for (Actor* crab : game::entities::ofType(kCrab, std::get<0>(*it))) {
            if (crab && crab->isAlive() && crab->getPosition().distanceTo(c) <= kJukeboxRadius) startRave(*crab, *it);
        }
        ++it;
    }
}

void jukeboxStarted(int dim, BlockPos const& pos) {
    if (!config().enableCrabs) return;
    gJukeboxes.insert(keyOf(dim, pos));
}

void jukeboxStopped(int dim, BlockPos const& pos) {
    auto key = keyOf(dim, pos);
    gJukeboxes.erase(key);
    // JUKEBOX_STOP_PLAY: crabs dancing to this jukebox stop.
    for (Actor* crab : game::entities::ofType(kCrab, dim)) {
        if (!crab) continue;
        auto it = gRaveJukebox.find(game::entities::idOf(*crab));
        if (it != gRaveJukebox.end() && it->second == key) stopRave(*crab);
    }
}

bool onHurt(Actor& crab, ActorDamageSource const& source, float& damage) {
    Cause cause = source.mCause;
    int   size  = sizeOf(crab);
    switch (cause) {
    case Cause::Lightning:
        strikeLightning(crab, gameTick());
        return true;
    case Cause::Contact:  // cactus, sweet berry bush
    case Cause::FireTick: // ON_FIRE
        return true;
    case Cause::Fire: // IN_FIRE, when bigger than normal
        if (size > 0) return true;
        break;
    default:
        break;
    }
    if (source.isReducedByArmorReduction()) {
        // Java armour: 3 (+0.125 per lightning strike) with toughness 2.
        float armor     = 3.0f + 0.125f * static_cast<float>(size);
        float toughness = 2.0f;
        float f         = std::clamp(armor - damage / (2.0f + toughness / 4.0f), armor * 0.2f, 20.0f);
        damage *= 1.0f - f / 25.0f;
    }
    if (crab.getHealth() - damage <= 0) playSound(crab, "mob.turtle.death", crab.getPosition(), 1.0f, 1.3f);
    else playSound(crab, "mob.turtle.hurt", crab.getPosition(), 1.0f, 1.3f);
    return false;
}

ItemStack bucketOf(Actor& crab) {
    ItemStack      bucket = game::makeItem("lepton:crab_bucket");
    nlohmann::json data{
        {"variant", crab.getVariant()},
        {"mold", crab.getMarkVariant()},
        {"health", crab.getHealth()},
        {"name", crab.getNameTag()},
        {"noSpike", boolProperty(crab, "lepton:no_spike")},
    };
    auto tag = std::make_unique<CompoundTag>();
    tag->putString("lepton_crab", data.dump());
    bucket.setUserData(std::move(tag));
    return bucket;
}

Actor* releaseFromBucket(ItemStack const& bucket, int dim, Vec3 const& pos) {
    if (!config().enableCrabs) return nullptr;
    Actor* crab = game::entities::spawn(dim, kCrab, pos);
    if (!crab) return nullptr;
    nlohmann::json data = nlohmann::json::object();
    if (bucket.mUserData && bucket.mUserData->contains("lepton_crab")) {
        auto const* t = bucket.mUserData->get("lepton_crab");
        if (t && t->getId() == Tag::Type::String) {
            data = nlohmann::json::parse(static_cast<std::string const&>(*static_cast<StringTag const*>(t)), nullptr, false);
            if (data.is_discarded()) data = nlohmann::json::object();
        }
    }
    if (data.contains("variant")) {
        game::entities::triggerEvent(*crab, "lepton:color_" + std::to_string(std::clamp(data.value("variant", 0), 0, 2)));
        game::entities::triggerEvent(*crab, data.value("mold", 0) == 1 ? "lepton:mold" : "lepton:no_mold");
    }
    if (data.value("noSpike", false)) game::entities::triggerEvent(*crab, "lepton:no_spike");
    if (auto name = data.value("name", std::string{}); !name.empty()) crab->setNameTag(name);
    int health = data.value("health", 0);
    if (health > 0 && health < crab->getHealth()) crab->hurtByCause(static_cast<float>(crab->getHealth() - health));
    game::entities::triggerEvent(*crab, "lepton:from_bucket");
    log().info("mobsoverworld: crab released from a bucket");
    return crab;
}

Handled interact(Player& player, Actor& crab) {
    if (!config().enableCrabs) return Handled::No;
    ItemStack const& held = player.getSelectedItem();
    // Bucketable.bucketMobPickup: only crabs of normal size (scale < 2).
    if (isWaterBucket(held) && sizeOf(crab) == 0 && crab.isAlive()) {
        ItemStack filled = bucketOf(crab);
        playSound(crab, "bucket.fill_fish", crab.getPosition());
        game::entities::despawn(crab);
        if (isCreative(player)) giveOrDrop(player, filled);
        else if (held.mCount <= 1) setSelected(player, filled);
        else {
            consumeSelected(player);
            giveOrDrop(player, filled);
        }
        log().info("mobsoverworld: crab caught in a bucket");
        return Handled::Yes;
    }
    return Handled::No;
}

bool useBucket(Player& player, BlockPos const& clicked, int face) {
    if (!config().enableCrabs) return false;
    auto&    region = player.getDimensionBlockSource();
    BlockPos target = clicked;
    if (!game::replaceable(region, clicked)) {
        static constexpr int kOffsets[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        if (face >= 0 && face < 6) {
            target.x += kOffsets[face][0];
            target.y += kOffsets[face][1];
            target.z += kOffsets[face][2];
        }
    }
    gPending.push_back({game::entities::idOf(player), static_cast<int>(player.getDimensionId()), target,
                        ItemStack(player.getSelectedItem())});
    return true;
}

void clear() {
    gJukeboxes.clear();
    gRaveJukebox.clear();
    gAmbient.clear();
    gPending.clear();
}

} // namespace lp::feature::mobsoverworld::crabs
