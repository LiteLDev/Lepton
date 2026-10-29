// Torch arrows (Quark TorchArrowModule) and the projectile hit hook shared with parrot eggs.

#include "feature/throwables/Throwables.h"

#include <cmath>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/projectile/Arrow.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/components/ShooterItemComponent.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/phys/HitResult.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::throwables::torcharrow {

namespace ent = game::entities;

namespace {

bool isTorchArrowItem(ItemStackBase const& s) { return game::itemId(s) == kTorchArrow; }

/// Places the torch for an arrow that hit `block` on `face`. True when a torch was placed.
bool placeTorch(Actor& arrow, BlockPos const& block, int face) {
    if (face <= 0 || face > 5) return false; // never on bottom faces (FacingID::Down = 0)
    static int const dx[6] = {0, 0, 0, 0, -1, 1}, dy[6] = {-1, 1, 0, 0, 0, 0}, dz[6] = {0, 0, -1, 1, 0, 0};
    auto&    region = arrow.getDimensionBlockSource();
    BlockPos at{block.x + dx[face], block.y + dy[face], block.z + dz[face]};
    auto const& here = region.getBlock(at);
    if (!here.isAir() && !game::replaceable(region, at)) return false;
    // Support (Java: a sturdy side face for wall torches, a centre support for standing ones);
    // Block::canSurvive of torches only checks the floor, whatever the state.
    auto const& support = region.getBlock(block);
    bool ok = face == 1 ? support.canProvideSupport(static_cast<uchar>(face), BlockSupportType::Center)
                        : support.canProvideFullSupport(static_cast<uchar>(face));
    if (!ok) return false;
    // Bedrock names a wall torch after the side of the torch block its support is on.
    static char const* dirs[6] = {"unknown", "top", "south", "north", "east", "west"};
    auto const* torch = game::blockstates::of("minecraft:torch", {{"torch_facing_direction", std::string(dirs[face])}});
    if (!torch) return false;
    if (!game::setBlock(region, at, *torch)) return false;
    auto level = ll::service::getLevel();
    if (level) {
        Vec3 c{at.x + 0.5f, at.y + 0.5f, at.z + 0.5f};
        level->broadcastSoundEvent(
            region, SharedTypes::Legacy::LevelSoundEvent::Place, c, *torch, ActorSoundIdentifier(arrow), false, std::nullopt
        );
    }
    return true;
}

// Arrow hits: torch placement and ignition; parrot eggs hatch.
LL_TYPE_INSTANCE_HOOK(
    ThrowablesProjectileHitHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    Actor&           owner,
    HitResult const& hit
) {
    auto type = owner.getTypeName();
    if (type == kTorchArrow && cfg().enableTorchArrow && !owner.isClientSide()) {
        if (hit.mType == HitResultType::Tile) {
            if (placeTorch(owner, hit.mBlock, hit.mFacing)) {
                log().info("throwables: torch arrow placed a torch at {} {} {}", hit.mBlock.x, hit.mBlock.y, hit.mBlock.z);
                ent::despawn(owner);
                return;
            }
        } else if (hit.mType == HitResultType::Entity) {
            Actor* target = hit.getEntity();
            origin(owner, hit);
            if (target && target->isAlive()) target->setOnFire(5, true);
            return;
        }
    } else if (type.starts_with("lepton:parrot_egg_") && !owner.isClientSide()) {
        Vec3 pos = owner.getPosition();
        int  dim = static_cast<int>(owner.getDimensionId());
        origin(owner, hit);
        parroteggs::onEggHit(type, pos, dim);
        return;
    }
    origin(owner, hit);
}

// Bows / crossbows: a torch arrow that comes before the arrow the game picked is shot instead.
LL_TYPE_INSTANCE_HOOK(
    ThrowablesAmmoHook,
    ll::memory::HookPriority::Normal,
    ShooterItemComponent,
    &ShooterItemComponent::_getAmmunition,
    int,
    Player const* player,
    bool          flag,
    ItemStack&    ammo,
    bool&         fromOffhand
) {
    int slot = origin(player, flag, ammo, fromOffhand);
    if (!player || !cfg().enableTorchArrow) return slot;
    bool found = !game::isEmpty(ammo);
    if (found && fromOffhand) return slot; // the off hand comes first
    auto const& off = player->getOffhandSlot();
    if (isTorchArrowItem(off)) {
        ammo        = off;
        fromOffhand = true;
        return slot < 0 ? 0 : slot;
    }
    auto& pinv = game::inv::playerInventory(const_cast<Player&>(*player));
    for (int i = 0; i < game::inv::kPlayerSlots && i < pinv.getContainerSize(); ++i) {
        if (found && i >= slot) break;
        auto const& s = pinv.getItem(i);
        if (isTorchArrowItem(s)) {
            ammo        = s;
            fromOffhand = false;
            return i;
        }
    }
    return slot;
}

// Infinity never saves torch arrows (Java: only plain arrows are infinite).
LL_TYPE_INSTANCE_HOOK(
    ThrowablesConsumeAmmoHook,
    ll::memory::HookPriority::Normal,
    ShooterItemComponent,
    &ShooterItemComponent::_consumeAmmunition,
    void,
    Player*          player,
    ItemStack const& ammunition,
    int              slotIndex,
    bool             infiniteAmmo,
    bool             fromOffhand
) {
    if (infiniteAmmo && isTorchArrowItem(ammunition) && player && !player->isCreative()) infiniteAmmo = false;
    origin(player, ammunition, slotIndex, infiniteAmmo, fromOffhand);
}

// Picking a stuck torch arrow up gives it back (or a plain arrow with extinguishOnMiss).
LL_TYPE_INSTANCE_HOOK(ThrowablesArrowPickupHook, ll::memory::HookPriority::Normal, Arrow, &Arrow::$_getPickupItem, ItemStack) {
    if (this->getTypeName() == kTorchArrow) {
        return game::makeItem(cfg().extinguishOnMiss ? "minecraft:arrow" : std::string(kTorchArrow));
    }
    return origin();
}

} // namespace

void install() {
    ThrowablesProjectileHitHook::hook();
    ThrowablesAmmoHook::hook();
    ThrowablesConsumeAmmoHook::hook();
    ThrowablesArrowPickupHook::hook();
}

void uninstall() {
    ThrowablesProjectileHitHook::unhook();
    ThrowablesAmmoHook::unhook();
    ThrowablesConsumeAmmoHook::unhook();
    ThrowablesArrowPickupHook::unhook();
}

/// Flame trail of flying torch arrows (Quark draws it on the client).
void tickTrails() {
    // The full actor scan runs every 4 ticks; in between only the known torch arrows are looked up.
    static std::vector<int64_t> known;
    static uint64_t             n = 0;
    if (n++ % 4 == 0) {
        known.clear();
        for (auto* a : ent::ofType(kTorchArrow))
            if (a) known.push_back(ent::idOf(*a));
    }
    for (auto id : known) {
        auto* a = ent::find(id);
        if (!a) continue;
        Vec3 d = a->getPosDelta();
        if (d.x * d.x + d.y * d.y + d.z * d.z < 0.01) continue;
        Vec3 p = a->getPosition();
        particle("minecraft:basic_flame_particle", Vec3{p.x - d.x * 0.5f, p.y - d.y * 0.5f, p.z - d.z * 0.5f}, static_cast<int>(a->getDimensionId()));
    }
}

} // namespace lp::feature::throwables::torcharrow
