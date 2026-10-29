// U24 GlassItemFrame (Quark building): glass item frames are custom blocks
// (`lepton:glass_item_frame`, `lepton:glowing_glass_item_frame`, `minecraft:block_face` +
// `lepton:filled`) whose item is shown by a `lepton:glass_frame` display entity. The item,
// rotation and display id live in the world DB. See docs/spec/frames.md §3.1.

#include "feature/frames/Frames.h"

#include <map>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"

#include "feature/frames/SignAttachment.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace lp::feature::frames::glass {

namespace bs = game::blockstates;
using core::GlobalPos;

namespace {

constexpr std::string_view kDbPrefix = "lp:frames:glass:";

/// Display kinds (`lepton:kind`), each with its own offset in the display animation.
enum Kind : int { Flat = 0, Tool = 1, BlockItem = 2, Banner = 3, Shield = 4, Map = 5 };

struct Frame {
    ItemStack    item;      ///< held item (empty = none)
    int          rotation = 0;
    std::int64_t display  = 0;
    SignInfo     sign;
    SignAttachment attachment = SignAttachment::NotAttached;
    bool         dirty = true; ///< the display needs a refresh
};

struct State {
    std::map<GlobalPos, Frame>                               frames;
    std::set<GlobalPos>                                      checkSoon;  ///< survival checks for the next tick
    std::map<GlobalPos, bool>                                setFilled;  ///< deferred `lepton:filled` changes
    std::unordered_map<std::int64_t, std::pair<GlobalPos, std::uint64_t>> lastUse; ///< debounce per player
    std::uint64_t                                            tick       = 0;
    int                                                      mapUpdates = 0;
    bool                                                     loaded     = false;
};
State& st() {
    static State s;
    return s;
}

std::vector<ll::event::ListenerPtr> gListeners;

std::string dbKey(GlobalPos const& p) { return std::string(kDbPrefix) + p.key(); }

void save(GlobalPos const& p, Frame const& f) {
    if (!game::db::available()) return;
    nlohmann::json j{{"item", game::isEmpty(f.item) ? std::string() : game::itemToSnbt(f.item)},
                     {"rotation", f.rotation},
                     {"display", f.display}};
    game::db::save(dbKey(p), j);
}

void erase(GlobalPos const& p) {
    st().frames.erase(p);
    if (game::db::available()) game::db::erase(dbKey(p));
}

void loadAll() {
    auto& s = st();
    s.frames.clear();
    s.checkSoon.clear();
    s.setFilled.clear();
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
        Frame f;
        auto  snbt = j->value("item", std::string());
        if (!snbt.empty()) f.item = game::itemFromSnbt(snbt);
        f.rotation = j->value("rotation", 0) & 7;
        f.display  = j->value("display", std::int64_t{0});
        s.frames[p] = std::move(f);
    }
    log().debug("frames: {} glass frames loaded", s.frames.size());
}

bool isGlassBlock(Block const& b) {
    auto const& id = b.getTypeName();
    return id == kGlassFrame || id == kGlowingGlassFrame;
}
bool isShiny(Block const& b) { return b.getTypeName() == kGlowingGlassFrame; }
int  dirOf(Block const& b) { return dirFromName(bs::getString(b, "minecraft:block_face").value_or("up")); }

Vec3 center(BlockPos const& p) { return Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}; }

int facingToRotation(int facing) {
    switch (facing) {
    case 3: return 0;  // south
    case 4: return 4;  // west
    case 2: return 8;  // north
    case 5: return 12; // east
    default: return 0;
    }
}

} // namespace

/// The sign kind of `block` (Bedrock ids: `*standing_sign`, `*wall_sign`, `*hanging_sign`).
SignInfo signInfoOf(Block const& block) {
    SignInfo    s;
    auto const& id = block.getTypeName();
    if (id.ends_with("standing_sign")) {
        s.kind     = SignKind::Standing;
        s.rotation = bs::getInt(block, "ground_sign_direction").value_or(0) & 15;
    } else if (id.ends_with("wall_sign")) {
        s.kind   = SignKind::Wall;
        s.facing = bs::getInt(block, "facing_direction").value_or(2);
    } else if (id.ends_with("hanging_sign")) {
        int facing = bs::getInt(block, "facing_direction").value_or(2);
        if (bs::getBool(block, "hanging").value_or(false)) {
            s.kind     = SignKind::CeilingHanging;
            s.rotation = bs::getBool(block, "attached_bit").value_or(false)
                           ? bs::getInt(block, "ground_sign_direction").value_or(0) & 15
                           : facingToRotation(facing);
        } else {
            s.kind   = SignKind::WallHanging;
            s.facing = facing;
        }
    }
    return s;
}

