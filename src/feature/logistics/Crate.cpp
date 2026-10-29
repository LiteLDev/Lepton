// Storage Crate (Quark oddities/CrateModule): holds `maxItems` items in total regardless of the
// stack sizes. Its screen shows 6 rows of 9 stacks and scrolls by rows.
//
// Bedrock: the stacks live in the world DB (`lp:logistics:crate:<pos>`). Opening a crate shows a
// window of 54 stacks in the vanilla double-chest screen (game/VirtualContainer); changes made in
// the window are written back to the crate, which is then compacted like Quark's crate. Mod
// clients get a scroll bar, scroll buttons and the item count drawn over the screen
// (src/client/logistics); vanilla clients pick the rows to show in a form when the crate holds
// more than one screen of stacks.

#include <algorithm>
#include <map>
#include <random>
#include <set>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"

#include "feature/logistics/CrateMessages.h"
#include "feature/logistics/Crate.h"
#include "feature/logistics/Logistics.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Forms.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

LP_REGISTER_MESSAGE(lp::feature::logistics::CrateView);
LP_REGISTER_MESSAGE(lp::feature::logistics::CrateScroll);

namespace lp::feature::logistics::crate {

using core::GlobalPos;

namespace {

struct Data {
    std::vector<ItemStack> stacks; ///< compact: no empty entries
    std::string            name;   ///< custom name of the placed item
    bool                   loaded = false;
};

struct Session {
    int                    scroll = 0;
    std::vector<ItemStack> last;    ///< the window as last written
    std::set<std::string>  viewers; ///< UUIDs of players who opened it (for the close message)
};

struct State {
    std::map<GlobalPos, Data>    data;
    std::map<GlobalPos, Session> sessions;
    std::set<GlobalPos>          dirty;
    std::map<GlobalPos, int>     clearVc; ///< vc storage to wipe after a session (ticks left)
    std::mt19937                 random{std::random_device{}()};
};
State& st() {
    static State s;
    return s;
}

std::string dbKey(GlobalPos const& p) { return "lp:logistics:crate:" + p.key(); }

Data& dataAt(GlobalPos const& p) {
    auto& d = st().data[p];
    if (d.loaded) return d;
    d.loaded = true;
    if (auto j = game::db::load(dbKey(p)); j && j->is_object()) {
        d.name = j->value("name", std::string{});
        for (auto const& e : j->value("items", nlohmann::json::array())) {
            if (!e.is_string()) continue;
            auto s = game::itemFromSnbt(e.get<std::string>());
            if (!game::isEmpty(s)) d.stacks.push_back(std::move(s));
        }
    }
    return d;
}

void flush() {
    for (auto const& p : st().dirty) {
        auto it = st().data.find(p);
        if (it == st().data.end() || (it->second.stacks.empty() && it->second.name.empty())) {
            game::db::erase(dbKey(p));
            continue;
        }
        nlohmann::json items = nlohmann::json::array();
        for (auto const& s : it->second.stacks) items.push_back(game::itemToSnbt(s));
        game::db::save(dbKey(p), {{"name", it->second.name}, {"items", items}});
    }
    st().dirty.clear();
}

int totalOf(std::vector<ItemStack> const& v) {
    int n = 0;
    for (auto const& s : v)
        if (!game::isEmpty(s)) n += s.mCount;
    return n;
}

void compact(std::vector<ItemStack>& v) {
    std::erase_if(v, [](ItemStack const& s) { return game::isEmpty(s); });
}

/// Same item and data, whatever the count (unstackables such as tools compare their whole data).
bool sameItem(ItemStack const& a, ItemStack const& b) {
    if (game::isEmpty(a) || game::isEmpty(b)) return false;
    if (a.getId() != b.getId() || a.getAuxValue() != b.getAuxValue()) return false;
    if (game::canMerge(a, b)) return true;
    return a.getMaxStackSize() == 1 && game::itemToSnbt(a) == game::itemToSnbt(b);
}

bool sameStack(ItemStack const& a, ItemStack const& b) {
    if (game::isEmpty(a) || game::isEmpty(b)) return game::isEmpty(a) && game::isEmpty(b);
    return a.mCount == b.mCount && sameItem(a, b);
}

/// CrateMenu#fullyOccupiedRows * numCols.
int maxScroll(int stacks) { return std::max(0, (stacks - 1) / kCols) * kCols; }

Player* playerByUuid(std::string const& uuid) {
    auto level = ll::service::getLevel();
    return level ? level->getPlayer(mce::UUID::fromString(uuid)) : nullptr;
}

void setOpenState(GlobalPos const& p, bool open) {
    auto* r = loadedRegion(p);
    if (!r) return;
    auto        bp = game::blockPos(p);
    auto const& b  = r->getBlock(bp);
    if (game::blockId(b) != kCrate || game::blockstates::getBool(b, "lepton:open").value_or(false) == open) return;
    if (auto const* n = game::blockstates::with(b, {{"lepton:open", open}})) game::setBlock(*r, bp, *n);
    float pitch = std::uniform_real_distribution<float>(0.9f, 1.0f)(st().random);
    playSoundAt(*r, Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}, open ? "random.barrel.open" : "random.barrel.close", 0.5f, pitch);
}

std::vector<ItemStack> windowOf(GlobalPos const& p, int scroll) {
    auto const& stacks = dataAt(p).stacks;
    std::vector<ItemStack> w(kWindow);
    for (int i = 0; i < kWindow; ++i) {
        size_t idx = static_cast<size_t>(scroll + i);
        if (idx < stacks.size()) w[static_cast<size_t>(i)] = stacks[idx];
    }
    return w;
}

void sendView(GlobalPos const& p, bool open) {
    auto it = st().sessions.find(p);
    CrateView view;
    view.open   = open;
    view.total  = total(p);
    view.max    = cfg().maxItems;
    view.stacks = stackCount(p);
    view.scroll = it != st().sessions.end() ? it->second.scroll : 0;
    std::set<std::string> const* recorded = it != st().sessions.end() ? &it->second.viewers : nullptr;
    for (auto* pl : net::modPlayers()) {
        if (!pl) continue;
        auto o     = vc::openBy(*pl);
        bool sees  = o && *o == p;
        bool known = recorded && recorded->contains(pl->getUuid().asString());
        if (sees || (!open && known)) net::sendToClient(*pl, view);
    }
}

void onWindowChanged(GlobalPos const& p);

/// Takes edits the viewers made in the window since the last poll into the crate first, so a
/// change from outside (hopper, pipe) never overwrites them.
void syncWindow(GlobalPos const& p) {
    if (st().sessions.contains(p)) onWindowChanged(p);
}

/// Rewrites the open window from the crate (after scrolling or a change from outside).
void refreshWindow(GlobalPos const& p) {
    auto it = st().sessions.find(p);
    if (it == st().sessions.end()) return;
    auto& s  = it->second;
    s.scroll = std::clamp(s.scroll, 0, maxScroll(stackCount(p)));
    auto w   = windowOf(p, s.scroll);
    vc::setItems(p, w);
    s.last = std::move(w);
    sendView(p, true);
}

void giveBack(GlobalPos const& p, ItemStack stack) {
    if (game::isEmpty(stack)) return;
    std::vector<Player*> viewers;
    if (auto level = ll::service::getLevel()) {
        level->forEachPlayer([&](Player& pl) {
            if (auto o = vc::openBy(pl); o && *o == p) viewers.push_back(&pl);
            return true;
        });
    }
    if (viewers.size() == 1) {
        game::inv::insert(game::inv::playerInventory(*viewers[0]), stack, game::inv::kPlayerAll);
        game::inv::sync(*viewers[0]);
    }
    if (!game::isEmpty(stack))
        if (auto* r = game::regionOf(p.dim)) spawnItemAt(*r, stack, Vec3{p.x + 0.5f, p.y + 1.1f, p.z + 0.5f}, Vec3{0, 0.1f, 0}, true);
}

/// Window edits by viewers -> crate (CrateSlot / CrateMenu rules: the total stays <= maxItems).
void onWindowChanged(GlobalPos const& p) {
    auto it = st().sessions.find(p);
    if (it == st().sessions.end()) return;
    auto& s      = it->second;
    auto  live   = vc::items(p, kWindow);
    auto& stacks = dataAt(p).stacks;
    s.last.resize(kWindow);
    struct Grown {
        size_t idx;
        int    amount;
    };
    std::vector<Grown> grown;
    bool               any = false;
    for (int i = 0; i < kWindow; ++i) {
        auto const& now  = live[static_cast<size_t>(i)];
        auto const& prev = s.last[static_cast<size_t>(i)];
        if (sameStack(now, prev)) continue;
        any        = true;
        size_t idx = static_cast<size_t>(s.scroll + i);
        if (stacks.size() <= idx) stacks.resize(idx + 1);
        int before = sameItem(now, prev) ? prev.mCount : 0;
        stacks[idx] = now;
        if (!game::isEmpty(now) && now.mCount > before) grown.push_back({idx, now.mCount - before});
    }
    if (!any) return;
    int excess = totalOf(stacks) - cfg().maxItems;
    for (auto g = grown.rbegin(); excess > 0 && g != grown.rend(); ++g) {
        auto& slot = stacks[g->idx];
        int   cut  = std::min({excess, g->amount, static_cast<int>(slot.mCount)});
        ItemStack back = slot;
        back.set(cut);
        slot.set(slot.mCount - cut);
        if (slot.mCount == 0) slot = ItemStack{};
        excess -= cut;
        giveBack(p, back);
    }
    compact(stacks);
    st().dirty.insert(p);
    s.scroll = std::clamp(s.scroll, 0, maxScroll(stackCount(p)));
    auto w   = windowOf(p, s.scroll);
    bool differs = false;
    for (int i = 0; i < kWindow && !differs; ++i) differs = !sameStack(w[static_cast<size_t>(i)], live[static_cast<size_t>(i)]);
    if (differs) vc::setItems(p, w);
    s.last = std::move(w);
    sendView(p, true);
}

vc::Spec specFor(GlobalPos const& p) {
    vc::Spec spec;
    spec.size          = kWindow;
    spec.screen        = vc::Screen::Chest;
    spec.titleKey      = "block.quark.crate";
    spec.titleFallback = "Storage Crate";
    auto const& name   = dataAt(p).name;
    if (!name.empty()) {
        spec.titleKey      = "lp.logistics.crate.named";
        spec.titleFallback = name;
    }
    spec.onChanged = [](GlobalPos const& pos) { onWindowChanged(pos); };
    return spec;
}

/// Opens the crate at row `scroll` for `player` (Quark: serverPlayer.openMenu(crate)).
bool openAt(Player& player, GlobalPos const& p, int scroll) {
    auto& sessions = st().sessions;
    auto  it       = sessions.find(p);
    if (it == sessions.end()) {
        Session s;
        s.scroll = std::clamp(scroll, 0, maxScroll(stackCount(p)));
        s.last   = windowOf(p, s.scroll);
        vc::setItems(p, s.last);
        it = sessions.emplace(p, std::move(s)).first;
        st().clearVc.erase(p);
    } else if (scroll != it->second.scroll) {
        syncWindow(p); // keep what the other viewers changed before moving the window
        it->second.scroll = scroll;
        refreshWindow(p);
    }
    if (!vc::open(player, p, specFor(p))) {
        if (vc::viewers(p) == 0) sessions.erase(p);
        return false;
    }
    st().sessions[p].viewers.insert(player.getUuid().asString());
    setOpenState(p, true);
    sendView(p, true);
    log().info("crate {} opened by {} ({} items)", p.key(), player.getRealName(), total(p));
    return true;
}

/// Vanilla clients cannot scroll the screen: when the crate holds more than one screen of stacks
/// they pick the rows to show first.
void openFor(Player& player, GlobalPos const& p) {
    int stacks = stackCount(p);
    if (net::hasClientMod(player) || stacks <= kWindow) {
        openAt(player, p, 0);
        return;
    }
    auto uuid = player.getUuid().asString();
    game::forms::SimpleForm form(
        trFor(player, "block.quark.crate", "Storage Crate"),
        trFor(player, "lp.logistics.crate.form", "{}/{} items. Choose the rows to show:", total(p), cfg().maxItems)
    );
    int rows = (stacks + kCols - 1) / kCols;
    for (int first = 0; first < rows; first += kRows) {
        int last   = std::min(rows, first + kRows);
        int scroll = std::min(first * kCols, maxScroll(stacks));
        form.button(trFor(player, "lp.logistics.crate.rows", "Rows {}-{}", first + 1, last), [p, scroll](Player& pl) {
            // the form may be answered much later: the crate must still be there and in reach
            if (!cfg().enableCrate || idAt(p) != kCrate || vc::openBy(pl)) return;
            auto at = pl.getPosition();
            float dx = at.x - (p.x + 0.5f), dy = at.y - (p.y + 0.5f), dz = at.z - (p.z + 0.5f);
            if (game::globalPos(pl.getDimensionBlockSource(), BlockPos{0, 0, 0}).dim != p.dim || dx * dx + dy * dy + dz * dz > 8 * 8) return;
            openAt(pl, p, scroll);
        });
    }
    form.sendTo(player);
}

/// Spills the crate when its block disappears (CrateBlockEntity#spillTheTea, stacks merged).
void spill(BlockSource& region, GlobalPos const& p) {
    vc::closeAll(p);
    if (st().sessions.contains(p)) sendView(p, false);
    st().sessions.erase(p);
    auto& d      = dataAt(p);
    auto  stacks = std::move(d.stacks);
    d.stacks.clear();
    d.name.clear();
    st().dirty.insert(p);
    vc::setItems(p, {});
    std::vector<ItemStack> merged;
    for (auto& s : stacks) {
        for (auto& m : merged) {
            if (game::isEmpty(s)) break;
            if (!game::canMerge(m, s) || m.mCount >= m.getMaxStackSize()) continue;
            int move = std::min<int>(m.getMaxStackSize() - m.mCount, s.mCount);
            m.set(m.mCount + move);
            s.set(s.mCount - move);
        }
        if (!game::isEmpty(s) && s.mCount > 0) merged.push_back(s);
    }
    for (auto const& s : merged)
        spawnItemAt(region, s, Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}, Vec3{0, 0.2f, 0}, true);
    if (!merged.empty()) log().info("crate {} spilled {} stacks", p.key(), merged.size());
}

