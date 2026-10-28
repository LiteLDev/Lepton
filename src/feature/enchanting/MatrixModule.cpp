// MatrixEnchanting: hooks the enchanting tables (vanilla with automaticallyConvert, and
// lepton:matrix_enchanter), the companion-protocol handlers and the session tick.

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
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
                        if (bid == table::kBlockId)
                            if (auto const* b = game::blockstates::of("minecraft:enchanting_table"))
                                game::setBlock(*r, game::blockPos(pos), *b);
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

} // namespace

} // namespace lp::feature::enchanting

LP_REGISTER_MODULE(lp::feature::enchanting::MatrixModule);
