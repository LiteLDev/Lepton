// ShearVines (cut vines) and MorePottedPlants.

#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/Container.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"

#include "feature/storage/Storage.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::storage::plants {

namespace {

namespace bs                        = game::blockstates;
using SoundEvent                    = SharedTypes::Legacy::LevelSoundEvent;
constexpr std::string_view kCutVine = "lepton:cut_vine";

struct Dir {
    std::string_view name;
    int              dx, dy, dz;
    int              face;     ///< FacingID of this side
    int              opposite; ///< FacingID of the neighbour's face towards us
    int              vineBit;  ///< Bedrock vine_direction_bits
};
// Bedrock vine_direction_bits: 1 south, 2 west, 4 north, 8 east
constexpr Dir kSides[4] = {
    {"north", 0, 0, -1, 2, 3, 4},
    {"south", 0, 0, 1,  3, 2, 1},
    {"west",  -1, 0, 0, 4, 5, 2},
    {"east",  1, 0, 0,  5, 4, 8},
};

BlockPos offset(BlockPos p, int dx, int dy, int dz) { return BlockPos{p.x + dx, p.y + dy, p.z + dz}; }

bool isVine(Block const& b) { return b.getTypeName() == "minecraft:vine"; }
bool isCutVine(Block const& b) { return b.getTypeName() == kCutVine; }

/// A full face of `b` towards `face` (Java Block.isFaceFull of the support shape).
bool fullFace(Block const& b, int face) {
    if (b.isAir()) return false;
    auto const& t = b.getBlockType();
    return t.canProvideSupport(b, static_cast<uchar>(face), BlockSupportType::Center)
        && t.canProvideSupport(b, static_cast<uchar>(face), BlockSupportType::Edge);
}

/// The vine at / above carries this side (vanilla vines and cut vines).
bool sideAbove(Block const& above, Dir const& d) {
    if (isCutVine(above)) return bs::getBool(above, std::string("lepton:") + std::string(d.name)).value_or(false);
    if (isVine(above)) return (bs::getInt(above, "vine_direction_bits").value_or(0) & d.vineBit) != 0;
    return false;
}

Player* playerById(int64_t id) {
    auto* a = game::entities::find(id);
    return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
}

void sound(BlockSource& region, BlockPos const& pos, SoundEvent event, Player& by) {
    Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    region.getLevel().broadcastSoundEvent(region, event, at, region.getBlock(pos), ActorSoundIdentifier(by), false, std::nullopt);
}

bool emptyFlowerPot(BlockSource& region, BlockPos const& pos) {
    if (region.getBlock(pos).getTypeName() != "minecraft:flower_pot") return false;
    auto* actor = region.getBlockEntity(pos);
    if (!actor) return true;
    return static_cast<FlowerPotBlockActor*>(actor)->mPlant == nullptr;
}

// ---- events -----------------------------------------------------------------------------------

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto&       region = player.getDimensionBlockSource();
    auto        pos    = ev.blockPos();
    auto const& b      = region.getBlock(pos);
    auto        id     = game::itemId(ev.item());
    auto        pid    = player.getOrCreateUniqueID().rawID;
    int         dim    = static_cast<int>(region.getDimensionId());

    // ShearVinesModule#onRightClick
    if (isVine(b) && id == "minecraft:shears" && cfg().enableShearVines) {
        ev.cancel();
        int slot = player.getSelectedItemSlot();
        defer([pid, dim, pos, slot] {
            auto* p = playerById(pid);
            auto* r = game::regionOf(dim);
            if (!p || !r || !shearVine(*r, pos)) return;
            sound(*r, pos, SoundEvent::Shear, *p);
            if (!p->isCreative()) {
                auto& inv   = game::inv::playerInventory(*p);
                auto  stack = inv.getItem(slot);
                if (game::itemId(stack) == "minecraft:shears") {
                    stack.hurtAndBreak(1, p);
                    inv.setItem(slot, stack);
                    game::inv::sync(*p);
                }
            }
            log().info("storage: {} cut a vine at {} {} {}", p->getRealName(), pos.x, pos.y, pos.z);
        });
        return;
    }

