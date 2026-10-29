// U21 mobsdeep: the native half of Stonelings, Forgotten, Foxhound and Wraith (docs/spec/mobsdeep.md).
//
// The behaviour pack entities carry the vanilla-expressible parts (movement, goals, taming state,
// breeding, loot, spawn rules). This module adds Quark's rules on top:
//   - entity interactions (Player::interact hook): stoneling items / feeding / taming, foxhound coal
//     taming and tiny potatoes;
//   - a per-tick pass over the loaded mobs: stoneling wariness, startling and poofing, Forgotten
//     weapon swapping, foxhound furnace boosting / resting / potatoes, wraith voices, soul beads;
//   - hurt / death events: projectile immunity, startle on hit, blinding arrows, burning bites,
//     wraith aggro / slowness / leap, carried item and quill drops, sounds;
//   - item use: Heart of Diamond on stone, Soul Bead; Forgotten Hat luck;
//   - spawn filtering from config and the skeleton -> Forgotten replacement.

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/ActorHurtEvent.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/event/world/SpawnMobEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/util/Random.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/level/ShapeType.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorEvent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/AttributeModificationContext.h"
#include "mc/world/attribute/AttributeModifier.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/mobsdeep/Mobsdeep.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::mobsdeep {
namespace {

namespace ent = lp::game::entities;
using Cause = SharedTypes::Legacy::ActorDamageCause;

constexpr char const* kDbKey = "lp:mobsdeep:state";

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
double rand01() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }
int    randInt(int n) { return n <= 0 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(rng()); }

std::optional<double> gForcedTameChance;

// ---- per-entity transient state (rebuilt after a world load) ----------------------------------

struct Ambient {
    int time = 0; ///< Java's ambientSoundTime: a sound plays when rand(1000) < time++, then time = -interval
    bool roll(int interval) {
        if (randInt(1000) < time++) {
            time = -interval;
            return true;
        }
        return false;
    }
};

struct WatchedPlayer {
    Vec3 lastPos;
    Vec3 lastSpeed;
};

struct StonelingState {
    bool  seen      = false;
    int   loot      = -1;
    int   tempt     = -1;
    int   wary      = -1;
    bool  fleeing   = false;
    int   fleeTicks = 0;
    int   still     = 0;
    Vec3  lastPos{};
    std::unordered_map<int64_t, WatchedPlayer> watched;
    Ambient ambient;
};

struct FoxState {
    int     sleepCooldown = 0;
    bool    tatering      = false;
    bool    loggedFurnace = false;
    Ambient ambient;
};

struct WraithState {
    bool    aggro = false;
    Ambient ambient;
};

struct BeadState {
    Vec3 start{};
    int  targetX = 0;
    int  targetZ = 0;
    int  life    = 0;
    int  dim     = 0;
};

struct PendingLeap {
    int64_t wraith;
    int64_t victim;
};
struct PendingHeart {
    int64_t  player;
    BlockPos pos;
    int      dim;
    int      variant;
    std::string block;
};
struct PendingForgotten {
    int  dim;
    Vec3 pos;
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isCreativeLike(Actor& a) { return a.isCreative() || a.isSpectator(); }

std::string biomeName(BlockSource& region, Vec3 const& pos) {
    auto const& biome = region.getBiome(game::blockPosOf(pos));
    return biome.mHash->getString();
}

// Java biome ids in the config -> Bedrock biome names.
std::string normalizeBiome(std::string id) {
    if (id.starts_with("minecraft:")) id.erase(0, 10);
    if (id == "nether_wastes") return "hell";
    if (id == "soul_sand_valley") return "soulsand_valley";
    if (id == "the_void") return "the_void";
    return id;
}

bool tagMatches(std::string const& tag, int dim, std::string const& biome) {
    auto t = tag;
    if (t.starts_with("c:") || t.starts_with("minecraft:")) t.erase(0, t.find(':') + 1);
    if (t.starts_with("is_")) t.erase(0, 3);
    if (t == "nether") return dim == 1;
    if (t == "end" || t == "the_end") return dim == 2;
    if (t == "overworld") return dim == 0;
    if (t == "void") return biome == "the_void";
    return biome.find(t) != std::string::npos;
}

bool biomeAllowed(SpawnConfig const& sc, int dim, std::string const& biome) {
    bool inList = false;
    for (auto const& b : sc.biomes.biomes)
        if (normalizeBiome(b) == biome) inList = true;
    bool okList = sc.biomes.isBlacklist ? !inList : inList;
    bool inTags = false;
    for (auto const& t : sc.tags.biomeTags)
        if (tagMatches(t, dim, biome)) inTags = true;
    bool okTags = sc.tags.isBlacklist ? !inTags : (sc.tags.biomeTags.empty() || inTags);
    if (!sc.biomes.isBlacklist && sc.biomes.biomes.empty()) okList = true;
    return okList && okTags;
}

bool weightAllows(SpawnConfig const& sc, int packWeight) {
    if (sc.spawnWeight <= 0) return false;
    if (sc.spawnWeight >= packWeight) return true;
    return rand01() < static_cast<double>(sc.spawnWeight) / packWeight;
}

std::string dimensionName(int dim) {
    switch (dim) {
    case 0: return "minecraft:overworld";
    case 1: return "minecraft:the_nether";
    case 2: return "minecraft:the_end";
    default: return "";
    }
}

bool glowShroomExists() {
    static int known = -1;
    if (known < 0) known = game::isEmpty(game::makeItem("lepton:glow_shroom")) ? 0 : 1;
    return known == 1;
}

Player* playerOf(int64_t id) {
    auto* a = ent::find(id);
    return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
}

void shrinkHeld(Player& p) {
    if (p.isCreative()) return;
    ItemStack held = p.getSelectedItem();
    if (held.mCount <= 1) held = ItemStack{};
    else held.mCount = held.mCount - 1;
    p.setSelectedItem(held);
    game::inv::sync(p);
}

void hearts(Actor& a, int n) {
    auto at = a.getPosition();
    for (int i = 0; i < n; ++i) {
        Vec3 p{at.x + static_cast<float>(rand01() * 0.2 - 0.1), at.y + 0.9f, at.z + static_cast<float>(rand01() * 0.2 - 0.1)};
        particleAt(a, "minecraft:heart_particle", p);
    }
}

void blockCrumbs(Actor& a, Block const& block, Vec3 const& pos, int n) {
    auto level = ll::service::getLevel();
    if (!level) return;
    for (int i = 0; i < std::max(1, n / 8); ++i)
        level->broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock, pos, static_cast<int>(block.mNetworkId), nullptr);
}

