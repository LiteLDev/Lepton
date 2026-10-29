// MonsterBox runtime: the registry of boxes, activation by nearby players, the growl, the mobs and
// their extra loot (docs/spec/worldsurface.md §3 "Monster box").
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <tuple>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/service/Bedrock.h"
#include "nlohmann/json.hpp"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include "feature/worldsurface/Common.h"
#include "feature/worldsurface/Runtime.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace lp::feature::worldsurface::box {

namespace {

constexpr char const* kDbKey   = "lp:worldsurface:boxes";
constexpr char const* kBoxId   = "lepton:monster_box";
constexpr int         kBreakAt = 40; // Java: breakProgress > 40

std::mutex                       gPendingMutex;
std::vector<core::GlobalPos>     gPending;
std::set<core::GlobalPos>        gBoxes;
/// gBoxes by (dim, chunk x, chunk z): the tick only looks at the chunks around players.
std::map<std::tuple<int, int, int>, std::set<core::GlobalPos>> gByChunk;
std::map<core::GlobalPos, int>   gProgress;
bool                             gDirty  = false;
bool                             gLoaded = false;
std::vector<std::pair<std::string, int>> gSpawns;
std::vector<ll::event::ListenerPtr>      gListeners;

Rng& rng() {
    static Rng r(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return r;
}

std::vector<std::pair<std::string, int>> defaultSpawns() {
    return {{"minecraft:witch", 1}, {"minecraft:cave_spider", 2}, {"minecraft:zombie", 7}};
}

/// Reads behavior_packs/Lepton_BP/loot_tables/lepton/monster_box_spawns.json (spawn eggs + weights).
void loadSpawnTable() {
    gSpawns.clear();
    try {
        auto file = config::configDir().parent_path() / "behavior_packs" / "Lepton_BP" / "loot_tables" / "lepton"
                  / "monster_box_spawns.json";
        std::ifstream in(file);
        if (in) {
            auto j = nlohmann::json::parse(in, nullptr, true, true);
            for (auto const& pool : j.value("pools", nlohmann::json::array()))
                for (auto const& e : pool.value("entries", nlohmann::json::array())) {
                    std::string name   = e.value("name", "");
                    int         weight = e.value("weight", 1);
                    if (name.ends_with("_spawn_egg") && weight > 0)
                        gSpawns.emplace_back(name.substr(0, name.size() - 10), weight);
                }
        }
    } catch (std::exception const& e) {
        log().warn("worldsurface: monster box spawn table: {}", e.what());
    }
    if (gSpawns.empty()) gSpawns = defaultSpawns();
}

bool isBox(Block const& b) { return nameOf(b) == kBoxId; }

std::tuple<int, int, int> chunkKey(core::GlobalPos const& p) { return {p.dim, p.x >> 4, p.z >> 4}; }

bool addBox(core::GlobalPos const& p) {
    if (!gBoxes.insert(p).second) return false;
    gByChunk[chunkKey(p)].insert(p);
    gDirty = true;
    return true;
}

void removeBox(core::GlobalPos const& p) {
    gBoxes.erase(p);
    gProgress.erase(p);
    if (auto it = gByChunk.find(chunkKey(p)); it != gByChunk.end()) {
        it->second.erase(p);
        if (it->second.empty()) gByChunk.erase(it);
    }
    gDirty = true;
}

void clearBoxes() {
    gBoxes.clear();
    gByChunk.clear();
    gProgress.clear();
}

Vec3 center(BlockPos const& p) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f};
}

void drainPending() {
    std::vector<core::GlobalPos> pending;
    {
        std::lock_guard lock(gPendingMutex);
        pending.swap(gPending);
    }
    for (auto const& p : pending) addBox(p);
}

void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !isBox(ev.placedBlock())) return;
    auto pos = ev.pos();
    int  dim = static_cast<int>(player.getDimensionId());
    // the block is in the world on the next tick
    later(1, [dim, pos] {
        auto* r = game::regionOf(dim);
        if (r && r->hasChunksAt(pos, 0, false) && isBox(r->getBlock(pos))) registerBox(game::globalPos(dim, pos));
    });
}