namespace {

/// Java's BlockState#isSolid: a collision shape whose bounding box is at least 0.73 on average or
/// a full block high (chests, slabs, fences, leaves, glass; not carpets, flowers, liquids).
bool javaSolid(BlockSource& region, BlockPos const& pos, Block const& b) {
    if (b.isAir()) return false;
    AABB box = b.getBlockType().getCollisionShape(b, region, pos, nullptr);
    Vec3 d   = box.max - box.min;
    if (d.x <= 0.f || d.y <= 0.f || d.z <= 0.f) return false;
    return (d.x + d.y + d.z) / 3.f >= 0.7291666f || d.y >= 1.f;
}

bool isDiode(Block const& b) {
    auto const& id = b.getTypeName();
    return id == "minecraft:unpowered_repeater" || id == "minecraft:powered_repeater"
        || id == "minecraft:unpowered_comparator" || id == "minecraft:powered_comparator";
}

/// The support of a frame facing `dir` holds it (Java's HangingEntity#survives: a solid block or a
/// diode; signs are `forceSolidOn` in Java, so they count as solid).
bool supportOk(BlockSource& region, BlockPos const& framePos, int dir) {
    BlockPos    sp = offset(framePos, oppositeDir(dir));
    auto const& b  = region.getBlock(sp);
    if (signInfoOf(b).kind != SignKind::None || isDiode(b)) return true;
    return javaSolid(region, sp, b);
}

void updateAttachment(BlockSource& region, BlockPos const& pos, int dir, Frame& f) {
    auto sign = signInfoOf(region.getBlock(offset(pos, oppositeDir(dir))));
    auto att  = signAttachment(dir, sign);
    if (att != f.attachment || sign.rotation != f.sign.rotation) f.dirty = true;
    f.sign       = sign;
    f.attachment = att;
}

int kindOf(ItemStack const& item) {
    auto id = item.getTypeName();
    if (id == "minecraft:banner") return Banner;
    if (id == "minecraft:shield") return Shield;
    if (id == "minecraft:filled_map") return Map;
    auto const* it = item.mItem.get();
    if (it && it->isHandEquipped()) return Tool;
    if (item.mBlock) return BlockItem;
    return Flat;
}

void despawnDisplay(Frame& f) {
    if (f.display != 0)
        if (auto* a = game::entities::find(f.display)) game::entities::despawn(*a);
    f.display = 0;
}

/// Spawns / updates the display entity of a filled frame (despawns it for an empty one).
void syncDisplay(BlockSource& region, GlobalPos const& gp, Frame& f, Block const& block) {
    f.dirty = false;
    if (game::isEmpty(f.item)) {
        despawnDisplay(f);
        return;
    }
    BlockPos pos = game::blockPos(gp);
    int      dir = dirOf(block);
    Vec3     at{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f};
    Actor*   a = f.display ? game::entities::find(f.display) : nullptr;
    if (!a) {
        a = game::entities::spawn(region, kGlassDisplay, at);
        if (!a) return;
        f.display = game::entities::idOf(*a);
        save(gp, f);
    }
    a->teleportTo(at, false, 0, 1, false);
    orient(*a, yawFor(dir));
    a->setCarriedItem(f.item);
    a->setNameTag(f.item.getCustomName());
    setIntProperty(*a, "lepton:facing", dir);
    setIntProperty(*a, "lepton:rotation", f.rotation);
    setIntProperty(*a, "lepton:kind", kindOf(f.item));
    setIntProperty(*a, "lepton:sign", static_cast<int>(f.attachment));
    setIntProperty(*a, "lepton:sign_rotation", f.sign.rotation);
    setFloatProperty(*a, "lepton:scale", static_cast<float>(cfg().itemRenderScale));
    setBoolProperty(*a, "lepton:shiny", isShiny(block));
}

Frame& frameAt(GlobalPos const& gp) { return st().frames[gp]; }

void requestFilled(GlobalPos const& gp, bool filled) { st().setFilled[gp] = filled; }

/// Drops the frame's item at the frame (as Java's dropItem; nothing for creative players).
void dropHeld(BlockSource& region, BlockPos const& pos, Frame& f, Player* player) {
    if (game::isEmpty(f.item)) return;
    if (!(player && player->isCreative())) dropItem(region, f.item, center(pos));
    f.item     = ItemStack{};
    f.rotation = 0;
}

/// The frame broke (support gone): it drops itself; its item drops when the block is removed.
void breakFrame(BlockSource& region, BlockPos const& pos, Block const& block) {
    std::string item = std::string(isShiny(block) ? kGlowingGlassFrame : kGlassFrame);
    dropItem(region, game::makeItem(item), center(pos));
    playSound(region, center(pos), "block.itemframe.break");
    game::clearBlock(region, pos);
}

void updateMap(BlockSource& region, BlockPos const& pos, Frame const& f) {
    if (game::isEmpty(f.item) || f.item.getTypeName() != "minecraft:filled_map") return;
    auto const* item = f.item.mItem.get();
    if (!item) return;
    static_cast<MapItem const&>(*item).blockTick(f.item, region, pos);
    ++st().mapUpdates;
}

/// Survival, sign attachment, display and maps of every loaded frame.
void sweep(bool maps) {
    auto& s = st();
    std::vector<GlobalPos> gone, broken;
    for (auto& [gp, f] : s.frames) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos pos = game::blockPos(gp);
        if (!region->hasChunksAt(pos, 0, false)) continue;
        auto const& block = region->getBlock(pos);
        if (!isGlassBlock(block)) {
            dropHeld(*region, pos, f, nullptr);
            despawnDisplay(f);
            gone.push_back(gp);
            continue;
        }
        int dir = dirOf(block);
        if (!supportOk(*region, pos, dir)) {
            broken.push_back(gp); // broken after the loop (the removal handler edits `frames`)
            continue;
        }
        updateAttachment(*region, pos, dir, f);
        bool filled = !game::isEmpty(f.item);
        if (bs::getBool(block, "lepton:filled").value_or(false) != filled) requestFilled(gp, filled);
        bool missing = filled && (!f.display || !game::entities::find(f.display));
        if (f.dirty || missing || (!filled && f.display)) syncDisplay(*region, gp, f, block);
        if (maps && cfg().glassItemFramesUpdateMaps) updateMap(*region, pos, f);
    }
    for (auto const& gp : gone) erase(gp);
    for (auto const& gp : broken) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos pos = game::blockPos(gp);
        auto const& block = region->getBlock(pos);
        if (!isGlassBlock(block)) continue;
        log().info("frames: a glass frame at {} lost its support", gp.key());
        breakFrame(*region, pos, block); // the removal handler drops the item and forgets the frame
    }

    // displays whose frame is gone (killed frames, edited worlds)
    std::set<std::int64_t> owned;
    for (auto const& [gp, f] : s.frames)
        if (f.display) owned.insert(f.display);
    for (auto* a : game::entities::ofType(kGlassDisplay))
        if (!owned.contains(game::entities::idOf(*a))) game::entities::despawn(*a);
}

