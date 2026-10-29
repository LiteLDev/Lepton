// Rope (Quark RopeBlock / RopeModule): hanging, pulling down / up, moving the block at the end,
// dispenser behaviour.

#include <fstream>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/dataloadhelper/DefaultDataLoadHelper.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/phys/AABB.h"

#include "feature/storage/Storage.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace lp::feature::storage::rope {

namespace {

namespace bs                     = game::blockstates;
using SoundEvent                 = SharedTypes::Legacy::LevelSoundEvent;
constexpr std::string_view kRope = "lepton:rope";

BlockPos below(BlockPos p) { return BlockPos{p.x, p.y - 1, p.z}; }
BlockPos above(BlockPos p) { return BlockPos{p.x, p.y + 1, p.z}; }

bool isRope(Block const& b) { return b.getTypeName() == kRope; }
bool inWorld(BlockSource& region, BlockPos const& p) { return p.y >= region.getMinHeight() && p.y < region.getMaxHeight(); }

bool isLiquid(Block const& b) {
    auto const& n = b.getTypeName();
    return n == "minecraft:water" || n == "minecraft:flowing_water" || n == "minecraft:lava" || n == "minecraft:flowing_lava";
}
bool isWater(Block const& b) {
    auto const& n = b.getTypeName();
    return n == "minecraft:water" || n == "minecraft:flowing_water";
}

/// Water at `p`, as the block or as the liquid layer of a waterlogged block.
bool waterAt(BlockSource& region, BlockPos const& p) {
    return isWater(region.getBlock(p)) || isWater(region.getExtraBlock(p));
}

void sound(BlockSource& region, BlockPos const& pos, SoundEvent event, Player* by, Block const& block) {
    if (!by) return; // sounds need an actor identifier
    Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    region.getLevel().broadcastSoundEvent(region, event, at, block, ActorSoundIdentifier(*by), false, std::nullopt);
}

// ---- PistonsMoveTileEntities (U10) rules -------------------------------------------------------

nlohmann::json readOther(char const* section) {
    std::ifstream in(config::configDir() / (std::string(section) + ".json"));
    if (!in) return {};
    return nlohmann::json::parse(in, nullptr, false, true);
}

struct PmteRules {
    bool                     enabled = true;
    std::vector<std::string> blacklist{
        "minecraft:mob_spawner",
        "minecraft:trial_spawner",
        "minecraft:vault",
        "minecraft:ender_chest",
        "minecraft:enchanting_table",
        "minecraft:trapped_chest",
    };
    std::vector<std::string> namespaces;
};

PmteRules& cachedRules() {
    static PmteRules r;
    return r;
}

PmteRules readRules() {
    PmteRules r;
    auto      j = readOther("redstone");
    if (!j.is_object()) return r;
    if (auto it = j.find("enablePistonsMoveTileEntities"); it != j.end() && it->is_boolean()) r.enabled = it->get<bool>();
    if (auto it = j.find("movementBlacklist"); it != j.end() && it->is_array()) {
        r.blacklist.clear();
        for (auto const& v : *it)
            if (v.is_string()) {
                auto s = v.get<std::string>();
                if (s == "minecraft:spawner") s = "minecraft:mob_spawner";
                if (s == "#c:chests/trapped") s = "minecraft:trapped_chest";
                r.blacklist.push_back(s);
            }
    }
    if (auto it = j.find("namespaceMovementBlacklist"); it != j.end() && it->is_array())
        for (auto const& v : *it)
            if (v.is_string()) r.namespaces.push_back(v.get<std::string>());
    return r;
}

/// Quark PistonsMoveTileEntitiesModule#shouldMoveTE(state): true = must NOT move.
bool blacklisted(Block const& b, PmteRules const& r) {
    auto const& n = b.getTypeName();
    if (n.ends_with("_hanging_sign")) return true; // #minecraft:ceiling_hanging_signs
    if (n == "minecraft:jukebox") return true;      // a jukebox playing a disc (Bedrock: any block actor data)
    if (n == "minecraft:piston_arm_collision" || n == "minecraft:sticky_piston_arm_collision") return true;
    for (auto const& x : r.blacklist)
        if (x == n || (x.starts_with("#") && n.find(x.substr(x.find(':') + 1)) != std::string::npos)) return true;
    auto ns = n.substr(0, n.find(':'));
    for (auto const& x : r.namespaces)
        if (x == ns) return true;
    return false;
}

/// Blocks a piston would not push normally (Java PushReaction != NORMAL) or that Quark refuses.
bool immovable(Block const& b) {
    auto const& n = b.getTypeName();
    static constexpr std::string_view kNo[] = {
        "minecraft:obsidian", "minecraft:crying_obsidian", "minecraft:respawn_anchor", "minecraft:moving_block",
        "minecraft:piston", "minecraft:sticky_piston", "minecraft:end_portal", "minecraft:portal",
        "minecraft:end_gateway", "minecraft:reinforced_deepslate", "minecraft:beacon",
    };
    for (auto x : kNo)
        if (x == n) return true;
    return b.getBlockType().getDestroySpeed() < 0.f; // unbreakable (bedrock, barrier, command blocks, ...)
}

/// Quark RopeBlock#moveBlock.
void moveBlock(BlockSource& region, BlockPos const& src, BlockPos const& dst) {
    if (region.getLevel().isClientSide()) return;
    auto const& state = region.getBlock(src);
    if (state.isAir() || isLiquid(state) || immovable(state)) return;
    if (!state.getBlockType().canSurvive(region, dst)) return;

    std::optional<CompoundTag> data;
    if (auto* be = region.getBlockEntity(src)) {
        auto const& c = cfg();
        auto const& r = cachedRules();
        if (blacklisted(state, r) || (!c.forceEnableMoveTileEntities && !r.enabled)) return;
        CompoundTag tag;
        auto        ctx = SaveContextFactory::createCloneSaveContext();
        if (!be->save(tag, *ctx)) return;
        data = std::move(tag); // setBlock does not drop container contents: the data moves
    }

    Block const& moved = state;
    game::clearBlock(region, src); // a waterlogged block leaves its water behind (liquid layer)
    game::setBlock(region, dst, moved);
    if (data) {
        if (auto* nbe = region.getBlockEntity(dst)) {
            data->putInt("x", dst.x);
            data->putInt("y", dst.y);
            data->putInt("z", dst.z);
            DefaultDataLoadHelper helper;
            nbe->load(region.getLevel(), *data, helper);
            nbe->refresh(region);
        }
    }
    // entities standing on the moved block move with it
    AABB box{
        Vec3{static_cast<float>(src.x), static_cast<float>(src.y + 1), static_cast<float>(src.z)},
        Vec3{static_cast<float>(src.x + 1), static_cast<float>(src.y + 2), static_cast<float>(src.z + 1)}
    };
    float dy = static_cast<float>(dst.y - src.y);
    for (auto* a : game::entities::inBox(region, box)) {
        auto p = a->getPosition();
        if (a->isPlayer()) p = a->getFeetPos();
        a->teleportTo(Vec3{p.x, p.y + dy, p.z}, false, 0, 0, false);
    }
}

/// The plan of a pull down: the position the rope goes to (the block there moves one down).
std::optional<BlockPos> planDown(BlockSource& region, BlockPos pos) {
    bool endRope = false, wasAirAtEnd = false, can = false;
    while (true) {
        pos = below(pos);
        if (!inWorld(region, pos)) return std::nullopt;
        auto const& b = region.getBlock(pos);
        if (isRope(b)) continue;
        if (endRope) {
            can = wasAirAtEnd || b.isAir() || game::replaceable(region, pos);
            break;
        }
        endRope     = true;
        wasAirAtEnd = b.isAir() || isLiquid(b);
    }
    if (!can) return std::nullopt;
    return above(pos);
}

Player* playerById(int64_t id) {
    auto* a = game::entities::find(id);
    return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
}

void giveRope(Player& player) {
    if (player.isCreative()) return;
    giveOrDrop(player, game::makeItem(kRope, 1));
}

void consumeHeld(Player& player, int slot, std::string_view id) {
    if (player.isCreative()) return;
    auto& inv = game::inv::playerInventory(player);
    auto  s    = inv.getItem(slot);
    if (game::itemId(s) != id) return;
    inv.removeItem(slot, 1);
    game::inv::sync(player);
}

/// Replaces one held `from` with `to` (bottles / buckets).
void exchangeHeld(Player& player, int slot, std::string_view from, ItemStack const& to) {
    auto& inv = game::inv::playerInventory(player);
    auto  s    = inv.getItem(slot);
    if (game::itemId(s) != from) return;
    if (s.mCount <= 1) {
        inv.setItem(slot, to);
        game::inv::sync(player);
    } else {
        inv.removeItem(slot, 1);
        giveOrDrop(player, to);
    }
}

// ---- interaction ----------------------------------------------------------------------------------

enum class Action { None, Down, Up, Bottle, Bucket };

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    if (!cfg().enableRope) return;
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.blockPos();
    if (!isRope(region.getBlock(pos))) return;
    auto const& item = ev.item();
    auto        id   = game::itemId(item);
    bool        sneaking = player.isSneaking();
    Action      action   = Action::None;
    if (sneaking) {
        // secondary use skips the block unless both hands are empty (rope in hand: places beside)
        if (item.isNull()) action = Action::Up;
    } else if (id == kRope) {
        if (planDown(region, pos)) action = Action::Down; // else: the rope is placed on the clicked face
    } else if (id == "minecraft:bucket") {
        auto const& bottom = region.getBlock(bottomOf(region, pos));
        action             = isLiquid(bottom) && bs::getInt(bottom, "liquid_depth").value_or(1) == 0 ? Action::Bucket : Action::Up;
    } else if (id == "minecraft:glass_bottle") {
        action = waterAt(region, bottomOf(region, pos)) ? Action::Bottle : Action::Up;
    } else {
        action = Action::Up;
    }
    if (action == Action::Up && bottomOf(region, pos) == below(pos)) return; // the clicked rope is the bottom one
    if (action == Action::None) return;
    ev.cancel();
    auto pid  = player.getOrCreateUniqueID().rawID;
    int  dim  = static_cast<int>(region.getDimensionId());
    int  slot = player.getSelectedItemSlot();
    defer([pid, dim, pos, action, id, slot] {
        auto* p = playerById(pid);
        auto* r = game::regionOf(dim);
        if (!p || !r || !isRope(r->getBlock(pos))) return;
        switch (action) {
        case Action::Down:
            if (pullDown(*r, pos)) {
                consumeHeld(*p, slot, kRope);
                sound(*r, pos, SoundEvent::Place, p, r->getBlock(pos));
            }
            break;
        case Action::Up: {
            auto bottomRope = above(bottomOf(*r, pos));
            auto const& rb  = r->getBlock(bottomRope);
            if (pullUp(*r, pos)) {
                giveRope(*p);
                sound(*r, bottomRope, SoundEvent::Break, p, rb);
            }
            break;
        }
        case Action::Bottle:
            if (waterAt(*r, bottomOf(*r, pos))) {
                auto water = game::itemFromSnbt(R"({"Name":"minecraft:potion","Count":1b,"Damage":0s})");
                exchangeHeld(*p, slot, "minecraft:glass_bottle", water);
                sound(*r, pos, SoundEvent::BottleFill, p, r->getBlock(pos));
            }
            break;
        case Action::Bucket: {
            auto        at     = bottomOf(*r, pos);
            auto const& bottom = r->getBlock(at);
            if (!isLiquid(bottom) || bs::getInt(bottom, "liquid_depth").value_or(1) != 0) break;
            bool lava = bottom.getTypeName().ends_with("lava");
            game::clearBlock(*r, at);
            exchangeHeld(*p, slot, "minecraft:bucket", game::makeItem(lava ? "minecraft:lava_bucket" : "minecraft:water_bucket"));
            sound(*r, pos, lava ? SoundEvent::BucketFillLava : SoundEvent::BucketFillWater, p, r->getBlock(pos));
            break;
        }
        default:
            break;
        }
        (void)id;
    });
}

