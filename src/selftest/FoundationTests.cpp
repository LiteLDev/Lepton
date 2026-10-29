// Selftests of the foundation: world storage, translations, configs, blocks and simulated players.

#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/player/Player.h"

#include "mc/world/item/ItemStack.h"

#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/ModConfig.h"
#include "selftest/SelfTest.h"

namespace {
using lp::selftest::Context;
}

LP_REGISTER_SELFTEST("foundation.world_db", [](Context& ctx) {
    namespace db = lp::game::db;
    ctx.require(db::available(), "a world is loaded");
    std::string const key = "lp:foundation:probe";
    db::save(key, {{"value", 42}});
    auto back = db::load(key);
    ctx.check(back && back->value("value", 0) == 42, "saved JSON reads back");
    int seen = 0;
    db::forEachKey("lp:foundation:", [&seen](std::string_view suffix) {
        if (suffix == "probe") ++seen;
    });
    ctx.check(seen == 1, "forEachKey finds the key by prefix");
    db::erase(key);
    ctx.check(!db::load(key), "erased key is gone");
});

LP_REGISTER_SELFTEST("foundation.i18n", [](Context& ctx) {
    ctx.check(lp::tr("lp.test.missing", "Fallback {}", 1) == "Fallback 1", "missing keys use the fallback");
    ctx.check(lp::translate("lp.mod.name", "?", "en_US") == "Lepton", "en_US lang file is loaded");
    ctx.check(lp::translate("lp.mod.name", "?", "zh_CN") != "?", "zh_CN lang file is loaded");
    ctx.check(!lp::config::configDir().empty(), "config directory is set");
    ctx.check(lp::config::get<lp::ModConfig>().version >= 1, "mod config is loaded");
});

// The server builds some texts with item names itself (`/give` feedback): they must not be raw keys
// (PackDeployer appends the pack's names to the server's I18n on BDS; the client has them).
LP_REGISTER_SELFTEST("foundation.item_names", [](Context& ctx) {
    int checked = 0;
    for (auto [id, name] : {std::pair{"lepton:ancient_tome", "Ancient Tome"}, {"lepton:apple_crate", "Apple Crate"}}) {
        auto item = lp::game::makeItem(id, 1, 0);
        if (lp::game::isEmpty(item)) {
            ctx.info(std::string(id) + " is not registered, skipped");
            continue;
        }
        auto shown = item.getName();
        ++checked;
        ctx.check(shown.find("lepton:") == std::string::npos, std::string(id) + " has a translated name: " + shown);
#ifdef LP_SERVER // the client uses the game's language
        if (lp::config::get<lp::ModConfig>().language == "en_US")
            ctx.check(shown == name, std::string(id) + " is named " + name + ": " + shown);
#else
        (void)name;
#endif
    }
    ctx.info("checked " + std::to_string(checked) + " names");
});

LP_REGISTER_SELFTEST("foundation.blocks", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto pos = ctx.testPos(10);
    ctx.require(co_await ctx.ensureLoaded(pos), "test area loaded");
    ctx.require(ctx.setBlock(pos, "minecraft:gold_block"), "set a vanilla block");
    co_await ctx.ticks(1);
    auto& region = ctx.player().getDimensionBlockSource();
    ctx.check(lp::game::blockIdAt(region, lp::game::blockPos(pos)) == "minecraft:gold_block", "block reads back");
    ctx.check(lp::game::blockWith("lepton:no_such_block") == nullptr, "unknown block ids give nullptr");
});

LP_REGISTER_SELFTEST("foundation.players", [](Context& ctx) -> ll::coro::CoroTask<> {
    auto& a = ctx.player();
    auto& b = ctx.spawnPlayer();
    co_await ctx.ticks(2);
    ctx.check(&a != static_cast<Player*>(&b), "two distinct simulated players");
    ctx.check(!a.getRealName().empty(), "the simulated player has a name");
});