bool debounce(Player& player, GlobalPos const& gp) {
    auto& s  = st();
    auto  id = player.getOrCreateUniqueID().rawID;
    auto  it = s.lastUse.find(id);
    if (it != s.lastUse.end() && it->second.first == gp && s.tick - it->second.second < 4) return true;
    s.lastUse[id] = {gp, s.tick};
    return false;
}

/// A player uses a glass frame (Quark's GlassItemFrame#interact + ItemFrame#interact).
bool use(Player& player, BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (!isGlassBlock(block)) return false;
    auto  gp  = game::globalPos(region, pos);
    auto& f   = frameAt(gp);
    int   dir = dirOf(block);
    bool  filled = !game::isEmpty(f.item);

    // 1. open the container behind the frame
    if (!player.isSneaking() && filled && kindOf(f.item) != Banner) {
        BlockPos behind = offset(pos, oppositeDir(dir));
        if (region.getBlockEntity(behind) && player.mGameMode.get()) {
            ItemStack   empty;
            auto const& bb  = region.getBlock(behind);
            Vec3        hit = center(pos);
            auto        r   = player.mGameMode.get()->useItemOn(empty, behind, static_cast<uchar>(dir), hit,
                                                       HandSlot::Mainhand, &bb, true);
            if (r.mSuccess) {
                log().info("frames: {} used {} through a glass frame", player.getRealName(), bb.getTypeName());
                return true;
            }
        }
    }

    ItemStack const& hand = player.getSelectedItem();
    if (!filled) {
        if (game::isEmpty(hand)) return true; // nothing to do, but the frame takes the click
        f.item = hand;
        f.item.set(1);
        f.rotation = 0;
        if (!player.isCreative()) {
            ItemStack rest = hand;
            rest.remove(1);
            player.setSelectedItem(rest.mCount > 0 ? rest : ItemStack{});
        }
        playSound(region, center(pos), "block.itemframe.add_item");
        requestFilled(gp, true);
        log().info("frames: {} put {} into a glass frame", player.getRealName(), f.item.getTypeName());
    } else {
        f.rotation = (f.rotation + 1) & 7;
        playSound(region, center(pos), "block.itemframe.rotate_item");
        log().debug("frames: glass frame rotated to {}", f.rotation);
    }
    updateAttachment(region, pos, dir, f);
    save(gp, f);
    syncDisplay(region, gp, f, block);
    return true;
}

