// Variant chests: placement and pairing (Java ChestBlock), opening through game/VirtualContainer,
// lid animation + sounds, trapped chest power, comparator signal, structure loot and custom names.

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/safety/RedactableString.h"
#include "mc/util/LootTableUtils.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"

#include "feature/variantcontainers/ItemsContainer.h"
#include "feature/variantcontainers/VariantContainers.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::variantcontainers {

namespace vc = game::vc;
using core::GlobalPos;

namespace {

// ---- scheduling ---------------------------------------------------------------------------------------

struct Task {
    uint64_t              due;
    std::function<void()> fn;
};
uint64_t           gTick = 0;
std::vector<Task>& tasks() {
    static std::vector<Task> v;
    return v;
}
void later(int ticks, std::function<void()> fn) { tasks().push_back({gTick + static_cast<uint64_t>(ticks), std::move(fn)}); }

std::mt19937& rng() {
    static std::mt19937 r{0x5eed9u};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

// ---- per-position data (world DB) ---------------------------------------------------------------------

std::string lootKey(GlobalPos const& p) { return "lp:variantcontainers:loot:" + p.key(); }
std::string nameKey(GlobalPos const& p) { return "lp:variantcontainers:name:" + p.key(); }

struct Loot {
    std::string table;
    int         seed = 0;
};
/// Pending loot per chest half (absent entries cached as nullopt).
std::map<GlobalPos, std::optional<Loot>>& lootCache() {
    static std::map<GlobalPos, std::optional<Loot>> m;
    return m;
}
std::optional<Loot> const& lootAt(GlobalPos const& p) {
    auto it = lootCache().find(p);
    if (it != lootCache().end()) return it->second;
    std::optional<Loot> v;
    if (auto j = game::db::load(lootKey(p)); j && j->is_object() && j->contains("table"))
        v = Loot{j->value("table", std::string{}), j->value("seed", 0)};
    return lootCache()[p] = v;
}
void clearLoot(GlobalPos const& p) {
    if (lootAt(p)) game::db::erase(lootKey(p));
    lootCache()[p] = std::nullopt;
}

std::string nameAt(GlobalPos const& p) {
    if (auto j = game::db::load(nameKey(p)); j && j->is_object()) return j->value("name", std::string{});
    return {};
}

// ---- lid animation -------------------------------------------------------------------------------------

struct Anim {
    int  openness = 0; ///< tenths (Java ChestLidController: +-0.1 per tick)
    bool open     = false;
};
std::map<GlobalPos, Anim>& anims() {
    static std::map<GlobalPos, Anim> m;
    return m;
}

/// Lid frame for an openness in tenths: Java's ease-out 1 - (1 - o)^3 of 90 degrees, in 22.5 degree steps.
int frameFor(int tenths) {
    double o     = std::clamp(tenths, 0, 10) / 10.0;
    double angle = 90.0 * (1.0 - std::pow(1.0 - o, 3.0));
    return std::clamp(static_cast<int>(std::floor(angle / 22.5 + 0.5)), 0, 4);
}

void setChestBlock(BlockSource& region, BlockPos const& pos, Block const* next) {
    if (next && *next != region.getBlock(pos)) game::setBlock(region, pos, *next);
}

/// Strength of the trapped chest's redstone producer (Java getSignal = viewers, max 15).
void setTrappedPower(BlockSource& region, BlockPos const& pos, int power) {
    if (auto* sys = region.getDimension().mCircuitSystem.get()) sys->setStrength(pos, power);
}

void soundAtChest(BlockSource& region, ChestStorage const& st, bool open) {
    auto  k = game::blockPos(st.key);
    float x = k.x + 0.5f, y = k.y + 0.5f, z = k.z + 0.5f;
    if (st.second) { // the middle of a double chest
        x = (k.x + st.second->x) / 2.f + 0.5f;
        z = (k.z + st.second->z) / 2.f + 0.5f;
    }
    playSoundAt(region, x, y, z, open ? "random.chestopen" : "random.chestclosed", 0.5f, nextFloat() * 0.1f + 0.9f);
}

void tickAnims() {
    std::vector<GlobalPos> done;
    for (auto& [key, a] : anims()) {
        auto* region = game::regionOf(key.dim);
        auto  pos    = game::blockPos(key);
        if (!region || !region->hasChunksAt(pos, 0, false)) {
            done.push_back(key);
            continue;
        }
        auto st = chestStorage(*region, pos);
        if (!st || st->key != key) { // broken or re-paired: the new key animates on its own
            done.push_back(key);
            continue;
        }
        int  viewers = vc::viewers(key);
        bool open    = viewers > 0;
        if (open != a.open) {
            soundAtChest(*region, *st, open);
            a.open = open;
        }
        a.openness  = std::clamp(a.openness + (open ? 1 : -1), 0, 10);
        int frame   = frameFor(a.openness);
        int power   = std::min(viewers, 15);
        std::vector<BlockPos> halves{pos};
        if (st->second) halves.push_back(*st->second);
        for (auto const& h : halves) {
            auto const& b = region->getBlock(h);
            auto        s = chestState(b);
            if (!s) continue;
            setChestBlock(*region, h, withChest(b, std::nullopt, std::nullopt, frame, s->trapped ? std::optional<int>(power) : std::nullopt));
            // a state change does not always rebuild the circuit component: set its strength too
            if (s->trapped) setTrappedPower(*region, h, power);
        }
        if (!open && a.openness == 0) done.push_back(key);
    }
    for (auto const& k : done) anims().erase(k);
}

// ---- storage transitions ------------------------------------------------------------------------------

std::vector<ItemStack> slice(std::vector<ItemStack> const& v, size_t from, size_t count) {
    std::vector<ItemStack> out(count);
    for (size_t i = 0; i < count && from + i < v.size(); ++i) out[i] = v[from + i];
    return out;
}
bool anyItems(std::vector<ItemStack> const& v) {
    return std::any_of(v.begin(), v.end(), [](ItemStack const& s) { return !game::isEmpty(s); });
}
void dropAll(BlockSource& region, BlockPos const& at, std::vector<ItemStack> const& items) {
    for (auto const& s : items) spawnItem(region, s, at.x + 0.5f, at.y + 0.5f, at.z + 0.5f);
}

/// Merges the second half's own slots into the first half's double inventory (idempotent).
void mergeHalves(BlockSource& region, BlockPos const& first, BlockPos const& second) {
    auto f = game::globalPos(region, first), s = game::globalPos(region, second);
    auto own = vc::items(s, kChestSlots);
    if (!anyItems(own)) return;
    vc::closeAll(f);
    vc::closeAll(s);
    auto all = vc::items(f, 2 * kChestSlots);
    std::vector<ItemStack> overflow;
    for (int i = 0; i < kChestSlots; ++i) {
        auto& slot = all[static_cast<size_t>(kChestSlots + i)];
        if (game::isEmpty(own[static_cast<size_t>(i)])) continue;
        if (game::isEmpty(slot)) slot = own[static_cast<size_t>(i)];
        else overflow.push_back(own[static_cast<size_t>(i)]);
    }
    vc::setItems(f, all);
    vc::setItems(s, {});
    dropAll(region, second, overflow);
}

/// Moves the double inventory's second part (slots 27..53 stored at `first`) to `to` (or drops it).
void splitTo(BlockSource& region, BlockPos const& first, std::optional<BlockPos> to, BlockPos const& dropAt) {
    auto f   = game::globalPos(region, first);
    auto all = vc::items(f, 2 * kChestSlots);
    auto rest = slice(all, kChestSlots, kChestSlots);
    if (!anyItems(rest)) return;
    vc::closeAll(f);
    vc::setItems(f, slice(all, 0, kChestSlots));
    if (to) {
        auto t   = game::globalPos(region, *to);
        auto own = vc::items(t, kChestSlots);
        std::vector<ItemStack> overflow;
        for (int i = 0; i < kChestSlots; ++i) {
            auto& r = rest[static_cast<size_t>(i)];
            if (game::isEmpty(r)) continue;
            if (game::isEmpty(own[static_cast<size_t>(i)])) own[static_cast<size_t>(i)] = r;
            else overflow.push_back(r);
        }
        vc::setItems(t, own);
        dropAll(region, *to, overflow);
    } else {
        dropAll(region, dropAt, rest);
    }
}

/// Rolls the pending loot of the half at `half` into the inventory stored at `key` from `offset`.
void rollLoot(BlockSource& region, BlockPos const& half, GlobalPos const& key, int size, int offset, Player* opener) {
    auto gp   = game::globalPos(region, half);
    auto loot = lootAt(gp);
    if (!loot) return;
    clearLoot(gp);
    auto level = ll::service::getLevel();
    if (!level) return;
    ItemsContainer tmp{std::vector<ItemStack>(kChestSlots)};
    try {
        Util::LootTableUtils::fillContainer(
            *level,
            tmp,
            level->getRandom(),
            loot->table,
            region.getDimensionId(),
            opener
        );
    } catch (...) {
        log().error("variantcontainers: loot table {} failed", loot->table);
        return;
    }
    auto items = vc::items(key, size);
    std::vector<ItemStack> overflow;
    for (int i = 0; i < kChestSlots; ++i) {
        auto const& s = tmp.items()[static_cast<size_t>(i)];
        if (game::isEmpty(s)) continue;
        auto& slot = items[static_cast<size_t>(offset + i)];
        if (game::isEmpty(slot)) slot = s;
        else overflow.push_back(s);
    }
    vc::setItems(key, items);
    dropAll(region, half, overflow);
    log().info("variantcontainers: rolled loot {} into the chest at {}", loot->table, gp.key());
}

void dropNamedChest(BlockSource& region, BlockPos const& pos, std::string const& id, std::string const& name) {
    // the loot table dropped the chest item this tick: give it the chest's custom name
    int dim = static_cast<int>(region.getDimensionId());
    later(1, [dim, pos, id, name] {
        auto* r = game::regionOf(dim);
        if (!r) return;
        AABB box{Vec3{pos.x - 1.f, pos.y - 1.f, pos.z - 1.f}, Vec3{pos.x + 2.f, pos.y + 2.f, pos.z + 2.f}};
        for (auto* a : game::entities::inBox(*r, box, "minecraft:item")) {
            auto& item = static_cast<ItemActor*>(a)->item();
            if (game::itemId(item) != id || !item.getCustomName().empty()) continue;
            item.setCustomName(Bedrock::Safety::RedactableString(name));
            return;
        }
    });
}

/// A chest half disappeared (`prev` was its state).
void onChestRemoved(BlockSource& region, BlockPos const& pos, ChestState const& prev) {
    auto gp = game::globalPos(region, pos);
    if (auto name = nameAt(gp); !name.empty()) {
        game::db::erase(nameKey(gp));
        dropNamedChest(region, pos, prev.id, name);
    }
    if (prev.type == ChestType::Single) {
        rollLoot(region, pos, gp, kChestSlots, 0, nullptr);
        vc::dropContents(region, gp);
        return;
    }
    BlockPos q     = step(pos, connectedDir(prev));
    bool     first = prev.type == ChestType::Right;
    BlockPos f     = first ? pos : q;
    auto     fk    = game::globalPos(region, f);
    vc::closeAll(fk);
    rollLoot(region, pos, fk, 2 * kChestSlots, first ? 0 : kChestSlots, nullptr);
    auto all = vc::items(fk, 2 * kChestSlots);
    auto qs  = chestState(region.getBlock(q));
    bool partnerStays = qs && qs->id == prev.id;
    if (first) {
        dropAll(region, pos, slice(all, 0, kChestSlots));
        vc::setItems(fk, {});
        auto rest = slice(all, kChestSlots, kChestSlots);
        if (partnerStays) {
            auto qk  = game::globalPos(region, q);
            auto own = vc::items(qk, kChestSlots);
            for (int i = 0; i < kChestSlots; ++i)
                if (game::isEmpty(own[static_cast<size_t>(i)])) own[static_cast<size_t>(i)] = rest[static_cast<size_t>(i)];
                else dropAll(region, q, {rest[static_cast<size_t>(i)]});
            vc::setItems(qk, own);
        } else {
            dropAll(region, q, rest);
        }
    } else {
        dropAll(region, pos, slice(all, kChestSlots, kChestSlots));
        vc::setItems(fk, slice(all, 0, kChestSlots));
    }
    vc::setItems(gp, {}); // nothing may stay behind under the removed position
    // Java updateShape: the partner turns single
    if (partnerStays && qs->type != ChestType::Single && step(q, connectedDir(*qs)) == pos) {
        auto const& qb = region.getBlock(q);
        setChestBlock(region, q, withChest(qb, std::nullopt, ChestType::Single));
    }
}

/// Java ChestBlock#updateShape for a chest that appeared or changed without a placing player.
void reconcile(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    auto        s = chestState(b);
    if (!s) return;
    if (s->type != ChestType::Single) {
        if (partnerOf(region, pos)) return;
        BlockPos q  = step(pos, connectedDir(*s));
        auto     qs = chestState(region.getBlock(q));
        if (qs && qs->id == s->id && qs->type == ChestType::Single && qs->facing == s->facing) {
            auto const& qb = region.getBlock(q);
            setChestBlock(region, q, withChest(qb, std::nullopt, s->type == ChestType::Left ? ChestType::Right : ChestType::Left));
        } else {
            setChestBlock(region, pos, withChest(b, std::nullopt, ChestType::Single));
        }
        return;
    }
    for (int d = 0; d < 4; ++d) {
        BlockPos q  = step(pos, d);
        auto     qs = chestState(region.getBlock(q));
        if (!qs || qs->id != s->id || qs->type == ChestType::Single || qs->facing != s->facing) continue;
        if (step(q, connectedDir(*qs)) != pos) continue;
        setChestBlock(region, pos, withChest(b, std::nullopt, qs->type == ChestType::Left ? ChestType::Right : ChestType::Left));
        return;
    }
}

void onChestChanged(int dim, BlockPos pos, std::optional<ChestState> prev, std::optional<ChestState> next) {
    auto* region = game::regionOf(dim);
    if (!region || !region->hasChunksAt(pos, 0, false)) return;
    bool replaced = prev && (!next || next->id != prev->id);
    if (replaced) onChestRemoved(*region, pos, *prev);
    auto now = chestState(region->getBlock(pos));
    if (!now) return;
    if (prev && !replaced && prev->type != ChestType::Single && (now->type == ChestType::Single || connectedDir(*now) != connectedDir(*prev))) {
        // unpaired: the first half hands the second part to its old partner (or drops it)
        if (prev->type == ChestType::Right) {
            BlockPos q  = step(pos, connectedDir(*prev));
            auto     qs = chestState(region->getBlock(q));
            std::optional<BlockPos> to;
            if (qs && qs->id == now->id && !partnerOf(*region, q)) to = q;
            splitTo(*region, pos, to, q);
        }
    }
    if (!prev || replaced) reconcile(*region, pos);
    if (auto st = chestStorage(*region, pos); st && st->second) mergeHalves(*region, game::blockPos(st->key), *st->second);
}

// ---- placement --------------------------------------------------------------------------------------

struct Click {
    BlockPos clicked;
    int      face = -1;
    uint64_t tick = 0;
};
std::unordered_map<int64_t, Click>& clicks() {
    static std::unordered_map<int64_t, Click> m;
    return m;
}

/// Bedrock face id (2 north, 3 south, 4 west, 5 east) -> Java 2D direction; -1 for up/down.
int faceToDir(int face) {
    switch (face) {
    case 2:
        return 2;
    case 3:
        return 0;
    case 4:
        return 1;
    case 5:
        return 3;
    default:
        return -1;
    }
}

/// Java ChestBlock#candidatePartnerFacing: the facing of a single equal chest in direction `d`.
std::optional<int> candidatePartnerFacing(BlockSource& region, BlockPos const& pos, int d, std::string const& id) {
    auto s = chestState(region.getBlock(step(pos, d)));
    if (!s || s->id != id || s->type != ChestType::Single) return std::nullopt;
    return s->facing;
}

/// Java ChestBlock#getStateForPlacement, applied the tick after the block appeared.
void finishPlacement(BlockSource& region, BlockPos const& pos, bool sneaking, int face) {
    auto const& b = region.getBlock(pos);
    auto        s = chestState(b);
    if (!s) return;
    int       facing  = s->facing;
    ChestType type    = ChestType::Single;
    int       clicked = faceToDir(face);
    if (clicked >= 0 && sneaking) {
        auto d = candidatePartnerFacing(region, pos, opp(clicked), s->id);
        if (d && ((*d & 1) != (clicked & 1))) {
            facing = *d;
            type   = ccw(*d) == opp(clicked) ? ChestType::Right : ChestType::Left;
        }
    }
    if (type == ChestType::Single && !sneaking) {
        if (candidatePartnerFacing(region, pos, cw(facing), s->id) == facing) type = ChestType::Left;
        else if (candidatePartnerFacing(region, pos, ccw(facing), s->id) == facing) type = ChestType::Right;
    }
    if (type == s->type && facing == s->facing) return;
    setChestBlock(region, pos, withChest(b, facing, type));
    if (type != ChestType::Single) {
        auto now = chestState(region.getBlock(pos));
        if (!now) return;
        BlockPos q  = step(pos, connectedDir(*now));
        auto const& qb = region.getBlock(q);
        setChestBlock(region, q, withChest(qb, std::nullopt, type == ChestType::Left ? ChestType::Right : ChestType::Left));
    }
}

// ---- opening ----------------------------------------------------------------------------------------

/// Java ChestBlock#isChestBlockedAt: a redstone conductor above, or a sitting cat on top.
bool blockedAt(BlockSource& region, BlockPos const& pos) {
    BlockPos above{pos.x, pos.y + 1, pos.z};
    auto const& b = region.getBlock(above);
    if (!b.isAir() && static_cast<bool>(b.mCachedComponentData->mIsSolid) && !isVariantChest(b)) return true;
    AABB box{Vec3{static_cast<float>(pos.x), pos.y + 1.f, static_cast<float>(pos.z)}, Vec3{pos.x + 1.f, pos.y + 2.f, pos.z + 1.f}};
    for (auto* cat : game::entities::inBox(region, box, "minecraft:cat"))
        if (cat->getStatusFlag(ActorFlags::Sitting)) return true;
    return false;
}

std::unordered_map<std::string, uint64_t>& recentOpen() {
    static std::unordered_map<std::string, uint64_t> m;
    return m;
}

} // namespace

// ---- public -----------------------------------------------------------------------------------------

void setPendingLoot(GlobalPos const& pos, std::string const& table, int seed) {
    game::db::save(lootKey(pos), {{"table", table}, {"seed", seed}});
    lootCache()[pos] = Loot{table, seed};
}

void ensureLoot(BlockSource& region, BlockPos const& pos, Player* opener) {
    auto st = chestStorage(region, pos);
    if (!st) return;
    auto k = game::blockPos(st->key);
    rollLoot(region, k, st->key, st->size, 0, opener);
    if (st->second) rollLoot(region, *st->second, st->key, st->size, kChestSlots, opener);
}

int chestComparatorSignal(BlockSource& region, BlockPos const& pos) {
    auto st = chestStorage(region, pos);
    if (!st) return 0;
    ensureLoot(region, pos);
    auto  items = vc::items(st->key, st->size);
    float f     = 0.f;
    bool  any   = false;
    for (auto const& s : items) {
        if (game::isEmpty(s)) continue;
        f   += static_cast<float>(s.mCount) / static_cast<float>(std::min(64, static_cast<int>(s.getMaxStackSize())));
        any  = true;
    }
    f /= static_cast<float>(items.size());
    return static_cast<int>(std::floor(f * 14.f)) + (any ? 1 : 0);
}

void notifyChestChanged(BlockSource& region, BlockPos const& pos) {
    region.updateNeighborsAt(pos);
    if (auto q = partnerOf(region, pos)) region.updateNeighborsAt(*q);
}

int chestViewers(BlockSource& region, BlockPos const& pos) {
    auto st = chestStorage(region, pos);
    return st ? vc::viewers(st->key) : 0;
}

bool openChest(Player& player, BlockPos const& pos) {
    auto& region = player.getDimensionBlockSource();
    auto  st     = chestStorage(region, pos);
    if (!st) return false;
    if (blockedAt(region, pos) || (st->second && blockedAt(region, game::blockPos(st->key) == pos ? *st->second : game::blockPos(st->key))))
        return false;
    ensureLoot(region, pos, &player);
    vc::Spec spec;
    spec.size   = st->size;
    spec.screen = vc::Screen::Chest;
    auto name   = nameAt(st->key);
    if (name.empty() && st->second) name = nameAt(game::globalPos(region, *st->second));
    if (!name.empty()) {
        spec.titleKey      = "";
        spec.titleFallback = name;
    } else if (st->second) {
        spec.titleKey      = "container.chestDouble";
        spec.titleFallback = "Large Chest";
    } else {
        spec.titleKey      = "container.chest";
        spec.titleFallback = "Chest";
    }
    spec.onChanged = [](GlobalPos const& key) {
        if (auto* r = game::regionOf(key.dim)) notifyChestChanged(*r, game::blockPos(key));
    };
    if (!vc::open(player, st->key, spec)) return false;
    anims().try_emplace(st->key);
    log().info("variantcontainers: {} opened {} at {}", player.getRealName(), region.getBlock(pos).getTypeName(), st->key.key());
    return true;
}

class ChestsModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            ++gTick;
            if (!tasks().empty()) {
                auto due = std::move(tasks());
                tasks().clear();
                for (auto& t : due) {
                    if (t.due > gTick) {
                        tasks().push_back(std::move(t));
                        continue;
                    }
                    try {
                        t.fn();
                    } catch (std::exception const& e) {
                        log().error("variantcontainers: task failed: {}", e.what());
                    }
                }
            }
            if (!anims().empty()) tickAnims();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide()) return;
            auto& region = player.getDimensionBlockSource();
            auto  pos    = ev.blockPos();
            clicks()[player.getOrCreateUniqueID().rawID] = Click{pos, static_cast<int>(ev.face()), gTick};
            if (!isVariantChest(region.getBlock(pos))) return;
            if (player.isSneaking() && !game::isEmpty(ev.item())) return; // sneak + item: place / use the item
            ev.cancel();
            auto uuid = player.getUuid().asString();
            if (auto it = recentOpen().find(uuid); it != recentOpen().end() && gTick - it->second < 5) return;
            recentOpen()[uuid] = gTick;
            int64_t id  = player.getOrCreateUniqueID().rawID;
            int     dim = static_cast<int>(region.getDimensionId());
            // defer: the cancelled interaction is rolled back after the event
            later(1, [id, dim, pos] {
                auto* p = playerById(id);
                auto* r = game::regionOf(dim);
                if (!p || !r || static_cast<int>(p->getDimensionId()) != dim || !isVariantChest(r->getBlock(pos))) return;
                openChest(*p, pos);
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide() || !isVariantChest(ev.placedBlock())) return;
            auto pos  = ev.pos();
            auto id   = player.getOrCreateUniqueID().rawID;
            auto it   = clicks().find(id);
            int  face = it != clicks().end() && gTick - it->second.tick <= 2 ? it->second.face : 1;
            bool sneak = player.isSneaking();
            int  dim   = static_cast<int>(player.getDimensionBlockSource().getDimensionId());
            auto const& held = player.getSelectedItem();
            std::string name = held.getCustomName();
            std::string block = ev.placedBlock().getTypeName();
            later(1, [dim, pos, sneak, face, name, block] {
                auto* r = game::regionOf(dim);
                if (!r || r->getBlock(pos).getTypeName() != block) return;
                if (!name.empty()) game::db::save(nameKey(game::globalPos(*r, pos)), {{"name", name}});
                finishPlacement(*r, pos, sneak, face);
            });
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            auto const& prevB = ev.previousBlock();
            auto const& nextB = ev.newBlock();
            bool pc = isVariantChest(prevB), nc = isVariantChest(nextB);
            if (!pc && !nc) return;
            auto prev = chestState(prevB);
            auto next = chestState(nextB);
            if (prev && next && prev->id == next->id && prev->type == next->type && prev->facing == next->facing) return; // lid / power
            int  dim = static_cast<int>(region.getDimensionId());
            auto pos = ev.pos();
            later(1, [dim, pos, prev, next] { onChestChanged(dim, pos, prev, next); });
        }));
        game::db::onWorldClosed([] {
            tasks().clear();
            anims().clear();
            lootCache().clear();
            clicks().clear();
            recentOpen().clear();
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::feature::variantcontainers

LP_REGISTER_MODULE(lp::feature::variantcontainers::ChestsModule);
