// BetterElytraRocket, CampfiresBoostElytra and GameNerfs' dimension-locked elytra.

#include <algorithm>
#include <cmath>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/SetActorMotionPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/FireworksItem.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/misctweaks/Internal.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::misctweaks {

// ---- helpers -------------------------------------------------------------------------------------

bool onClientSide(Actor const& actor) {
    ILevel* level = actor.mLevel;
    return !level || level->isClientSide();
}

std::string dimensionName(int dimId) {
    switch (dimId) {
    case 0:
        return "minecraft:overworld";
    case 1:
        return "minecraft:the_nether";
    case 2:
        return "minecraft:the_end";
    default:
        return "minecraft:dim" + std::to_string(dimId);
    }
}

bool dimensionMatches(std::string_view name, int dimId) {
    std::string n(name);
    if (n.find(':') == std::string::npos) n = "minecraft:" + n;
    if (n == "minecraft:nether") n = "minecraft:the_nether";
    if (n == "minecraft:end") n = "minecraft:the_end";
    return n == dimensionName(dimId);
}

bool elytraAllowedIn(Actor const& actor) {
    auto const& c = cfg();
    if (!c.enableGameNerfs || !c.enableDimensionLockedElytra) return true;
    int dim = static_cast<int>(actor.getDimensionId());
    return std::ranges::any_of(c.elytraAllowedDimensions, [&](std::string const& d) { return dimensionMatches(d, dim); });
}

bool canElytraFly(Mob const& mob) {
    ItemStack const& chest = mob.getArmor(SharedTypes::Legacy::ArmorSlot::Torso);
    if (game::itemId(chest) != "minecraft:elytra") return false;
    Item const* item = chest.mItem.get();
    if (item && item->getMaxDamage() > 0 && chest.getDamageValue() >= item->getMaxDamage() - 1) return false;
    return elytraAllowedIn(mob);
}

void sendMotion(Player& player, Vec3 const& motion) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::SetActorMotion);
    if (!pkt) return;
    auto& m      = static_cast<SetActorMotionPacket&>(*pkt);
    m.mRuntimeId = player.getRuntimeID();
    m.mMotion    = motion;
    pkt->sendTo(player);
}

bool isSignalFire(BlockSource& region, BlockPos const& campfire) {
    return region.getBlock(BlockPos{campfire.x, campfire.y - 1, campfire.z}).getTypeName() == "minecraft:hay_block";
}

std::optional<double> campfireForce(BlockSource& region, BlockPos const& feet, double boostStrength) {
    BlockPos pos   = feet;
    int      moves = 0;
    auto     inWorld = [&](BlockPos const& p) { return p.y >= region.getMinHeight() && p.y < region.getMaxHeight(); };
    while (moves < 20 && inWorld(pos)) {
        Block const& b = region.getBlock(pos);
        if (!b.isAir() && b.getTypeName() != "lepton:grate") break;
        pos.y -= 1;
        ++moves;
    }
    if (!inWorld(pos)) return std::nullopt;
    Block const& b    = region.getBlock(pos);
    auto const&  name = b.getTypeName();
    bool         soul = name == "minecraft:soul_campfire";
    if (name != "minecraft:campfire" && !soul) return std::nullopt;
    if (game::blockstates::getBool(b, "extinguished").value_or(true)) return std::nullopt;
    if (!isSignalFire(region, pos)) return std::nullopt;
    double force = boostStrength;
    // Quark's formula, kept as is: past 16 blocks the force drops to 1/4 and climbs back to full at 20.
    if (moves > 16) force -= force * (1.0 - (static_cast<double>(moves) - 16.0) / 4.0);
    if (soul) force *= -1.5;
    return force;
}

namespace {

// ---- BetterElytraRocket --------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    RocketUseHook,
    ll::memory::HookPriority::Normal,
    FireworksItem,
    &FireworksItem::$use,
    ItemStack&,
    ItemStack& item,
    Player&    player,
    HandSlot   handSlot
) {
    if (!cfg().enableBetterElytraRocket || player.isGliding() || player.getVehicle() || !canElytraFly(player))
        return origin(item, player, handSlot);
    player.startGliding();
    if (!player.isGliding()) return origin(item, player, handSlot); // refused (e.g. on the ground on this side)
    if (onClientSide(player)) {
        player.jumpFromGround();
        return origin(item, player, handSlot);
    }
    Vec3 delta = player.getPosDelta();
    auto& res  = origin(item, player, handSlot); // gliding now: vanilla fires the rocket attached to the player
    sendMotion(player, Vec3{delta.x, 0.42f, delta.z});
    log().info("misctweaks: elytra rocket launch {}", player.getRealName());
    return res;
}

// ---- dimension-locked elytra ---------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(StartGlidingHook, ll::memory::HookPriority::Normal, Player, &Player::startGliding, void) {
    if (!elytraAllowedIn(*this)) return;
    origin();
}

LL_TYPE_INSTANCE_HOOK(TryStartGlidingHook, ll::memory::HookPriority::Normal, Player, &Player::tryStartGliding, bool) {
    if (!elytraAllowedIn(*this)) return false;
    return origin();
}

// ---- server tick: campfires + elytra lock --------------------------------------------------------

void onTick(ll::event::ServerLevelTickEvent& ev) {
    auto const& c = cfg();
    bool        campfires = c.enableCampfiresBoostElytra;
    bool        lock      = c.enableGameNerfs && c.enableDimensionLockedElytra;
    if (!campfires && !lock) return;
    ev.level().forEachPlayer([&](Player& player) {
        if (!player.isGliding()) return true;
        if (lock && !elytraAllowedIn(player)) {
            player.stopGliding();
            return true;
        }
        if (!campfires) return true;
        Vec3 motion = player.getPosDelta();
        if (motion.y >= c.maxSpeed) return true;
        auto force = campfireForce(player.getDimensionBlockSource(), game::blockPosOf(player.getFeetPos()), c.boostStrength);
        if (!force) return true;
        float vy = static_cast<float>(std::min(c.maxSpeed, static_cast<double>(motion.y) + *force));
        sendMotion(player, Vec3{motion.x, vy, motion.z});
        return true;
    });
}

ll::event::ListenerPtr gTick;

} // namespace

void installElytra() {
    RocketUseHook::hook();
    StartGlidingHook::hook();
    TryStartGlidingHook::hook();
    gTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(onTick);
}

void uninstallElytra() {
    if (gTick) ll::event::EventBus::getInstance().removeListener(gTick);
    gTick = nullptr;
    TryStartGlidingHook::unhook();
    StartGlidingHook::unhook();
    RocketUseHook::unhook();
}

} // namespace lp::feature::misctweaks
