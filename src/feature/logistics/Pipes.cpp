// Pipes (Quark oddities/PipesModule): copper pipes and encased copper pipes move items put in by
// hoppers / droppers / chutes or picked up from item entities at open ends. Items prefer to fall
// down, then go straight, then a random side, and only then up; a full target sends them back.
// Open ends shoot items out. Redstone disables a pipe. Too many items break it.
//
// Pipes have no block entity on Bedrock: the travelling items live here (world DB
// `lp:logistics:pipe:<pos>`), the connection kinds are block states (`lepton:conn_<face>`), and
// the items are shown by `lepton:pipe_item` display entities.

#include <algorithm>
#include <map>
#include <random>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/events/gameevents/GameEventRegistry.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "feature/logistics/Logistics.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::logistics {
namespace pipes {

using core::GlobalPos;

/// Quark's PipeBlockEntity.ConnectionType (state value = index). Quark's TERMINAL_OFFSET (a flare drawn
/// 1 px further out for containers whose box does not reach the face) is shown as TERMINAL: a block
/// has at most 16 bits of states and 6 faces x 5 kinds would need 18.
enum Conn : int { None = 0, Pipe = 1, Opening = 2, Terminal = 3 };
constexpr bool isSolid(int c) { return c == Pipe || c == Terminal; }
constexpr bool isFlared(int c) { return c == Opening || c == Terminal; }

std::string stateName(int face) { return std::string("lepton:conn_") + kFaceNames[static_cast<size_t>(face)]; }

struct Item {
    ItemStack stack;
    int       ticks      = 0;
    int       in         = Up; ///< face it entered by
    int       out        = Up;
    int       backlog    = -1;
    uint64_t  seed       = 0;
    int       time       = 0;
    bool      valid      = true;
    int64_t   display    = 0; ///< display entity unique id (0 = none)
    int64_t   lastTick   = -1;
};

struct PipeData {
    std::vector<Item> items;
    std::vector<Item> queued;
    bool              iterating = false;
};

struct State {
    std::map<GlobalPos, PipeData> pipes;   ///< pipes holding items
    std::set<GlobalPos>           known;   ///< every pipe position seen (for open-end pickup)
    std::set<GlobalPos>           dirty;
    std::set<GlobalPos>           refresh; ///< connection updates for the next tick
    std::set<GlobalPos>           erasedKeys;
    bool                          loaded = false;
    int64_t                       tick   = 0;
    std::mt19937_64               rng{std::random_device{}()};
};
State& st() {
    static State s;
    return s;
}

std::string dbKey(GlobalPos const& p) { return "lp:logistics:pipe:" + p.key(); }
constexpr char const* kIndexKey = "lp:logistics:pipes";

int effectiveSpeed() { return std::max(2, cfg().pipeSpeed * 2); }

// ---- persistence ----------------------------------------------------------------------------------

nlohmann::json toJson(Item const& i) {
    return {
        {"item",    game::itemToSnbt(i.stack)},
        {"ticks",   i.ticks                  },
        {"in",      i.in                     },
        {"out",     i.out                    },
        {"backlog", i.backlog                },
        {"seed",    i.seed                   },
        {"time",    i.time                   },
        {"display", i.display                },
    };
}
Item fromJson(nlohmann::json const& j) {
    Item i;
    i.stack   = game::itemFromSnbt(j.value("item", std::string{}));
    i.ticks   = j.value("ticks", 0);
    i.in      = std::clamp(j.value("in", 1), 0, 5);
    i.out     = std::clamp(j.value("out", 1), 0, 5);
    i.backlog = std::clamp(j.value("backlog", -1), -1, 5);
    i.seed    = j.value("seed", uint64_t{0});
    i.time    = j.value("time", 0);
    i.display = j.value("display", int64_t{0});
    return i;
}

void ensureLoaded() {
    auto& s = st();
    if (s.loaded || !game::db::available()) return;
    s.loaded = true;
    if (auto j = game::db::load(kIndexKey); j && j->is_array())
        for (auto const& k : *j)
            if (k.is_string()) s.known.insert(parseKey(k.get<std::string>()));
    game::db::forEachKey("lp:logistics:pipe:", [&](std::string_view suffix) {
        auto pos = parseKey(suffix);
        auto j   = game::db::load(dbKey(pos));
        if (!j || !j->is_array()) return;
        PipeData d;
        for (auto const& e : *j) {
            auto it = fromJson(e);
            if (!game::isEmpty(it.stack)) d.items.push_back(std::move(it));
        }
        if (!d.items.empty()) s.pipes[pos] = std::move(d);
        s.known.insert(pos);
    });
}

void flush() {
    auto& s = st();
    if (!s.loaded) return;
    for (auto const& pos : s.dirty) {
        auto it = s.pipes.find(pos);
        if (it == s.pipes.end() || (it->second.items.empty() && it->second.queued.empty())) {
            game::db::erase(dbKey(pos));
            continue;
        }
        nlohmann::json arr = nlohmann::json::array();
        for (auto const& i : it->second.items) arr.push_back(toJson(i));
        for (auto const& i : it->second.queued) arr.push_back(toJson(i));
        game::db::save(dbKey(pos), arr);
    }
    s.dirty.clear();
    nlohmann::json idx = nlohmann::json::array();
    for (auto const& p : s.known) idx.push_back(p.key());
    game::db::save(kIndexKey, idx);
}

// ---- world helpers ----------------------------------------------------------------------------------

BlockSource* regionFor(GlobalPos const& p) {
    auto* r = game::regionOf(p.dim);
    if (!r || !r->hasChunksAt(game::blockPos(p), 0, false)) return nullptr;
    return r;
}

int connOf(Block const& b, int face) { return game::blockstates::getInt(b, stateName(face)).value_or(None); }

bool allowsFullConnection(std::string_view id, int conn) {
    if (id == kEncasedPipe) return isSolid(conn) || isFlared(conn);
    return isSolid(conn);
}

/// Quark's PipeBlockEntity#isPipeEnabled.
bool enabledAt(BlockSource& region, BlockPos const& pos) {
    return isPipe(game::blockIdAt(region, pos)) && !powered(region, pos);
}

/// A container (not a pipe) at `pos`: vanilla block entity or a Lepton port.
Container* terminalAt(BlockSource& region, BlockPos const& pos) {
    if (auto* c = portAt(region, pos)) return c;
    if (!region.getBlockEntity(pos)) return nullptr;
    return containerAt(region, pos);
}

int computeConnection(BlockSource& region, BlockPos const& pos, int face, bool recursed = false) {
    BlockPos n  = offset(pos, face);
    auto     id = game::blockIdAt(region, n);
    if (isPipe(id)) return Pipe;
    if (!id.empty() && id != "minecraft:air" && terminalAt(region, n)) return Terminal;
    if (!recursed) {
        int other = computeConnection(region, pos, opposite(face), true);
        if (isSolid(other)) {
            for (int d : kFaces) {
                if (axisOf(d) == axisOf(face)) continue;
                if (isSolid(computeConnection(region, pos, d, true))) return None;
            }
            return Opening;
        }
    }
    return None;
}

} // namespace pipes