// Rope must hang: placing it where it can't is refused.
void onPlacing(ll::event::PlayerPlacingBlockEvent& ev) {
    if (!cfg().enableRope) return;
    auto& player = ev.self();
    if (player.isClientSide() || game::itemId(player.getSelectedItem()) != kRope) return;
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.pos();
    if (!game::replaceable(region, pos)) {
        static constexpr int kOff[6][3] = {
            {0,  -1, 0 },
            {0,  1,  0 },
            {0,  0,  -1},
            {0,  0,  1 },
            {-1, 0,  0 },
            {1,  0,  0 }
        };
        int f = ev.face();
        if (f >= 0 && f < 6) pos = BlockPos{pos.x + kOff[f][0], pos.y + kOff[f][1], pos.z + kOff[f][2]};
    }
    if (!canHang(region, pos)) ev.cancel();
}

// ---- dispenser --------------------------------------------------------------------------------

bool dispenseRope(BlockSource& region, Container& container, int slot, Vec3 const& pos) {
    auto front = game::blockPosOf(pos);
    if (dispenseAt(region, front)) container.removeItem(slot, 1);
    return true; // handled either way: the rope stays in the dispenser on failure (Java)
}

bool ropeDispenser(Item const& item) {
    return cfg().enableRope && cfg().enableDispenserBehavior && item.mFullName->getString() == kRope;
}

