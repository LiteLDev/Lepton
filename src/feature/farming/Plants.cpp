// CoralOnCactus, RenewableSporeBlossoms, PetalsOnWater (docs/spec/farming.md §1, §3).
#include "feature/farming/Farming.h"

#include <cmath>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/FertilizerItem.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CoralFan.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::farming {

namespace bs = game::blockstates;

namespace {

constexpr std::string_view kWaterPetals = "lepton:water_pink_petals";

std::vector<ll::event::ListenerPtr> gListeners;

Vec3 center(BlockPos const& p) {
    return Vec3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y) + 0.5f, static_cast<float>(p.z) + 0.5f};
}

bool isLiveCoralFan(std::string const& id) {
    static std::set<std::string> const fans{
        "minecraft:tube_coral_fan",
        "minecraft:brain_coral_fan",
        "minecraft:bubble_coral_fan",
        "minecraft:fire_coral_fan",
        "minecraft:horn_coral_fan",
    };
    return fans.contains(id);
}

bool onCactus(BlockSource& region, BlockPos const& pos) {
    return region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z}).getTypeName() == "minecraft:cactus";
}

bool coralKeptAlive(BlockSource& region, BlockPos const& pos) {
    if (!cfg().enableCoralOnCactus) return false;
    return isLiveCoralFan(region.getBlock(pos).getTypeName()) && onCactus(region, pos);
}

bool isWaterSource(Block const& b) {
    auto const& id = b.getTypeName();
    if (id != "minecraft:water") return false;
    return bs::getInt(b, "liquid_depth").value_or(0) == 0;
}

/// Water source at `pos` (the block itself or its liquid layer).
bool waterSourceAt(BlockSource& region, BlockPos const& pos) {
    if (isWaterSource(region.getBlock(pos))) return true;
    auto const& liquid = region.getLiquidBlock(pos);
    return isWaterSource(liquid);
}

void playPlace(BlockSource& region, BlockPos const& pos, Block const& block, Player& by) {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->broadcastSoundEvent(
        region,
        SharedTypes::Legacy::LevelSoundEvent::Place,
        center(pos),
        block,
        ActorSoundIdentifier(by),
        false,
        std::nullopt
    );
}

bool isWater(std::string const& id) { return id == "minecraft:water" || id == "minecraft:flowing_water"; }

} // namespace

/// Places a block without the water layer the game adds to waterloggable blocks such as coral
/// fans (a fan put on a cactus in the air must not bring a water source with it).
void placeDry(BlockSource& region, BlockPos const& pos, Block const& block) {
    bool wet = isWater(region.getExtraBlock(pos).getTypeName()) || isWater(region.getBlock(pos).getTypeName());
    game::setBlock(region, pos, block);
    if (!wet && isWater(region.getExtraBlock(pos).getTypeName())) {
        if (auto const* air = bs::of("minecraft:air")) region.setExtraBlock(pos, *air, 3);
    }
}

// ---- PetalsOnWater -------------------------------------------------------------------------------

bool placePetal(Player& player, BlockSource& region, BlockPos pos, int face, bool fromBlockClick) {
    if (!cfg().enablePetalsOnWater) return false;
    auto const* state = &region.getBlock(pos);
    // clicked the top of a block: work on the block above (Java: direction UP, not water/petals)
    if (fromBlockClick && face == 1 && !waterSourceAt(region, pos) && state->getTypeName() != kWaterPetals) {
        pos   = BlockPos{pos.x, pos.y + 1, pos.z};
        state = &region.getBlock(pos);
    }
    if (state->getTypeName() != kWaterPetals) {
        if (!waterSourceAt(region, pos)) return false;
        BlockPos     above{pos.x, pos.y + 1, pos.z};
        Block const& up = region.getBlock(above);
        if (up.getTypeName() == kWaterPetals) {
            pos   = above;
            state = &up;
        } else if (up.isAir()) {
            int  facing = game::opposite(game::playerFacing(player));
            static char const* const names[] = {"south", "west", "north", "east"};
            auto const* petals = bs::of(kWaterPetals, {{"lepton:amount", 1}, {"minecraft:cardinal_direction", std::string(names[facing & 3])}});
            if (!petals) return false;
            game::setBlock(region, above, *petals);
            playPlace(region, above, *petals, player);
            return true;
        } else {
            return false;
        }
    }
    int amount = bs::getInt(*state, "lepton:amount").value_or(4);
    if (amount >= 4) return false;
    auto const* more = bs::with(*state, {{"lepton:amount", amount + 1}});
    if (!more) return false;
    game::setBlock(region, pos, *more);
    playPlace(region, pos, *more, player);
    return true;
}

