// `/quark debug ...` (operators): manual probes of the shared contracts, used by the client e2e
// scenarios to show the vanilla-client fallbacks on a real client.
//   /quark debug form        sends a SimpleForm (logs "form test: clicked <button>")
//   /quark debug container   opens a 27-slot virtual container at the player's position

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
#include "game/VirtualContainer.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Net.h"

namespace {

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
        auto* p = lp::commands::playerOf(origin);
        if (!p || !lp::commands::isAdmin(origin)) return output.error("Players with operator permission only.");
        auto                 pos = lp::game::globalPos(p->getDimensionBlockSource(), lp::game::blockPosOf(p->getPosition()));
        lp::game::vc::Spec   spec;
        spec.titleKey      = "lp.debug.container.title";
        spec.titleFallback = "Lepton test container";
        // Typed in the chat: the client rejects a container screen while its chat screen is still
        // closing, so open it half a second later.
        auto uuid = p->getUuid();
        ll::coro::keepThis([uuid, pos, spec]() -> ll::coro::CoroTask<> {
            co_await ll::chrono::ticks(10);
            auto level = ll::service::getLevel();
            auto* pl   = level ? level->getPlayer(uuid) : nullptr;
            if (pl && !lp::game::vc::open(*pl, pos, spec)) lp::log().warn("debug container: could not open it for {}", pl->getRealName());
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
        output.success("Lepton container opening.");
    });
});

} // namespace
