// Oretoise (Quark ToretoiseModule / Toretoise): ores on the shell, mining with pickaxes and
// pistoned iron rods, the stomp after a hit, feeding (taming, regrowing).

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/MovingBlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/mobsoverworld/Common.h"
#include "feature/mobsoverworld/Config.h"
#include "feature/mobsoverworld/Features.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::mobsoverworld::toretoise {

namespace {

constexpr int kAngeryTime = 20;
constexpr int kOreTypes   = 5;

struct State {
    int     angeryTicks = 0;
    int64_t aggressor   = 0;
    int     ambient     = 0;
};
std::unordered_map<int64_t, State> gState;

State& stateOf(Actor& a) { return gState[game::entities::idOf(a)]; }

float voicePitch() { return (randomFloat() - randomFloat()) * 0.2f + 0.6f; }

/// Vanilla "ore_drops" Fortune bonus: count * (1 + max(0, rand(fortune + 2) - 1)).
int oreDrops(int count, int fortune) {
    if (fortune <= 0) return count;
    int bonus = std::max(0, randomInt(fortune + 2) - 1);
    return count * (bonus + 1);
}

bool isFood(ItemStack const& stack) {
    auto id    = game::itemId(stack);
    auto const& foods = config().foods;
    return !id.empty() && std::find(foods.begin(), foods.end(), id) != foods.end();
}

bool breaksOre(Block const* block) {
    if (!block) return false;
    return block->getTypeName() == "lepton:iron_rod" || block->hasTag(HashedString("lepton:breaks_toretoise_ore"));
}

} // namespace

int  oreOf(Actor& t) { return std::clamp(intProperty(t, "lepton:ore"), 0, kOreTypes); }
void setOre(Actor& t, int ore) { game::entities::triggerEvent(t, "lepton:ore_" + std::to_string(std::clamp(ore, 0, kOreTypes))); }
int  angerOf(Actor& t) { return stateOf(t).angeryTicks; }

void dropOre(Actor& t, ItemStack const& tool, Player* miner) {
    int ore = oreOf(t);
    if (ore == 0) return;
    playSound(t, "dig.stone", t.getPosition(), 1.0f, 0.6f);
    int  fortune = game::isEmpty(tool) ? 0 : EnchantUtils::getEnchantLevel(Enchant::Type::Fortune, tool);
    bool silk    = !game::isEmpty(tool) && EnchantUtils::getEnchantLevel(Enchant::Type::SilkTouch, tool) > 0;
    std::vector<ItemStack> drops;
    static constexpr std::string_view kOreBlocks[] = {"", "minecraft:deepslate_coal_ore", "minecraft:deepslate_iron_ore",
                                                      "minecraft:deepslate_redstone_ore", "minecraft:deepslate_lapis_ore",
                                                      "minecraft:deepslate_copper_ore"};
    if (silk) {
        drops.push_back(game::makeItem(kOreBlocks[ore], 1));
    } else {
        switch (ore) {
        case 1: drops.push_back(game::makeItem("minecraft:coal", oreDrops(1, fortune))); break;
        case 2: drops.push_back(game::makeItem("minecraft:raw_iron", oreDrops(1, fortune))); break;
        case 3: drops.push_back(game::makeItem("minecraft:redstone", 4 + randomInt(2) + randomInt(fortune + 1))); break;
        case 4: drops.push_back(game::makeItem("minecraft:lapis_lazuli", oreDrops(4 + randomInt(6), fortune))); break;
        case 5: drops.push_back(game::makeItem("minecraft:raw_copper", oreDrops(2 + randomInt(4), fortune))); break;
        default: break;
        }
    }
    for (auto& d : drops) {
        if (!game::isEmpty(d)) t.spawnAtLocation(d, 1.2f);
    }
    setOre(t, 0);
    log().info("mobsoverworld: oretoise ore {} mined{}", ore, miner ? " by a player" : "");
}

void feed(Actor& t, int64_t now) {
    bool cooldown = intProperty(t, "lepton:regrow_at") > now;
    playSound(t, cooldown ? "random.burp" : "random.eat", t.getPosition(), 0.5f + 0.5f * static_cast<float>(randomInt(2)),
              (randomFloat() - randomFloat()) * 0.2f + 1.0f);
    t.heal(8);
    if (!boolProperty(t, "lepton:fed")) {
        game::entities::triggerEvent(t, "lepton:tamed");
        particles(t, "minecraft:heart_particle", t.getPosition(), 20, 0.5f, 0.5f);
        log().info("mobsoverworld: oretoise tamed");
        return;
    }
    if (cooldown) return;
    // popOre(false)
    int chance = config().regrowChance;
    if (chance <= 0) return;
    if (oreOf(t) == 0 && randomInt(chance) == 0) {
        setOre(t, randomInt(kOreTypes) + 1);
        setIntProperty(t, "lepton:regrow_at", static_cast<int>(now + config().cooldownTicks));
        Vec3 p = t.getPosition();
        p.y += 0.5f;
        particles(t, "minecraft:white_smoke_particle", p, 30, 0.6f, 0.6f);
        playSound(t, "random.orb", t.getPosition(), 10.0f, 0.7f);
        log().info("mobsoverworld: oretoise regrew its ore");
    }
}

