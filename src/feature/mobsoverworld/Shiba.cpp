// Shiba (Quark ShibaModule / Shiba): fetching arrows, mouth items, collar dye, wolf armour,
// petting, pointing at dark spots for its owner. Taming, sitting, healing and breeding are the
// behaviour-pack components of bp/entities/shiba.json.

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/projectile/AbstractArrow.h"
#include "mc/world/actor/projectile/Arrow.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "feature/mobsoverworld/Common.h"
#include "feature/mobsoverworld/Config.h"
#include "feature/mobsoverworld/Features.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::mobsoverworld::shiba {

namespace {

using Cause = SharedTypes::Legacy::ActorDamageCause;
using SharedTypes::Legacy::ArmorSlot;

struct State {
    int64_t                 fetching = 0; ///< arrow unique id
    std::optional<BlockPos> hyperfocus;
    int                     hyperfocusCooldown = 0;
    int                     timeTilNextJump    = 20;
    int                     recalcPath         = 0;
    int                     ambient            = 0;
    bool                    busy               = false; ///< wander goals removed
    /// False until the first setBusy: the component groups are saved with the entity, this state
    /// is not, so a reloaded shiba gets its goals set again once.
    bool                    synced             = false;
};
std::unordered_map<int64_t, State> gState;

State& stateOf(Actor& a) { return gState[game::entities::idOf(a)]; }

bool isLight(ItemStack const& stack) {
    auto id = game::itemId(stack);
    return id == "minecraft:torch" || id == "minecraft:soul_torch" || id == "minecraft:lantern"
        || id == "minecraft:soul_lantern";
}

bool holdsLight(Actor& owner) {
    ItemStack const& main = owner.isPlayer() ? static_cast<Player&>(owner).getSelectedItem() : owner.getCarriedItem();
    return isLight(main) || isLight(owner.getOffhandSlot());
}

bool isMeat(std::string const& id) {
    static constexpr std::string_view kMeat[] = {
        "minecraft:beef", "minecraft:chicken", "minecraft:cooked_beef", "minecraft:cooked_chicken",
        "minecraft:cooked_mutton", "minecraft:cooked_porkchop", "minecraft:cooked_rabbit", "minecraft:mutton",
        "minecraft:porkchop", "minecraft:rabbit", "minecraft:rotten_flesh"};
    return std::find(std::begin(kMeat), std::end(kMeat), id) != std::end(kMeat);
}

/// Arrow pick-up: 0 allowed, 1 creative only (infinity / creative shooter), 2 disallowed.
int pickupOf(Actor& arrow) {
    auto& a = static_cast<AbstractArrow&>(arrow);
    if (!a.mIsPlayerOwned) return 2;
    if (a.mIsCreative) return 1;
    if (game::entities::typeOf(arrow) == "minecraft:arrow" && static_cast<Arrow&>(arrow).mEnchantInfinity > 0) return 1;
    return 0;
}

Actor* arrowOf(State const& s) { return s.fetching ? game::entities::find(s.fetching) : nullptr; }

bool mouthEmpty(Actor& shiba) { return game::isEmpty(shiba.getCarriedItem()); }

void setMouth(Actor& shiba, ItemStack const& stack) { shiba.setCarriedItem(stack); }

void hearts(Actor& shiba) {
    Vec3 p = shiba.getPosition();
    p.y += 0.5f;
    particles(shiba, "minecraft:heart_particle", p);
}

void whine(Actor& shiba) { playSound(shiba, "mob.wolf.whine", shiba.getPosition(), 0.6f, 0.5f + randomFloat() * 0.5f); }

/// Quark's Shiba.hasLineOfSight: eye -> block centre, no colliding block in between, within range.
bool lineOfSight(Actor& shiba, BlockPos const& pos, float range) {
    Vec3 eye = shiba.getHeadPos();
    Vec3 target{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    if (eye.distanceTo(target) > range) return false;
    return shiba.canSee(target, ShapeType::Collision);
}

int blockLight(BlockSource& region, BlockPos const& pos) {
    return static_cast<int>(region.getBrightnessPair(pos).block->mValue);
}

void setBusy(Actor& shiba, State& s, bool busy) {
    if (s.synced && s.busy == busy) return;
    s.synced = true;
    s.busy   = busy;
    game::entities::triggerEvent(shiba, busy ? "lepton:busy" : "lepton:idle");
}

void jumpTimer(Actor& shiba, State& s) {
    if (--s.timeTilNextJump <= 0) {
        s.timeTilNextJump = randomInt(5) + 10;
        if (shiba.isOnGround()) pushUp(shiba, 0.3f);
    }
}

/// Java Crackiness.WOLF_ARMOR (0.95, 0.69, 0.32): 0 none .. 3 high.
int crackiness(ItemStack const& armor) {
    auto const* item = armor.mItem.get();
    if (!item) return 0;
    int max = item->getMaxDamage();
    if (max <= 0) return 0;
    float left = static_cast<float>(max - armor.getDamageValue()) / static_cast<float>(max);
    if (left < 0.32f) return 3;
    if (left < 0.69f) return 2;
    if (left < 0.95f) return 1;
    return 0;
}

bool armorAbsorbs(Cause cause) {
    switch (cause) {
    case Cause::EntityAttack:
    case Cause::Projectile:
    case Cause::BlockExplosion:
    case Cause::EntityExplosion:
    case Cause::Anvil:
    case Cause::FallingBlock:
    case Cause::Thorns:
    case Cause::Fireworks:
    case Cause::RamAttack:
    case Cause::MaceSmash:
    case Cause::Charging:
    case Cause::Piston:
        return true;
    default:
        return false;
    }
}

bool hasArmor(Actor& shiba) { return game::itemId(shiba.getArmor(ArmorSlot::Body)) == "minecraft:wolf_armor"; }

void setArmor(Actor& shiba, ItemStack const& stack) {
    shiba.setArmor(ArmorSlot::Body, stack);
    game::entities::triggerEvent(shiba, game::isEmpty(stack) ? "lepton:armor_off" : "lepton:armor_on");
}

bool isOwner(Actor& shiba, Player& player) {
    return shiba.isTame() && shiba.getOwnerId().rawID == game::entities::idOf(player);
}

} // namespace

bool hyperfocus(Actor& shiba, BlockPos& out) {
    auto it = gState.find(game::entities::idOf(shiba));
    if (it == gState.end() || !it->second.hyperfocus) return false;
    out = *it->second.hyperfocus;
    return true;
}

int64_t fetching(Actor& shiba) {
    auto it = gState.find(game::entities::idOf(shiba));
    return it == gState.end() ? 0 : it->second.fetching;
}

void tick(Actor& shiba, int64_t now) {
    State& s      = stateOf(shiba);
    auto&  region = shiba.getDimensionBlockSource();

    // Drop a fetch target that vanished / changed dimension / became disallowed.
    if (s.fetching) {
        Actor* arrow = arrowOf(s);
        if (!arrow || !arrow->isAlive() || arrow->getDimensionId() != shiba.getDimensionId() || pickupOf(*arrow) == 2) {
            s.fetching = 0;
        }
    }

    bool  tame   = shiba.isTame();
    Mob*  owner  = tame ? shiba.getOwner() : nullptr;
    bool  sitting = shiba.isSitting();

    // ---- dark spot search (hyperfocus) ----
    if (s.hyperfocusCooldown > 0) --s.hyperfocusCooldown;
    if (s.fetching || sitting || !tame || shiba.isLeashed()) {
        s.hyperfocus.reset();
    } else {
        if (s.hyperfocus) {
            bool clear       = blockLight(region, *s.hyperfocus) > 0;
            bool ownerAbsent = !owner || (owner->isPlayer() && !holdsLight(*owner));
            if (clear || ownerAbsent) {
                s.hyperfocus.reset();
                s.hyperfocusCooldown = 40;
                if (clear && !ownerAbsent) log().info("mobsoverworld: shiba dark spot lit");
            }
        }
        if (!s.hyperfocus && owner && owner->isPlayer() && s.hyperfocusCooldown == 0 && holdsLight(*owner)) {
            BlockPos ours  = shiba.getFeetBlockPos();
            int      range = 10;
            for (int i = 0; i < 20; ++i) {
                BlockPos test{ours.x + randomInt(range * 2 + 1) - range, ours.y + randomInt(range * 2 + 1) - range,
                              ours.z + randomInt(range * 2 + 1) - range};
                BlockPos above{test.x, test.y + 1, test.z};
                BlockPos under{test.x, test.y - 1, test.z};
                if (lineOfSight(shiba, above, static_cast<float>(range)) && game::blockIdAt(region, test) == "minecraft:air"
                    && region.isSolidBlockingBlock(under) && blockLight(region, test) == 0
                    && !(config().ignoreAreasWithSkylight && region.canSeeSky(test))) {
                    s.hyperfocus = test;
                }
            }
            if (s.hyperfocus) log().info("mobsoverworld: shiba found a dark spot");
        }
    }

    // ---- fetch target selection ----
    if (!s.fetching && mouthEmpty(shiba) && owner) {
        AABB box = owner->getAABB();
        box.min.x -= 2; box.min.y -= 2; box.min.z -= 2;
        box.max.x += 2; box.max.y += 2; box.max.z += 2;
        std::vector<Actor*> arrows;
        for (Actor* a : game::entities::inBox(region, box)) {
            if (!a) continue;
            auto t = game::entities::typeOf(*a);
            if (t != "minecraft:arrow" && t != "minecraft:thrown_trident") continue;
            if (a->getOwnerId().rawID != game::entities::idOf(*owner) || pickupOf(*a) == 2) continue;
            arrows.push_back(a);
        }
        if (!arrows.empty()) {
            s.fetching = game::entities::idOf(*arrows[static_cast<size_t>(randomInt(static_cast<int>(arrows.size())))]);
            log().info("mobsoverworld: shiba fetches an arrow");
        }
    }

    // ---- goals (priority: bark at darkness, fetch, deliver) ----
    bool busy = false;
    if (!sitting && s.hyperfocus) {
        busy    = true;
        Vec3 at{static_cast<float>(s.hyperfocus->x) + 0.5f, static_cast<float>(s.hyperfocus->y),
                static_cast<float>(s.hyperfocus->z) + 0.5f};
        if (--s.recalcPath <= 0) {
            s.recalcPath = 10;
            navigateTo(shiba, at, 1.1f);
        }
        if ((now + game::entities::idOf(shiba)) % 5 == 0) {
            Vec3 p = shiba.getPosition();
            p.y += 0.5f;
            particles(shiba, "minecraft:villager_angry", p, 1, 0.25f, 0.1f);
            lookAt(shiba, at);
            playSound(shiba, "mob.wolf.bark", shiba.getPosition(), 0.4f, (randomFloat() - randomFloat()) * 0.2f + 1.0f);
        }
    } else if (Actor* arrow = arrowOf(s); !sitting && arrow && mouthEmpty(shiba)) {
        busy = true;
        lookAt(shiba, arrow->getPosition());
        if (--s.recalcPath <= 0) {
            s.recalcPath = 10;
            if (!shiba.isLeashed() && !shiba.getVehicle()) navigateTo(shiba, arrow->getPosition(), 1.1f);
        }
        float dist = shiba.getPosition().distanceTo(arrow->getPosition());
        if (dist < 3 && arrow->isAlive()) {
            int pickup = pickupOf(*arrow);
            if (pickup == 0) {
                ItemStack item = static_cast<AbstractArrow&>(*arrow)._getPickupItem();
                setMouth(shiba, item);
                game::entities::despawn(*arrow);
                s.fetching = 0;
                log().info("mobsoverworld: shiba picked up an arrow");
            } else if (dist < 1) {
                playSound(shiba, "random.eat", shiba.getPosition());
                game::entities::despawn(*arrow);
                s.fetching = 0;
                log().info("mobsoverworld: shiba ate an arrow");
            }
        }
        jumpTimer(shiba, s);
    } else if (!sitting && owner && !mouthEmpty(shiba)) {
        float dist = shiba.getPosition().distanceTo(owner->getPosition());
        if (dist > 1.0f) {
            busy = true;
            if (--s.recalcPath <= 0) {
                s.recalcPath = 10;
                navigateTo(shiba, owner->getPosition(), 1.1f);
            }
            jumpTimer(shiba, s);
        }
    }
    setBusy(shiba, s, busy);

    // ---- ambient sounds: whine when hurt, pant, bark ----
    if (randomInt(1000) < s.ambient++) {
        s.ambient = -80;
        std::string sound = "mob.wolf.bark";
        if (randomInt(3) == 0) {
            sound = static_cast<float>(shiba.getHealth()) / static_cast<float>(std::max(1, shiba.getMaxHealth())) < 0.5f
                      ? "mob.wolf.whine"
                      : "mob.wolf.panting";
        }
        playSound(shiba, sound, shiba.getPosition(), 0.4f, (randomFloat() - randomFloat()) * 0.2f + 1.0f);
    }
}

bool onHurt(Actor& shiba, ActorDamageSource const& source, float& damage) {
    if (hasArmor(shiba) && armorAbsorbs(source.mCause)) {
        ItemStack armor  = shiba.getArmor(ArmorSlot::Body);
        int       before = crackiness(armor);
        int       amount = static_cast<int>(std::ceil(damage));
        auto const* item = armor.mItem.get();
        int       max    = item ? item->getMaxDamage() : 64;
        int       dmg    = armor.getDamageValue() + amount;
        if (dmg >= max) {
            setArmor(shiba, ItemStack::EMPTY_ITEM());
            playSound(shiba, "random.break", shiba.getPosition());
        } else {
            armor.setDamageValue(static_cast<short>(dmg));
            setArmor(shiba, armor);
            if (crackiness(armor) != before) {
                playSound(shiba, "armor.crack_wolf", shiba.getPosition());
                Vec3 p = shiba.getPosition();
                p.y += 1.0f;
                particles(shiba, "minecraft:critical_hit_emitter", p, 3, 0.2f, 0.1f);
            }
        }
        playSound(shiba, "armor.hit_wolf", shiba.getPosition());
        return true;
    }
    if (shiba.getHealth() - damage <= 0) playSound(shiba, "mob.wolf.death", shiba.getPosition(), 0.4f);
    else playSound(shiba, "mob.wolf.hurt", shiba.getPosition(), 0.4f);
    return false;
}

Handled interact(Player& player, Actor& shiba) {
    if (!config().enableShiba) return Handled::No;
    ItemStack const& held = player.getSelectedItem();
    std::string      id   = game::itemId(held);
    bool             tame = shiba.isTame();

    // Petting: sneaking with an empty main hand.
    if (player.isSneaking() && game::isEmpty(held)) {
        if (!tame) return Handled::No;
        hearts(shiba);
        whine(shiba);
        setIntProperty(shiba, "lepton:last_pet", static_cast<int>(gameTick()));
        log().info("mobsoverworld: shiba petted");
        return Handled::Yes;
    }
    if (!tame) return Handled::No; // bones: the behaviour-pack tameable component

    bool owner = isOwner(shiba, player);
    // Give back what it carries.
    if (!mouthEmpty(shiba)) {
        giveOrDrop(player, shiba.getCarriedItem());
        hearts(shiba);
        whine(shiba);
        setMouth(shiba, ItemStack::EMPTY_ITEM());
        log().info("mobsoverworld: shiba delivered its item");
        return Handled::Yes;
    }
    // Meat heals (behaviour-pack healable) when hurt.
    if (isMeat(id) && shiba.getHealth() < shiba.getMaxHealth()) return Handled::No;
    // Dye the collar.
    if (int dye = dyeColorOf(held); dye >= 0) {
        if (dye != intProperty(shiba, "lepton:collar")) {
            setIntProperty(shiba, "lepton:collar", dye);
            consumeSelected(player);
            log().info("mobsoverworld: shiba collar dyed {}", dye);
            return Handled::Yes;
        }
    }
    // Swords go into its mouth (one item; Quark shrinks the stack even in creative).
    if (!game::isEmpty(held) && itemHasTag(held, "minecraft:is_sword")) {
        ItemStack one = held;
        one.set(1);
        ItemStack rest = held;
        rest.remove(1);
        setMouth(shiba, one);
        setSelected(player, rest.mCount > 0 ? rest : ItemStack::EMPTY_ITEM());
        log().info("mobsoverworld: shiba holds {}", id);
        return Handled::Yes;
    }
    if (id == "minecraft:wolf_armor" && owner && !hasArmor(shiba) && !shiba.isBaby()) {
        ItemStack one = held;
        one.set(1);
        setArmor(shiba, one);
        consumeSelected(player);
        playSound(shiba, "armor.equip_wolf", shiba.getPosition());
        log().info("mobsoverworld: shiba wears wolf armor");
        return Handled::Yes;
    }
    if (id == "minecraft:shears" && owner && hasArmor(shiba)) {
        ItemStack shears = held;
        if (!isCreative(player)) {
            shears.hurtAndBreak(1, &player);
            setSelected(player, shears);
        }
        playSound(shiba, "armor.unequip_wolf", shiba.getPosition());
        dropItem(shiba.getDimensionBlockSource(), shiba.getPosition(), shiba.getArmor(ArmorSlot::Body));
        setArmor(shiba, ItemStack::EMPTY_ITEM());
        return Handled::Yes;
    }
    if (id == "minecraft:armadillo_scute" && shiba.isSitting() && hasArmor(shiba) && owner) {
        ItemStack armor = shiba.getArmor(ArmorSlot::Body);
        if (armor.getDamageValue() > 0) {
            consumeSelected(player);
            playSound(shiba, "armor.repair_wolf", shiba.getPosition());
            auto const* item = armor.mItem.get();
            int         max  = item ? item->getMaxDamage() : 64;
            armor.setDamageValue(static_cast<short>(std::max(0, armor.getDamageValue() - max / 8)));
            setArmor(shiba, armor);
            return Handled::Yes;
        }
    }
    return Handled::No;
}

void afterInteract(Player& player, Actor& shiba, bool wasTame, bool wasSitting, bool vanillaConsumed) {
    if (!config().enableShiba) return;
    if (!wasTame && shiba.isTame()) {
        // Tamed with a bone: it sits down, like Quark's Shiba.
        shiba.setSitting(true);
        stopNavigation(shiba);
        log().info("mobsoverworld: shiba tamed");
        return;
    }
    if (!wasTame || !isOwner(shiba, player)) return;
    if ((!vanillaConsumed || shiba.isBaby()) && shiba.isSitting() == wasSitting) {
        shiba.setSitting(!wasSitting);
        stopNavigation(shiba);
    }
}

void onDeath(Actor& shiba) {
    // The item in its mouth (a given sword, a fetched arrow) is dropped, never lost.
    if (!mouthEmpty(shiba)) {
        dropItem(shiba.getDimensionBlockSource(), shiba.getPosition(), shiba.getCarriedItem());
        setMouth(shiba, ItemStack::EMPTY_ITEM());
    }
    if (hasArmor(shiba)) {
        dropItem(shiba.getDimensionBlockSource(), shiba.getPosition(), shiba.getArmor(ArmorSlot::Body));
        shiba.setArmor(ArmorSlot::Body, ItemStack::EMPTY_ITEM());
    }
    gState.erase(game::entities::idOf(shiba));
}

void prune() {
    std::erase_if(gState, [](auto const& kv) { return game::entities::find(kv.first) == nullptr; });
}

void clear() { gState.clear(); }

} // namespace lp::feature::mobsoverworld::shiba
