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
#include "mc/world/item/components/RecordItemComponent.h"
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
    // Java spawnAtLocation: an item entity at the mob. Spawned on the next tick: item entities added
    // while the mob dies are lost.
    auto at  = mob.getPosition();
    int  dim = static_cast<int>(mob.getDimensionId());
    later(1, [at, dim, disc, type] {
        auto  level = ll::service::getLevel();
        auto* r     = game::regionOf(dim);
        if (!level || !r) return;
        auto* item = level->getSpawner().spawnItem(*r, disc, nullptr, at, 10);
        log().info("gadgets: a skeleton killed a {}: dropped {}{}", type, game::itemId(disc), item ? "" : " (failed)");
    });
}

// ---- jukeboxes ------------------------------------------------------------------------------------------
// Bedrock record items only accept vanilla sound events, so the discs declare `record.null` and Lepton
// plays their sound itself while the disc is in a jukebox: ambience loops every 24 s (the length of
// the generated loops, Quark plays them as looping sounds), Endermosh once.

constexpr int kLoopTicks = 24 * 20;

struct Playing {
    std::string disc;    ///< "rain", "endermosh"
    int64_t     started; ///< tick the record started (may be < 0 in tests)
    int         replays = 0;
};
std::map<core::GlobalPos, Playing> gJukeboxes;

std::string soundOf(std::string const& disc) {
    return disc == "endermosh" ? "lepton.music.endermosh" : "lepton.ambient." + disc;
}

void stopNear(core::GlobalPos const& pos, std::string const& sound) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) == pos.dim && p.getPosition().distanceTo(at) <= 96) stopSoundTo(p, sound);
        return true;
    });
}

void play(core::GlobalPos const& pos, std::string const& disc) {
    Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    if (disc == "endermosh") {
        playSoundNear(pos.dim, soundOf(disc), at, 4.0f, 1.0f, 64.0f); // Java jukebox songs: volume 4, 64 blocks
        return;
    }
    float vol = static_cast<float>(cfg().volume); // Java: linear attenuation over volume * 16 blocks
    playSoundNear(pos.dim, soundOf(disc), at, vol, 1.0f, 16.0f * std::max(1.0f, vol));
}

/// The disc a jukebox should play ("" for none or a disabled module).
std::string discOf(ItemStackBase const& record) {
    auto id = game::itemId(record);
    if (auto n = ambientNameOf(id); !n.empty()) return cfg().enableAmbientDiscs ? n : std::string();
    if (id == kEndermosh) return cfg().enableEndermoshMusicDisc ? "endermosh" : std::string();
    return {};
}

/// Starts, loops or stops the disc sound of the jukebox at `pos` (record = its current record).
void update(core::GlobalPos const& pos, ItemStackBase const& record) {
    auto disc = discOf(record);
    auto it   = gJukeboxes.find(pos);
    if (it != gJukeboxes.end() && it->second.disc != disc) {
        stopNear(pos, soundOf(it->second.disc));
        log().info("gadgets: disc {} stopped at {} (record now \"{}\")", it->second.disc, pos.key(), game::itemId(record));
        gJukeboxes.erase(it);
        it = gJukeboxes.end();
    }
    if (disc.empty()) return;
    if (it == gJukeboxes.end()) {
        gJukeboxes[pos] = Playing{disc, static_cast<int64_t>(now())};
        play(pos, disc);
        log().info("gadgets: disc {} playing at {}", disc, pos.key());
        return;
    }
    auto& p = it->second;
    if (disc == "endermosh") return;
    if (static_cast<int64_t>(now()) - p.started >= static_cast<int64_t>(kLoopTicks) * (p.replays + 1)) {
        ++p.replays;
        play(pos, disc);
        if (p.replays == 1 || p.replays % 50 == 0) log().info("gadgets: ambience disc {} looped at {} ({})", disc, pos.key(), p.replays);
    }
}

// Jukebox block actors do not tick on their own while idle and know neither their dimension nor
// their region: setRecord / load report the actor, the server tick finds its dimension by comparing
// block actor pointers (never dereferencing a stale one) and then follows it by position.
struct Pending {
    void const* actor;
    BlockPos    pos;
    uint64_t    at;
};
std::vector<Pending> gPending;

void note(JukeboxBlockActor const& jb) {
    if (gPending.size() > 256) return;
    gPending.push_back({&jb, jb.getPosition(), now()});
}

JukeboxBlockActor* jukeboxAt(BlockSource& region, BlockPos const& pos) {
    if (game::blockIdAt(region, pos) != "minecraft:jukebox") return nullptr;
    return static_cast<JukeboxBlockActor*>(region.getBlockEntity(pos));
}