void tick(Actor& t, int64_t now) {
    (void)now;
    State& s = stateOf(t);
    if (s.angeryTicks > 0 && t.isAlive()) {
        --s.angeryTicks;
        if (t.isOnGround()) {
            float dangerRange = 3;
            float width       = t.getAABB().max.x - t.getAABB().min.x;
            Vec3  pos         = t.getPosition();
            float x           = pos.x + width / 2;
            float y           = pos.y;
            float z           = pos.z + width / 2;
            if (s.angeryTicks == 3) playSound(t, "mob.irongolem.crack", pos, 1.0f, 0.2f);
            else if (s.angeryTicks == 0) particles(t, "minecraft:white_smoke_particle", Vec3{x, y, z}, 40, dangerRange, 0.5f);
            if (s.angeryTicks == 0) {
                AABB box{Vec3{x - dangerRange, y - 1, z - dangerRange}, Vec3{x + dangerRange, y + 1, z + dangerRange}};
                Actor* aggressor = s.aggressor ? game::entities::find(s.aggressor) : nullptr;
                if (!aggressor) aggressor = &t;
                float damage = static_cast<float>(4 + difficulty());
                for (Actor* e : game::entities::inBox(t.getDimensionBlockSource(), box)) {
                    if (!e || e == &t || !e->isAlive() || !asMob(*e)) continue;
                    if (game::entities::typeOf(*e) == kToretoise) continue;
                    Actor* by = e == aggressor ? &t : aggressor;
                    if (e->isPlayer() && difficulty() == 0) pushUp(*e, 0.25f);
                    e->hurtByCause(damage, SharedTypes::Legacy::ActorDamageCause::EntityAttack, *by);
                }
                log().info("mobsoverworld: oretoise stomps");
            }
        }
    }

    // A piston pushing an iron rod into the shell mines the ore with an iron pickaxe.
    if (oreOf(t) != 0) {
        auto& region = t.getDimensionBlockSource();
        AABB  bb     = t.getAABB();
        BlockPos min{static_cast<int>(std::round(bb.min.x)), static_cast<int>(std::round(bb.min.y)), static_cast<int>(std::round(bb.min.z))};
        BlockPos max{static_cast<int>(std::round(bb.max.x)), static_cast<int>(std::round(bb.max.y)), static_cast<int>(std::round(bb.max.z))};
        bool done = false;
        for (int ix = min.x; ix <= max.x && !done; ++ix) {
            for (int iy = min.y; iy <= max.y && !done; ++iy) {
                for (int iz = min.z; iz <= max.z && !done; ++iz) {
                    BlockPos p{ix, iy, iz};
                    if (game::blockIdAt(region, p) != "minecraft:moving_block") continue;
                    auto* be = region.getBlockEntity(p);
                    if (!be) continue;
                    auto* moving = static_cast<MovingBlockActor*>(be);
                    if (moving->mPistonBlockExpanding && breaksOre(moving->mWrappedBlock)) {
                        dropOre(t, game::makeItem("minecraft:iron_pickaxe"), nullptr);
                        done = true;
                    }
                }
            }
        }
    }

    if (randomInt(1000) < s.ambient++) {
        s.ambient = -80;
        playSound(t, "mob.turtle.ambient", t.getPosition(), 1.0f, voicePitch());
    }
}

bool onHurt(Actor& t, ActorDamageSource const& source, float& damage) {
    State& s      = stateOf(t);
    Actor* direct = source.isEntitySource() ? game::entities::find(source.getEntityUniqueID().rawID) : nullptr;
    if (direct && asMob(*direct)) {
        ItemStack const& held = direct->isPlayer() ? static_cast<Player&>(*direct).getSelectedItem() : direct->getCarriedItem();
        if (oreOf(t) != 0 && itemHasTag(held, "minecraft:is_pickaxe")) {
            Player* player = direct->isPlayer() ? static_cast<Player*>(direct) : nullptr;
            ItemStack tool = held;
            dropOre(t, tool, player);
            if (player && tool.isDamageableItem() && !isCreative(*player)) {
                tool.hurtAndBreak(1, player);
                setSelected(*player, tool);
            }
            return true;
        }
        if (s.angeryTicks == 0) {
            s.angeryTicks = kAngeryTime;
            s.aggressor   = game::entities::idOf(*direct);
        }
    }
    if (t.getHealth() - damage <= 0) playSound(t, "mob.turtle.death", t.getPosition(), 1.0f, voicePitch());
    else playSound(t, "mob.turtle.hurt", t.getPosition(), 1.0f, voicePitch());
    return false;
}

Handled interact(Player& player, Actor& t) {
    if (!config().enableToretoise) return Handled::No;
    ItemStack const& held = player.getSelectedItem();
    if (!isFood(held) || t.isBaby()) return Handled::No;
    consumeSelected(player);
    feed(t, gameTick());
    return Handled::Yes;
}

void clear() { gState.clear(); }

} // namespace lp::feature::mobsoverworld::toretoise