/// A player punches a glass frame: a filled frame drops its item and stays. False = let it break.
bool punch(Player& player, BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (!isGlassBlock(block)) return false;
    auto gp = game::globalPos(region, pos);
    auto it = st().frames.find(gp);
    if (it == st().frames.end() || game::isEmpty(it->second.item)) return false;
    auto& f = it->second;
    dropHeld(region, pos, f, &player);
    playSound(region, center(pos), "block.itemframe.remove_item");
    save(gp, f);
    syncDisplay(region, gp, f, block);
    requestFilled(gp, false);
    log().info("frames: {} knocked the item out of a glass frame", player.getRealName());
    return true;
}

// ---- events ---------------------------------------------------------------------------------------

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto&       region = player.getDimensionBlockSource();
    auto const& pos    = ev.blockPos();
    auto const& block  = region.getBlock(pos);

    if (isGlassBlock(block)) {
        ev.cancel();
        if (debounce(player, game::globalPos(region, pos))) return;
        use(player, region, pos);
        return;
    }

    // placing a glass frame item
    auto id = game::itemId(ev.item());
    if (id != kGlassFrame && id != kGlowingGlassFrame) return;
    if (!player.isSneaking() && block.getBlockType().isInteractiveBlock()) return; // the block reacts first
    if (!cfg().enableGlassItemFrame) {
        ev.cancel();
        return;
    }
    int      dir    = static_cast<int>(ev.face());
    BlockPos target = game::replaceable(region, pos) && !block.isAir() ? pos : offset(pos, dir);
    if (dir < 0 || dir > 5 || !game::replaceable(region, target) || !supportOk(region, target, dir)) ev.cancel();
}

void onDestroy(ll::event::PlayerDestroyBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& region = player.getDimensionBlockSource();
    if (punch(player, region, ev.pos())) ev.cancel();
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    auto const& pos  = ev.pos();
    bool        was  = isGlassBlock(ev.previousBlock());
    bool        is   = isGlassBlock(ev.newBlock());
    auto        gp   = game::globalPos(region, pos);
    auto&       s    = st();
    if (is && !was) {
        if (!s.frames.contains(gp)) {
            s.frames[gp] = Frame{};
            save(gp, s.frames[gp]);
        }
        s.checkSoon.insert(gp);
    } else if (was && !is) {
        auto it = s.frames.find(gp);
        if (it != s.frames.end()) {
            dropHeld(region, pos, it->second, nullptr);
            despawnDisplay(it->second);
            erase(gp);
            s.setFilled.erase(gp);
        }
    }
    // a support changed: check the frames hanging on it
    if (s.frames.empty()) return;
    for (int d = 0; d < 6; ++d) {
        auto np = game::globalPos(region, offset(pos, d));
        if (s.frames.contains(np)) s.checkSoon.insert(np);
    }
}

