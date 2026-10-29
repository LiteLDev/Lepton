// Resilience (Quark CrabsModule.RESILIENCE): +0.5 knockback resistance per level, the Potion of
// Stability (normal / long / strong, drinkable, splash, lingering) and its brewing mixes.
//
// Bedrock has no API for new mob effects, so the effect is native: an attribute modifier on the
// knockback resistance, a timer per entity saved in the world DB, a HUD line on mod clients and a
// tip text on vanilla clients.

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"
#include "mc/platform/UUID.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/AttributeModifier.h"
#include "mc/world/attribute/AttributeModifierOperation.h"
#include "mc/world/attribute/AttributeOperands.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemDescriptor.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/alchemy/PotionBrewing.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"

#include "feature/mobsoverworld/Common.h"
#include "feature/mobsoverworld/Config.h"
#include "feature/mobsoverworld/Features.h"
#include "feature/mobsoverworld/Messages.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Net.h"

namespace lp::feature::mobsoverworld::resilience {

namespace {

constexpr char const* kDbKey = "lp:mobsoverworld:effects";
const mce::UUID       kModifierId{0x6c6570746f6e7265ull, 0x73696c69656e6365ull}; // "leptonresilience"

struct Effect {
    int     amplifier = 0;
    int64_t endTick   = 0;
};
std::unordered_map<int64_t, Effect> gEffects;
std::unordered_map<int64_t, bool>   gApplied; ///< modifier currently on the (loaded) entity
bool                                gDirty = false;

struct Thrown {
    std::string kind; ///< item path prefix ("", "long_", "strong_") + splash/lingering
    bool        lingering = false;
    int         dim       = 0;
    Vec3        pos;
    int64_t     owner = 0;
    int64_t     seen  = 0;
};
std::unordered_map<int64_t, Thrown> gThrown;

struct Cloud {
    int                               dim;
    Vec3                              pos;
    float                             radius;
    int                               age = 0;
    int                               duration;  ///< potion effect duration (applied / 4)
    int                               amplifier;
    std::unordered_map<int64_t, int64_t> lastApplied;
};
std::vector<Cloud> gClouds;

bool gMixesAdded = false;

struct Strength {
    std::string_view prefix;
    int              duration;
    int              amplifier;
};
constexpr Strength kStrengths[] = {{"", 3600, 0}, {"long_", 9600, 0}, {"strong_", 1800, 1}};

std::optional<Strength> strengthOfPotion(std::string const& id, std::string_view kind) {
    for (auto const& s : kStrengths) {
        std::string want = "lepton:" + std::string(s.prefix) + "resilience_" + (kind.empty() ? "" : std::string(kind) + "_") + "potion";
        if (id == want) return s;
    }
    return std::nullopt;
}
std::string formatTicks(int ticks) {
    int s = std::max(0, ticks) / 20;
    return std::to_string(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string(s % 60);
}

void setModifier(Actor& actor, int amplifier, bool on) {
    Mob* mob = asMob(actor);
    if (!mob) return;
    auto attr = mob->getMutableAttribute(SharedAttributes::KNOCKBACK_RESISTANCE());
    AttributeInstance* inst = attr.mInstance->mPtr;
    if (!inst) return;
    inst->removeModifier(kModifierId, attr.mContext);
    if (on) {
        AttributeModifier mod(kModifierId, "lepton:resilience", 0.5f * static_cast<float>(amplifier + 1),
                              AttributeModifierOperation::OperationAddition, AttributeOperands::OperandCurrent, false);
        inst->addModifier(mod, attr.mContext);
    }
    gApplied[game::entities::idOf(actor)] = on;
}

/// A tip line above the hotbar (vanilla clients have no Resilience icon).
void tip(Player& player, std::string const& text) {
    TextPacket packet = TextPacket::createRawMessage(text);
    if (auto* body = std::get_if<TextPacketPayload::MessageOnly>(&packet.mBody.get())) body->mType = TextPacketType::Tip;
    packet.sendTo(player);
}

/// Tells the player about the effect: mod clients always (HUD), vanilla clients only when it
/// starts / gets stronger (`announce`) or ends.
void notify(Actor& actor, Effect const* effect, int64_t now, bool announce = true) {
    if (!actor.isPlayer()) return;
    auto& player = static_cast<Player&>(actor);
    EffectMessage msg;
    msg.amplifier = effect ? effect->amplifier : 0;
    msg.ticks     = effect ? static_cast<int>(effect->endTick - now) : 0;
    if (net::hasClientMod(player)) {
        net::sendToClient(player, msg);
        return;
    }
    if (effect && !announce) return;
    if (effect) {
        std::string name = trFor(player, "effect.quark.resilience", "Resilience");
        if (effect->amplifier > 0) name += " " + trFor(player, "lp.mobsoverworld.level_2", "II");
        tip(player, trFor(player, "lp.mobsoverworld.effect_start", "You feel steadier ({0})", name + " " + formatTicks(msg.ticks)));
    } else {
        tip(player, trFor(player, "lp.mobsoverworld.effect_end", "Resilience has worn off"));
    }
}

void applyAt(Vec3 const& center, int dim, std::optional<Strength> strength, int64_t hit, bool lingering) {
    if (!strength) return;
    auto* region = game::regionOf(dim);
    if (!region) return;
    if (lingering) {
        gClouds.push_back(Cloud{dim, center, 3.0f, 0, strength->duration, strength->amplifier, {}});
        log().info("mobsoverworld: lingering stability cloud");
        return;
    }
    AABB box{Vec3{center.x - 4, center.y - 2, center.z - 4}, Vec3{center.x + 4, center.y + 2, center.z + 4}};
    for (Actor* e : game::entities::inBox(*region, box)) {
        if (!e || !e->isAlive() || !asMob(*e)) continue;
        float d2 = e->getPosition().distanceToSqr(center);
        if (d2 >= 16.0f) continue;
        float scale    = game::entities::idOf(*e) == hit ? 1.0f : 1.0f - std::sqrt(d2) / 4.0f;
        int   duration = static_cast<int>(scale * static_cast<float>(strength->duration) + 0.5f);
        if (duration > 20) apply(*e, duration, strength->amplifier);
    }
    log().info("mobsoverworld: splash potion of stability");
}

} // namespace

void apply(Actor& target, int duration, int amplifier) {
    if (!asMob(target) || duration <= 0) return;
    int64_t now = gameTick();
    auto    uid = game::entities::idOf(target);
    auto&   e   = gEffects[uid];
    bool    had      = e.endTick > now;
    bool    stronger = !had || amplifier > e.amplifier;
    // MobEffectInstance.update: a higher level replaces, the same level extends.
    if (stronger || (amplifier == e.amplifier && now + duration > e.endTick)) {
        e.amplifier = amplifier;
        e.endTick   = now + duration;
    }
    setModifier(target, e.amplifier, true);
    gDirty = true;
    notify(target, &e, now, stronger);
    log().info("mobsoverworld: resilience {} for {} ticks", e.amplifier + 1, duration);
}

void clearEffect(Actor& target) {
    auto uid = game::entities::idOf(target);
    if (gEffects.erase(uid) == 0) return;
    setModifier(target, 0, false);
    gDirty = true;
    notify(target, nullptr, gameTick());
}

int remaining(Actor& target, int* amplifier) {
    auto it = gEffects.find(game::entities::idOf(target));
    if (it == gEffects.end()) return 0;
    if (amplifier) *amplifier = it->second.amplifier;
    return static_cast<int>(std::max<int64_t>(0, it->second.endTick - gameTick()));
}

void onDrink(Player& player, std::string const& itemId) {
    if (itemId == "minecraft:milk_bucket") {
        clearEffect(player);
        return;
    }
    if (auto s = strengthOfPotion(itemId, ""); s && config().enableCrabs) apply(player, s->duration, s->amplifier);
}

void see(Actor& actor, int64_t now) {
    std::string_view type = actor.getTypeName();
    bool             potion = type.starts_with("lepton:") && type.ends_with("_potion_entity");
    if (!potion && gEffects.empty() && gApplied.empty()) return;
    auto uid = game::entities::idOf(actor);
    // Thrown potions: remember where they are; when one disappears it has hit something.
    if (potion) {
        auto& t     = gThrown[uid];
        t.kind      = type.substr(7, type.size() - 7 - 7); // drop "lepton:" and "_entity"
        t.lingering = type.ends_with("lingering_potion_entity");
        t.dim       = static_cast<int>(actor.getDimensionId());
        t.pos       = actor.getPosition();
        t.owner     = actor.getOwnerId().rawID;
        t.seen      = now;
        return;
    }
    // Re-apply the modifier to entities with a running effect that were (re)loaded.
    if (auto it = gEffects.find(uid); it != gEffects.end()) {
        if (it->second.endTick <= now) {
            gEffects.erase(it);
            setModifier(actor, 0, false);
            gDirty = true;
            notify(actor, nullptr, now);
        } else if (!gApplied[uid]) {
            setModifier(actor, it->second.amplifier, true);
            notify(actor, &it->second, now);
        }
    } else if (auto a = gApplied.find(uid); a != gApplied.end()) {
        if (a->second) setModifier(actor, 0, false);
        gApplied.erase(uid);
    }
}

void tick(int64_t now) {
    // Thrown potions that were not seen this tick have hit.
    for (auto it = gThrown.begin(); it != gThrown.end();) {
        if (it->second.seen < now) {
            int64_t uid = it->first;
            Thrown  t   = it->second;
            it       = gThrown.erase(it);
            auto  s      = strengthOfPotion("lepton:" + t.kind, t.lingering ? "lingering" : "splash");
            auto* region = game::regionOf(t.dim);
            // Gone because its chunk unloaded (or it changed dimension): it did not land.
            bool landed = region && region->hasChunksAt(game::blockPosOf(t.pos), 2, false)
                       && game::entities::find(uid) == nullptr;
            if (config().enableCrabs && landed) {
                // The entity whose box it was in when it vanished took a direct hit.
                int64_t hit = 0;
                AABB    near{Vec3{t.pos.x - 1, t.pos.y - 2, t.pos.z - 1}, Vec3{t.pos.x + 1, t.pos.y + 2, t.pos.z + 1}};
                for (Actor* e : game::entities::inBox(*region, near)) {
                    if (!e || !asMob(*e) || game::entities::idOf(*e) == t.owner) continue;
                    AABB bb = e->getAABB();
                    if (t.pos.x >= bb.min.x - 0.4f && t.pos.x <= bb.max.x + 0.4f && t.pos.y >= bb.min.y - 0.4f
                        && t.pos.y <= bb.max.y + 0.4f && t.pos.z >= bb.min.z - 0.4f && t.pos.z <= bb.max.z + 0.4f) {
                        hit = game::entities::idOf(*e);
                        break;
                    }
                }
                {
                    // Glass breaking + the potion cloud for everyone around.
                    for (Actor* a : game::entities::inBox(*region, AABB{Vec3{t.pos.x - 16, t.pos.y - 16, t.pos.z - 16}, Vec3{t.pos.x + 16, t.pos.y + 16, t.pos.z + 16}})) {
                        if (a && a->isPlayer()) {
                            playSound(*a, "random.glass", t.pos);
                            particles(*a, "minecraft:splash_spell_emitter", t.pos);
                            break;
                        }
                    }
                }
                applyAt(t.pos, t.dim, s, hit, t.lingering);
            }
        } else {
            ++it;
        }
    }
    // Lingering clouds (Quark/vanilla AreaEffectCloud: radius 3, 600 ticks, wait 10, -0.5 radius per
    // application, effect duration / 4, re-application every 20 ticks).
    for (auto it = gClouds.begin(); it != gClouds.end();) {
        Cloud& c = *it;
        ++c.age;
        float radius = c.radius - 3.0f * static_cast<float>(c.age) / 600.0f;
        if (c.age >= 610 || radius < 0.5f) {
            it = gClouds.erase(it);
            continue;
        }
        if (c.age % 5 == 0 && c.age > 10) {
            if (auto* region = game::regionOf(c.dim)) {
                AABB box{Vec3{c.pos.x - radius, c.pos.y - 0.5f, c.pos.z - radius}, Vec3{c.pos.x + radius, c.pos.y + 1.5f, c.pos.z + radius}};
                for (Actor* e : game::entities::inBox(*region, box)) {
                    if (!e || !e->isAlive() || !asMob(*e)) continue;
                    float dx = e->getPosition().x - c.pos.x, dz = e->getPosition().z - c.pos.z;
                    if (dx * dx + dz * dz > radius * radius) continue;
                    auto uid = game::entities::idOf(*e);
                    if (auto l = c.lastApplied.find(uid); l != c.lastApplied.end() && now - l->second < 20) continue;
                    c.lastApplied[uid] = now;
                    apply(*e, c.duration / 4, c.amplifier);
                    c.radius -= 0.5f;
                }
                for (Actor* a : game::entities::inBox(*region, box)) {
                    if (a && a->isPlayer()) {
                        particles(*a, "minecraft:mobspell_emitter", c.pos, 2, radius, 0.2f);
                        break;
                    }
                }
            }
        }
        ++it;
    }
    // Expiry of effects on unloaded entities is handled when they are seen again; players are
    // always loaded.
    if (gDirty && now % 200 == 0) save();
}

// ---- brewing ----------------------------------------------------------------------------------

void resetBrewingMixes() { gMixesAdded = false; }

void addBrewingMixes() {
    if (gMixesAdded || !config().enableCrabs || !config().enableBrewing) return;
    auto shell = game::makeItem("lepton:crab_shell");
    if (game::isEmpty(shell)) return; // behaviour pack missing
    // PotionBrewing may be re-initialised per world (client): never add the mixes twice.
    if (PotionBrewing::hasMix(ItemInstance(game::makeItem("minecraft:potion", 1, 4)), ItemDescriptor("lepton:crab_shell", 0))) {
        gMixesAdded = true;
        return;
    }
    auto ingredient = [](std::string_view id) {
        PotionBrewing::Ingredient ing;
        ing.mItemId = game::makeItem(id).getId();
        ing.mData   = 0;
        return ing;
    };
    auto add = [&](std::string_view from, int fromAux, std::string_view reagent, std::string_view to, int toAux) {
        PotionBrewing::addPotionMix(ItemDescriptor(from, fromAux), ingredient(reagent), ItemDescriptor(to, toAux));
    };
    constexpr int kWater = 0, kMundane = 1, kAwkward = 4;
    for (std::string_view container : {"minecraft:potion", "minecraft:splash_potion", "minecraft:lingering_potion"}) {
        add(container, kWater, "lepton:crab_shell", container, kMundane);
    }
    struct Kind {
        std::string_view vanilla;
        std::string_view suffix;
    };
    for (Kind k : {Kind{"minecraft:potion", "potion"}, Kind{"minecraft:splash_potion", "splash_potion"},
                   Kind{"minecraft:lingering_potion", "lingering_potion"}}) {
        std::string base   = "lepton:resilience_" + std::string(k.suffix);
        std::string lng    = "lepton:long_resilience_" + std::string(k.suffix);
        std::string strong = "lepton:strong_resilience_" + std::string(k.suffix);
        add(k.vanilla, kAwkward, "lepton:crab_shell", base, 0);
        add(base, 0, "minecraft:redstone", lng, 0);
        add(base, 0, "minecraft:glowstone_dust", strong, 0);
    }
    // Container changes (gunpowder: splash, dragon's breath: lingering) for each strength.
    for (std::string_view p : {"", "long_", "strong_"}) {
        std::string potion    = "lepton:" + std::string(p) + "resilience_potion";
        std::string splash    = "lepton:" + std::string(p) + "resilience_splash_potion";
        std::string lingering = "lepton:" + std::string(p) + "resilience_lingering_potion";
        add(potion, 0, "minecraft:gunpowder", splash, 0);
        add(splash, 0, "minecraft:dragon_breath", lingering, 0);
        for (auto const& id : {potion, splash, lingering}) {
            PotionBrewing::Ingredient c = ingredient(id);
            auto& containers = PotionBrewing::mValidContainers();
            bool  known      = std::any_of(containers.begin(), containers.end(), [&](auto const& x) {
                return x.mItemId == c.mItemId;
            });
            if (!known) containers.push_back(c);
        }
    }
    gMixesAdded = true;
    log().info("mobsoverworld: brewing mixes for the potion of stability added");
}

std::string brew(std::string const& source, int sourceAux, std::string const& ingredient, int* aux) {
    ItemInstance  src(game::makeItem(source, 1, sourceAux));
    ItemDescriptor ing(ingredient, 0);
    ItemInstance  out = PotionBrewing::mix(ing, src);
    if (out.isNull()) return {};
    if (aux) *aux = out.getAuxValue();
    return out.getTypeName();
}

// ---- persistence --------------------------------------------------------------------------------

void load() {
    gEffects.clear();
    gApplied.clear();
    if (!game::db::available()) return;
    auto doc = game::db::load(kDbKey);
    if (!doc || !doc->is_object()) return;
    int64_t now = gameTick();
    for (auto& [k, v] : doc->items()) {
        if (!v.is_array() || v.size() != 2 || !v[0].is_number_integer() || !v[1].is_number_integer()) continue;
        try {
            gEffects[std::stoll(k)] = Effect{v[0].get<int>(), now + v[1].get<int64_t>()};
        } catch (std::exception const&) {
            log().warn("mobsoverworld: ignoring a bad effect entry '{}'", k);
        }
    }
}

void save() {
    if (!game::db::available()) return;
    nlohmann::json doc = nlohmann::json::object();
    int64_t        now = gameTick();
    for (auto const& [uid, e] : gEffects) {
        if (e.endTick > now) doc[std::to_string(uid)] = nlohmann::json::array({e.amplifier, e.endTick - now});
    }
    game::db::save(kDbKey, doc);
    gDirty = false;
}

void prune() {
    std::erase_if(gApplied, [](auto const& kv) { return game::entities::find(kv.first) == nullptr; });
}

void clear() {
    gEffects.clear();
    gApplied.clear();
    gThrown.clear();
    gClouds.clear();
    gDirty = false;
}

} // namespace lp::feature::mobsoverworld::resilience