void pipes::refresh(BlockSource& region, BlockPos const& pos) {
    auto const& b  = region.getBlock(pos);
    auto        id = game::blockId(b);
    if (!isPipe(id)) return;
    game::blockstates::Changes changes;
    for (int f : kFaces) {
        int c = computeConnection(region, pos, f);
        if (connOf(b, f) != c) changes.emplace_back(stateName(f), c);
    }
    if (changes.empty()) return;
    if (auto const* n = game::blockstates::with(b, changes)) game::setBlock(region, pos, *n);
}

namespace pipes {

// ---- item display -----------------------------------------------------------------------------------

Vec3 displayPos(GlobalPos const& p, Item const& i) {
    float fract = static_cast<float>(i.ticks) / static_cast<float>(effectiveSpeed());
    float shift = fract - 0.5f;
    int   face  = fract < 0.5f ? opposite(i.in) : i.out;
    auto const& s = kFaceStep[static_cast<size_t>(face)];
    // the display entity's origin is its feet: lower it so the item sits at the pipe's centre
    return Vec3{p.x + 0.5f + s[0] * shift, p.y + 0.5f + s[1] * shift - 0.25f, p.z + 0.5f + s[2] * shift};
}

void removeDisplay(Item& i) {
    if (i.display == 0) return;
    if (auto* a = game::entities::find(i.display)) game::entities::despawn(*a);
    i.display = 0;
}

void updateDisplay(BlockSource& region, GlobalPos const& p, Item& i) {
    if (!cfg().renderPipeItems) {
        removeDisplay(i);
        return;
    }
    Vec3   at = displayPos(p, i);
    Actor* a  = i.display ? game::entities::find(i.display) : nullptr;
    if (!a) {
        a = game::entities::spawn(region, kPipeItem, at);
        if (!a) return;
        i.display = game::entities::idOf(*a);
        a->setCarriedItem(i.stack);
    }
    a->teleportTo(at, false, 0, 1, false);
    a->mBuiltInComponents->mActorRotationComponent->mRot = Vec2{0, static_cast<float>(i.time % 90) * 4.0f};
}

// ---- dropping / passing ------------------------------------------------------------------------------

void vibration(BlockSource& region, BlockPos const& pos, bool shoot) {
    if (!cfg().emitVibrations) return;
    auto const& ev = shoot ? GameEventRegistry::projectileShoot() : GameEventRegistry::projectileLand();
    region.postGameEvent(nullptr, ev, pos, &region.getBlock(pos));
}

/// Quark's PipeBlockEntity#dropItem.
void dropItem(BlockSource& region, GlobalPos const& p, ItemStack const& stack, int facing, bool playSound) {
    auto  bp = game::blockPos(p);
    float x = p.x + 0.5f, y = p.y + 0.25f, z = p.z + 0.5f;
    if (facing >= 0) {
        float shift   = allowsFullConnection(game::blockIdAt(region, bp), Opening) ? 0.7f : 0.4f;
        auto const& s = kFaceStep[static_cast<size_t>(facing)];
        x -= s[0] * shift;
        y -= s[1] * (shift + 0.15f);
        z -= s[2] * shift;
    }
    bool  shootOut = enabledAt(region, bp);
    float pitch    = shootOut ? 1.0f : 0.025f;
    if (playSound) {
        if (cfg().doPipesWhoosh) playSoundAt(region, Vec3{x, y, z}, "lepton.pipe.shoot", 1.0f, pitch);
        vibration(region, bp, true);
    }
    float vel = shootOut ? 0.5f : 0.125f;
    Vec3  motion{0, 0, 0};
    if (facing >= 0) {
        auto const& s = kFaceStep[static_cast<size_t>(facing)];
        motion        = Vec3{-s[0] * vel, -s[1] * vel, -s[2] * vel};
    }
    spawnItemAt(region, stack, Vec3{x, y, z}, motion, true);
}

PipeData& dataAt(GlobalPos const& p) { return st().pipes[p]; }

/// Quark's PipeBlockEntity#passIn (with the display entity carried along).
void passInItem(GlobalPos const& p, Item item) {
    auto& d       = dataAt(p);
    item.ticks    = 0;
    item.out      = item.in;
    item.valid    = true;
    item.lastTick = st().tick;
    if (d.iterating) d.queued.push_back(std::move(item));
    else d.items.push_back(std::move(item));
    st().dirty.insert(p);
    st().known.insert(p);
}

/// Quark's canFit.
bool canFit(BlockSource& region, ItemStack const& stack, BlockPos const& target, int face) {
    if (isPipe(game::blockIdAt(region, target))) return enabledAt(region, target);
    auto* c = terminalAt(region, target);
    if (!c) return false;
    ItemStack copy = stack;
    return insertThroughFace(*c, copy, face, true) > 0;
}

/// Quark's PipeItem#getTargetFace.
int targetFace(BlockSource& region, BlockPos const& pos, Item& i) {
    auto const& stack = i.stack;
    if (i.in != Down && i.backlog != Down && canFit(region, stack, offset(pos, Down), Up)) return Down;
    int inOpp = i.in;
    if (axisOf(i.in) != 0) {
        inOpp = opposite(i.in);
        if (inOpp != i.backlog && canFit(region, stack, offset(pos, inOpp), i.in)) return inOpp;
    }
    std::vector<int> sides{North, South, West, East};
    std::erase(sides, i.in);
    std::erase(sides, inOpp);
    std::mt19937_64 rng(i.seed);
    i.seed = rng();
    std::shuffle(sides.begin(), sides.end(), rng);
    for (int side : sides)
        if (side != i.backlog && canFit(region, stack, offset(pos, side), opposite(side))) return side;
    if (i.in != Up && i.backlog != Up && canFit(region, stack, offset(pos, Up), Down)) return Up;
    if (i.backlog >= 0) return i.backlog;
    return -1;
}

void bounceBack(GlobalPos const& p, Item const& item, ItemStack const* rest) {
    Item back    = item;
    back.stack   = rest ? *rest : item.stack;
    back.in      = item.out;
    back.backlog = item.in;
    passInItem(p, std::move(back));
}

/// Quark's PipeBlockEntity#passOut.
void passOut(BlockSource& region, GlobalPos const& p, Item& item) {
    auto     bp     = game::blockPos(p);
    BlockPos target = offset(bp, item.out);
    bool     did    = false;
    if (isPipe(game::blockIdAt(region, target))) {
        Item next  = item;
        next.in    = opposite(item.out);
        next.backlog = -1;
        passInItem(game::globalPos(region, target), std::move(next));
        return;
    }
    if (auto* c = terminalAt(region, target)) {
        ItemStack rest  = item.stack;
        int       moved = insertThroughFace(*c, rest, opposite(item.out));
        if (moved > 0) {
            did = true;
            if (!game::isEmpty(rest)) bounceBack(p, item, &rest);
            else removeDisplay(item);
        }
    }
    if (!did) bounceBack(p, item, nullptr);
}

void dropAll(BlockSource& region, GlobalPos const& p) {
    auto it = st().pipes.find(p);
    if (it == st().pipes.end()) return;
    auto data = std::move(it->second);
    st().pipes.erase(it);
    st().dirty.insert(p);
    for (auto* list : {&data.items, &data.queued}) {
        for (auto& i : *list) {
            removeDisplay(i);
            dropItem(region, p, i.stack, -1, false);
        }
    }
}

/// Java's Direction.getNearest(x, y, z).
int nearestFace(Vec3 const& v) {
    int   best = North;
    float max  = 1.4e-45f;
    for (int f : kFaces) {
        auto const& s   = kFaceStep[static_cast<size_t>(f)];
        float       dot = v.x * s[0] + v.y * s[1] + v.z * s[2];
        if (dot > max) {
            max  = dot;
            best = f;
        }
    }
    return best;
}

/// Item entities moving into an open end are absorbed (PipeBlockEntity#tick).
void absorb(BlockSource& region, GlobalPos const& p, Block const& b) {
    for (int side : kFaces) {
        if (connOf(b, side) != Opening) continue;
        auto const& s = kFaceStep[static_cast<size_t>(side)];
        Vec3 lo{p.x + 0.25f + 0.5f * std::min(0, s[0]), p.y + 0.25f + 0.5f * std::min(0, s[1]), p.z + 0.25f + 0.5f * std::min(0, s[2])};
        Vec3 hi{p.x + 0.75f + 0.5f * std::max(0, s[0]), p.y + 0.75f + 0.5f * std::max(0, s[1]), p.z + 0.75f + 0.5f * std::max(0, s[2])};
        bool picked = false;
        for (auto* a : game::entities::inBox(region, AABB{lo, hi}, "minecraft:item")) {
            if (!a || !a->isAlive()) continue;
            if (nearestFace(a->getPosDelta()) != opposite(side)) continue;
            auto& ia   = *static_cast<ItemActor*>(a);
            ItemStack copy = ia.mItem.get();
            if (game::isEmpty(copy)) continue;
            Item item;
            item.stack = copy;
            item.in    = side;
            item.seed  = st().rng();
            passInItem(p, std::move(item));
            if (cfg().doPipesWhoosh) playSoundAt(region, a->getPosition(), "lepton.pipe.pickup");
            vibration(region, game::blockPos(p), false);
            game::entities::despawn(*a);
            picked = true;
        }
        if (picked) log().debug("pipe {} absorbed items", p.key());
    }
}

void breakPipe(BlockSource& region, GlobalPos const& p) {
    auto bp  = game::blockPos(p);
    auto id  = game::blockIdAt(region, bp);
    log().info("pipe {} broke under too many items", p.key());
    dropItem(region, p, game::makeItem(id, 1), -1, false);
    game::clearBlock(region, bp); // BlockChangedEvent drops the items
}

void tickPipe(BlockSource& region, GlobalPos const& p) {
    auto& s  = st();
    auto  it = s.pipes.find(p);
    if (it == s.pipes.end()) return;
    auto& d = it->second;
    if (d.items.empty() && d.queued.empty()) return;
    int maxItems = cfg().maxPipeItems;
    if (maxItems > 0 && static_cast<int>(d.items.size()) > maxItems) {
        breakPipe(region, p);
        return;
    }
    int const speed = effectiveSpeed();
    d.iterating     = true;
    std::vector<Item> done;
    std::vector<int>  lastFacing;
    for (auto iter = d.items.begin(); iter != d.items.end();) {
        auto& item = *iter;
        int   last = item.out;
        bool  fin  = false;
        if (item.lastTick != s.tick) {
            item.lastTick = s.tick;
            ++item.ticks;
            ++item.time;
            if (item.ticks == speed / 2 - 1) item.out = targetFace(region, game::blockPos(p), item);
            if (item.out < 0) {
                item.valid = false;
                fin        = true;
            }
        }
        fin = fin || item.ticks >= speed;
        if (fin) {
            done.push_back(std::move(item));
            lastFacing.push_back(last);
            iter = d.items.erase(iter);
        } else {
            updateDisplay(region, p, item);
            ++iter;
        }
    }
    d.iterating = false;
    for (size_t k = 0; k < done.size(); ++k) {
        auto& item = done[k];
        if (item.valid) passOut(region, p, item);
        else {
            removeDisplay(item);
            dropItem(region, p, item.stack, lastFacing[k], true);
        }
    }
    auto again = s.pipes.find(p); // passOut may have added entries (and rehashed nothing: std::map)
    if (again != s.pipes.end() && !again->second.queued.empty()) {
        auto& q = again->second.queued;
        for (auto& i : q) again->second.items.push_back(std::move(i));
        q.clear();
    }
    s.dirty.insert(p);
}

void tickAll() {
    auto& s = st();
    ++s.tick;
    // connection updates queued by block changes
    if (!s.refresh.empty()) {
        auto queue = std::move(s.refresh);
        s.refresh.clear();
        for (auto const& p : queue)
            if (auto* r = regionFor(p)) refresh(*r, game::blockPos(p));
    }
    std::vector<GlobalPos> active;
    for (auto const& [p, d] : s.pipes)
        if (!d.items.empty() || !d.queued.empty()) active.push_back(p);
    for (auto const& p : active)
        if (auto* r = regionFor(p)) tickPipe(*r, p);
    // open ends of enabled pipes absorb items; disabled pipes show red dust every 10 ticks
    std::vector<GlobalPos> gone;
    for (auto const& p : s.known) {
        auto* r = game::regionOf(p.dim);
        if (!r) continue;
        auto bp = game::blockPos(p);
        if (!r->hasChunksAt(bp, 0, false)) continue;
        auto const& b = r->getBlock(bp);
        if (!isPipe(game::blockId(b))) {
            gone.push_back(p);
            continue;
        }
        bool on = !powered(*r, bp);
        if (on) absorb(*r, p, b);
        else if (s.tick % 10 == 0) {
            if (auto level = ll::service::getLevel())
                level->spawnParticleEffect("minecraft:redstone_wire_dust_particle", Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}, &r->getDimension());
        }
    }
    for (auto const& p : gone) s.known.erase(p);
    if (s.tick % 100 == 0) {
        // remove display entities nobody owns (crash, config change, chunk saved meanwhile)
        std::set<int64_t> owned;
        for (auto const& [p, d] : s.pipes) {
            for (auto const& i : d.items) owned.insert(i.display);
            for (auto const& i : d.queued) owned.insert(i.display);
        }
        for (auto* a : game::entities::ofType(kPipeItem))
            if (!owned.contains(game::entities::idOf(*a))) game::entities::despawn(*a);
    }
}