void onMobDie(ll::event::MobDieEvent& ev) {
    auto& mob = ev.self();
    if (mob.isClientSide() || !cfg().enableMonsterBox || !cfg().enableExtraLootTable) return;
    if (!mob.hasTag(kSpawnedTag)) return;
    if (mob.mLastHurtByPlayerTime <= 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    GameRuleId rule;
    rule.mValue = static_cast<int>(GameRules::GameRulesIndex::DoMobLoot);
    if (!level->getGameRules().getBool(rule, true)) return;
    auto const& p   = mob.getPosition();
    int         dim = static_cast<int>(mob.getDimensionId());
    runCommand(std::format("loot spawn {:.2f} {:.2f} {:.2f} loot \"lepton/monster_box\"", p.x, p.y, p.z), dim);
    log().info("worldsurface: monster box mob dropped its extra loot");
}

} // namespace

std::vector<std::pair<std::string, int>> const& spawnTable() {
    if (gSpawns.empty()) loadSpawnTable();
    return gSpawns;
}

void queueGenerated(int dim, std::vector<BlockPos> const& positions) {
    if (positions.empty()) return;
    std::lock_guard lock(gPendingMutex);
    for (auto const& p : positions) gPending.push_back(core::GlobalPos{dim, p.x, p.y, p.z});
}

void registerBox(core::GlobalPos const& pos) { addBox(pos); }

std::set<core::GlobalPos> const& registry() {
    drainPending();
    return gBoxes;
}

int progress(core::GlobalPos const& pos) {
    auto it = gProgress.find(pos);
    return it == gProgress.end() ? 0 : it->second;
}

int spawnMobs(BlockSource& region, BlockPos const& pos, Rng& r) {
    auto const& c     = cfg();
    auto const& table = spawnTable();
    int         total = 0;
    for (auto const& [_, w] : table) total += w;
    if (total <= 0) return 0;
    int count   = c.minMobCount + r.nextInt(std::max(c.maxMobCount - c.minMobCount + 1, 1));
    int spawned = 0;
    for (int i = 0; i < count; ++i) {
        int roll = r.nextInt(total);
        std::string const* type = nullptr;
        for (auto const& [id, w] : table) {
            if (roll < w) {
                type = &id;
                break;
            }
            roll -= w;
        }
        if (!type) continue;
        Actor* a = game::entities::spawn(region, *type, center(pos));
        if (!a) continue;
        constexpr double motion = 0.4;
        Vec3             v{
            static_cast<float>((r.nextFloat() - 0.5) * motion),
            static_cast<float>((r.nextFloat() - 0.5) * motion),
            static_cast<float>((r.nextFloat() - 0.5) * motion)
        };
        const_cast<Vec3&>(a->getPosDelta()) = v;
        a->addTag(kSpawnedTag);
        ++spawned;
    }
    return spawned;
}

