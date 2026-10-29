// Block tweaks: ShulkerPacking, MagmaKeepsConcretePowder, ImprovedSponges, DoubleDoorOpening.
// Spec: docs/spec/mobtweaks.md §3.

#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <unordered_set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/Container.h"
#include "mc/world/Direction.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/ConcretePowderBlock.h"
#include "mc/world/level/block/DoorBlock.h"
#include "mc/world/level/block/FenceGateBlock.h"
#include "mc/world/level/block/SpongeBlock.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"

#include "feature/mobtweaks/Blocks.h"
#include "feature/mobtweaks/Items.h"
#include "feature/mobtweaks/MobTweaks.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "game/VirtualContainer.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::mobtweaks {

namespace bs     = game::blockstates;
namespace shapes = game::shapes;
using SoundEvent = SharedTypes::Legacy::LevelSoundEvent;

namespace {

std::string const& nameOf(Block const& b) { return b.getTypeName(); }
bool hasTag(Block const& b, char const* tag) { return b.hasTag(HashedString(tag)); }
BlockPos below(BlockPos const& p) { return {p.x, p.y - 1, p.z}; }
BlockPos above(BlockPos const& p) { return {p.x, p.y + 1, p.z}; }

Player* playerById(int64_t id) {
    auto* a = game::entities::find(id);
    return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
}

void playSound(BlockSource& region, BlockPos const& pos, SoundEvent event, Player& by) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    level->broadcastSoundEvent(region, event, at, region.getBlock(pos), ActorSoundIdentifier(by), false, std::nullopt);
}

/// Per (player, position) debounce: Bedrock reports one click several times.
bool debounced(int64_t player, core::GlobalPos const& at) {
    static std::map<std::pair<int64_t, core::GlobalPos>, uint64_t> seen;
    auto key = std::make_pair(player, at);
    auto now = tickNow();
    if (auto it = seen.find(key); it != seen.end() && now - it->second < 5) return true;
    if (seen.size() > 256) seen.clear();
    seen[key] = now;
    return false;
}

// ---- ShulkerPacking ------------------------------------------------------------------------------

bool isShell(ItemStack const& s) { return game::itemId(s) == "minecraft:shulker_shell"; }

bool isPackableChest(Block const& b) {
    auto const& n = nameOf(b);
    return n == "minecraft:chest" || n == "minecraft:trapped_chest" || hasTag(b, kChestTag);
}

void onInteractForShulker(ll::event::PlayerInteractBlockEvent& ev) {
    if (!cfg().enableShulkerPacking) return;
    auto& player = ev.self();
    if (player.isClientSide() || !player.isSneaking()) return;
    auto const& main = player.getSelectedItem();
    bool        hands = isShell(main) && isShell(player.getOffhandSlot());
    bool        stack = isShell(main) && main.mCount >= 2;
    if (!hands && !stack) return;
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.blockPos();
    if (!isPackableChest(region.getBlock(pos))) return;
    ev.cancel();
    auto id = player.getOrCreateUniqueID().rawID;
    if (debounced(id, game::globalPos(region, pos))) return;
    int dim = static_cast<int>(region.getDimensionId());
    later(1, [id, dim, pos] {
        auto* p      = playerById(id);
        auto* region = game::regionOf(dim);
        if (p && region) packChest(*p, *region, pos);
    });
}

// ---- MagmaKeepsConcretePowder --------------------------------------------------------------------

bool magmaBelow(BlockSource const& region, BlockPos const& pos) {
    if (!cfg().enableMagmaKeepsConcretePowder) return false;
    return nameOf(region.getBlock(below(pos))) == "minecraft:magma";
}

LL_TYPE_STATIC_HOOK(
    ConcreteTouchingHook,
    ll::memory::HookPriority::Normal,
    ConcretePowderBlock,
    &ConcretePowderBlock::_isTouchingWater,
    bool,
    BlockSource const& region,
    BlockPos const&    pos
) {
    if (magmaBelow(region, pos)) return false;
    return origin(region, pos);
}

LL_TYPE_STATIC_HOOK(
    ConcreteTryTouchHook,
    ll::memory::HookPriority::Normal,
    ConcretePowderBlock,
    &ConcretePowderBlock::_tryTouchWater,
    bool,
    BlockSource&                 region,
    BlockPos const&              pos,
    std::optional<HashedString>  name
) {
    if (magmaBelow(region, pos)) return false;
    return origin(region, pos, std::move(name));
}