void resolvePending() {
    if (gPending.empty()) return;
    std::vector<Pending> keep;
    for (auto const& p : gPending) {
        bool done = false;
        for (int dim = 0; dim < 3 && !done; ++dim) {
            auto* r = game::regionOf(dim);
            if (!r || !r->hasChunksAt(p.pos, 0, false) || r->getBlockEntity(p.pos) != p.actor) continue;
            if (auto* jb = jukeboxAt(*r, p.pos)) update(game::globalPos(dim, p.pos), *jb->mRecord);
            done = true;
        }
        if (!done && now() - p.at < 40) keep.push_back(p);
    }
    gPending.swap(keep);
}

// The discs declare the vanilla record.13 sound event (the only kind a record item accepts): the
// jukebox's own playback is skipped for them and Lepton plays the real sound.
LL_TYPE_INSTANCE_HOOK(
    JukeboxSoundHook,
    ll::memory::HookPriority::Normal,
    JukeboxBlockActor,
    &JukeboxBlockActor::_startRecordSound,
    void,
    BlockSource&               region,
    RecordItemComponent const& record
) {
    auto id = game::itemId(*mRecord);
    if (id.starts_with("lepton:music_disc_")) {
        if (!region.getLevel().isClientSide()) {
            log().info("gadgets: jukebox playback of {} replaced", id);
            update(game::globalPos(region, getPosition()), *mRecord);
        }
        return;
    }
    origin(region, record);
}

LL_TYPE_INSTANCE_HOOK(JukeboxStopHook, ll::memory::HookPriority::Normal, JukeboxBlockActor, &JukeboxBlockActor::stopPlayingRecord, void, BlockSource& region) {
    origin(region);
    if (!region.getLevel().isClientSide()) update(game::globalPos(region, getPosition()), *mRecord);
}

LL_TYPE_INSTANCE_HOOK(JukeboxSetHook, ll::memory::HookPriority::Normal, JukeboxBlockActor, &JukeboxBlockActor::setRecord, void, ItemStack const& record, bool start) {
    origin(record, start);
    note(*this);
}

LL_TYPE_INSTANCE_HOOK(
    JukeboxLoadHook,
    ll::memory::HookPriority::Normal,
    JukeboxBlockActor,
    &JukeboxBlockActor::$load,
    void,
    ILevel&             level,
    CompoundTag const&  tag,
    DataLoadHelper&     helper
) {
    origin(level, tag, helper);
    if (!discOf(*mRecord).empty()) note(*this);
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
    if (name) *name = it->second.disc;
    return true;
}

int replaysAt(core::GlobalPos const& pos) {
    auto it = gJukeboxes.find(pos);
    return it == gJukeboxes.end() ? -1 : it->second.replays;
}

void ageForTest(core::GlobalPos const& pos, int ticks) {
    auto it = gJukeboxes.find(pos);
    if (it == gJukeboxes.end()) return;
    it->second.started -= ticks;
}

void install() {
    gDie = ll::event::EventBus::getInstance().emplaceListener<ll::event::MobDieEvent>(onMobDie);
    JukeboxSetHook::hook();
    JukeboxSoundHook::hook();
    JukeboxStopHook::hook();
    JukeboxLoadHook::hook();

    LootFillHook::hook();
    LootPoolHook::hook();
}

void uninstall() {
    if (gDie) ll::event::EventBus::getInstance().removeListener(gDie);
    gDie.reset();
    JukeboxSetHook::unhook();
    JukeboxSoundHook::unhook();
    JukeboxStopHook::unhook();
    JukeboxLoadHook::unhook();
    gPending.clear();

    LootFillHook::unhook();
    LootPoolHook::unhook();
    gJukeboxes.clear();
    gFights.clear();
}

void tick() {
    tickDragonMusic();
    resolvePending();
    if (gJukeboxes.empty()) return;
    std::vector<std::pair<core::GlobalPos, ItemStack>> seen;
    std::vector<core::GlobalPos>                       gone;
    for (auto const& [pos, p] : gJukeboxes) {
        auto* r  = game::regionOf(pos.dim);
        auto  bp = game::blockPos(pos);
        if (!r || !r->hasChunksAt(bp, 0, false)) {
            gone.push_back(pos); // unloaded: picked up again by load()
            continue;
        }
        auto* jb = jukeboxAt(*r, bp);
        seen.emplace_back(pos, jb ? ItemStack(*jb->mRecord) : ItemStack{});
    }
    for (auto const& pos : gone) gJukeboxes.erase(pos);
    for (auto const& [pos, record] : seen) update(pos, record);
}

void onWorldClosed() {
    gJukeboxes.clear();
    gFights.clear();
    gPending.clear();
}

} // namespace lp::feature::gadgets::discs