// The damaging entity of a source: the attacker for melee, the shooter (owner) for projectiles.
Actor* attackerOf(ActorDamageSource const& src) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    if (auto id = src.getEntityUniqueID(); id.rawID != 0 && id.rawID != -1)
        if (auto* a = level->fetchEntity(id, false)) return a;
    return nullptr;
}
Actor* directOf(ActorDamageSource const& src) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    if (auto id = src.getDamagingEntityUniqueID(); id.rawID != 0 && id.rawID != -1)
        if (auto* a = level->fetchEntity(id, false)) return a;
    return nullptr;
}

// ================================================================================================

class MobsdeepModule {
public:
    static MobsdeepModule& get() {
        static MobsdeepModule m;
        return m;
    }

    void install();
    void uninstall();

    std::unordered_map<int64_t, StonelingState> stonelings;
    std::unordered_map<int64_t, FoxState>       foxes;
    std::unordered_map<int64_t, WraithState>    wraiths;
    std::unordered_map<int64_t, Voice>          voices;
    std::unordered_map<int64_t, int>            taters;
    std::unordered_map<int64_t, BeadState>      beads;
    std::unordered_set<int64_t>                 hatWearers;
    std::deque<PendingLeap>                     leaps;
    std::deque<PendingHeart>                    heartUses;
    std::deque<PendingForgotten>                forgottenSpawns;
    bool                                        dirty = false;
    uint64_t                                    tick  = 0;

    void load();
    void save();
    void clear();

    std::optional<InteractionResult> preInteract(Player& p, Actor& target);
    void                             postInteract(Player& p, Actor& target, InteractionResult const& r);

private:
    void onTick();
    void onHurt(ll::event::ActorHurtEvent& ev);
    void onDie(ll::event::MobDieEvent& ev);
    void onInteractBlock(ll::event::PlayerInteractBlockEvent& ev);
    void onUseItem(ll::event::PlayerUseItemEvent& ev);
    void onSpawning(ll::event::SpawningMobEvent& ev);

    void tickStoneling(Actor& a, std::vector<Player*> const& players);
    void tickForgotten(Actor& a);
    void tickFoxhound(Actor& a, std::unordered_set<uint64_t>& boosted);
    void tickWraith(Actor& a);
    void tickSoulBead(Actor& a);
    void tickHats(std::vector<Player*> const& players);
    void runPending();

    void startle(Actor& a, StonelingState& st);
    void poof(Actor& a);

    std::vector<ll::event::ListenerPtr> mListeners;
};

MobsdeepModule& M() { return MobsdeepModule::get(); }

// ---- persistence ------------------------------------------------------------------------------

void MobsdeepModule::load() {
    clear();
    auto doc = game::db::load(kDbKey);
    if (!doc) return;
    for (auto& [k, v] : doc->value("voices", nlohmann::json::object()).items())
        voices[std::stoll(k)] = Voice{v.value("idle", ""), v.value("hurt", ""), v.value("death", "")};
    for (auto& [k, v] : doc->value("taters", nlohmann::json::object()).items()) taters[std::stoll(k)] = v.get<int>();
    for (auto& [k, v] : doc->value("beads", nlohmann::json::object()).items()) {
        BeadState b;
        b.start   = Vec3{v.value("sx", 0.0f), v.value("sy", 0.0f), v.value("sz", 0.0f)};
        b.targetX = v.value("tx", 0);
        b.targetZ = v.value("tz", 0);
        b.life    = v.value("life", 0);
        b.dim     = v.value("dim", 0);
        beads[std::stoll(k)] = b;
    }
}

void MobsdeepModule::save() {
    if (!dirty || !game::db::available()) return;
    nlohmann::json doc;
    auto&          jv = doc["voices"] = nlohmann::json::object();
    for (auto& [id, v] : voices) jv[std::to_string(id)] = {{"idle", v.idle}, {"hurt", v.hurt}, {"death", v.death}};
    auto& jt = doc["taters"] = nlohmann::json::object();
    for (auto& [id, t] : taters) jt[std::to_string(id)] = t;
    auto& jb = doc["beads"] = nlohmann::json::object();
    for (auto& [id, b] : beads)
        jb[std::to_string(id)] = {{"sx", b.start.x}, {"sy", b.start.y}, {"sz", b.start.z}, {"tx", b.targetX},
                                  {"tz", b.targetZ}, {"life", b.life}, {"dim", b.dim}};
    game::db::save(kDbKey, doc);
    dirty = false;
}