// ---- ImprovedSponges -----------------------------------------------------------------------------

LL_TYPE_STATIC_HOOK(
    SpongeAbsorbHook,
    ll::memory::HookPriority::Normal,
    SpongeBlock,
    &SpongeBlock::_performAbsorbWater,
    bool,
    BlockSource&    region,
    BlockPos const& start
) {
    auto const& c = cfg();
    if (!c.enableImprovedSponges) return origin(region, start);
    return absorbWater(region, start, std::max(c.maximumWaterDrain, 64), std::max(c.maximumCrawlDistance, 6)) > 0;
}

/// Opens / closes a vanilla fence gate like a player's use (Java FenceGateBlock#useWithoutItem: an
/// opening gate swings away from the player).
bool setVanillaGateOpen(BlockSource& region, BlockPos const& pos, bool open, Player* by) {
    auto const& b = region.getBlock(pos);
    bs::Changes ch{{"open_bit", open}};
    if (open && by) {
        auto look   = shapes::lookDirection(by->getRotation().y);
        auto facing = shapes::facingOf(b).value_or("");
        if (facing == shapes::opposite(look)) {
            if (bs::get(b, "minecraft:cardinal_direction")) {
                ch.emplace_back("minecraft:cardinal_direction", std::string(look));
            } else if (bs::get(b, "direction")) {
                static constexpr std::string_view kLegacy[4] = {"south", "west", "north", "east"};
                for (int i = 0; i < 4; ++i)
                    if (kLegacy[i] == look) ch.emplace_back("direction", i);
            }
        }
    }
    auto const* n = bs::with(b, ch);
    if (!n || !game::setBlock(region, pos, *n)) return false;
    if (by) playSound(region, pos, open ? SoundEvent::FenceGateOpen : SoundEvent::FenceGateClose, *by);
    return true;
}

bool isWaterSource(Block const& b) {
    auto const& n = nameOf(b);
    return n == "minecraft:water" && bs::getInt(b, "liquid_depth").value_or(0) == 0;
}

void onUseForSponge(ll::event::PlayerUseItemEvent& ev) {
    auto const& c = cfg();
    if (!c.enableImprovedSponges || !c.enablePlacingOnWater) return;
    auto& player = ev.self();
    if (player.isClientSide() || game::itemId(ev.item()) != "minecraft:sponge") return;
    auto& region = player.getDimensionBlockSource();
    auto  target = waterSourceInView(player);
    if (!target) return;
    ev.cancel();
    auto id = player.getOrCreateUniqueID().rawID;
    if (debounced(id, game::globalPos(region, *target))) return;
    int  dim = static_cast<int>(region.getDimensionId());
    auto pos = *target;
    later(1, [id, dim, pos] {
        auto* p      = playerById(id);
        auto* region = game::regionOf(dim);
        if (!p || !region) return;
        if (game::itemId(p->getSelectedItem()) != "minecraft:sponge") return;
        if (!placeSpongeOnWater(*p, *region, pos)) return;
        if (!p->isCreative()) {
            ItemStack held = p->getSelectedItem();
            held.remove(1);
            p->setSelectedItem(held);
            p->sendInventory(false);
        }
    });
}

// ---- DoubleDoorOpening ---------------------------------------------------------------------------

enum class Openable { None, VanillaDoor, VanillaGate, LeptonDoor, LeptonGate };

Openable openableOf(Block const& b) {
    switch (shapes::kindOf(b)) {
    case shapes::Kind::Door:
        return Openable::LeptonDoor;
    case shapes::Kind::FenceGate:
        return Openable::LeptonGate;
    default:
        break;
    }
    auto const& n = nameOf(b);
    if (!n.starts_with("minecraft:")) return Openable::None;
    if (n.ends_with("fence_gate")) return Openable::VanillaGate;
    if (n.ends_with("_door") && n.find("trapdoor") == std::string::npos) return Openable::VanillaDoor;
    return Openable::None;
}