    if (!cfg().enableMorePottedPlants) return;
    // FlowerPotBlock#useItemOn: a pottable plant into an empty pot
    if (b.getTypeName() == "minecraft:flower_pot") {
        auto potted = pottedFor(id);
        if (potted.empty() || !emptyFlowerPot(region, pos) || player.isSneaking()) return;
        ev.cancel();
        int slot = player.getSelectedItemSlot();
        defer([pid, dim, pos, id, slot] {
            auto* p = playerById(pid);
            auto* r = game::regionOf(dim);
            if (!p || !r || !pot(*r, pos, id)) return;
            if (!p->isCreative()) {
                auto& inv = game::inv::playerInventory(*p);
                if (game::itemId(inv.getItem(slot)) == id) {
                    inv.removeItem(slot, 1);
                    game::inv::sync(*p);
                }
            }
        });
        return;
    }
    // FlowerPotBlock#useWithoutItem: take the plant out
    if (!plantOf(b.getTypeName()).empty()) {
        if (player.isSneaking() && !ev.item().isNull()) return; // secondary use skips the block
        ev.cancel();
        if (!pottedFor(id).empty()) return; // a pottable plant on a full pot does nothing
        defer([pid, dim, pos] {
            auto* p = playerById(pid);
            auto* r = game::regionOf(dim);
            if (!p || !r) return;
            auto plant = unpot(*r, pos);
            if (!plant.empty()) giveOrDrop(*p, game::makeItem(plant, 1));
        });
    }
}

std::vector<ll::event::ListenerPtr>& listeners() {
    static std::vector<ll::event::ListenerPtr> v;
    return v;
}

} // namespace

// ---- public -----------------------------------------------------------------------------------

void blockChanged(BlockSource& region, BlockPos const& pos, Block const& newBlock, Block const& below) {
    // a cut vine's support is the block on each of its sides and the block above it
    int  dim   = static_cast<int>(region.getDimensionId());
    auto queue = [dim](BlockPos at) {
        defer([dim, at] {
            if (auto* r = game::regionOf(dim)) checkCutVine(*r, at);
        });
    };
    if (isCutVine(newBlock)) queue(pos);
    if (isCutVine(below)) queue(offset(pos, 0, -1, 0));
    for (auto const& d : kSides) {
        auto p = offset(pos, d.dx, 0, d.dz);
        if (isCutVine(region.getBlock(p))) queue(p);
    }
}

bool shearVine(BlockSource& region, BlockPos const& pos) {
    if (!cfg().enableShearVines) return false;
    auto const& b = region.getBlock(pos);
    if (!isVine(b)) return false;
    int           bits = bs::getInt(b, "vine_direction_bits").value_or(0);
    bs::Changes   ch;
    for (auto const& d : kSides) ch.emplace_back("lepton:" + std::string(d.name), (bits & d.vineBit) != 0);
    ch.emplace_back("lepton:up", bits == 0); // a vine without sides hangs from the block above
    auto const* cut = bs::of(kCutVine, ch);
    if (!cut) return false;
    game::setBlock(region, pos, *cut);
    for (auto p = offset(pos, 0, -1, 0); p.y >= region.getMinHeight(); p = offset(p, 0, -1, 0)) {
        auto const& x = region.getBlock(p);
        if (!isVine(x) && !isCutVine(x)) break;
        game::clearBlock(region, p);
    }
    return true;
}

bool checkCutVine(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (!isCutVine(b)) return false;
    auto const& above = region.getBlock(offset(pos, 0, 1, 0));
    bs::Changes ch;
    bool        any = false, changed = false;
    for (auto const& d : kSides) {
        auto key = "lepton:" + std::string(d.name);
        bool has = bs::getBool(b, key).value_or(false);
        if (!has) continue;
        bool ok = fullFace(region.getBlock(offset(pos, d.dx, d.dy, d.dz)), d.opposite) || sideAbove(above, d);
        if (!ok) {
            ch.emplace_back(key, false);
            changed = true;
        } else any = true;
    }
    if (bs::getBool(b, "lepton:up").value_or(false)) {
        if (fullFace(above, 0)) any = true;
        else {
            ch.emplace_back("lepton:up", false);
            changed = true;
        }
    }
    if (!any) {
        game::clearBlock(region, pos); // Quark's cut vine drops nothing
        return true;
    }
    if (!changed) return false;
    if (auto const* n = bs::with(b, ch)) game::setBlock(region, pos, *n);
    return true;
}

bool pot(BlockSource& region, BlockPos const& pos, std::string_view itemId) {
    if (!cfg().enableMorePottedPlants || !emptyFlowerPot(region, pos)) return false;
    auto potted = pottedFor(itemId);
    if (potted.empty()) return false;
    auto const* b = bs::of(potted);
    return b && game::setBlock(region, pos, *b);
}

std::string unpot(BlockSource& region, BlockPos const& pos) {
    auto plant = plantOf(region.getBlock(pos).getTypeName());
    if (plant.empty()) return {};
    auto const* pot = bs::of("minecraft:flower_pot");
    if (!pot || !game::setBlock(region, pos, *pot)) return {};
    return plant;
}

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    listeners().push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : listeners()) bus.removeListener(l);
    listeners().clear();
}

} // namespace lp::feature::storage::plants