void MobsdeepModule::clear() {
    stonelings.clear();
    foxes.clear();
    wraiths.clear();
    voices.clear();
    taters.clear();
    beads.clear();
    hatWearers.clear();
    leaps.clear();
    heartUses.clear();
    forgottenSpawns.clear();
    dirty = false;
}

// ---- interactions -----------------------------------------------------------------------------

InteractionResult result(bool success) {
    InteractionResult r{};
    r.mSuccess = success;
    r.mSwing   = success;
    return r;
}

std::optional<InteractionResult> MobsdeepModule::preInteract(Player& p, Actor& target) {
    auto const& cfg  = config();
    auto        type = ent::typeOf(target);
    if (!target.isAlive()) return std::nullopt;
    ItemStack const& held   = p.getSelectedItem();
    std::string      heldId = game::itemId(held);

    if (type == kStoneling && cfg.enableStonelings) {
        if (heldId == "minecraft:name_tag") return std::nullopt; // vanilla name tags
        auto pos = target.getPosition();
        if (boolProp(target, "lepton:player_made")) {
            if (!p.isSneaking() && !game::isEmpty(held)) {
                int v = variantOf(heldId);
                if (v < 0) return result(false);
                int cur = intProp(target, "lepton:variant");
                hearts(target, 1);
                if (v != cur) {
                    if (auto const* b = game::blockWith(heldId)) blockCrumbs(target, *b, Vec3{pos.x, pos.y + 0.45f, pos.z}, 16);
                    playSoundAt(target, "lepton.stoneling.eat", pos);
                    ent::triggerEvent(target, "lepton:variant_" + std::to_string(v));
                }
                playSoundAt(target, "lepton.stoneling.purr", pos, 1.0f, 1.0f + static_cast<float>(rand01()));
                target.heal(1);
                shrinkHeld(p);
                log().info("mobsdeep: stoneling fed {} (variant {})", heldId, kVariants[v].name);
                return result(true);
            }
            ItemStack carried = target.getCarriedItem();
            if (!game::isEmpty(carried) || !game::isEmpty(held)) {
                ItemStack give = held;
                p.setSelectedItem(carried);
                target.setCarriedItem(give);
                if (target.hasCategory(ActorCategory::Mob)) static_cast<Mob&>(target).refreshInventory();
                game::inv::sync(p);
                playSoundAt(target, game::isEmpty(held) ? "lepton.stoneling.give" : "lepton.stoneling.take", pos);
                log().info("mobsdeep: stoneling swapped items ({} <-> {})", game::itemId(carried), heldId);
                return result(true);
            }
            return std::nullopt;
        }
        bool food = heldId == (glowShroomExists() ? "lepton:glow_shroom" : "minecraft:diamond");
        if (cfg.tamableStonelings && food) {
            target.heal(8);
            ent::triggerEvent(target, "lepton:make_player_made");
            playSoundAt(target, "lepton.stoneling.purr", pos, 1.0f, 1.0f + static_cast<float>(rand01()));
            shrinkHeld(p);
            hearts(target, 4);
            log().info("mobsdeep: stoneling tamed");
            return result(true);
        }
        return std::nullopt;
    }

    if (type == kFoxhound && cfg.enableFoxhound) {
        auto pos = target.getPosition();
        if (!target.isTame()) {
            if (heldId == "minecraft:coal") {
                auto level    = ll::service::getLevel();
                bool peaceful = level && level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful;
                bool immune   = peaceful || p.canUseAbility(AbilitiesIndex::Invulnerable) || p.getEffect(12u) != nullptr;
                if (!immune) return std::nullopt;
                double chance = gForcedTameChance.value_or(cfg.tameChance);
                if (rand01() < chance) {
                    ent::triggerEvent(target, "lepton:on_tame");
                    target.setOwner(p.getOrCreateUniqueID());
                    target.setSitting(true);
                    target.setTarget(nullptr);
                    if (level) level->broadcastActorEvent(target, ::ActorEvent::TamingSucceeded, 0, std::nullopt);
                    log().info("mobsdeep: foxhound tamed by {}", p.getRealName());
                } else if (level) {
                    level->broadcastActorEvent(target, ::ActorEvent::TamingFailed, 0, std::nullopt);
                }
                shrinkHeld(p);
                return result(true);
            }
            if (heldId == "minecraft:bone") return result(false);
            return std::nullopt;
        }
        auto id = ent::idOf(target);
        if (heldId == "lepton:tiny_potato" && taters[id] <= 0) {
            taters[id] = 600;
            dirty      = true;
            playSoundAt(target, "lepton.foxhound.eat", pos);
            shrinkHeld(p);
            return result(true);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void MobsdeepModule::postInteract(Player&, Actor& target, InteractionResult const& r) {
    if (!r.mSuccess || ent::typeOf(target) != kFoxhound) return;
    if (boolProp(target, "lepton:resting")) {
        ent::triggerEvent(target, "lepton:wake");
        foxes[ent::idOf(target)].sleepCooldown = 100;
    }
}

// ---- per tick ---------------------------------------------------------------------------------

void MobsdeepModule::startle(Actor& a, StonelingState& st) {
    if (boolProp(a, "lepton:startled")) return;
    ent::triggerEvent(a, "lepton:startle");
    st.tempt = 0;
    st.wary  = 0;
}

void MobsdeepModule::poof(Actor& a) {
    auto pos = a.getPosition();
    particleAt(a, "lepton:stoneling_poof", pos);
    for (int guard = 0; guard < 8; ++guard) {
        auto* passenger = a.getFirstPassenger();
        if (!passenger || passenger->isPlayer()) break;
        ent::despawn(*passenger);
    }
    stonelings.erase(ent::idOf(a));
    ent::despawn(a);
    log().info("mobsdeep: stoneling poofed");
}

void MobsdeepModule::tickStoneling(Actor& a, std::vector<Player*> const& players) {
    auto const& cfg = config();
    auto        id  = ent::idOf(a);
    auto&       st  = stonelings[id];
    if (!cfg.enableStonelings) return;
    bool playerMade = boolProp(a, "lepton:player_made");
    bool startled   = boolProp(a, "lepton:startled");
    auto pos        = a.getPosition();

    if (!st.seen) {
        st.seen = true;
        if (!boolProp(a, "lepton:lichen") && !playerMade && a.getDimensionId() == 0) {
            auto biome = biomeName(a.getDimensionBlockSource(), pos);
            if (biome.find("glimmering_weald") != std::string::npos && randInt(5) < 3 && intProp(a, "lepton:variant") >= 0
                && a.getHealth() == a.getMaxHealth() && molang(a, "query.life_time") < 1.0f)
                ent::triggerEvent(a, "lepton:lichen");
        }
    }
    int loot = cfg.enableDiamondHeart ? 1 : 0;
    if (loot != st.loot) {
        ent::triggerEvent(a, loot ? "lepton:loot_on" : "lepton:loot_off");
        st.loot = loot;
    }
    int tempt = 0;
    if (!startled && (cfg.tamableStonelings || cfg.enableDiamondHeart)) tempt = glowShroomExists() ? 2 : 1;
    if (tempt != st.tempt) {
        ent::triggerEvent(a, tempt == 0 ? "lepton:tempt_none" : tempt == 1 ? "lepton:tempt_diamond" : "lepton:tempt_glow_shroom");
        st.tempt = tempt;
    }

    if (!playerMade && !startled) {
        // ActWaryGoal: range 6, startled by any non-sneaking non-creative player
        bool near = false, scare = false;
        for (auto* p : players) {
            if (p->getDimensionId() != a.getDimensionId() || p->isCreative()) continue;
            auto pp = p->getPosition();
            if (pp.distanceToSqr(pos) >= 36.0f) continue;
            near = true;
            if (!p->isSneaking()) {
                scare = true;
                break;
            }
            if (cfg.cautiousStonelings) {
                auto pid = ent::idOf(*p);
                auto it  = st.watched.find(pid);
                if (it == st.watched.end()) it = st.watched.emplace(pid, WatchedPlayer{pp, Vec3{0, 0, 0}}).first;
                auto& w  = it->second;
                Vec3  d  = pp - w.lastPos;
                Vec3  dd = d - w.lastSpeed;
                w.lastSpeed = d;
                w.lastPos   = pp;
                if (dd.lengthSqr() >= 0.01f) {
                    scare = true;
                    break;
                }
            }
        }
        if (scare) {
            startle(a, st);
            startled = true;
            log().info("mobsdeep: stoneling startled");
        } else if ((near ? 1 : 0) != st.wary) {
            ent::triggerEvent(a, near ? "lepton:wary_on" : "lepton:wary_off");
            st.wary = near ? 1 : 0;
        }
    }

    if (!playerMade && startled) {
        // RunAndPoofGoal: flee from a visible player within 4 (3 vertically), poof when stopped
        if (!st.fleeing) {
            for (auto* p : players) {
                if (p->getDimensionId() != a.getDimensionId() || isCreativeLike(*p)) continue;
                auto pp = p->getPosition();
                if (std::abs(pp.x - pos.x) > 4.3f || std::abs(pp.z - pos.z) > 4.3f || std::abs(pp.y - pos.y) > 3.9f) continue;
                if (!a.canSee(p->getHeadPos(), ShapeType::Collision) && !a.canSee(pp, ShapeType::Collision)) continue;
                st.fleeing   = true;
                st.fleeTicks = 0;
                st.still     = 0;
                st.lastPos   = pos;
                playSoundAt(a, "lepton.stoneling.meep", pos);
                break;
            }
        } else {
            ++st.fleeTicks;
            float moved = (pos - st.lastPos).lengthSqr();
            st.lastPos  = pos;
            st.still    = moved < 0.0004f ? st.still + 1 : 0;
            if ((st.fleeTicks > 10 && st.still >= 10) || st.fleeTicks >= 100) {
                poof(a);
                return;
            }
        }
    }

    // Vsauce, Michael here
    auto name = lower(a.getNameTag());
    if ((name == "michael stevens" || name == "vsauce") && st.ambient.roll(1200)) playSoundAt(a, "lepton.stoneling.michael", pos);
}

void MobsdeepModule::tickForgotten(Actor& a) {
    if (!config().enableForgotten || !a.isAlive()) return;
    auto& mob = static_cast<Mob&>(a);
    if (!boolProp(a, "lepton:init")) {
        // Quark's prepareEquipment: an enchanted bow in hand, an enchanted iron sword sheathed.
        auto level = ll::service::getLevel();
        int  diff  = level ? static_cast<int>(level->getDifficulty()) : 2;
        auto enchant = [&](ItemStack& item) {
            int cost = 5 + static_cast<int>(randInt(18) * std::clamp(diff / 3.0, 0.0, 1.0));
            EnchantUtils::randomlyEnchant(a.getRandom(), item, cost, 0, false);
        };
        ItemStack bow   = game::makeItem("minecraft:bow");
        ItemStack sword = game::makeItem("minecraft:iron_sword");
        enchant(bow);
        enchant(sword);
        a.setCarriedItem(bow);
        a.setOffhandSlot(sword);
        mob.refreshInventory();
        ent::triggerEvent(a, "lepton:init");
    }
    bool useBow   = shouldUseBow(a);
    bool usingBow = game::itemId(a.getCarriedItem()) == "minecraft:bow";
    if (useBow != usingBow) {
        ItemStack hand = a.getCarriedItem();
        ItemStack off  = a.getOffhandSlot();
        a.setCarriedItem(off);
        a.setOffhandSlot(hand);
        mob.refreshInventory();
        ent::triggerEvent(a, useBow ? "lepton:to_ranged" : "lepton:to_melee");
        log().info("mobsdeep: forgotten switched to {}", useBow ? "bow" : "sword");
    }
}

bool isFurnace(std::string const& id) {
    return id == "minecraft:furnace" || id == "minecraft:lit_furnace" || id == "minecraft:blast_furnace"
        || id == "minecraft:lit_blast_furnace" || id == "minecraft:smoker" || id == "minecraft:lit_smoker";
}

void MobsdeepModule::tickFoxhound(Actor& a, std::unordered_set<uint64_t>& boosted) {
    auto const& cfg = config();
    if (!cfg.enableFoxhound || !a.isAlive()) return;
    auto  id  = ent::idOf(a);
    auto& st  = foxes[id];
    auto  pos = a.getPosition();
    bool  tame = a.isTame();
    bool  resting = boolProp(a, "lepton:resting");
    if (st.sleepCooldown > 0) --st.sleepCooldown;
    if (resting && (st.sleepCooldown > 0 || !tame || a.isSitting())) {
        ent::triggerEvent(a, "lepton:wake");
        resting = false;
    }

    // tiny potato
    if (auto it = taters.find(id); it != taters.end() && it->second > 0) {
        if (--it->second == 0) {
            taters.erase(it);
            ent::triggerEvent(a, "lepton:tater_off");
            st.tatering = false;
            auto potato = game::makeItem("lepton:tiny_potato");
            if (!game::isEmpty(potato)) a.spawnAtLocation(potato, 0.0f);
            playSoundAt(a, "random.pop", pos);
        } else {
            if (!st.tatering) {
                ent::triggerEvent(a, "lepton:tater_on");
                st.tatering = true;
            }
            if (rand01() < 0.1) playSoundAt(a, "lepton.foxhound.crackle", pos);
        }
        dirty = true;
    } else if (st.tatering) {
        ent::triggerEvent(a, "lepton:tater_off");
        st.tatering = false;
    }

    // furnaces cook a third faster under a tame foxhound
    if (tame && cfg.foxhoundsSpeedUpFurnaces) {
        auto  feet  = game::blockPosOf(pos);
        BlockPos below{feet.x, feet.y - 1, feet.z};
        auto& region = a.getDimensionBlockSource();
        if (isFurnace(game::blockIdAt(region, below))) {
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(below.x)) << 32) ^ (static_cast<uint64_t>(below.y & 0xfff) << 20)
                         ^ static_cast<uint32_t>(below.z) ^ (static_cast<uint64_t>(a.getDimensionId()) << 62);
            if (auto* be = region.getBlockEntity(below); be && !boosted.contains(key)) {
                auto& furnace = static_cast<FurnaceBlockActor&>(*be);
                int   cook    = furnace.mCookingProgress;
                int   total   = furnace.mBurnInterval;
                if (cook > 0 && cook % 3 == 0) {
                    boosted.insert(key);
                    furnace.mCookingProgress = cook == 3 ? 5 : std::min(total - 1, cook + 1);
                    if (!st.loggedFurnace) {
                        st.loggedFurnace = true;
                        log().info("mobsdeep: foxhound speeds up a furnace");
                    }
                }
            }
        }
    }

    // ambient sounds (Mob ambient interval 80)
    if (!resting && st.ambient.roll(80)) {
        std::string s;
        if (a.mTargetId->rawID != 0 && a.mTargetId->rawID != -1) s = "mob.wolf.growl";
        else if (randInt(3) == 0) s = tame && a.getHealth() < 10 ? "mob.wolf.whine" : "mob.wolf.panting";
        else s = "mob.wolf.bark";
        playSoundAt(a, s, pos, 1.0f, 1.0f + static_cast<float>((rand01() - rand01()) * 0.2));
    }
}

void MobsdeepModule::tickWraith(Actor& a) {
    if (!config().enableWraith || !a.isAlive()) return;
    auto id = ent::idOf(a);
    if (!voices.contains(id)) {
        auto all = wraithVoices();
        voices[id] = all.empty() ? Voice{} : all[randInt(static_cast<int>(all.size()))];
        dirty      = true;
    }
    auto& st = wraiths[id];
    if (st.ambient.roll(80)) playSoundAt(a, voices[id].idle, a.getPosition(), 1.0f, 0.75f + static_cast<float>(rand01() * 0.1));
}

void MobsdeepModule::tickSoulBead(Actor& a) {
    auto id = ent::idOf(a);
    auto it = beads.find(id);
    if (it == beads.end()) {
        ent::despawn(a); // orphan (state lost)
        return;
    }
    auto& b = it->second;
    if (b.life > 6000) {
        beads.erase(it);
        dirty = true;
        ent::despawn(a);
        return;
    }
    Vec3 p = soulBeadPoint(b.start, b.targetX, b.targetZ, b.life);
    a.teleport(p, a.getDimensionId());
    if (rand01() < 0.1) playSoundAt(a, "lepton.soul_bead.idle", p, 0.2f, 1.0f);
    ++b.life;
    if (b.life % 20 == 0) dirty = true;
}

void MobsdeepModule::tickHats(std::vector<Player*> const& players) {
    static mce::UUID const kLuckId = mce::UUID(std::string("7f4a3a52-5f5d-4c35-9a1d-2b9f7b4a2121"));
    for (auto* p : players) {
        bool wearing = game::itemId(p->getArmor(SharedTypes::Legacy::ArmorSlot::Head)) == kForgottenHat;
        auto attr    = p->getMutableAttribute(SharedAttributes::LUCK());
        auto* inst   = attr.mInstance->mPtr;
        if (!inst) continue;
        auto pid = ent::idOf(*p);
        bool has = hatWearers.contains(pid);
        if (wearing == has) continue;
        if (wearing) {
            inst->addModifier(AttributeModifier(kLuckId, "lepton:forgotten_hat_luck", 1.0f, AttributeModifierOperation::OperationAddition,
                                                AttributeOperands::OperandCurrent, false),
                              attr.mContext);
            hatWearers.insert(pid);
        } else {
            inst->removeModifier(kLuckId, attr.mContext);
            hatWearers.erase(pid);
        }
    }
}

void MobsdeepModule::runPending() {
    while (!leaps.empty()) {
        auto l = leaps.front();
        leaps.pop_front();
        auto* w = ent::find(l.wraith);
        auto* v = ent::find(l.victim);
        if (!w || !v) continue;
        auto  wp = w->getPosition(), vp = v->getPosition();
        Vec3  d{wp.x - vp.x, 0.0f, wp.z - vp.z};
        float len = std::sqrt(d.x * d.x + d.z * d.z);
        if (len > 1e-6f) d = Vec3{d.x / len, 0.0f, d.z / len};
        d.y       = 0.5f;
        float l2  = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        Vec3  vel{d.x / l2 * 0.85f, d.y / l2 * 0.85f, d.z / l2 * 0.85f};
        w->lerpMotion(vel);
    }
    while (!heartUses.empty()) {
        auto h = heartUses.front();
        heartUses.pop_front();
        auto* region = game::regionOf(h.dim);
        auto* p      = playerOf(h.player);
        if (!region || !p || game::blockIdAt(*region, h.pos) != h.block) continue;
        auto const& block = region->getBlock(h.pos);
        Vec3        center{h.pos.x + 0.5f, static_cast<float>(h.pos.y), h.pos.z + 0.5f};
        auto        level = ll::service::getLevel();
        if (level)
            level->broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock, center,
                                       static_cast<int>(block.mNetworkId), nullptr);
        game::clearBlock(*region, h.pos);
        auto* st = ent::spawn(*region, std::string(kStoneling) + "<lepton:from_heart>", center);
        if (!st) continue;
        ent::triggerEvent(*st, "lepton:variant_" + std::to_string(h.variant));
        auto rot = p->getRotation();
        st->teleport(center, static_cast<int>(h.dim), Vec2{0.0f, rot.y + 180.0f});
        shrinkHeld(*p);
        log().info("mobsdeep: stoneling made ({})", kVariants[h.variant].name);
    }
    while (!forgottenSpawns.empty()) {
        auto f = forgottenSpawns.front();
        forgottenSpawns.pop_front();
        if (ent::spawn(f.dim, kForgotten, f.pos)) log().info("mobsdeep: skeleton replaced by forgotten");
    }
}