void onInteractForDoors(ll::event::PlayerInteractBlockEvent& ev) {
    auto const& c = cfg();
    if (!c.enableDoubleDoorOpening) return;
    auto& player = ev.self();
    if (player.isClientSide() || player.isSneaking()) return;
    auto&       region = player.getDimensionBlockSource();
    auto        pos    = ev.blockPos();
    auto const& b      = region.getBlock(pos);
    auto        kind   = openableOf(b);
    if (kind == Openable::None || hasTag(b, kNonDoubleDoorTag)) return;
    bool door = kind == Openable::VanillaDoor || kind == Openable::LeptonDoor;
    if (door ? !c.enableDoors : !c.enableFenceGates) return;
    auto id = player.getOrCreateUniqueID().rawID;
    if (debounced(id, game::globalPos(region, pos))) return;
    auto before = doorState(region, pos);
    if (!before) return;
    int dim = static_cast<int>(region.getDimensionId());
    // The door/gate toggles during this interaction (vanilla) or on the next tick (Lepton shapes).
    later(2, [id, dim, pos, before = *before] {
        auto* region = game::regionOf(dim);
        if (!region) return;
        auto now = doorState(*region, pos);
        if (!now || now->block != before.block || now->open == before.open) return; // not toggled
        if (followPartner(*region, pos, before, now->open, playerById(id))) {
            log().info("mobtweaks: double door at {} {} {}", pos.x, pos.y, pos.z);
        }
    });
}

} // namespace

// ---- shared (selftests) --------------------------------------------------------------------------

bool packChest(Player& player, BlockSource& region, BlockPos const& pos) {
    if (!cfg().enableShulkerPacking) return false;
    auto const& block = region.getBlock(pos);
    if (!isPackableChest(block)) return false;

    ItemStack const& main  = player.getSelectedItem();
    bool             hands = isShell(main) && isShell(player.getOffhandSlot());
    if (!hands && !(isShell(main) && main.mCount >= 2)) return false;

    std::vector<ItemStack> items;
    std::vector<ItemStack> leftover;
    std::string            customName;
    if (nameOf(block).starts_with("minecraft:")) {
        auto* be = region.getBlockEntity(pos);
        if (!be) return false;
        auto* container = static_cast<ChestBlockActor*>(be)->getContainer();
        if (!container || container->getContainerSize() != 27) return false; // double chests stay
        auto        ctx = SaveContextFactory::createCloneSaveContext();
        CompoundTag saved;
        if (be->save(saved, *ctx) && saved.contains("CustomName")) customName = saved.at("CustomName").get<StringTag>();
        for (int i = 0; i < 27; ++i) {
            items.push_back(container->getItem(i));
            container->setItem(i, ItemStack{});
        }
    } else {
        items = game::vc::takeAll(game::globalPos(region, pos));
        items.resize(27);
    }
    // Shulker boxes don't go into shulker boxes (Java canPlaceItemThroughFace): they spill.
    for (auto& s : items) {
        if (!game::isEmpty(s) && game::itemId(s).find("shulker_box") != std::string::npos) {
            leftover.push_back(s);
            s = ItemStack{};
        }
    }
    auto box = shulkerBoxWith(items, customName);
    if (game::isEmpty(box)) return false;

    game::clearBlock(region, pos);
    Vec3 center{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    for (auto const& s : leftover) player.getLevel().getSpawner().spawnItem(region, s, nullptr, center, 0);
    playSound(region, pos, SoundEvent::ShulkerBoxClosed, player);

    if (!player.isCreative()) {
        ItemStack held = player.getSelectedItem();
        if (hands) {
            held.remove(1);
            ItemStack off = player.getOffhandSlot();
            off.remove(1);
            player.setOffhandSlot(off.mCount > 0 ? off : ItemStack{});
        } else {
            held.remove(2);
        }
        player.setSelectedItem(held.mCount > 0 ? held : ItemStack{});
    }
    // Java ItemHandlerHelper.giveItemToPlayer(player, box, selected slot): the selected slot first.
    if (game::isEmpty(player.getSelectedItem())) player.setSelectedItem(box);
    else giveOrDrop(player, {box});
    player.sendInventory(false);
    log().info("mobtweaks: packed chest at {} {} {}", pos.x, pos.y, pos.z);
    return true;
}

ItemStack shulkerBoxWith(std::vector<ItemStack> const& items, std::string const& customName) {
    auto box = game::makeItem("minecraft:undyed_shulker_box");
    if (game::isEmpty(box)) return box;
    auto ctx  = SaveContextFactory::createCloneSaveContext();
    auto list = std::make_unique<ListTag>();
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (game::isEmpty(items[i])) continue;
        auto tag = items[i].save(*ctx);
        if (!tag) continue;
        tag->putByte("Slot", static_cast<uchar>(i));
        list->add(std::move(tag));
    }
    auto data = std::make_unique<CompoundTag>();
    data->put("Items", std::move(list));
    box.setUserData(std::move(data));
    if (!customName.empty()) box.setCustomName(Bedrock::Safety::RedactableString(customName));
    return box;
}