void tick() {
    drainPending();
    auto level = ll::service::getLevel();
    if (!level || !cfg().enableMonsterBox) return;
    if (level->getDifficulty() == SharedTypes::Legacy::Difficulty::Peaceful) return;

    struct P {
        int  dim;
        Vec3 pos;
        bool spectator;
    };
    std::vector<P> players;
    level->forEachPlayer([&](Player& p) {
        players.push_back({static_cast<int>(p.getDimensionId()), p.getPosition(), p.isSpectator()});
        return true;
    });
    if (players.empty()) return;

    // boxes that never reached the registry (other tools, lost saves): scan around players
    if (tickCount() % 10 == 0) {
        for (auto const& p : players) {
            auto* r = game::regionOf(p.dim);
            if (!r) continue;
            BlockPos base = game::blockPosOf(p.pos);
            for (int dx = -3; dx <= 3; ++dx)
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dz = -3; dz <= 3; ++dz) {
                        BlockPos q{base.x + dx, base.y + dy, base.z + dz};
                        if (q.y < r->getMinHeight() || q.y >= r->getMaxHeight() || !r->hasChunksAt(q, 0, false)) continue;
                        if (isBox(r->getBlock(q))) registerBox(game::globalPos(p.dim, q));
                    }
        }
    }

    // the boxes in the chunks around players (48 blocks) and the ones already breaking
    std::set<core::GlobalPos> candidates;
    for (auto const& [gp, _] : gProgress) candidates.insert(gp);
    for (auto const& p : players) {
        int cx = static_cast<int>(std::floor(p.pos.x)) >> 4, cz = static_cast<int>(std::floor(p.pos.z)) >> 4;
        for (int dx = -3; dx <= 3; ++dx)
            for (int dz = -3; dz <= 3; ++dz)
                if (auto it = gByChunk.find({p.dim, cx + dx, cz + dz}); it != gByChunk.end())
                    candidates.insert(it->second.begin(), it->second.end());
    }

    double range2 = cfg().activationRange * cfg().activationRange;
    for (auto const& gp : candidates) {
        BlockPos    pos  = game::blockPos(gp);
        Vec3        c    = center(pos);
        double      near = 1e18;
        bool        activate = false;
        for (auto const& p : players) {
            if (p.dim != gp.dim) continue;
            double dx = p.pos.x - c.x, dy = p.pos.y - c.y, dz = p.pos.z - c.z;
            double d2 = dx * dx + dy * dy + dz * dz;
            // Java measures from the player's feet position; Bedrock reports the eye position
            double fy = (p.pos.y - 1.62) - c.y;
            double f2 = dx * dx + fy * fy + dz * dz;
            near      = std::min(near, d2);
            if (f2 < range2 && !p.spectator) activate = true;
        }
        int prog = progress(gp);
        if (near > 48.0 * 48.0 && prog == 0) continue;
        auto* region = game::regionOf(gp.dim);
        if (!region || !region->hasChunksAt(pos, 0, false)) continue;
        auto const& b = region->getBlock(pos);
        if (!isBox(b)) {
            removeBox(gp);
            continue;
        }
        if (near < 32.0 * 32.0) {
            if (auto dim = level->getDimension(gp.dim).lock()) {
                Vec3 at{
                    static_cast<float>(pos.x) + rng().nextFloat(),
                    static_cast<float>(pos.y) + rng().nextFloat(),
                    static_cast<float>(pos.z) + rng().nextFloat()
                };
                level->spawnParticleEffect(
                    prog == 0 ? "minecraft:basic_flame_particle" : "minecraft:basic_smoke_particle",
                    at,
                    dim.get()
                );
            }
        }
        if (prog == 0 && !activate) continue;
        if (prog == 0) {
            runCommand(std::format("playsound lepton.monster_box.growl @a {} {} {} 0.5 1", pos.x, pos.y, pos.z), gp.dim);
            log().info("worldsurface: monster box at {} {} {} activated", pos.x, pos.y, pos.z);
        }
        ++prog;
        if (prog > kBreakAt) {
            int n = spawnMobs(*region, pos, rng());
            level->broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock, c, static_cast<int>(b.mNetworkId), nullptr);
            game::clearBlock(*region, pos);
            log().info("worldsurface: monster box at {} {} {} broke and spawned {} mobs", pos.x, pos.y, pos.z, n);
            removeBox(gp);
            continue;
        }
        gProgress[gp] = prog;
    }
}

void onWorldReady() {
    clearBoxes();
    loadSpawnTable();
    if (auto j = game::db::load(kDbKey); j && j->is_array()) {
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 4 && e[0].is_number_integer() && e[1].is_number_integer()
                && e[2].is_number_integer() && e[3].is_number_integer())
                addBox(core::GlobalPos{e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    }
    gDirty  = false;
    gLoaded = true;
}

void onWorldSave() {
    drainPending();
    if (!gDirty || !gLoaded) return;
    auto arr = nlohmann::json::array();
    for (auto const& p : gBoxes) arr.push_back({p.dim, p.x, p.y, p.z});
    game::db::save(kDbKey, arr);
    gDirty = false;
}

void onWorldClosed() {
    clearBoxes();
    gDirty  = false;
    gLoaded = false;
    std::lock_guard lock(gPendingMutex);
    gPending.clear();
}

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced));
    gListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>(onMobDie));
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
}

} // namespace lp::feature::worldsurface::box
