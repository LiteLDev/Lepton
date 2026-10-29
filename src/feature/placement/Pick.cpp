// U17 placement, LongRangePickBlock (server side) and the VariantSelector's pick-block rule: the
// client mod asks the server to pick a block (far away) or an item (a variant's original), and
// the server does what vanilla pick block does with the hotbar.

#include "feature/placement/Placement.h"

#include <cmath>

#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::feature::placement {

LP_REGISTER_MESSAGE(PickMsg);

namespace {

/// Java Inventory#getSuitableHotbarSlot.
int suitableHotbarSlot(Player& player, Container& inv) {
    int selected = player.getSelectedItemSlot();
    for (int i = 0; i < 9; ++i) {
        int j = (selected + i) % 9;
        if (game::isEmpty(inv.getItem(j))) return j;
    }
    for (int i = 0; i < 9; ++i) {
        int j = (selected + i) % 9;
        if (!inv.getItem(j).isEnchanted()) return j;
    }
    return selected;
}

} // namespace

bool pickItem(Player& player, std::string const& itemId) {
    if (itemId.empty()) return false;
    auto& inv   = player.getInventory();
    int   found = -1;
    for (int i = 0; i < game::inv::kPlayerSlots; ++i) {
        if (game::itemId(inv.getItem(i)) == itemId) {
            found = i;
            break;
        }
    }
    int slot = -1;
    if (found >= 0 && found < 9) {
        slot = found;
    } else if (found >= 9) {
        slot          = suitableHotbarSlot(player, inv);
        ItemStack a   = inv.getItem(slot);
        ItemStack b   = inv.getItem(found);
        inv.setItem(slot, b);
        inv.setItem(found, a);
    } else if (player.isCreative()) {
        ItemStack stack = game::makeItem(itemId, 1);
        if (game::isEmpty(stack)) return false;
        slot = suitableHotbarSlot(player, inv);
        inv.setItem(slot, stack);
    } else {
        return false;
    }
    player.setSelectedSlot(slot);
    player.sendInventory(true);
    log().info("placement: picked {} into hotbar slot {}", itemId, slot);
    return true;
}

namespace {

void onPick(Player& player, PickMsg const& m) {
    BlockPos pos{m.x, m.y, m.z};
    auto&    region = player.getDimensionBlockSource();
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight() || !region.hasChunksAt(pos, 0, false)) return;
    Vec3  eye = player.getEyePos();
    float dx = pos.x + 0.5f - eye.x, dy = pos.y + 0.5f - eye.y, dz = pos.z + 0.5f - eye.z;
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > 202.0f) return; // 200 blocks (+ the block's size)
    auto const& block = region.getBlock(pos);
    if (block.isAir()) return;

    if (!m.item.empty()) {
        // VariantSelector: pick the original of a variant block
        if (!variantsEnabled() || originalOf(block.getTypeName()) != m.item || m.item == block.getTypeName()) return;
        pickItem(player, m.item);
        return;
    }
    if (!cfg().enableLongRangePickBlock) return;
    ItemInstance item = block.asItemInstance(region, pos, false);
    pickItem(player, game::itemId(item));
}

} // namespace

class LongRangePickModule {
public:
    void install() { mHandler = net::onServer<PickMsg>(onPick); }
    void uninstall() { net::removeHandler(mHandler); }

private:
    net::HandlerId mHandler = 0;
};

} // namespace lp::feature::placement

LP_REGISTER_MODULE(lp::feature::placement::LongRangePickModule);