void MobsdeepModule::onTick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    ++tick;
    runPending();
    std::vector<Player*> players;
    std::vector<Actor*>  mine;
    for (auto* a : level->getRuntimeActorList()) {
        if (!a) continue;
        if (a->isPlayer()) {
            if (!a->isClientSide()) players.push_back(static_cast<Player*>(a));
            continue;
        }
        auto const& t = a->getTypeName();
        if (t.starts_with("lepton:") && (t == kStoneling || t == kForgotten || t == kFoxhound || t == kWraith || t == kSoulBead))
            mine.push_back(a);
    }
    std::unordered_set<uint64_t> boosted;
    for (auto* a : mine) {
        auto const& t = a->getTypeName();
        if (t == kStoneling) tickStoneling(*a, players);
        else if (t == kForgotten) tickForgotten(*a);
        else if (t == kFoxhound) tickFoxhound(*a, boosted);
        else if (t == kWraith) tickWraith(*a);
        else tickSoulBead(*a);
    }
    if (tick % 10 == 0 && config().enableForgotten) tickHats(players);
}

// ---- events -----------------------------------------------------------------------------------

void MobsdeepModule::onHurt(ll::event::ActorHurtEvent& ev) {
    auto&       victim = ev.self();
    auto const& src    = ev.source();
    auto const& cfg    = config();
    auto        vtype  = ent::typeOf(victim);
    Actor*      attacker = attackerOf(src);
    Actor*      direct   = directOf(src);
    auto        cause    = src.mCause;

    if (vtype == kStoneling && cfg.enableStonelings) {
        if (cause == Cause::Projectile) {
            int pierce = 0;
            if (direct) {
                if (auto proj = direct->getEntityContext().tryGetComponent<ProjectileComponent>()) pierce = proj->mPiercingEnchantLevel;
            }
            if (pierce <= 0) {
                ev.cancel();
                return;
            }
        }
        playSoundAt(victim, "lepton.stoneling.cry", victim.getPosition());
        if (attacker && attacker->isPlayer() && !boolProp(victim, "lepton:player_made")) {
            auto& st = stonelings[ent::idOf(victim)];
            if (!boolProp(victim, "lepton:startled")) {
                startle(victim, st);
                log().info("mobsdeep: stoneling startled");
            }
        }
        return;
    }
    if (vtype == kFoxhound && cfg.enableFoxhound) {
        if (boolProp(victim, "lepton:resting")) {
            ent::triggerEvent(victim, "lepton:wake");
            foxes[ent::idOf(victim)].sleepCooldown = 100;
        }
        playSoundAt(victim, "mob.wolf.hurt", victim.getPosition());
    }
    if (vtype == kWraith && cfg.enableWraith) {
        auto id = ent::idOf(victim);
        if (auto it = voices.find(id); it != voices.end())
            playSoundAt(victim, it->second.hurt, victim.getPosition(), 1.0f, 0.75f + static_cast<float>(rand01() * 0.1));
        // only a player's own hit (not a projectile) makes a wraith hostile
        if (attacker && attacker->isPlayer() && cause != Cause::Projectile && !src.isChildEntitySource()) {
            auto& st = wraiths[id];
            if (!st.aggro) {
                st.aggro = true;
                ent::triggerEvent(victim, "lepton:aggro");
                log().info("mobsdeep: wraith aggroed");
            }
        }
    }

    if (!attacker) return;
    auto atype = ent::typeOf(*attacker);
    if (atype == kForgotten && cfg.enableForgotten && cause == Cause::Projectile) {
        MobEffectInstance blind(15u, EffectDuration{100});
        victim.addEffect(blind);
    } else if (atype == kFoxhound && cfg.enableFoxhound && cause == Cause::EntityAttack) {
        auto level = ll::service::getLevel();
        if (victim.isPlayer() && level && level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful) {
            ev.cancel();
            return;
        }
        if (victim.isFireImmune()) return;
        if (victim.getEffect(12u) != nullptr) { // fire resistance: Quark's bite does nothing at all
            ev.cancel();
            return;
        }
        if (victim.isBlocking()) return; // the shield takes it, no fire
        victim.setOnFire(5, true);
    } else if (atype == kWraith && cfg.enableWraith && cause == Cause::EntityAttack) {
        MobEffectInstance slow(2u, EffectDuration{60});
        slow.mAmplifier = 1;
        victim.addEffect(slow);
        leaps.push_back({ent::idOf(*attacker), ent::idOf(victim)});
    }
}

