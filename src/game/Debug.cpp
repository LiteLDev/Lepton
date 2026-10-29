// `/quark debug ...` (operators): manual probes of the shared contracts, used by the client e2e
// scenarios to show the vanilla-client fallbacks on a real client.
//   /quark debug form        sends a SimpleForm (logs "form test: clicked <button>")
//   /quark debug container [dispenser|large|hopper]   opens a prefilled virtual container (27 or 54-slot chest, 3x3 or
//                            hopper screen) at the player's position

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/platform/UUID.h"

#include "game/Blocks.h"
#include "game/Forms.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Net.h"

namespace {

int screenSlots(lp::game::vc::Spec const& spec) {
    switch (spec.screen) {
    case lp::game::vc::Screen::Hopper:
        return 5;
    case lp::game::vc::Screen::Dispenser:
        return 9;
    default:
        return spec.size;
    }
}

void openDebugContainer(CommandOrigin const& origin, CommandOutput& output, lp::game::vc::Screen screen, int size = 27) {
    auto* p = lp::commands::playerOf(origin);
    if (!p || !lp::commands::isAdmin(origin)) return output.error("Players with operator permission only.");
    auto               pos = lp::game::globalPos(p->getDimensionBlockSource(), lp::game::blockPosOf(p->getPosition()));
    lp::game::vc::Spec spec;
    spec.screen        = screen;
    spec.size          = size;
    spec.titleKey      = "lp.debug.container.title";
    spec.titleFallback = "Lepton test container";
    spec.onChanged     = [](lp::core::GlobalPos const& at) {
        int n = 0;
        for (auto const& s : lp::game::vc::items(at, 27)) n += lp::game::isEmpty(s) ? 0 : s.mCount;
        lp::log().info("debug container: contents changed ({} items)", n);
    };
    // Typed in the chat: the client rejects a container screen while its chat screen is still
    // closing, so open it half a second later.
    auto uuid = p->getUuid();
    ll::coro::keepThis([uuid, pos, spec]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(10);
        auto  level = ll::service::getLevel();
        auto* pl    = level ? level->getPlayer(uuid) : nullptr;
        if (!pl) co_return;
        // Something to see: the screen must show stored contents at once.
        int  slots = screenSlots(spec);
        auto items = lp::game::vc::items(pos, slots);
        if (lp::game::isEmpty(items[0])) items[0] = lp::game::makeItem("minecraft:apple", 2);
        if (slots > 4 && lp::game::isEmpty(items[4])) items[4] = lp::game::makeItem("minecraft:emerald", 5);
        if (slots > 40 && lp::game::isEmpty(items[40])) items[40] = lp::game::makeItem("minecraft:gold_ingot", 3);
        lp::game::vc::setItems(pos, items);
        if (!lp::game::vc::open(*pl, pos, spec)) lp::log().warn("debug container: could not open it for {}", pl->getRealName());
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
    output.success("Lepton container opening.");
}

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("debug").text("form").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = lp::commands::playerOf(origin);
        if (!p || !lp::commands::isAdmin(origin)) return output.error("Players with operator permission only.");
        auto clicked = [](std::string name) {
            return [name](Player& pl) { lp::log().info("form test: clicked {} ({})", name, pl.getRealName()); };
        };
        lp::game::forms::SimpleForm(
            lp::trFor(*p, "lp.debug.form.title", "Lepton form test"),
            lp::trFor(*p, "lp.debug.form.body", "Client mod: {}", lp::net::hasClientMod(*p) ? "yes" : "no")
        )
            .button(lp::trFor(*p, "lp.debug.form.first", "First"), clicked("First"), "textures/items/diamond")
            .button(lp::trFor(*p, "lp.debug.form.second", "Second"), clicked("Second"))
            .onClose([](Player& pl) { lp::log().info("form test: closed ({})", pl.getRealName()); })
            .sendTo(*p);
        lp::log().info("form test: sent to {}", p->getRealName());
        output.success("Lepton form sent.");
    });

    cmd.overload().text("debug").text("container").execute([](CommandOrigin const& origin, CommandOutput& output) {
        openDebugContainer(origin, output, lp::game::vc::Screen::Chest);
    });
    cmd.overload().text("debug").text("container").text("dispenser").execute([](CommandOrigin const& origin, CommandOutput& output) {
        openDebugContainer(origin, output, lp::game::vc::Screen::Dispenser);
    });
    cmd.overload().text("debug").text("container").text("large").execute([](CommandOrigin const& origin, CommandOutput& output) {
        openDebugContainer(origin, output, lp::game::vc::Screen::Chest, 54);
    });
    cmd.overload().text("debug").text("container").text("hopper").execute([](CommandOrigin const& origin, CommandOutput& output) {
        openDebugContainer(origin, output, lp::game::vc::Screen::Hopper);
    });
});


} // namespace