int absorbWater(BlockSource& region, BlockPos const& start, int maxDrain, int maxDepth) {
    // Java BlockPos.breadthFirstTraversal(start, depth, drain + 1, 6 neighbours, removeWater) > 1
    int const maxCount = maxDrain + 1;
    std::deque<std::pair<BlockPos, int>> queue;
    std::set<std::tuple<int, int, int>>  visited;
    queue.emplace_back(start, 0);
    int        count = 0;
    auto const& air  = *bs::of("minecraft:air");
    auto       level = ll::service::getLevel();
    while (!queue.empty()) {
        auto [p, depth] = queue.front();
        queue.pop_front();
        if (!visited.insert({p.x, p.y, p.z}).second) continue;
        bool matched = false;
        if (p == start) {
            matched = true;
        } else {
            auto const& b = region.getBlock(p);
            auto const& n = nameOf(b);
            if (n == "minecraft:water" || n == "minecraft:flowing_water") {
                matched = game::setBlock(region, p, air);
            } else if (auto const& extra = region.getExtraBlock(p);
                       nameOf(extra) == "minecraft:water" || nameOf(extra) == "minecraft:flowing_water") {
                matched = region.setExtraBlock(p, air, 3); // un-waterlog
            } else if (n == "minecraft:kelp" || n == "minecraft:seagrass") {
                matched = level && level->destroyBlock(region, p, true, BlockChangeContext{});
            }
        }
        if (!matched) continue;
        if (depth < maxDepth) {
            for (auto const& d : {BlockPos{1, 0, 0}, BlockPos{-1, 0, 0}, BlockPos{0, 1, 0}, BlockPos{0, -1, 0},
                                  BlockPos{0, 0, 1}, BlockPos{0, 0, -1}}) {
                queue.emplace_back(BlockPos{p.x + d.x, p.y + d.y, p.z + d.z}, depth + 1);
            }
        }
        if (++count >= maxCount) break;
    }
    return count - 1; // blocks absorbed (the sponge itself counted once)
}

std::optional<BlockPos> waterSourceInView(Player& player) {
    auto& region = player.getDimensionBlockSource();
    Vec3  eye    = player.getEyePos();
    Vec3  dir    = player.getViewVector(1.0f);
    // Java Item.getPlayerPOVHitResult(SOURCE_ONLY) with the 4.5 block interaction range.
    constexpr float kReach = 4.5f;
    BlockPos        last{INT32_MIN, INT32_MIN, INT32_MIN};
    for (float t = 0.0f; t <= kReach; t += 0.05f) {
        Vec3     at = eye + dir * t;
        BlockPos bp{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)), static_cast<int>(std::floor(at.z))};
        if (bp == last) continue;
        last          = bp;
        auto const& b = region.getBlock(bp);
        if (isWaterSource(b)) return bp;
        auto const& n = nameOf(b);
        if (b.isAir() || n == "minecraft:water" || n == "minecraft:flowing_water") continue; // flowing water: ignored
        return std::nullopt; // a block is in the way
    }
    return std::nullopt;
}

bool placeSpongeOnWater(Player& player, BlockSource& region, BlockPos const& pos) {
    if (!isWaterSource(region.getBlock(pos))) return false;
    auto const* sponge = bs::of("minecraft:sponge");
    if (!sponge || !game::setBlock(region, pos, *sponge)) return false;
    playSound(region, pos, SoundEvent::Place, player);
    SpongeBlock::_attemptAbsorbWater(region, pos); // also run by onPlace; a second run finds no water
    log().info("mobtweaks: sponge placed on water at {} {} {}", pos.x, pos.y, pos.z);
    return true;
}