class CratePort final : public Port {
public:
    explicit CratePort(GlobalPos p) : mPos(p) {
        syncWindow(p);
        mItems = dataAt(p).stacks;
        mItems.emplace_back();
    }
    int              size() const override { return static_cast<int>(mItems.size()); }
    ItemStack const& get(int slot) const override { return mItems[static_cast<size_t>(slot)]; }
    void             set(int slot, ItemStack const& item) override {
        mItems[static_cast<size_t>(slot)] = item;
        auto& stacks = dataAt(mPos).stacks;
        stacks       = mItems;
        compact(stacks);
        st().dirty.insert(mPos);
        refreshWindow(mPos);
    }
    /// CrateBlockEntity#getSlotLimit > 0.
    bool canPushIn(int slot, int, ItemStack const&) const override { return slotLimit(slot) > 0; }
    int  room(int, ItemStack const&) const override { return std::max(0, cfg().maxItems - totalOf(mItems)); }

private:
    int slotLimit(int slot) const {
        return std::clamp(mItems[static_cast<size_t>(slot)].mCount + cfg().maxItems - totalOf(mItems), 0, 64);
    }
    GlobalPos              mPos;
    std::vector<ItemStack> mItems;
};

class CrateModule {
public:
    void install() {
        game::db::onWorldSave([] { flush(); });
        game::db::onWorldClosed([] {
            st().data.clear();
            st().sessions.clear();
            st().dirty.clear();
            st().clearVc.clear();
        });
        registerPort(kCrate, [](GlobalPos const& p) -> std::unique_ptr<Port> {
            if (!cfg().enableCrate) return nullptr;
            return std::make_unique<CratePort>(p);
        });
        mScroll = net::onServer<CrateScroll>([](Player& player, CrateScroll const& m) {
            auto open = vc::openBy(player);
            if (!open || !st().sessions.contains(*open)) return;
            scroll(*open, m.down);
        });
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide() || !cfg().enableCrate) return;
            auto& region = player.getDimensionBlockSource();
            if (game::blockIdAt(region, ev.blockPos()) != kCrate) return;
            if (player.isSneaking() && !game::isEmpty(ev.item())) return;
            ev.cancel();
            auto uuid = player.getUuid().asString();
            auto pos  = game::globalPos(region, ev.blockPos());
            if (vc::openBy(player)) return;
            ll::coro::keepThis([uuid, pos]() -> ll::coro::CoroTask<> {
                co_await ll::chrono::ticks(1);
                auto* p = playerByUuid(uuid);
                if (!p || idAt(pos) != kCrate) co_return;
                openFor(*p, pos);
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide()) return;
            auto const& held = player.getSelectedItem();
            if (game::itemId(held) != kCrate || held.getCustomName().empty()) return;
            auto pos  = game::globalPos(player.getDimensionBlockSource(), ev.pos());
            auto name = held.getCustomName();
            ll::coro::keepThis([pos, name]() -> ll::coro::CoroTask<> {
                co_await ll::chrono::ticks(1);
                if (idAt(pos) != kCrate) co_return;
                dataAt(pos).name = name;
                st().dirty.insert(pos);
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            if (ev.previousBlock().getTypeName() != kCrate || ev.newBlock().getTypeName() == kCrate) return;
            auto pos = game::globalPos(region, ev.pos());
            int  dim = pos.dim;
            ll::coro::keepThis([pos, dim]() -> ll::coro::CoroTask<> {
                co_await ll::chrono::ticks(1);
                if (auto* r = game::regionOf(dim))
                    if (idAt(pos) != kCrate) spill(*r, pos);
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            auto& s = st();
            std::vector<GlobalPos> ended;
            for (auto const& [p, sess] : s.sessions)
                if (vc::viewers(p) == 0) ended.push_back(p);
            for (auto const& p : ended) {
                sendView(p, false);
                s.sessions.erase(p);
                setOpenState(p, false);
                s.clearVc[p] = 2; // the container helper writes the window back when it closes
            }
            for (auto it = s.clearVc.begin(); it != s.clearVc.end();) {
                if (--it->second > 0) {
                    ++it;
                    continue;
                }
                if (!s.sessions.contains(it->first) && vc::viewers(it->first) == 0) vc::setItems(it->first, {});
                it = s.clearVc.erase(it);
            }
        }));
    }
    void uninstall() {
        unregisterPort(kCrate);
        net::removeHandler(mScroll);
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        for (auto const& [p, s] : st().sessions) vc::closeAll(p);
        st().sessions.clear();
        flush();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    net::HandlerId                      mScroll = 0;
};

} // namespace

int total(GlobalPos const& p) { return totalOf(dataAt(p).stacks); }
int stackCount(GlobalPos const& p) { return static_cast<int>(dataAt(p).stacks.size()); }
std::vector<ItemStack> contents(GlobalPos const& p) { return dataAt(p).stacks; }
void setContents(GlobalPos const& p, std::vector<ItemStack> stacks) {
    syncWindow(p);
    compact(stacks);
    dataAt(p).stacks = std::move(stacks);
    st().dirty.insert(p);
    refreshWindow(p);
}
int scrollOf(GlobalPos const& p) {
    auto it = st().sessions.find(p);
    return it == st().sessions.end() ? -1 : it->second.scroll;
}
bool open(Player& player, GlobalPos const& p) {
    if (!cfg().enableCrate || idAt(p) != kCrate) return false;
    openFor(player, p);
    return true;
}

/// CrateMenu#scroll.
bool scroll(GlobalPos const& p, bool down) {
    auto it = st().sessions.find(p);
    if (it == st().sessions.end()) return false;
    syncWindow(p); // edits made in the window this tick go to the crate before it moves
    auto& s      = it->second;
    int   target = s.scroll + (down ? kCols : -kCols);
    if (target < 0 || target > maxScroll(stackCount(p))) return false;
    s.scroll = target;
    refreshWindow(p);
    return true;
}

} // namespace lp::feature::logistics::crate

LP_REGISTER_MODULE(lp::feature::logistics::crate::CrateModule);