namespace {

/// Server-side look ray (Bedrock sends no hit result for item use in the air): the first water
/// source or water petals within 5 blocks, stopping at other non-air blocks.
std::optional<BlockPos> lookAtWater(Player& player) {
    auto& region = player.getDimensionBlockSource();
    Vec3  eye    = player.getHeadPos();
    Vec3  dir    = player.getViewVector(1.0f);
    for (float d = 0.0f; d <= 5.0f; d += 0.05f) {
        BlockPos p = game::blockPosOf(Vec3{eye.x + dir.x * d, eye.y + dir.y * d, eye.z + dir.z * d});
        if (!region.hasChunksAt(p, 0, false)) return std::nullopt;
        auto const& b = region.getBlock(p);
        if (b.getTypeName() == kWaterPetals || waterSourceAt(region, p)) return p;
        if (!b.isAir()) {
            if (b.getTypeName() == "minecraft:water" || b.getTypeName() == "minecraft:flowing_water") continue;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void petalsLater(Player& player, BlockPos pos, int face, bool fromBlockClick) {
    auto id = player.getOrCreateUniqueID().rawID;
    later(1, [id, pos, face, fromBlockClick] {
        auto* p = playerById(id);
        if (!p) return;
        auto stack = p->getSelectedItem();
        if (stack.isNull() || stack.getTypeName() != "minecraft:pink_petals") return;
        if (placePetal(*p, p->getDimensionBlockSource(), pos, face, fromBlockClick)) {
            consumeOne(*p, stack);
            log().info("farming: {} placed pink petals on water", p->getRealName());
        }
    });
}

/// Would `placePetal` succeed? (read-only twin used to decide whether to cancel the event)
bool petalTarget(BlockSource& region, BlockPos pos, int face, bool fromBlockClick) {
    auto const* state = &region.getBlock(pos);
    if (fromBlockClick && face == 1 && !waterSourceAt(region, pos) && state->getTypeName() != kWaterPetals) {
        pos   = BlockPos{pos.x, pos.y + 1, pos.z};
        state = &region.getBlock(pos);
    }
    if (state->getTypeName() == kWaterPetals) return bs::getInt(*state, "lepton:amount").value_or(4) < 4;
    if (!waterSourceAt(region, pos)) return false;
    Block const& up = region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z});
    if (up.getTypeName() == kWaterPetals) return bs::getInt(up, "lepton:amount").value_or(4) < 4;
    return up.isAir();
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || ev.isCancelled()) return;
    auto const& item = ev.item();
    if (item.isNull()) return;
    auto  itemId = item.getTypeName();
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.blockPos();
    int   face   = static_cast<int>(ev.face());
    if (itemId == "minecraft:pink_petals" && cfg().enablePetalsOnWater) {
        if (petalTarget(region, pos, face, true)) {
            ev.cancel();
            petalsLater(player, pos, face, true);
        }
        return;
    }
    // CoralOnCactus: a live fan can be put on top of a cactus
    if (cfg().enableCoralOnCactus && isLiveCoralFan(itemId) && face == 1
        && region.getBlock(pos).getTypeName() == "minecraft:cactus") {
        BlockPos above{pos.x, pos.y + 1, pos.z};
        if (!game::replaceable(region, above)) return;
        ev.cancel();
        auto id = player.getOrCreateUniqueID().rawID;
        later(1, [id, above, itemId] {
            auto* p = playerById(id);
            if (!p) return;
            auto& r = p->getDimensionBlockSource();
            if (!game::replaceable(r, above) || !onCactus(r, above)) return;
            auto const* fan = bs::of(itemId);
            if (!fan) return;
            placeDry(r, above, *fan);
            playPlace(r, above, *fan, *p);
            consumeOne(*p, p->getSelectedItem());
            log().info("farming: {} placed {} on a cactus", p->getRealName(), itemId);
        });
    }
}

void onUseItem(ll::event::PlayerUseItemEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !cfg().enablePetalsOnWater) return;
    auto const& item = ev.item();
    if (item.isNull() || item.getTypeName() != "minecraft:pink_petals") return;
    auto target = lookAtWater(player);
    if (!target) return;
    auto& region = player.getDimensionBlockSource();
    if (!petalTarget(region, *target, 1, false)) return;
    ev.cancel();
    petalsLater(player, *target, 1, false);
}

} // namespace