/// Using a stick re-checks every connected pipe of the same kind (BasePipeBlock#useItemOn).
bool fixNetwork(BlockSource& region, BlockPos const& start) {
    auto                id = game::blockIdAt(region, start);
    std::set<GlobalPos> found;
    std::vector<BlockPos> todo{start};
    bool                fixed = false;
    while (!todo.empty() && found.size() < 4096) {
        BlockPos cur = todo.back();
        todo.pop_back();
        auto gp = game::globalPos(region, cur);
        if (!found.insert(gp).second) continue;
        auto before = &region.getBlock(cur);
        refresh(region, cur);
        if (&region.getBlock(cur) != before) fixed = true;
        for (int f : kFaces) {
            auto n = offset(cur, f);
            if (game::blockIdAt(region, n) == id && !found.contains(game::globalPos(region, n))) todo.push_back(n);
        }
    }
    return fixed;
}

/// Glass on a copper pipe encases it (CopperPipeBlock#useItemOn); the items stay.
void encase(GlobalPos p, std::string uuid) {
    ll::coro::keepThis([p, uuid]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(1);
        auto* r = regionFor(p);
        if (!r) co_return;
        auto bp = game::blockPos(p);
        if (game::blockIdAt(*r, bp) != kPipe) co_return;
        auto level = ll::service::getLevel();
        auto* pl   = level ? level->getPlayer(mce::UUID::fromString(uuid)) : nullptr;
        auto const* encased = game::blockstates::of(kEncasedPipe);
        if (!encased) co_return;
        auto const* target = game::blockstates::copyStates(r->getBlock(bp), *encased);
        if (!target) co_return;
        game::setBlock(*r, bp, *target);
        refresh(*r, bp);
        playSoundAt(*r, Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}, "dig.glass", 1.0f, 0.8f);
        if (pl && !pl->isCreative()) {
            auto& inv = game::inv::playerInventory(*pl);
            game::inv::remove(inv, [](ItemStack const& s) { return game::itemId(s) == "minecraft:glass"; }, 1, {pl->getSelectedItemSlot(), pl->getSelectedItemSlot() + 1});
            game::inv::sync(*pl);
        }
        log().info("pipe {} encased", p.key());
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

class PipePort final : public Port {
public:
    explicit PipePort(GlobalPos p) : mPos(p) {}
    int              size() const override { return 6; }
    ItemStack const& get(int) const override { return ItemStack::EMPTY_ITEM(); }
    void             set(int slot, ItemStack const& item) override {
        if (game::isEmpty(item)) return;
        pipes::passIn(mPos, item, slot);
    }
    bool canPushIn(int slot, int face, ItemStack const&) const override {
        auto* r = regionFor(mPos);
        return r && slot == face && enabledAt(*r, game::blockPos(mPos));
    }
    bool canPullOut(int, int, ItemStack const&) const override { return false; }
    int  maxStack() const override { return 64; }

private:
    GlobalPos mPos;
};

class PipesModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        game::db::onWorldReady([] { ensureLoaded(); });
        game::db::onWorldSave([] { flush(); });
        game::db::onWorldClosed([] {
            auto& s = st();
            s.pipes.clear();
            s.known.clear();
            s.dirty.clear();
            s.refresh.clear();
            s.loaded = false;
        });
        ensureLoaded();
        registerPort(kPipe, [](GlobalPos const& p) -> std::unique_ptr<Port> {
            if (!cfg().enablePipes) return nullptr;
            return std::make_unique<PipePort>(p);
        });
        registerPort(kEncasedPipe, [](GlobalPos const& p) -> std::unique_ptr<Port> {
            if (!cfg().enablePipes || !cfg().enableEncasedPipes) return nullptr;
            return std::make_unique<PipePort>(p);
        });
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            if (!cfg().enablePipes) return;
            ensureLoaded();
            tickAll();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            auto const& prev = ev.previousBlock().getTypeName();
            auto const& now  = ev.newBlock().getTypeName();
            auto        pos  = game::globalPos(region, ev.pos());
            auto&       s    = st();
            if (isPipe(now)) {
                s.known.insert(pos);
                if (!isPipe(prev)) s.refresh.insert(pos);
            } else if (isPipe(prev)) {
                s.known.erase(pos);
                int dim = pos.dim;
                // the block is gone: its items drop (BasePipeBlock#onRemove)
                ll::coro::keepThis([pos, dim]() -> ll::coro::CoroTask<> {
                    co_await ll::chrono::ticks(1);
                    if (auto* r = game::regionOf(dim))
                        if (!isPipe(game::blockIdAt(*r, game::blockPos(pos)))) dropAll(*r, pos);
                }).launch(ll::thread::ServerThreadExecutor::getDefault());
            }
            if (prev == now) return; // state-only change of the same block (our own refreshes)
            for (int f : kFaces) {
                auto n = offset(ev.pos(), f);
                if (isPipe(region.getBlock(n).getTypeName())) s.refresh.insert(game::globalPos(region, n));
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide() || !cfg().enablePipes) return;
            auto& region = player.getDimensionBlockSource();
            auto  id     = game::blockIdAt(region, ev.blockPos());
            if (!isPipe(id)) return;
            auto item = game::itemId(ev.item());
            if (item == "minecraft:stick") {
                if (fixNetwork(region, ev.blockPos())) ev.cancel();
            } else if (item == "minecraft:glass" && id == kPipe && cfg().enableEncasedPipes) {
                ev.cancel();
                encase(game::globalPos(region, ev.blockPos()), player.getUuid().asString());
            }
        }));
    }
    void uninstall() {
        unregisterPort(kPipe);
        unregisterPort(kEncasedPipe);
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        flush();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace pipes

bool pipes::passIn(core::GlobalPos const& pos, ItemStack const& stack, int face) {
    if (!cfg().enablePipes || game::isEmpty(stack)) return false;
    auto* r = regionFor(pos);
    if (!r || !isPipe(game::blockIdAt(*r, game::blockPos(pos)))) return false;
    Item item;
    item.stack = stack;
    item.in    = face;
    item.seed  = st().rng();
    passInItem(pos, std::move(item));
    return true;
}

bool pipes::isEnabledPipe(core::GlobalPos const& pos) {
    auto* r = regionFor(pos);
    return r && enabledAt(*r, game::blockPos(pos));
}

int pipes::itemCount(core::GlobalPos const& pos) {
    auto it = st().pipes.find(pos);
    if (it == st().pipes.end()) return 0;
    return static_cast<int>(it->second.items.size() + it->second.queued.size());
}

} // namespace lp::feature::logistics

LP_REGISTER_MODULE(lp::feature::logistics::pipes::PipesModule);
