// CompassesWorkEverywhere (Quark tweaks/CompassesWorkEverywhereModule), server part: marks compasses
// and clocks that ticked in a player's inventory and remembers the nether portal a compass came
// through. The needle itself is drawn by the client mod (src/client/gadgets/). See
// docs/spec/gadgets.md.

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"

#include "ll/api/service/Bedrock.h"

#include "feature/gadgets/Gadgets.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"

namespace lp::feature::gadgets::compasses {

bool tickStack(ItemStack& stack, bool nether, bool inPortal, BlockPos const& pos) {
    auto id = game::itemId(stack);
    if (id == "minecraft:clock") {
        if (getBool(stack, kClockCalculated)) return false;
        putBool(stack, kClockCalculated, true);
        return true;
    }
    if (id != "minecraft:compass") return false;
    if (!getBool(stack, kCompassCalculated)) {
        putBool(stack, kCompassCalculated, true);
        putBool(stack, kWasInNether, nether);
        return true;
    }
    bool was = getBool(stack, kWasInNether);
    if (nether && !was && inPortal) {
        putInt(stack, kNetherX, pos.x);
        putInt(stack, kNetherZ, pos.z);
        putBool(stack, kWasInNether, true);
        putBool(stack, kPosSet, true);
        return true;
    }
    if (!nether && was) {
        putBool(stack, kWasInNether, false);
        putBool(stack, kPosSet, false);
        return true;
    }
    return false;
}

void tickPlayer(Player& p) {
    bool nether   = static_cast<int>(p.getDimensionId()) == 1;
    auto pos      = game::blockPosOf(p.getFeetPos()); // Java blockPosition(): the feet
    bool inPortal = nether && game::blockIdAt(p.getDimensionBlockSource(), pos) == "minecraft:portal";
    auto& inv     = game::inv::playerInventory(p);
    bool  changed = false;
    for (int i = 0; i < game::inv::kPlayerSlots; ++i) {
        auto const& cur = inv.getItem(i);
        auto        id  = game::itemId(cur);
        if (id != "minecraft:compass" && id != "minecraft:clock") continue;
        ItemStack s = cur;
        if (tickStack(s, nether, inPortal, pos)) {
            inv.setItem(i, s);
            changed = true;
        }
    }
    {
        auto id = game::itemId(p.getOffhandSlot());
        if (id == "minecraft:compass" || id == "minecraft:clock") {
            ItemStack s = p.getOffhandSlot();
            if (tickStack(s, nether, inPortal, pos)) {
                p.setOffhandSlot(s);
                changed = true;
            }
        }
    }
    if (changed) game::inv::sync(p);
}

void tick() {
    if (!cfg().enableCompassesWorkEverywhere) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([](Player& p) {
        tickPlayer(p);
        return true;
    });
}

} // namespace lp::feature::gadgets::compasses