void MobsdeepModule::onDie(ll::event::MobDieEvent& ev) {
    auto& mob  = ev.self();
    auto  type = ent::typeOf(mob);
    auto  id   = ent::idOf(mob);
    auto  pos  = mob.getPosition();
    auto const& cfg = config();
    if (type == kStoneling) {
        playSoundAt(mob, "lepton.stoneling.die", pos);
        ItemStack carried = mob.getCarriedItem();
        if (!game::isEmpty(carried)) {
            mob.setCarriedItem(ItemStack{});
            mob.spawnAtLocation(carried, 0.0f);
        }
        Actor* killer = attackerOf(ev.source());
        if (cfg.wealdPathfinderMaps && killer && killer->isPlayer() && !boolProp(mob, "lepton:player_made")
            && biomeName(mob.getDimensionBlockSource(), pos).find("glimmering_weald") != std::string::npos) {
            auto  quill  = game::makeItem("lepton:pathfinders_quill");
            auto& player = static_cast<Player&>(*killer);
            int   looting = EnchantUtils::getEnchantLevel(Enchant::Type::Looting, player.getSelectedItem());
            double chance = looting > 0 ? 0.1 + 0.02 * (looting - 1) : 0.08;
            if (!game::isEmpty(quill) && rand01() < chance) mob.spawnAtLocation(quill, 0.0f);
        }
        stonelings.erase(id);
    } else if (type == kFoxhound) {
        playSoundAt(mob, "mob.wolf.death", pos);
        foxes.erase(id);
        if (taters.erase(id)) dirty = true;
    } else if (type == kWraith) {
        if (auto it = voices.find(id); it != voices.end()) {
            playSoundAt(mob, it->second.death, pos, 1.0f, 0.75f + static_cast<float>(rand01() * 0.1));
            voices.erase(it);
            dirty = true;
        }
        wraiths.erase(id);
    }
}