LL_TYPE_INSTANCE_HOOK(
    RopeDispenseHook,
    ll::memory::HookPriority::Normal,
    Item,
    &Item::$dispense,
    bool,
    BlockSource& region,
    Container&   container,
    int          slot,
    Vec3 const&  pos,
    uchar        face
) {
    if (ropeDispenser(*this)) return dispenseRope(region, container, slot, pos);
    return origin(region, container, slot, pos, face);
}

LL_TYPE_INSTANCE_HOOK(
    RopeComponentDispenseHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$dispense,
    bool,
    BlockSource& region,
    Container&   container,
    int          slot,
    Vec3 const&  pos,
    uchar        face
) {
    if (ropeDispenser(*this)) return dispenseRope(region, container, slot, pos);
    return origin(region, container, slot, pos, face);
}

std::vector<ll::event::ListenerPtr>& listeners() {
    static std::vector<ll::event::ListenerPtr> v;
    return v;
}

} // namespace

// ---- public -----------------------------------------------------------------------------------

void reloadRules() { cachedRules() = readRules(); }

void blockChanged(BlockSource& region, BlockPos const& pos, Block const& newBlock, Block const& below) {
    // a placed rope, or the rope below lost (or got) its support
    int dim = static_cast<int>(region.getDimensionId());
    for (auto const& [hit, at] : {std::pair{isRope(newBlock), pos}, std::pair{isRope(below), BlockPos{pos.x, pos.y - 1, pos.z}}}) {
        if (!hit) continue;
        defer([dim, at] {
            if (auto* r = game::regionOf(dim)) checkSurvival(*r, at);
        });
    }
}

