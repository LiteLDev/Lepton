// MatrixEnchanting: hooks the enchanting tables (vanilla with automaticallyConvert, and
// lepton:matrix_enchanter), the companion-protocol handlers and the session tick.

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/enchanting/Config.h"
#include "feature/enchanting/Messages.h"
#include "feature/enchanting/Table.h"
#include "feature/enchanting/Util.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "feature/enchanting/Enchants.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::feature::enchanting {

LP_REGISTER_MESSAGE(MatrixOpMsg);
LP_REGISTER_MESSAGE(MatrixSlotMsg);
LP_REGISTER_MESSAGE(MatrixCloseMsg);
LP_REGISTER_MESSAGE(MatrixStateMsg);
LP_REGISTER_MESSAGE(CraftPreviewMsg);
LP_REGISTER_MESSAGE(CraftMsg);

namespace {

int64_t gTick = 0;
std::map<std::string, int64_t>& recentUse() {
    static std::map<std::string, int64_t> m;
    return m;
}

class MatrixModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            ++gTick;
            if (gTick % 20 == 0) table::tickSessions();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
            [](ll::event::PlayerInteractBlockEvent& ev) {
                auto& player = ev.self();
                if (player.isClientSide()) return;
                auto& region = player.getDimensionBlockSource();
                auto  id     = game::blockIdAt(region, ev.blockPos());
                bool  ours   = id == table::kBlockId;
                if (!ours && !(id == "minecraft:enchanting_table" && cfg().enableMatrixEnchanting && cfg().automaticallyConvert))
                    return;
                if (player.isSneaking() && !game::isEmpty(ev.item())) return;
                ev.cancel();
                auto uuid = uuidOf(player);
                if (auto it = recentUse().find(uuid); it != recentUse().end() && gTick - it->second < 5) return;
                recentUse()[uuid] = gTick;
                auto pos          = game::globalPos(region, ev.blockPos());
                later(1, [uuid, pos] {
                    auto* p = playerByUuid(uuid);
                    auto* r = game::regionOf(pos.dim);
                    if (!p || !r) return;
                    auto bid = game::blockIdAt(*r, game::blockPos(pos));
                    if (!cfg().enableMatrixEnchanting) {
                        // Quark: a matrix enchanter of a disabled module turns back into a vanilla table.
                        if (bid == table::kBlockId) {
                            // The vanilla table has no matrix: give the contents back first.
                            table::dropContents(*r, pos);
                            if (auto const* b = game::blockstates::of("minecraft:enchanting_table"))
                                game::setBlock(*r, game::blockPos(pos), *b);
                        }
                        return;
                    }
                    if (!table::isTableId(bid)) return;
                    table::open(*p, pos);
                });
            }
        ));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            auto const& prev = ev.previousBlock().getTypeName();
            if (prev != table::kBlockId && prev != "minecraft:enchanting_table") return;
            auto const& next = ev.newBlock().getTypeName();
            if (next == table::kBlockId || next == "minecraft:enchanting_table") return; // conversions keep the contents
            auto pos = game::globalPos(region, ev.pos());
            later(1, [pos] {
                if (auto* r = game::regionOf(pos.dim)) {
                    auto bid = game::blockIdAt(*r, game::blockPos(pos));
                    if (bid == table::kBlockId || bid == "minecraft:enchanting_table") return;
                    table::dropContents(*r, pos);
                }
            });
        }));

        mHandlers.push_back(net::onServer<MatrixOpMsg>([](Player& p, MatrixOpMsg const& m) {
            table::operate(p, m.op, m.arg0, m.arg1, m.arg2);
        }));
        mHandlers.push_back(net::onServer<MatrixSlotMsg>([](Player& p, MatrixSlotMsg const& m) {
            table::slotAction(p, m.action, m.slot);
        }));
        mHandlers.push_back(net::onServer<MatrixCloseMsg>([](Player& p, MatrixCloseMsg const&) { table::close(p, false); }));

        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldSave([] { table::flush(); });
            game::db::onWorldClosed([] {
                table::resetCaches();
                recentUse().clear();
            });
            config::onReload([] { table::reparseInfluences(); });
        }
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        for (auto h : mHandlers) net::removeHandler(h);
        mHandlers.clear();
        table::flush();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    std::vector<net::HandlerId>         mHandlers;
};

/// `/quark enchanting demo` (operators): builds a matrix enchanter with bookshelves and a candle
/// next to the player, puts a sword and lapis in, rolls and places pieces, and opens it. Used by
/// the client e2e scenario for screenshots.
LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("enchanting").text("demo").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = commands::playerOf(origin);
        if (!p || !commands::isAdmin(origin)) return output.error("Players with operator permission only.");
        auto& region = p->getDimensionBlockSource();
        auto  eye    = p->getPosition();
        auto  feet   = game::blockPosOf(Vec3{eye.x, eye.y - 1.5f, eye.z});
        BlockPos center{feet.x + 4, feet.y, feet.z};
        auto set = [&](int dx, int dy, int dz, char const* id) {
            if (auto const* b = game::blockstates::of(id)) game::setBlock(region, {center.x + dx, center.y + dy, center.z + dz}, *b);
        };
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz) {
                set(dx, -1, dz, "minecraft:stone");
                for (int dy = 0; dy <= 2; ++dy) set(dx, dy, dz, "minecraft:air");
            }
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = 0; dy <= 1; ++dy) set(2, dy, dz, "minecraft:bookshelf");
        // A lit red candle on a bookshelf (influences fire aspect, counts as one bookshelf).
        if (auto const* c = game::blockstates::of("minecraft:red_candle", {{"lit", true}, {"candles", 3}}))
            game::setBlock(region, {center.x + 2, center.y + 1, center.z}, *c);
        set(0, 0, 0, "minecraft:enchanting_table");
        auto pos = game::globalPos(region, center);
        auto& d  = table::data(pos);
        d.item   = game::makeItem("minecraft:diamond_sword");
        d.lapis  = game::makeItem("minecraft:lapis_lazuli", 16);
        table::markDirty(pos);
        addLevels(*p, 30);
        if (!table::open(*p, pos)) return output.error("Matrix enchanting is disabled.");
        // Three pieces on the bench; the grid stays empty (the client scenario places one).
        for (int i = 0; i < 3; ++i) table::operate(*p, OperAdd, 0, 0, 0);
        log().info("enchanting: demo table ready at {}", pos.key());
        output.success("Matrix enchanter demo opened.");
    });
});

} // namespace

} // namespace lp::feature::enchanting

LP_REGISTER_MODULE(lp::feature::enchanting::MatrixModule);