void MobsdeepModule::onInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
    auto& p = ev.self();
    if (p.isClientSide()) return;
    auto const& cfg = config();
    if (game::itemId(ev.item()) != kDiamondHeart || !cfg.enableStonelings || !cfg.enableDiamondHeart) return;
    auto& region = p.getDimensionBlockSource();
    auto  pos    = ev.blockPos();
    auto  id     = game::blockIdAt(region, pos);
    int   v      = variantOf(id);
    if (v < 0 || !p.canUseAbility(AbilitiesIndex::Build)) return;
    ev.cancel();
    // the cancelled interaction rolls back world edits: do it next tick
    heartUses.push_back({ent::idOf(p), pos, static_cast<int>(p.getDimensionId()), v, id});
}

void MobsdeepModule::onUseItem(ll::event::PlayerUseItemEvent& ev) {
    auto& p = ev.self();
    if (p.isClientSide() || game::itemId(ev.item()) != kSoulBeadItem || !config().enableWraith) return;
    auto at     = p.getPosition();
    auto target = findFortress(static_cast<int>(p.getDimensionId()), at);
    ev.cancel();
    if (!target) return;
    auto  rot = p.getRotation();
    float yaw = rot.y * 3.14159265f / 180.0f, pitch = rot.x * 3.14159265f / 180.0f;
    Vec3  look{-std::sin(yaw) * std::cos(pitch), 0.0f, std::cos(yaw) * std::cos(pitch)};
    auto  feet = p.getFeetPos();
    Vec3  start{feet.x + look.x * 2.0f, feet.y + 0.25f, feet.z + look.z * 2.0f};
    if (spawnSoulBead(p, start, target->first, target->second) != 0) {
        shrinkHeld(p);
        playSoundAt(p, "lepton.soul_bead.spawn", feet);
    }
}

