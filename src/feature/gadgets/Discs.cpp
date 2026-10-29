// AmbientDiscs and EndermoshMusicDisc (Quark tools/AmbientDiscsModule, EndermoshMusicDiscModule):
// eight looping ambience discs dropped when a skeleton kills a spider, and the Endermosh record in
// End City treasure (+ optional dragon fight music). See docs/spec/gadgets.md.

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/Container.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/actor/JukeboxBlockActor.h"
#include "mc/world/level/storage/loot/LootPool.h"
#include "mc/world/level/storage/loot/LootTable.h"
#include "mc/world/level/storage/loot/LootTableContext.h"
#include "mc/world/level/storage/loot/entries/LootPoolEntry.h"

#include "core/GlobalPos.h"
#include "feature/gadgets/Discs.h"
#include "feature/gadgets/Gadgets.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace lp::feature::gadgets::discs {

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

ll::event::ListenerPtr gDie;

// ---- ambience discs: spider killed by a skeleton ------------------------------------------------------

bool isSkeleton(int64_t id) {
    if (id == -1 || id == 0) return false;
    auto* a = game::entities::find(id);
    return a && game::entities::typeOf(*a) == "minecraft:skeleton"; // Java: instanceof Skeleton
}

void onMobDie(ll::event::MobDieEvent& ev) {
    auto& mob = ev.self();
    if (mob.isClientSide()) return;
    auto const& c = cfg();
    if (!c.enableAmbientDiscs || !c.dropOnSpiderKill) return;
    auto type = game::entities::typeOf(mob);
    if (type != "minecraft:spider" && type != "minecraft:cave_spider") return; // Java: instanceof Spider
    auto const& src = ev.source();
    // Java getEntity(): the shooter of an arrow, the attacker of a melee hit
    if (!isSkeleton(src.getEntityUniqueID().rawID) && !isSkeleton(src.getDamagingEntityUniqueID().rawID)) return;
    auto disc = randomAmbientDisc();
    if (game::isEmpty(disc)) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    level->getSpawner().spawnItem(mob.getDimensionBlockSource(), disc, nullptr, mob.getPosition(), 0);
    log().info("gadgets: a skeleton killed a {}: dropped {}", type, game::itemId(disc));
}

// ---- ambience discs: looping jukeboxes -------------------------------------------------------------------

constexpr int kLoopTicks = 24 * 20; // the generated loops are 24 s long

struct Playing {
    std::string name;     ///< ambience name ("rain")
    uint64_t    started;  ///< tick the record started
    int         replays = 0;
};
std::map<core::GlobalPos, Playing> gJukeboxes;

void stopNear(core::GlobalPos const& pos, std::string const& sound) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == pos.dim && p.getPosition().distanceTo(at) <= 96) stopSoundTo(p, sound);
        return true;
    });
}

void jukeboxTick(JukeboxBlockActor& jb, BlockSource& region) {
    auto pos  = game::globalPos(region, jb.getPosition());
    auto name = ambientNameOf(game::itemId(*jb.mRecord));
    auto it   = gJukeboxes.find(pos);
    if (name.empty() || !cfg().enableAmbientDiscs) {
        if (it != gJukeboxes.end()) {
            stopNear(pos, "lepton.ambient." + it->second.name);
            log().info("gadgets: ambience disc {} stopped at {}", it->second.name, pos.key());
            gJukeboxes.erase(it);
        }
        return;
    }
    if (it == gJukeboxes.end() || it->second.name != name) {
        if (it != gJukeboxes.end()) stopNear(pos, "lepton.ambient." + it->second.name);
        gJukeboxes[pos] = Playing{name, now()}; // the jukebox itself starts the first loop
        log().info("gadgets: ambience disc {} playing at {}", name, pos.key());
        return;
    }
    auto& p = it->second;
    if (now() - p.started >= static_cast<uint64_t>(kLoopTicks) * (p.replays + 1)) {
        ++p.replays;
        float vol = static_cast<float>(cfg().volume);
        playSoundNear(pos.dim, "lepton.ambient." + name, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, vol, 1.0f, 16.0f * std::max(1.0f, vol));
        if (p.replays == 1 || p.replays % 50 == 0) log().info("gadgets: ambience disc {} looped at {} ({})", name, pos.key(), p.replays);
    }
}

LL_TYPE_INSTANCE_HOOK(JukeboxTickHook, ll::memory::HookPriority::Normal, JukeboxBlockActor, &JukeboxBlockActor::$tick, void, BlockSource& region) {
    origin(region);
    if (region.getLevel().isClientSide()) return;
    try {
        jukeboxTick(*this, region);
    } catch (...) {}
}

// ---- Endermosh in End City treasure ------------------------------------------------------------------------

thread_local LootTable const* gFilling = nullptr;

bool isEndCity(LootTable const& t) { return t.mDir->find("end_city_treasure") != std::string::npos; }

LL_TYPE_INSTANCE_HOOK(LootFillHook, ll::memory::HookPriority::Normal, LootTable, &LootTable::fill, void, Container& container, Random& random, LootTableContext& context) {
    auto const& c      = cfg();
    bool        mine   = c.enableEndermoshMusicDisc && c.addToEndCityLoot && isEndCity(*this);
    auto const* before = gFilling;
    if (mine) gFilling = this;
    origin(container, random, context);
    gFilling = before;
}

