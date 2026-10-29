// Skull pikes (Quark SkullPikesModule): a trophy skull on a fence scares simple monsters away.

#include "feature/throwables/Throwables.h"

#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/actor/ai/util/RandomPos.h"
#include "mc/world/actor/ai/util/SolidityCheckType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/pathfinder/Path.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/phys/HitResult.h"

#include "core/GlobalPos.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace lp::feature::throwables::skullpikes {

namespace ent = game::entities;

namespace {

constexpr char const* kDbKey = "lp:throwables:pikes";

struct State {
    std::set<core::GlobalPos>           pikes;
    std::vector<core::GlobalPos>        pending;
    std::map<int64_t, uint64_t>         fleeing; ///< mob -> tick its current flight was planned
    uint64_t                            tick  = 0;
    bool                                dirty = false;
};
State& st() {
    static State s;
    return s;
}

bool isTrophy(Block const& b) {
    auto const& id = b.getTypeName();
    return id == "minecraft:skeleton_skull" || id == "minecraft:zombie_head" || id == "minecraft:creeper_head"
        || b.hasTag(HashedString("lepton:pike_trophy"));
}

bool isPike(BlockSource& region, BlockPos const& pos) {
    if (!isTrophy(region.getBlock(pos))) return false;
    BlockPos below{pos.x, pos.y - 1, pos.z};
    return isFenceBlock(region.getBlock(below).getTypeName(), region, below);
}

/// Java Monster minus raiders, the warden and the wither (and Lepton's own monsters).
bool fooled(Actor& a) {
    static std::set<std::string> const ids{
        "minecraft:zombie", "minecraft:husk", "minecraft:drowned", "minecraft:zombie_villager",
        "minecraft:zombie_villager_v2", "minecraft:zombie_pigman", "minecraft:skeleton", "minecraft:stray",
        "minecraft:bogged", "minecraft:wither_skeleton", "minecraft:creeper", "minecraft:spider",
        "minecraft:cave_spider", "minecraft:enderman", "minecraft:endermite", "minecraft:silverfish",
        "minecraft:blaze", "minecraft:guardian", "minecraft:elder_guardian", "minecraft:vex",
        "minecraft:piglin", "minecraft:piglin_brute", "minecraft:zoglin", "minecraft:breeze",
        "minecraft:creaking", "minecraft:parched",
    };
    auto type = a.getTypeName();
    if (ids.contains(type)) return true;
    return type.starts_with("lepton:") && a.hasFamily(HashedString("monster"));
}

Vec3 center(core::GlobalPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

bool visible(BlockSource& region, Vec3 from, Actor& mob) {
    Vec3 to  = mob.getEyePos();
    auto hit = region.clip(
        from, to, false, ShapeType::Collision, 64, false, false, nullptr,
        [](BlockSource const&, Block const& b, bool) { return !isTrophy(b); }, false
    );
    return hit.mType != HitResultType::Tile;
}

double dist(Vec3 a, Vec3 b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void flee(Mob& mob, Vec3 const& pike) {
    auto pos = RandomPos::getPosAvoid(mob, 16, 7, pike, SolidityCheckType::CheckCollisionShape, 10);
    if (!pos) return;
    if (dist(pike, *pos) < dist(pike, mob.getPosition())) return;
    auto nav = mob.getEntityContext().tryGetComponent<NavigationComponent>();
    if (!nav || !nav->mNavigation.get()) return;
    auto path = nav->mNavigation.get()->createPath(*nav, mob, *pos);
    if (!path) return;
    float speed = dist(pike, mob.getPosition()) < 7.0 ? 1.2f : 1.0f;
    nav->moveTo(mob, std::move(path), speed);
    st().fleeing[ent::idOf(mob)] = st().tick;
}

void scare() {
    auto& s     = st();
    double range = cfg().pikeRange;
    // closest visible pike per mob
    std::map<int64_t, std::pair<double, Vec3>> best;
    std::map<int64_t, Mob*>                     mobs;
    for (auto const& p : s.pikes) {
        auto* region = game::regionOf(p.dim);
        if (!region) continue;
        BlockPos bp = game::blockPos(p);
        if (!region->hasChunksAt(bp, 0, false)) continue;
        Vec3 c = center(p);
        Vec3 eye{c.x, c.y + 0.4f, c.z};
        AABB box{Vec3{c.x - float(range), c.y - 3.0f, c.z - float(range)}, Vec3{c.x + float(range), c.y + 3.0f, c.z + float(range)}};
        for (auto* a : ent::inBox(*region, box)) {
            if (!a || !a->isAlive() || !a->hasCategory(ActorCategory::Mob) || !fooled(*a)) continue;
            if (a->mTargetId->rawID != -1 && a->mTargetId->rawID != 0) continue; // busy attacking
            double d = dist(c, a->getPosition());
            if (d > range || !visible(*region, eye, *a)) continue;
            auto id = ent::idOf(*a);
            if (!best.contains(id) || d < best[id].first) {
                best[id] = {d, c};
                mobs[id] = static_cast<Mob*>(a);
            }
        }
    }
    for (auto& [id, b] : best) {
        auto it = s.fleeing.find(id);
        if (it != s.fleeing.end() && s.tick - it->second < 40) continue; // still running
        flee(*mobs[id], b.second);
    }
    std::erase_if(s.fleeing, [&](auto const& kv) { return s.tick - kv.second > 200; });
}

} // namespace

int pikeCount() { return static_cast<int>(st().pikes.size()); }

void scareNow() { scare(); }

void onPlaced(Player& player, BlockPos const& pos) {
    if (!cfg().enableSkullPikes) return;
    st().pending.push_back(game::globalPos(player.getDimensionBlockSource(), pos));
}

void tick() {
    auto& s = st();
    ++s.tick;
    if (!cfg().enableSkullPikes) return;
    for (auto const& p : s.pending) {
        auto* region = game::regionOf(p.dim);
        if (region && isPike(*region, game::blockPos(p))) {
            s.pikes.insert(p);
            s.dirty = true;
            log().info("throwables: skull pike at {} {} {}", p.x, p.y, p.z);
        }
    }
    s.pending.clear();
    std::vector<core::GlobalPos> gone;
    for (auto const& p : s.pikes) {
        auto* region = game::regionOf(p.dim);
        if (!region) continue;
        BlockPos bp = game::blockPos(p);
        if (!region->hasChunksAt(bp, 0, false)) continue;
        if (!isPike(*region, bp)) {
            gone.push_back(p);
            continue;
        }
        if (randomDouble() < 0.4) {
            Vec3 c = center(p);
            particle(randomDouble() < 0.05 ? "lepton:pike_spore" : "lepton:pike_ash", Vec3{c.x, c.y - 0.25f, c.z}, p.dim);
        }
    }
    for (auto const& p : gone) {
        s.pikes.erase(p);
        s.dirty = true;
    }
    if (s.tick % 10 == 0) scare();
}

void onWorldReady() {
    st() = State{};
    auto v = game::db::load(kDbKey);
    if (!v || !v->is_array()) return;
    for (auto const& j : *v)
        if (j.is_array() && j.size() == 4)
            st().pikes.insert(core::GlobalPos{j[0].get<int>(), j[1].get<int>(), j[2].get<int>(), j[3].get<int>()});
}

void onWorldSave() {
    if (!game::db::available() || !st().dirty) return;
    auto arr = nlohmann::json::array();
    for (auto const& p : st().pikes) arr.push_back({p.dim, p.x, p.y, p.z});
    game::db::save(kDbKey, arr);
    st().dirty = false;
}

void onWorldClosed() { st() = State{}; }

} // namespace lp::feature::throwables::skullpikes

namespace lp::feature::throwables::skullpikes {
int fleeingCount() { return static_cast<int>(st().fleeing.size()); }

bool isFleeing(int64_t mobId) { return st().fleeing.contains(mobId); }
} // namespace lp::feature::throwables::skullpikes
