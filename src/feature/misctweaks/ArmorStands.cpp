// ArmedArmorStands: Bedrock armor stands already have arms; a shield goes to the off hand, as on Java.

#include "ll/api/memory/Hook.h"

#include "mc/deps/shared_types/legacy/EquipmentSlot.h"
#include "mc/world/actor/ArmorStand.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/misctweaks/Internal.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::misctweaks {

namespace {

using Slot = SharedTypes::Legacy::EquipmentSlot;

LL_TYPE_INSTANCE_HOOK(
    ArmorStandSwapHook,
    ll::memory::HookPriority::Normal,
    ArmorStand,
    &ArmorStand::_trySwapItem,
    bool,
    Player& player,
    Slot    slot
) {
    if (cfg().enableArmedArmorStands && slot == Slot::Mainhand && game::itemId(player.getCarriedItem()) == "minecraft:shield"
        && game::isEmpty(getOffhandSlot())) {
        bool ok = origin(player, Slot::Offhand);
        if (ok) log().info("misctweaks: shield placed in an armor stand's off hand by {}", player.getRealName());
        return ok;
    }
    return origin(player, slot);
}

} // namespace

void installArmorStands() { ArmorStandSwapHook::hook(); }
void uninstallArmorStands() { ArmorStandSwapHook::unhook(); }

} // namespace lp::feature::misctweaks