std::optional<DoorState> doorState(BlockSource& region, BlockPos const& pos) {
    auto const& b    = region.getBlock(pos);
    auto        kind = openableOf(b);
    DoorState   s;
    s.block = nameOf(b);
    switch (kind) {
    case Openable::VanillaDoor: {
        auto const& door = static_cast<DoorBlock const&>(b.getBlockType());
        s.door           = true;
        s.lower          = bs::getBool(b, "upper_block_bit").value_or(false) ? below(pos) : pos;
        s.open           = DoorBlock::_isDoorToggled(region, b.getBlockType(), s.lower);
        s.facing         = static_cast<int>(door.getDir(region, s.lower));
        s.hingeRight     = door.hasRightHinge(region, s.lower);
        return s;
    }
    case Openable::LeptonDoor: {
        bool upper = bs::getBool(b, "lepton:upper").value_or(false);
        s.door     = true;
        s.lower    = upper ? below(pos) : pos;
        auto const& lb = region.getBlock(s.lower);
        s.open         = bs::getBool(lb, "lepton:open").value_or(false);
        s.facingName   = shapes::facingOf(lb).value_or("north");
        s.hingeRight   = bs::getBool(lb, "lepton:hinge_right").value_or(false);
        return s;
    }
    case Openable::VanillaGate:
        s.lower      = pos;
        s.open       = bs::getBool(b, "open_bit").value_or(false);
        s.facingName = shapes::facingOf(b).value_or("");
        return s;
    case Openable::LeptonGate:
        s.lower      = pos;
        s.open       = bs::getBool(b, "lepton:open").value_or(false);
        s.facingName = shapes::facingOf(b).value_or("");
        return s;
    default:
        return std::nullopt;
    }
}

std::optional<BlockPos> doorPartner(BlockSource& region, DoorState const& self) {
    if (!self.door) return std::nullopt;
    std::string facing = self.facingName;
    if (facing.empty()) {
        // Vanilla doors: DoorBlock::getDir is a Direction::Type (0 south, 1 west, 2 north, 3 east) one
        // quarter turn clockwise of Java's FACING (a door placed looking north reports east).
        static constexpr char const* kDir[4] = {"south", "west", "north", "east"};
        facing = std::string(shapes::counterClockwise(kDir[self.facing & 3]));
    }
    // Java: the mirror door is counter-clockwise of the facing for a right hinge, clockwise otherwise.
    auto side = self.hingeRight ? shapes::counterClockwise(facing) : shapes::clockwise(facing);
    return shapes::neighbor(self.lower, side);
}

bool followPartner(BlockSource& region, BlockPos const& pos, DoorState const& before, bool open, Player* by) {
    auto setTo = [&](BlockPos const& at) -> bool {
        auto const& b = region.getBlock(at);
        switch (openableOf(b)) {
        case Openable::VanillaDoor:
            static_cast<DoorBlock const&>(b.getBlockType()).setToggled(region, at, by, open);
            return true;
        case Openable::VanillaGate:
            return setVanillaGateOpen(region, at, open, by);
        default:
            return shapes::setOpen(region, at, open, by);
        }
    };
    // The partner must look like the used block did before the click (same block, facing, state).
    auto matches = [&](BlockPos const& at, bool oppositeHinge) {
        auto other = doorState(region, at);
        if (!other || other->block != before.block || other->open != before.open) return false;
        if (other->facing != before.facing || other->facingName != before.facingName) return false;
        if (hasTag(region.getBlock(at), kNonDoubleDoorTag)) return false;
        return !oppositeHinge || other->hingeRight != before.hingeRight;
    };
    if (before.door) {
        auto partner = doorPartner(region, before);
        if (!partner || !matches(*partner, true)) return false;
        return setTo(*partner);
    }
    // Fence gates: the one below first, then the one above (only one follows).
    for (auto const& at : {below(pos), above(pos)}) {
        if (matches(at, false)) return setTo(at);
    }
    return false;
}

/// Bedrock's off-hand refuses shulker shells; ShulkerPacking needs one in each hand.
void allowShellOffhand() {
    if (!cfg().enableShulkerPacking) return;
    auto shell = ItemRegistryManager::getItemRegistry().lookupByName(HashedString("minecraft:shulker_shell"));
    if (auto* item = shell.get()) item->mAllowOffhand = Item::OffhandAllowed::Yes;
}

class BlocksModule {
public:
    void install() {
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] { allowShellOffhand(); });
        }
        if (ll::service::getLevel()) allowShellOffhand();
        auto& bus = ll::event::EventBus::getInstance();
        // High priority: the shapes module cancels the event for Lepton doors and gates, which ends
        // the dispatch, so these listeners must see it first.
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            onInteractForShulker,
            ll::event::EventPriority::High
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            onInteractForDoors,
            ll::event::EventPriority::High
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>(onUseForSponge));
        ConcreteTouchingHook::hook();
        ConcreteTryTouchHook::hook();
        SpongeAbsorbHook::hook();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        ConcreteTouchingHook::unhook();
        ConcreteTryTouchHook::unhook();
        SpongeAbsorbHook::unhook();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::feature::mobtweaks

LP_REGISTER_MODULE(lp::feature::mobtweaks::BlocksModule);