// ---- RenewableSporeBlossoms ----------------------------------------------------------------------

bool boneMealSporeBlossom(BlockSource& region, BlockPos const& pos) {
    auto const& c = cfg();
    if (!c.enableRenewableSporeBlossoms || c.boneMealChance <= 0) return false;
    if (region.getBlock(pos).getTypeName() != "minecraft:spore_blossom") return false;
    if (random01() < c.boneMealChance) popItem(region, pos, game::makeItem("minecraft:spore_blossom"));
    if (auto level = ll::service::getLevel())
        level->broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::ParticleCropGrowth, center(pos), 15, nullptr);
    return true;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    BoneMealUseHook,
    ll::memory::HookPriority::Normal,
    FertilizerItem,
    &FertilizerItem::$_useOn,
    InteractionResult,
    ItemStack&    instance,
    Actor&        entity,
    BlockPos      pos,
    uchar         face,
    HandSlot      handSlot,
    Vec3 const&   clickPos
) {
    if (!entity.isClientSide() && instance.getTypeName() == "minecraft:bone_meal") {
        auto& region = entity.getDimensionBlockSource();
        bool ok = boneMealSporeBlossom(region, pos);
        if (ok) {
            if (!(entity.isPlayer() && isCreative(static_cast<Player&>(entity)))) instance.remove(1);
            InteractionResult r{};
            r.mSuccess = true;
            r.mSwing   = true;
            return r;
        }
    }
    return origin(instance, entity, pos, face, handSlot, clickPos);
}

LL_TYPE_INSTANCE_HOOK(
    BoneMealDispenseHook,
    ll::memory::HookPriority::Normal,
    FertilizerItem,
    &FertilizerItem::$dispense,
    bool,
    BlockSource& region,
    Container&   container,
    int          slot,
    Vec3 const&  pos,
    uchar        face
) {
    auto const& item = container.getItem(slot);
    if (!item.isNull() && item.getTypeName() == "minecraft:bone_meal") {
        BlockPos target = game::blockPosOf(pos);
        if (boneMealSporeBlossom(region, target)) {
            container.removeItem(slot, 1);
            return true;
        }
    }
    return origin(region, container, slot, pos, face);
}

// ---- CoralOnCactus -------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    CoralCheckAliveHook,
    ll::memory::HookPriority::Normal,
    CoralFan,
    &CoralFan::$checkAlive,
    void,
    BlockSource&    region,
    BlockPos const& pos
) {
    if (coralKeptAlive(region, pos)) return;
    origin(region, pos);
}

LL_TYPE_INSTANCE_HOOK(
    CoralNeighborHook,
    ll::memory::HookPriority::Normal,
    CoralFan,
    &CoralFan::$neighborChanged,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    BlockPos const& neighborPos
) {
    if (coralKeptAlive(region, pos)) return;
    origin(region, pos, neighborPos);
}

// ---- water petals pick block -----------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    PetalsPickHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::$asItemInstance,
    ItemInstance,
    Block const&      block,
    BlockActor const* blockActor
) {
    if (block.getTypeName() == kWaterPetals) return ItemInstance(game::makeItem("minecraft:pink_petals"));
    return origin(block, blockActor);
}

} // namespace

void installPlants() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>(onUseItem));
    BoneMealUseHook::hook();
    BoneMealDispenseHook::hook();
    CoralCheckAliveHook::hook();
    CoralNeighborHook::hook();
    PetalsPickHook::hook();
}

void uninstallPlants() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
    BoneMealUseHook::unhook();
    BoneMealDispenseHook::unhook();
    CoralCheckAliveHook::unhook();
    CoralNeighborHook::unhook();
    PetalsPickHook::unhook();
}

} // namespace lp::feature::farming
