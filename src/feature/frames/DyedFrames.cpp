// U24 DyeableItemFrames (Quark tweaks) and MapWashing (Quark tweaks).
//
// Dyed frames are vanilla `minecraft:frame` / `minecraft:glow_frame` blocks (so holding, rotating,
// maps and comparators stay native) with a tinted `lepton:dyed_item_frame` overlay entity; the
// colour lives in the world DB and comes back as the dyed item when the frame breaks. Frames are
// dyed and washed in cauldrons (Bedrock's way of dyeing leather armour); filled maps are washed
// into blank maps. See docs/spec/frames.md §3.2 and §3.4.

#include "feature/frames/Frames.h"

#include <cmath>
#include <map>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/deps/vanilla_components/StateVectorComponent.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace lp::feature::frames {

namespace bs = game::blockstates;
using core::GlobalPos;

namespace {

constexpr std::string_view kVanillaFrame = "minecraft:frame";
constexpr std::string_view kVanillaGlow  = "minecraft:glow_frame";

Vec3 center(BlockPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

bool isVanillaFrameItem(std::string_view id) { return id == kVanillaFrame || id == kVanillaGlow; }
bool isDyedItem(std::string_view id) { return id == kDyedFrame || id == kDyedGlowFrame; }

ItemStack dyedItem(bool glow, int rgb) {
    ItemStack s = game::makeItem(glow ? kDyedGlowFrame : kDyedFrame);
    if (!game::isEmpty(s)) setItemColor(s, rgb);
    return s;
}

} // namespace

// ---- cauldrons (dyeing, washing; map washing) -------------------------------------------------------

bool onCauldronUse(Player& player, BlockSource& region, BlockPos const& pos) {
    return onCauldronUse(player, region, pos, player.getSelectedItem());
}

bool onCauldronUse(Player& player, BlockSource& region, BlockPos const& pos, ItemStack const& hand) {
    auto info = cauldronAt(region, pos);
    if (!info || !info->water) return false;
    if (game::isEmpty(hand) || player.isSneaking()) return false; // sneaking with an item skips block use (Java)
    auto id = game::itemId(hand);

    if (id == "minecraft:filled_map") {
        if (!cfg().enableMapWashing || info->dyed) return false;
        int  aux     = MapItem::doesDisplayPlayerMarkers(hand) ? 2 : 0; // locator maps stay locator maps
        int  count   = hand.mCount;
        auto blank   = game::makeItem("minecraft:empty_map", count, aux);
        player.setSelectedItem(blank);
        lowerCauldron(region, pos, 2);
        playSound(region, center(pos), "cauldron.cleanbanner");
        log().info("frames: {} washed {} map(s)", player.getRealName(), count);
        return true;
    }

    if (!cfg().enableDyeableItemFrames) return false;
    if (isVanillaFrameItem(id)) {
        if (!info->dyed) return false; // plain water: the frame hangs on the cauldron as usual
        exchangeOne(player, dyedItem(id == kVanillaGlow, info->rgb));
        lowerCauldron(region, pos, 2);
        playSound(region, center(pos), "cauldron.dyearmor");
        log().info("frames: {} dyed a {} #{:06x}", player.getRealName(), id, info->rgb);
        return true;
    }
    if (isDyedItem(id)) {
        bool glow = id == kDyedGlowFrame;
        if (info->dyed) {
            if (itemColor(hand) == info->rgb) return true; // same colour: nothing to do
            exchangeOne(player, dyedItem(glow, info->rgb));
            playSound(region, center(pos), "cauldron.dyearmor");
            log().info("frames: {} re-dyed a frame #{:06x}", player.getRealName(), info->rgb);
        } else {
            exchangeOne(player, game::makeItem(glow ? kVanillaGlow : kVanillaFrame));
            playSound(region, center(pos), "cauldron.cleanarmor");
            log().info("frames: {} washed a dyed frame", player.getRealName());
        }
        lowerCauldron(region, pos, 2);
        return true;
    }
    return false;
}

namespace dyed {

namespace {

constexpr std::string_view kDbPrefix = "lp:frames:dyed:";

struct Dyed {
    int          rgb     = 0;
    bool         glow    = false;
    std::int64_t overlay = 0;
};

struct Placement {
    GlobalPos    pos;
    int          dir  = 0;
    int          rgb  = 0;
    bool         glow = false;
    std::int64_t player = 0;
    ItemStack    refund;
};

struct PendingDrop {
    GlobalPos     pos;
    int           rgb  = 0;
    bool          glow = false;
    std::uint64_t until = 0;
};

struct State {
    std::map<GlobalPos, Dyed>  frames;
    std::vector<Placement>     placements;
    std::vector<PendingDrop>   drops;
    std::uint64_t              tick   = 0;
    bool                       loaded = false;
};
State& st() {
    static State s;
    return s;
}

std::vector<ll::event::ListenerPtr> gListeners;

std::string dbKey(GlobalPos const& p) { return std::string(kDbPrefix) + p.key(); }

void save(GlobalPos const& p, Dyed const& d) {
    if (game::db::available()) game::db::save(dbKey(p), {{"color", d.rgb}, {"glow", d.glow}, {"overlay", d.overlay}});
}
void erase(GlobalPos const& p) {
    st().frames.erase(p);
    if (game::db::available()) game::db::erase(dbKey(p));
}

void loadAll() {
    auto& s = st();
    s.frames.clear();
    s.placements.clear();
    s.drops.clear();
    s.loaded = true;
    if (!game::db::available()) return;
    std::vector<std::string> keys;
    game::db::forEachKey(std::string(kDbPrefix), [&](std::string_view suffix) { keys.emplace_back(suffix); });
    for (auto const& k : keys) {
        auto parsed = parseKey(k);
        if (!parsed) continue;
        GlobalPos p = *parsed;
        auto j = game::db::load(std::string(kDbPrefix) + k);
        if (!j || !j->is_object()) continue;
        s.frames[p] = Dyed{j->value("color", 0), j->value("glow", false), j->value("overlay", std::int64_t{0})};
    }
}

bool isFrameBlock(Block const& b, bool glow) { return b.getTypeName() == (glow ? kVanillaGlow : kVanillaFrame); }

void despawnOverlay(Dyed& d) {
    if (d.overlay)
        if (auto* a = game::entities::find(d.overlay)) game::entities::despawn(*a);
    d.overlay = 0;
}

void syncOverlay(BlockSource& region, GlobalPos const& gp, Dyed& d, Block const& block) {
    BlockPos pos = game::blockPos(gp);
    int      dir = bs::getInt(block, "facing_direction").value_or(0);
    Vec3     at{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f};
    Actor*   a = d.overlay ? game::entities::find(d.overlay) : nullptr;
    if (!a) {
        a = game::entities::spawn(region, kDyedOverlay, at);
        if (!a) return;
        d.overlay = game::entities::idOf(*a);
        save(gp, d);
    }
    a->teleportTo(at, false, 0, 1, false);
    orient(*a, yawFor(dir));
    setIntProperty(*a, "lepton:facing", dir);
    setIntProperty(*a, "lepton:color", d.rgb & 0xFFFFFF);
    setBoolProperty(*a, "lepton:map", bs::getBool(block, "item_frame_map_bit").value_or(false));
    setBoolProperty(*a, "lepton:glow", d.glow);
}

/// Places a dyed frame now (from the tick: block edits inside interaction events are rolled back).
bool place(BlockSource& region, Placement const& pl) {
    BlockPos pos = game::blockPos(pl.pos);
    if (!frameMayPlace(region, pos, pl.dir)) return false;
    auto const* block = bs::of(pl.glow ? kVanillaGlow : kVanillaFrame, {{"facing_direction", pl.dir}});
    if (!block || !game::setBlock(region, pos, *block)) return false;
    auto& d = st().frames[pl.pos];
    d       = Dyed{pl.rgb, pl.glow, 0};
    save(pl.pos, d);
    syncOverlay(region, pl.pos, d, region.getBlock(pos));
    playSound(region, center(pos), "block.itemframe.place");
    log().info("frames: placed a dyed {}frame #{:06x} at {}", pl.glow ? "glow " : "", pl.rgb, pl.pos.key());
    return true;
}

/// Validates a dyed frame placement by `player` (selected item) on `support`'s face `dir`, takes the
/// item and queues the placement. False when nothing can be placed there.
bool queuePlacement(Player& player, BlockSource& region, BlockPos const& support, int dir) {
    ItemStack const& hand = player.getSelectedItem();
    auto             id   = game::itemId(hand);
    if (!isDyedItem(id) || dir < 0 || dir > 5) return false;
    BlockPos target = offset(support, dir);
    if (!frameMayPlace(region, target, dir)) return false;
    auto gp = game::globalPos(region, target);
    for (auto const& p : st().placements)
        if (p.pos == gp) return false;
    Placement pl{gp, dir, itemColor(hand).value_or(0xFFFFFF), id == kDyedGlowFrame,
                 player.getOrCreateUniqueID().rawID, ItemStack{}};
    if (!player.isCreative()) {
        pl.refund = hand;
        pl.refund.set(1);
        ItemStack rest = hand;
        rest.remove(1);
        player.setSelectedItem(rest.mCount > 0 ? rest : ItemStack{});
    }
    st().placements.push_back(std::move(pl));
    return true;
}

/// Turns the vanilla frame item that a broken dyed frame dropped back into the dyed item.
bool convertDrop(PendingDrop const& d) {
    auto* region = game::regionOf(d.pos.dim);
    if (!region) return false;
    BlockPos     pos = game::blockPos(d.pos);
    AABB         box{Vec3{pos.x - 1.f, pos.y - 1.f, pos.z - 1.f}, Vec3{pos.x + 2.f, pos.y + 2.f, pos.z + 2.f}};
    std::string_view want = d.glow ? kVanillaGlow : kVanillaFrame;
    // the frame's own drop: a single frame item spawned at the frame in the last few ticks (the
    // nearest one, so two frames broken together keep their colours)
    Vec3   c = center(pos);
    Actor* best = nullptr;
    float  bestD = 1.0f;
    for (auto* a : game::entities::inBox(*region, box, "minecraft:item")) {
        if (!a || !a->isAlive()) continue;
        auto& ia = static_cast<ItemActor&>(*a);
        ItemStack const& stack = ia.mItem;
        if (game::itemId(stack) != want || stack.mCount != 1 || ia.mAge > 6) continue;
        Vec3  q    = a->getPosition();
        float dist = std::sqrt((q.x - c.x) * (q.x - c.x) + (q.y - c.y) * (q.y - c.y) + (q.z - c.z) * (q.z - c.z));
        if (dist < bestD) {
            bestD = dist;
            best  = a;
        }
    }
    if (best) {
        Actor* a = best;
        Vec3 at = a->getPosition(), motion = a->getPosDelta();
        game::entities::despawn(*a);
        auto level = ll::service::getLevel();
        if (!level) return false;
        if (auto* n = level->getSpawner().spawnItem(*region, dyedItem(d.glow, d.rgb), nullptr, at, 0)) {
            n->mBuiltInComponents->mStateVectorComponent->mPosDelta = motion;
            n->mPickupDelay                                          = 10;
        }
        log().info("frames: a dyed frame at {} dropped its dyed item", d.pos.key());
        return true;
    }
    return false;
}

void sweep() {
    auto& s = st();
    std::vector<GlobalPos> gone;
    for (auto& [gp, d] : s.frames) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos pos = game::blockPos(gp);
        if (!region->hasChunksAt(pos, 0, false)) continue;
        auto const& block = region->getBlock(pos);
        if (!isFrameBlock(block, d.glow)) {
            despawnOverlay(d);
            gone.push_back(gp);
            continue;
        }
        syncOverlay(*region, gp, d, block);
    }
    for (auto const& gp : gone) erase(gp);
    std::set<std::int64_t> owned;
    for (auto const& [gp, d] : s.frames)
        if (d.overlay) owned.insert(d.overlay);
    for (auto* a : game::entities::ofType(kDyedOverlay))
        if (!owned.contains(game::entities::idOf(*a))) game::entities::despawn(*a);
}

// ---- events ---------------------------------------------------------------------------------------

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto&       region = player.getDimensionBlockSource();
    auto const& pos    = ev.blockPos();
    auto const& block  = region.getBlock(pos);