void onTick(ll::event::ServerLevelTickEvent&) {
    auto& s = st();
    ++s.tick;
    if (!s.loaded) return;

    // deferred `lepton:filled` changes (block edits inside cancelled interactions are rolled back)
    if (!s.setFilled.empty()) {
        auto todo = std::move(s.setFilled);
        s.setFilled.clear();
        for (auto const& [gp, filled] : todo) {
            auto* region = game::regionOf(gp.dim);
            if (!region) continue;
            BlockPos    pos   = game::blockPos(gp);
            auto const& block = region->getBlock(pos);
            if (!isGlassBlock(block)) continue;
            if (auto const* nb = bs::with(block, {{"lepton:filled", filled}}); nb && nb != &block)
                game::setBlock(*region, pos, *nb);
        }
    }

    if (!s.checkSoon.empty()) {
        auto todo = std::move(s.checkSoon);
        s.checkSoon.clear();
        for (auto const& gp : todo) {
            auto it = s.frames.find(gp);
            if (it == s.frames.end()) continue;
            auto* region = game::regionOf(gp.dim);
            if (!region) continue;
            BlockPos    pos   = game::blockPos(gp);
            auto const& block = region->getBlock(pos);
            if (!isGlassBlock(block)) continue;
            int dir = dirOf(block);
            if (!supportOk(*region, pos, dir)) {
                log().info("frames: a glass frame at {} lost its support", gp.key());
                breakFrame(*region, pos, block);
                continue;
            }
            updateAttachment(*region, pos, dir, it->second);
            if (it->second.dirty) syncDisplay(*region, gp, it->second, block);
        }
    }

    if (s.tick % 100 == 0) sweep(true);
    else if (cfg().glassItemFramesUpdateMaps && cfg().glassItemFramesUpdateMapsEveryTick) {
        for (auto& [gp, f] : s.frames) {
            if (game::isEmpty(f.item)) continue;
            auto* region = game::regionOf(gp.dim);
            if (!region) continue;
            BlockPos pos = game::blockPos(gp);
            if (region->hasChunksAt(pos, 0, false)) updateMap(*region, pos, f);
        }
    }
}

std::vector<std::string> recipeIds() { return {"lepton:glass_item_frame", "lepton:glowing_glass_item_frame"}; }

} // namespace

void applyConfig() {
    setRecipesEnabled(recipeIds(), cfg().enableGlassItemFrame);
    if (!st().loaded) return;
    for (auto& [gp, f] : st().frames) f.dirty = true; // e.g. a new itemRenderScale
    sweep(false);
}

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(onDestroy));
    gListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
    gListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
    static bool once = false;
    if (!once) {
        once = true;
        game::db::onWorldReady([] {
            loadAll();
            setRecipesEnabled(recipeIds(), cfg().enableGlassItemFrame);
        });
        game::db::onWorldClosed([] {
            st().frames.clear();
            st().loaded = false;
        });
    }
    if (game::db::available() && !st().loaded) {
        loadAll();
        setRecipesEnabled(recipeIds(), cfg().enableGlassItemFrame);
    }
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
}

} // namespace lp::feature::frames::glass

namespace lp::feature::frames::testing {

std::optional<GlassRecord> glassRecord(core::GlobalPos const& pos) {
    auto& frames = glass::st().frames;
    auto  it     = frames.find(pos);
    if (it == frames.end()) return std::nullopt;
    GlassRecord r;
    r.item       = game::isEmpty(it->second.item) ? std::string() : game::itemToSnbt(it->second.item);
    r.rotation   = it->second.rotation;
    r.display    = it->second.display;
    r.attachment = static_cast<int>(it->second.attachment);
    return r;
}

void glassSweep() { glass::sweep(true); }
int  mapUpdates() { return glass::st().mapUpdates; }

bool useGlass(Player& player, core::GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    return region && glass::use(player, *region, game::blockPos(pos));
}

void emptyGlass(core::GlobalPos const& pos) {
    auto& frames = glass::st().frames;
    auto  it     = frames.find(pos);
    if (it == frames.end()) return;
    it->second.item     = ItemStack{};
    it->second.rotation = 0;
    glass::despawnDisplay(it->second);
    glass::save(pos, it->second);
}

bool punchGlass(Player& player, core::GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    return region && glass::punch(player, *region, game::blockPos(pos));
}

} // namespace lp::feature::frames::testing