void MobsdeepModule::onSpawning(ll::event::SpawningMobEvent& ev) {
    auto const& cfg  = config();
    auto const& type = ev.identifier().mCanonicalName->getString();
    auto const& pos  = ev.pos();
    auto&       region = ev.blockSource();
    int         dim  = static_cast<int>(region.getDimensionId());

    if (type == kStoneling) {
        bool dimOk = false;
        for (auto const& d : cfg.dimensions.dimensions) dimOk |= d == dimensionName(dim);
        if (cfg.dimensions.isBlacklist) dimOk = !dimOk;
        if (!cfg.enableStonelings || pos.y > cfg.maxYLevel || !dimOk
            || !biomeAllowed(cfg.stonelingsSpawnConfig, dim, biomeName(region, pos)) || !weightAllows(cfg.stonelingsSpawnConfig, 80))
            ev.cancel();
        return;
    }
    if (type == kFoxhound) {
        auto biome = biomeName(region, pos);
        bool lesser = biome == "soulsand_valley";
        auto const& sc = lesser ? cfg.lesserSpawnConfig : cfg.foxhoundSpawnConfig;
        if (!cfg.enableFoxhound || !biomeAllowed(sc, dim, biome) || !weightAllows(sc, lesser ? 2 : 30)) ev.cancel();
        return;
    }
    if (type == kWraith) {
        if (!cfg.enableWraith || !biomeAllowed(cfg.wraithSpawnConfig, dim, biomeName(region, pos)) || !weightAllows(cfg.wraithSpawnConfig, 5))
            ev.cancel();
        return;
    }
    if (type == "minecraft:skeleton" && cfg.enableForgotten && !ev.fromSpawner() && ev.naturalSpawn()
        && testing::replacesSkeleton(rand01(), pos.y, ev.fromSpawner())) {
        ev.cancel();
        forgottenSpawns.push_back({dim, pos});
    }
}