    // the event's item: a glass frame uses its support with an empty hand (no dyeing, no washing)
    if (block.getTypeName() == "minecraft:cauldron" && onCauldronUse(player, region, pos, ev.item())) {
        ev.cancel();
        return;
    }

    auto id = game::itemId(ev.item());
    if (!isDyedItem(id)) return;
    if (!player.isSneaking() && block.getBlockType().isInteractiveBlock()) return; // the block reacts first
    ev.cancel();
    if (!cfg().enableDyeableItemFrames) return;
    queuePlacement(player, region, pos, static_cast<int>(ev.face()));
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    auto& s = st();
    if (s.frames.empty()) return;
    auto gp = game::globalPos(region, ev.pos());
    auto it = s.frames.find(gp);
    if (it == s.frames.end()) return;
    auto& d = it->second;
    if (isFrameBlock(ev.newBlock(), d.glow)) {
        syncOverlay(region, gp, d, ev.newBlock()); // e.g. a map went in or out
        return;
    }
    if (!isFrameBlock(ev.previousBlock(), d.glow)) return;
    despawnOverlay(d);
    s.drops.push_back(PendingDrop{gp, d.rgb, d.glow, s.tick + 3});
    erase(gp);
}

void onTick(ll::event::ServerLevelTickEvent&) {
    auto& s = st();
    ++s.tick;
    if (!s.loaded) return;
    if (!s.placements.empty()) {
        auto todo = std::move(s.placements);
        s.placements.clear();
        for (auto& pl : todo) {
            auto* region = game::regionOf(pl.pos.dim);
            bool  ok     = region && place(*region, pl);
            if (!ok && !game::isEmpty(pl.refund))
                if (auto* p = game::entities::find(pl.player); p && p->isPlayer())
                    giveOrDrop(static_cast<Player&>(*p), pl.refund);
        }
    }
    if (!s.drops.empty()) {
        std::vector<PendingDrop> keep;
        for (auto const& d : s.drops)
            if (!convertDrop(d) && s.tick < d.until) keep.push_back(d);
        s.drops = std::move(keep);
    }
    if (s.tick % 100 == 50) sweep();
}

} // namespace

void applyConfig() {
    if (st().loaded) sweep();
}

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
    gListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
    gListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
    static bool once = false;
    if (!once) {
        once = true;
        game::db::onWorldReady([] { loadAll(); });
        game::db::onWorldClosed([] {
            st().frames.clear();
            st().placements.clear();
            st().drops.clear();
            st().loaded = false;
        });
    }
    if (game::db::available() && !st().loaded) loadAll();
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
}

} // namespace dyed

namespace testing {

std::optional<DyedRecord> dyedRecord(core::GlobalPos const& pos) {
    auto& f  = dyed::st().frames;
    auto  it = f.find(pos);
    if (it == f.end()) return std::nullopt;
    return DyedRecord{it->second.rgb, it->second.glow, it->second.overlay};
}

bool placeDyed(Player& player, BlockSource& region, BlockPos const& support, int dir) {
    return dyed::queuePlacement(player, region, support, dir);
}

void dyedSweep() { dyed::sweep(); }

} // namespace testing

} // namespace lp::feature::frames