LL_TYPE_INSTANCE_HOOK(
    LootPoolHook,
    ll::memory::HookPriority::Normal,
    LootPool,
    &LootPool::addRandomItems,
    void,
    std::vector<ItemStack>& result,
    Random&                 random,
    LootTableContext&       context
) {
    auto const* table = gFilling;
    if (!table || table->mPools->empty() || table->mPools->front().get() != this) {
        origin(result, random, context);
        return;
    }
    size_t first = result.size();
    origin(result, random, context);
    auto const& c = cfg();
    int  weight = std::max(0, c.lootWeight + static_cast<int>(c.lootQuality * context.mLuck));
    int  total  = 0;
    for (auto const& e : *mEntries)
        if (e) total += std::max(0, e->mWeight + static_cast<int>(e->mQuality * context.mLuck));
    if (weight <= 0) return;
    std::uniform_int_distribution<int> roll(0, total + weight - 1);
    for (size_t i = first; i < result.size(); ++i) {
        if (roll(rng()) < weight) {
            result[i] = game::makeItem(kEndermosh);
            log().info("gadgets: Endermosh added to an End City chest");
        }
    }
}

// ---- Endermosh during the dragon fight ---------------------------------------------------------------------

struct Fight {
    int  delay   = 0;
    bool playing = false;
};
std::unordered_map<int64_t, Fight> gFights;

void tickDragonMusic() {
    auto const& c = cfg();
    if (!c.enableEndermoshMusicDisc || !c.playEndermoshDuringEnderdragonFight) {
        gFights.clear();
        return;
    }
    if (now() % 10 != 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    bool                 dragon = false;
    bool                 checked = false;
    std::vector<int64_t> seen;
    level->forEachPlayer([&](Player& p) {
        auto  id    = p.getOrCreateUniqueID().rawID;
        auto& f     = gFights[id];
        bool  inEnd = static_cast<int>(p.getDimensionId()) == 2;
        if (inEnd && !checked) {
            dragon  = !game::entities::ofType("minecraft:ender_dragon", 2).empty();
            checked = true;
        }
        seen.push_back(id);
        if (inEnd && dragon) {
            auto pos = p.getPosition();
            if (f.delay == 50 && !f.playing) {
                f.playing = true;
                playSoundTo(p, "lepton.music.endermosh_fight", pos, 1.0f, 1.0f);
                actionBar(p, trFor(p, "lp.gadgets.now_playing", "Now Playing: {}", trFor(p, "lp.gadgets.music_disc", "Music Disc")));
                log().info("gadgets: Endermosh plays for {} (dragon fight)", p.getRealName());
            }
            if (pos.x * pos.x + pos.z * pos.z < 3000 && f.delay < 50) f.delay = std::min(50, f.delay + 10);
        } else if (f.playing || f.delay) {
            if (f.playing) stopSoundTo(p, "lepton.music.endermosh_fight");
            f = {};
        }
        return true;
    });
    std::erase_if(gFights, [&](auto const& kv) { return std::find(seen.begin(), seen.end(), kv.first) == seen.end(); });
}

} // namespace

ItemStack randomAmbientDisc() {
    constexpr int n = static_cast<int>(std::size(kAmbientNames));
    auto          i = std::uniform_int_distribution<int>(0, n - 1)(rng());
    return makeDisc(kAmbientNames[i], nullptr);
}

ItemStack makeDisc(std::string const& name, Player const* player) {
    auto s = game::makeItem("lepton:music_disc_" + name);
    if (game::isEmpty(s)) return s;
    auto key = "item.quark.music_disc_" + name + ".desc";
    s.setCustomLore({"§7" + (player ? trFor(*player, key, name) : tr(key, name))});
    return s;
}

bool isTracked(core::GlobalPos const& pos, std::string* name) {
    auto it = gJukeboxes.find(pos);
    if (it == gJukeboxes.end()) return false;
    if (name) *name = it->second.name;
    return true;
}

int replaysAt(core::GlobalPos const& pos) {
    auto it = gJukeboxes.find(pos);
    return it == gJukeboxes.end() ? -1 : it->second.replays;
}

void ageForTest(core::GlobalPos const& pos, int ticks) {
    auto it = gJukeboxes.find(pos);
    if (it == gJukeboxes.end()) return;
    it->second.started = it->second.started > static_cast<uint64_t>(ticks) ? it->second.started - ticks : 0;
}

void install() {
    gDie = ll::event::EventBus::getInstance().emplaceListener<ll::event::MobDieEvent>(onMobDie);
    JukeboxTickHook::hook();
    LootFillHook::hook();
    LootPoolHook::hook();
}

void uninstall() {
    if (gDie) ll::event::EventBus::getInstance().removeListener(gDie);
    gDie.reset();
    JukeboxTickHook::unhook();
    LootFillHook::unhook();
    LootPoolHook::unhook();
    gJukeboxes.clear();
    gFights.clear();
}

void tick() {
    tickDragonMusic();
    // forget jukeboxes that stopped ticking (chunk unloaded, block broken)
    if (now() % 100 == 0) {
        std::erase_if(gJukeboxes, [](auto const& kv) {
            auto* r = game::regionOf(kv.first.dim);
            if (r && game::blockIdAt(*r, game::blockPos(kv.first)) == "minecraft:jukebox") return false;
            stopNear(kv.first, "lepton.ambient." + kv.second.name);
            return true;
        });
    }
}

void onWorldClosed() {
    gJukeboxes.clear();
    gFights.clear();
}

} // namespace lp::feature::gadgets::discs
