// U12 crystals: `/quark crystals info` prints Quark's hints (JEI information pages) for the unit's
// modules; `/quark crystals magnetic` tells whether the held item is magnetic.

#include "ll/api/command/CommandHandle.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

#include "feature/crystals/Crystals.h"
#include "feature/crystals/Magnets.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/I18n.h"

namespace lp::feature::crystals {
namespace {

std::string text(Player* p, std::string_view key, std::string_view fallback) {
    return p ? trFor(*p, key, fallback) : tr(key, fallback);
}

void printInfo(Player* p, CommandOutput& out) {
    auto const& c = cfg();
    auto        section = [&](std::string_view name, bool on, std::vector<std::string> lines) {
        out.success(p ? trFor(*p, "lp.crystals.info.header", "Crystals ({}):", name) : tr("lp.crystals.info.header", "Crystals ({}):", name));
        if (!on) lines = {text(p, "lp.crystals.info.disabled", "This module is disabled.")};
        for (auto const& l : lines) out.success("  " + l);
    };
    section("Corundum", c.enableCorundum,
            {text(p, "quark.jei.hint.corundum",
                  "Can be found underground in large clusters of two colors each. Will slowly grow upwards if placed deep "
                  "underground. Can be waxed to prevent growth and particle emission."),
             text(p, "quark.jei.hint.corundum_cluster_grow",
                  "Grows alongside the respective Corundum, either naturally spawned or when left to grow."),
             text(p, "quark.jei.hint.corundum_cluster_redirect", "Can redirect Beacon beams, check the Beacon item for info.")});
    section("RainbowLamps", c.enableRainbowLamps,
            {text(p, "quark.jei.hint.crystal_lamp", "These lamps will only change the color of a beacon beam when turned on.")});
    auto redirector = c.enableCorundum ? text(p, "quark.jei.hint.beacon_corundum", "Corundum")
                                       : text(p, "quark.jei.hint.beacon_amethyst", "Amethyst");
    std::string redirect = p ? trFor(*p, "quark.jei.hint.beacon_redirection",
                                     "The beacon beam can be redirected with {} by placing it in the beam's path. Note that "
                                     "the final one must always face towards the sky, and that the beam has a limited "
                                     "horizontal distance it can cover.",
                                     redirector)
                             : tr("quark.jei.hint.beacon_redirection",
                                  "The beacon beam can be redirected with {} by placing it in the beam's path. Note that "
                                  "the final one must always face towards the sky, and that the beam has a limited "
                                  "horizontal distance it can cover.",
                                  redirector);
    std::vector<std::string> beacon{redirect};
    if (c.allowTintedGlassTransparency)
        beacon.push_back(text(p, "quark.jei.hint.minecraft.tinted_glass",
                              "Can be used to make Beacon beams dimmer by placing the block in the way. Multiple will stack."));
    section("BeaconRedirection", c.enableBeaconRedirection, beacon);
    section("Magnets", c.enableMagnets,
            {text(p, "quark.jei.hint.magnet",
                  "Can push and pull any blocks made of Copper or Iron when powered, depending on the side. If multiple "
                  "magnets are moving the same block, the biggest force will win. A magnet's force on a block is the "
                  "redstone signal it's given minus the distance to the block. Hoppers moved with a Magnet will drop one "
                  "item in front for each block moved. If the item is a seed, it'll try to plant it instead. Stonecutters "
                  "moved with a Magnet will try to break the block above them.")});
}

} // namespace
} // namespace lp::feature::crystals

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace lp::feature::crystals;
    cmd.overload().text("crystals").text("info").execute([](CommandOrigin const& origin, CommandOutput& out) {
        printInfo(lp::commands::playerOf(origin), out);
    });
    cmd.overload().text("crystals").text("magnetic").execute([](CommandOrigin const& origin, CommandOutput& out) {
        auto* p = lp::commands::playerOf(origin);
        if (!p) return out.error(lp::tr("lp.crystals.players_only", "Only players can use this command."));
        auto const& held = p->getSelectedItem();
        if (lp::game::isEmpty(held)) return out.error(lp::trFor(*p, "lp.crystals.hold_item", "Hold an item in your main hand."));
        auto id = held.getTypeName();
        bool yes = isItemMagnetic(id);
        out.success(lp::trFor(*p, "lp.crystals.magnetic", "{} is magnetic: {}", id,
                              yes ? lp::trFor(*p, "lp.crystals.yes", "yes") : lp::trFor(*p, "lp.crystals.no", "no")));
    });
});
