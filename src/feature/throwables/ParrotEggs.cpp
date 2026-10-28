// Parrot eggs (Quark ParrotEggsModule): feeding tamed parrots, laying, hatching baby parrots,
// and the kotobirb mark.

#include "feature/throwables/Throwables.h"

#include <map>
#include <string>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace lp::feature::throwables::parroteggs {

namespace ent = game::entities;

namespace {

constexpr int         kDataVariant     = 2;
constexpr int         kDataScale       = 38;
constexpr int         kDataMarkVariant = 43;
constexpr int         kGrowTicks       = 24000;
constexpr char const* kDbKey           = "lp:throwables:parrots";

struct State {
    std::map<int64_t, int> timers; ///< parrot -> egg timer
    std::map<int64_t, int> babies; ///< parrot -> ticks until grown up
    uint64_t               tick = 0;
    bool                   dirty = false;
};
State& st() {
    static State s;
    return s;
}

bool isFeed(ItemStack const& s) { return game::itemId(s) == "minecraft:beetroot_seeds"; }

void spawnEggItem(Actor& parrot, int variant) {
    auto  stack  = game::makeItem("lepton:egg_parrot_" + std::string(kParrotVariants[variant]));
    auto& region = parrot.getDimensionBlockSource();
    region.getLevel().getSpawner().spawnItem(region, stack, nullptr, parrot.getPosition(), 0);
}

void makeBaby(Actor& parrot, bool baby) {
    setActorFloat(parrot, kDataScale, baby ? 0.5f : 1.0f);
    setActorFlag(parrot, static_cast<int>(ActorFlags::Baby), baby);
}

} // namespace

int  eggTimer(int64_t id) { return st().timers.contains(id) ? st().timers[id] : 0; }
void setEggTimer(int64_t id, int ticks) {
    if (ticks > 0) st().timers[id] = ticks;
    else st().timers.erase(id);
    st().dirty = true;
}
bool isBaby(int64_t id) { return st().babies.contains(id); }

bool onInteract(Player& player, Actor& target) {
    if (!cfg().enableParrotEggs || target.getTypeName() != "minecraft:parrot") return false;
    bool        main  = isFeed(player.getSelectedItem());
    ItemStack   stack = main ? player.getSelectedItem() : player.getOffhandSlot();
    if (!isFeed(stack)) return false;
    auto id  = ent::idOf(target);
    int  dim = static_cast<int>(target.getDimensionId());
    Vec3 at  = target.getPosition();
    if (eggTimer(id) > 0) {
        particle("minecraft:heart_particle", Vec3{at.x, at.y + 0.5f, at.z}, dim);
        return false;
    }
    if (!target.getStatusFlag(ActorFlags::Tamed)) return false;
    if (!isCreative(player)) {
        stack.remove(1);
        if (main) player.setSelectedItem(stack);
        else player.setOffhandSlot(stack);
        player.refreshInventory();
    }
    float pitch = 1.0f + static_cast<float>(randomDouble() - randomDouble()) * 0.2f;
    playSound("mob.parrot.eat", at, 1.0f, pitch);
    if (randomDouble() < cfg().chance) {
        setEggTimer(id, cfg().eggTime);
        for (int i = 0; i < 5; ++i)
            particle("minecraft:villager_happy", Vec3{at.x + float(randomDouble() - 0.5), at.y + float(randomDouble()) * 0.9f, at.z + float(randomDouble() - 0.5)}, dim);
        log().info("throwables: parrot {} will lay an egg", id);
    } else {
        for (int i = 0; i < 5; ++i)
            particle("minecraft:basic_smoke_particle", Vec3{at.x + float(randomDouble() - 0.5), at.y + float(randomDouble()) * 0.9f, at.z + float(randomDouble() - 0.5)}, dim);
    }
    return true;
}

void onEggHit(std::string const& type, Vec3 const& pos, int dim) {
    std::string variant = type.substr(std::string("lepton:parrot_egg_").size());
    int         v       = 0;
    for (int i = 0; i < 5; ++i)
        if (kParrotVariants[i] == variant) v = i;
    auto* parrot = ent::spawn(dim, "minecraft:parrot", pos);
    if (!parrot) return;
    setActorInt(*parrot, kDataVariant, v);
    makeBaby(*parrot, true);
    st().babies[ent::idOf(*parrot)] = kGrowTicks;
    st().dirty                      = true;
    log().info("throwables: parrot egg hatched ({})", variant);
}

void tick() {
    auto& s = st();
    ++s.tick;
    for (auto it = s.timers.begin(); it != s.timers.end();) {
        auto* p = ent::find(it->first);
        if (!p) {
            ++it;
            continue;
        }
        if (!p->isAlive()) {
            it = s.timers.erase(it);
            continue;
        }
        if (it->second == 1) {
            float pitch = static_cast<float>(randomDouble() - randomDouble()) * 0.2f + 1.0f;
            playSound("lepton.parrot.egg", p->getPosition(), 1.0f, pitch);
            int v = p->getVariant();
            if (randomInt(2) != 0) v = randomInt(5);
            spawnEggItem(*p, std::clamp(v, 0, 4));
            log().info("throwables: parrot laid an egg");
        }
        if (--it->second <= 0) it = s.timers.erase(it);
        else ++it;
        s.dirty = true;
    }
    for (auto it = s.babies.begin(); it != s.babies.end();) {
        auto* p = ent::find(it->first);
        if (!p) {
            ++it;
            continue;
        }
        if (!p->isAlive()) {
            it = s.babies.erase(it);
            continue;
        }
        if (--it->second <= 0) {
            makeBaby(*p, false);
            it = s.babies.erase(it);
            continue;
        }
        if (s.tick % 100 == 0) makeBaby(*p, true); // re-apply after reloads
        ++it;
    }
    if (s.tick % 100 == 0) {
        bool on = cfg().enableParrotEggs && cfg().enableKotobirb;
        for (auto* p : ent::ofType("minecraft:parrot")) {
            if (!p) continue;
            bool koto = on && p->getVariant() == 4 && ent::idOf(*p) % 20 == 0;
            setActorInt(*p, kDataMarkVariant, koto ? 1 : 0);
        }
    }
}

void onWorldReady() {
    st() = State{};
    auto v = game::db::load(kDbKey);
    if (!v || !v->is_object()) return;
    auto read = [&](char const* name, std::map<int64_t, int>& out) {
        if (auto it = v->find(name); it != v->end() && it->is_object())
            for (auto& [k, val] : it->items()) out[std::stoll(k)] = val.get<int>();
    };
    read("timers", st().timers);
    read("babies", st().babies);
}

void onWorldSave() {
    if (!game::db::available() || !st().dirty) return;
    nlohmann::json j{{"timers", nlohmann::json::object()}, {"babies", nlohmann::json::object()}};
    for (auto& [k, v] : st().timers) j["timers"][std::to_string(k)] = v;
    for (auto& [k, v] : st().babies) j["babies"][std::to_string(k)] = v;
    game::db::save(kDbKey, j);
    st().dirty = false;
}

void onWorldClosed() { st() = State{}; }

void install() {}
void uninstall() { onWorldSave(); }

} // namespace lp::feature::throwables::parroteggs