// ---- hook + lifecycle ---------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    InteractHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    InteractionResult,
    Actor&      actor,
    Vec3 const& location
) {
    if (isClientSide()) return origin(actor, location);
    if (auto r = M().preInteract(*this, actor)) return *r;
    auto r = origin(actor, location);
    M().postInteract(*this, actor, r);
    return r;
}

void MobsdeepModule::install() {
    auto& bus = ll::event::EventBus::getInstance();
    mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) { onTick(); }));
    mListeners.push_back(bus.emplaceListener<ll::event::ActorHurtEvent>([this](auto& e) { onHurt(e); }));
    mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>([this](auto& e) { onDie(e); }));
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([this](auto& e) { onInteractBlock(e); }));
    mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>([this](auto& e) { onUseItem(e); }));
    mListeners.push_back(bus.emplaceListener<ll::event::SpawningMobEvent>([this](auto& e) { onSpawning(e); }));
    InteractHook::hook();
    if (game::db::available()) load();
}

void MobsdeepModule::uninstall() {
    InteractHook::unhook();
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : mListeners) bus.removeListener(l);
    mListeners.clear();
    save();
}

struct Lifecycle {
    Lifecycle() {
        game::db::onWorldReady([] { M().load(); });
        game::db::onWorldSave([] { M().save(); });
        game::db::onWorldClosed([] { M().clear(); });
    }
} gLifecycle;

} // namespace

int64_t spawnSoulBead(Player& player, Vec3 const& start, int targetX, int targetZ) {
    auto* bead = ent::spawn(player.getDimensionBlockSource(), kSoulBead, start);
    if (!bead) return 0;
    auto      id = ent::idOf(*bead);
    BeadState b;
    b.start   = start;
    b.targetX = targetX;
    b.targetZ = targetZ;
    b.dim     = static_cast<int>(player.getDimensionId());
    M().beads[id] = b;
    M().dirty     = true;
    log().info("mobsdeep: soul bead -> {} {}", targetX, targetZ);
    return id;
}

namespace testing {
void forceTameChance(std::optional<double> chance) { gForcedTameChance = chance; }
std::optional<Voice> voiceOf(int64_t id) {
    auto& v = M().voices;
    if (auto it = v.find(id); it != v.end()) return it->second;
    return std::nullopt;
}
int soulBeadAge(int64_t id) {
    auto& b = M().beads;
    if (auto it = b.find(id); it != b.end()) return it->second.life;
    return -1;
}
void setSoulBeadAge(int64_t id, int age) {
    if (auto it = M().beads.find(id); it != M().beads.end()) it->second.life = age;
}
int taterTicks(int64_t id) {
    auto& t = M().taters;
    if (auto it = t.find(id); it != t.end()) return it->second;
    return 0;
}
bool replacesSkeleton(double roll, float y, bool fromSpawner) {
    auto const& cfg = config();
    return cfg.enableForgotten && !fromSpawner && y < cfg.maxHeightForSpawn && roll < cfg.forgottenSpawnRate;
}
} // namespace testing

} // namespace lp::feature::mobsdeep

LP_REGISTER_MODULE(lp::feature::mobsdeep::MobsdeepModule);