bool canHang(BlockSource& region, BlockPos const& pos) {
    auto        up = above(pos);
    auto const& b  = region.getBlock(up);
    if (isRope(b)) return true;
    if (b.isAir()) return false;
    // Java isFaceSturdy(DOWN) (a full face): both the centre and the edges give support
    auto const& t = b.getBlockType();
    return t.canProvideSupport(b, 0, BlockSupportType::Center) && t.canProvideSupport(b, 0, BlockSupportType::Edge);
}

BlockPos bottomOf(BlockSource& region, BlockPos const& pos) {
    BlockPos p = pos;
    do {
        p = below(p);
    } while (inWorld(region, p) && isRope(region.getBlock(p)));
    return p;
}

bool pullDown(BlockSource& region, BlockPos const& pos) {
    auto ropePos = planDown(region, pos);
    if (!ropePos) return false;
    moveBlock(region, *ropePos, below(*ropePos));
    auto const& here = region.getBlock(*ropePos);
    if (!here.isAir() && !game::replaceable(region, *ropePos)) return false;
    auto const* rope = bs::of(kRope);
    if (!rope) return false;
    game::setBlock(region, *ropePos, *rope);
    return true;
}

bool pullUp(BlockSource& region, BlockPos const& pos) {
    auto bottom  = bottomOf(region, pos);
    auto ropePos = above(bottom);
    if (ropePos == pos) return false;
    game::clearBlock(region, ropePos); // a waterlogged rope leaves its water
    moveBlock(region, bottom, ropePos);
    return true;
}

bool dispenseAt(BlockSource& region, BlockPos const& front) {
    if (!cfg().enableRope) return false;
    auto const& b = region.getBlock(front);
    if (isRope(b)) return pullDown(region, front);
    if (b.isAir() && canHang(region, front)) {
        if (auto const* rope = bs::of(kRope)) return game::setBlock(region, front, *rope);
    }
    return false;
}

void checkSurvival(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (!isRope(b) || canHang(region, pos)) return;
    // Quark: destroy particles, drop the rope, air
    BlockChangeContext ctx{};
    region.getLevel().destroyBlock(region, pos, true, ctx);
}

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    listeners().push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
    listeners().push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(onPlacing));
    RopeDispenseHook::hook();
    RopeComponentDispenseHook::hook();
    reloadRules();
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : listeners()) bus.removeListener(l);
    listeners().clear();
    RopeDispenseHook::unhook();
    RopeComponentDispenseHook::unhook();
}

} // namespace lp::feature::storage::rope
