// U7 newwoods potted saplings (Quark's potted_<x>_sapling, docs/spec/newwoods.md §2.2): using a
// sapling on an empty vanilla flower pot swaps the pot for lepton:potted_<x>_sapling; using the potted
// block without a pottable item gives the sapling back and puts the empty pot back. Breaking it drops
// the pot and the sapling (loot table).

#include <string>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/FlowerPotBlockActor.h"

#include "feature/newwoods/NewWoods.h"
#include "feature/newwoods/Runtime.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::newwoods {

namespace bs = game::blockstates;

namespace {

bool emptyFlowerPot(BlockSource& region, BlockPos const& pos) {
    if (region.getBlock(pos).getTypeName() != "minecraft:flower_pot") return false;
    auto* actor = region.getBlockEntity(pos);
    if (!actor) return true;
    Block const* plant = static_cast<FlowerPotBlockActor*>(actor)->mPlant;
    return plant == nullptr;
}

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

void giveOrDrop(Player& player, ItemStack stack) {
    auto& inv = game::inv::playerInventory(player);
    game::inv::insert(inv, stack, game::inv::kPlayerAll);
    game::inv::sync(player);
    if (!game::isEmpty(stack)) {
        auto& region = player.getDimensionBlockSource();
        region.getLevel().getSpawner().spawnItem(region, stack, nullptr, player.getPosition(), 0);
    }
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto&       region = player.getDimensionBlockSource();
    auto        pos    = ev.blockPos();
    auto const& b      = region.getBlock(pos);
    auto        id     = game::itemId(ev.item());
    auto        pid    = player.getOrCreateUniqueID().rawID;
    int         dim    = static_cast<int>(region.getDimensionId());

    // FlowerPotBlock#useItemOn: a sapling into an empty pot (the change is made next tick: a
    // cancelled interaction rolls back block changes)
    if (b.getTypeName() == "minecraft:flower_pot") {
        auto const* tree = treeOfSapling(id);
        if (!tree || !moduleEnabled(tree->module) || player.isSneaking() || !emptyFlowerPot(region, pos)) return;
        ev.cancel();
        int slot = player.getSelectedItemSlot();
        defer([pid, dim, pos, id, slot] {
            auto* p = playerById(pid);
            auto* r = game::regionOf(dim);
            if (!p || !r || !potSapling(*r, pos, id)) return;
            if (!p->isCreative()) {
                auto& inv = game::inv::playerInventory(*p);
                if (game::itemId(inv.getItem(slot)) == id) {
                    inv.removeItem(slot, 1);
                    game::inv::sync(*p);
                }
            }
            log().info("newwoods: {} potted a {}", p->getRealName(), id);
        });
        return;
    }
    // FlowerPotBlock#useWithoutItem: take the sapling out
    if (b.getTypeName().starts_with("lepton:potted_") && hasTag(b, "lepton:nw_potted")) {
        if (player.isSneaking() && !ev.item().isNull()) return;
        ev.cancel();
        if (treeOfSapling(id)) return; // a sapling on a full pot does nothing
        defer([pid, dim, pos] {
            auto* p = playerById(pid);
            auto* r = game::regionOf(dim);
            if (!p || !r) return;
            auto sapling = unpotSapling(*r, pos);
            if (!sapling.empty()) giveOrDrop(*p, game::makeItem(sapling, 1));
        });
    }
}

std::vector<ll::event::ListenerPtr>& listeners() {
    static std::vector<ll::event::ListenerPtr> v;
    return v;
}

} // namespace

bool potSapling(BlockSource& region, BlockPos const& pos, std::string_view saplingId) {
    auto const* tree = treeOfSapling(saplingId);
    if (!tree || !moduleEnabled(tree->module) || !emptyFlowerPot(region, pos)) return false;
    auto const* potted = bs::of("lepton:potted_" + std::string(tree->name) + "_sapling");
    return potted && game::setBlock(region, pos, *potted);
}

std::string unpotSapling(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    auto const& n = b.getTypeName();
    if (!n.starts_with("lepton:potted_") || !hasTag(b, "lepton:nw_potted")) return {};
    std::string sapling = "lepton:" + n.substr(14); // lepton:potted_<x>_sapling -> lepton:<x>_sapling
    auto const* pot     = bs::of("minecraft:flower_pot");
    if (!pot || !game::setBlock(region, pos, *pot)) return {};
    return sapling;
}

void installPottedHooks() {
    auto& bus = ll::event::EventBus::getInstance();
    listeners().push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
}

void uninstallPottedHooks() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : listeners()) bus.removeListener(l);
    listeners().clear();
}

} // namespace lp::feature::newwoods
