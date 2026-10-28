// U15 inventory: `/quark` sub-commands, the vanilla-client fallbacks (docs/spec/inventory.md §4).
//   /quark sort                         sort your inventory
//   /quark sort container               sort the container you look at
//   /quark transfer insert|extract      move items into / out of the container you look at
//   /quark transfer insert|extract filtered
//   /quark hotbar [1-3]                 swap the hotbar with an inventory row (form without a row)
//   /quark shulker                      open the shulker box in your main hand
//   /quark share                        share the item in your main hand

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/EnderChestContainer.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/phys/HitResult.h"

#include "feature/inventory/Common.h"
#include "feature/inventory/Config.h"
#include "feature/inventory/Server.h"
#include "feature/inventory/Sorting.h"
#include "game/Forms.h"
#include "mod/Commands.h"
#include "mod/I18n.h"

namespace lp::feature::inventory {

/// Command parameters need external linkage.
struct HotbarParams {
    int bar = 0;
};

Container* lookedAtContainer(Player& player) {
    auto hit = player.traceRay(6.f, false, true);
    if (hit.mType != HitResultType::Tile) return nullptr;
    auto& region = player.getDimensionBlockSource();
    if (region.getBlock(hit.mBlock).getTypeName() == "minecraft:ender_chest") {
        auto ender = player.getEnderChestContainer();
        return ender ? ender.as_ptr() : nullptr;
    }
    auto* c = blockActorContainer(region.getBlockEntity(hit.mBlock));
    return c && c->getContainerSize() >= cfg().chestButtonSlotTarget ? c : nullptr;
}

namespace {

void hotbarForm(Player& player) {
    game::forms::SimpleForm form(trFor(player, "lp.inventory.hotbar.title", "Hotbar Swapper"),
                                 trFor(player, "lp.inventory.hotbar.content", "Swap the hotbar with a row of your inventory."));
    for (int bar = 1; bar <= 3; ++bar)
        form.button(trFor(player, "lp.inventory.hotbar.row", "Row {}", bar), [bar](Player& p) { changeHotbar(p, bar); });
    form.sendTo(player);
}

} // namespace

void registerCommands(ll::command::CommandHandle& cmd) {
    cmd.overload().text("sort").execute([](CommandOrigin const& o, CommandOutput& out) {
        auto* p = commands::playerOf(o);
        if (!p) return out.error("Players only.");
        if (!cfg().enableInventorySorting || !cfg().enablePlayerInventory)
            return out.error(trFor(*p, "lp.inventory.disabled", "This feature is disabled."));
        sortPlayerInventory(*p);
        out.success(trFor(*p, "lp.inventory.sorted", "Sorted."));
    });
    cmd.overload().text("sort").text("container").execute([](CommandOrigin const& o, CommandOutput& out) {
        auto* p = commands::playerOf(o);
        if (!p) return out.error("Players only.");
        if (!cfg().enableInventorySorting || !cfg().enableChests)
            return out.error(trFor(*p, "lp.inventory.disabled", "This feature is disabled."));
        auto* c = lookedAtContainer(*p);
        if (!c) return out.error(trFor(*p, "lp.inventory.no_container", "Look at a container within reach."));
        sortRange(*c, 0, c->getContainerSize());
        out.success(trFor(*p, "lp.inventory.sorted", "Sorted."));
    });
    // Each overload is built in one expression: the builder returned by overload() is a temporary.
    auto transfer = [](bool restock, bool smart) {
        return [restock, smart](CommandOrigin const& o, CommandOutput& out) {
            auto* p = commands::playerOf(o);
            if (!p) return out.error("Players only.");
            if (!cfg().enableEasyTransferring || p->isSpectator())
                return out.error(trFor(*p, "lp.inventory.disabled", "This feature is disabled."));
            auto* c = lookedAtContainer(*p);
            if (!c) return out.error(trFor(*p, "lp.inventory.no_container", "Look at a container within reach."));
            int n = transferItems(*p, *c, restock, smart);
            out.success(trFor(*p, "lp.inventory.transferred", "Moved {} items.", n));
        };
    };
    cmd.overload().text("transfer").text("insert").execute(transfer(false, false));
    cmd.overload().text("transfer").text("insert").text("filtered").execute(transfer(false, true));
    cmd.overload().text("transfer").text("extract").execute(transfer(true, false));
    cmd.overload().text("transfer").text("extract").text("filtered").execute(transfer(true, true));
    cmd.overload().text("hotbar").execute([](CommandOrigin const& o, CommandOutput& out) {
        auto* p = commands::playerOf(o);
        if (!p) return out.error("Players only.");
        if (!cfg().enableHotbarChanger) return out.error(trFor(*p, "lp.inventory.disabled", "This feature is disabled."));
        hotbarForm(*p);
    });
    cmd.overload<HotbarParams>().text("hotbar").required("bar").execute(
        [](CommandOrigin const& o, CommandOutput& out, HotbarParams const& params) {
            auto* p = commands::playerOf(o);
            if (!p) return out.error("Players only.");
            if (!changeHotbar(*p, params.bar)) return out.error(trFor(*p, "lp.inventory.disabled", "This feature is disabled."));
            out.success(trFor(*p, "lp.inventory.hotbar.row", "Row {}", params.bar));
        }
    );
    cmd.overload().text("shulker").execute([](CommandOrigin const& o, CommandOutput& out) {
        auto* p = commands::playerOf(o);
        if (!p) return out.error("Players only.");
        if (!isShulkerBox(p->getSelectedItem()))
            return out.error(trFor(*p, "lp.inventory.no_shulker", "Hold a shulker box in your main hand."));
        if (!openHeldShulker(*p, p->getSelectedItemSlot()))
            return out.error(trFor(*p, "lp.inventory.disabled", "This feature is disabled."));
    });
    cmd.overload().text("share").execute([](CommandOrigin const& o, CommandOutput& out) {
        auto* p = commands::playerOf(o);
        if (!p) return out.error("Players only.");
        if (!shareItem(*p, p->getSelectedItem()))
            return out.error(trFor(*p, "lp.inventory.cooldown", "You can't share an item right now."));
    });
}

} // namespace lp::feature::inventory

LP_REGISTER_SUBCOMMAND(lp::feature::inventory::registerCommands);
