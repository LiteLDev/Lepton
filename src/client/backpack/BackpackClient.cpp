// U14 backpack, client mod part (docs/spec/backpack.md §4): buttons that switch between the
// backpack screen and the normal inventory, seed pouch Insert / Take buttons, the pouch tooltip line.

#ifdef LP_CLIENT

#include <algorithm>
#include <atomic>
#include <chrono>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/Packet.h"
#include "mc/network/packet/InteractPacket.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/backpack/Backpack.h"
#include "feature/backpack/Messages.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::client::backpack {

namespace bp = lp::feature::backpack;
using namespace lp::client::ui;

namespace {

std::atomic<bool> gBackpackScreen{false};
/// "Inventory" was clicked: 1 = waiting for the server to close the backpack screen, 2 = closed,
/// open the inventory as soon as no screen is shown. "Backpack" was clicked: 3 = ask the server for
/// the backpack screen as soon as the inventory is gone. A switch gives up after kSwitchTimeout.
enum SwitchState : int { kIdle = 0, kAwaitClose = 1, kOpenInventory = 2, kAskBackpack = 3 };
std::atomic<int>                                   gSwitchState{kIdle};
std::atomic<std::chrono::steady_clock::rep>        gSwitchDeadline{0};
constexpr std::chrono::seconds                     kSwitchTimeout{5};

void startSwitch(SwitchState state) {
    gSwitchDeadline = (std::chrono::steady_clock::now() + kSwitchTimeout).time_since_epoch().count();
    gSwitchState    = state;
}

bool wearsBackpack() {
    auto* p = currentPlayer();
    return p && bp::isItem(p->getArmor(SharedTypes::Legacy::ArmorSlot::Torso), bp::kBackpack) && !p->isCreative();
}

void report(char const* what, Rect const& r, Vec size, std::string const& screen) {
    float fx = (r.x + r.w / 2) / size.x, fy = (r.y + r.h / 2) / size.y;
    log().info("backpack client: {} button at {:.4f},{:.4f} ({})", what, fx, fy, screen);
}

constexpr float kButtonW = 58, kButtonH = 16, kButtonGap = 4;

/// Top-left corner of the button column: right of the player's inventory and hotbar slots (the
/// panel's layout differs between screens, sizes and with the recipe book), centre as a fallback.
Vec buttonAnchor(ScreenInfo const& s) {
    float right = -1, top = 1e9f;
    for (auto const& slot : s.slots) {
        if (slot.collection != "inventory_items" && slot.collection != "hotbar_items") continue;
        right = std::max(right, slot.rect.x + slot.rect.w);
        top   = std::min(top, slot.rect.y);
    }
    if (right < 0) return {s.size.x / 2 + 96, s.size.y / 2};
    return {std::min(right + 10, s.size.x - kButtonW - 2), top};
}

/// Logs a button's position once per opening of the screen, after the screen settled (e2e).
void reportOnce(bool& reported, char const* what, Rect const& r, ScreenInfo const& s) {
    if (s.frames <= 1) reported = false;
    if (reported || !s.settled) return;
    reported = true;
    report(what, r, s.size, s.name);
}

} // namespace

LP_REGISTER_SCREEN_OVERLAY("*chest_screen", [](Overlay& o, ScreenInfo const& s) {
    if (!gBackpackScreen || !net::serverHasMod()) return;
    auto a = buttonAnchor(s);
    Rect r{a.x, a.y, kButtonW, kButtonH};
    o.button(r, ctr("lp.backpack.button.inventory", "Inventory"), [] {
        log().info("backpack client: switching to the inventory");
        startSwitch(kAwaitClose);
        if (!net::sendToServer(bp::BackpackSwitchMsg{"inventory"})) gSwitchState = kIdle;
    }, ctr("lp.backpack.button.inventory_tip", "Armor and crafting"));
    static bool reported = false;
    reportOnce(reported, "inventory", r, s);
});

LP_REGISTER_SCREEN_OVERLAY("inventory_screen", [](Overlay& o, ScreenInfo const& s) {
    gBackpackScreen = false;
    if (!net::serverHasMod()) return;
    auto a = buttonAnchor(s);
    if (wearsBackpack()) {
        Rect r{a.x, a.y, kButtonW, kButtonH};
        o.button(r, ctr("lp.backpack.button.backpack", "Backpack"), [] {
            log().info("backpack client: switching to the backpack");
            // The inventory screen may be the client's own (the server does not know it): close it
            // here; once it is gone the server is asked for the backpack screen (the game ignores a
            // container the server opens over another screen).
            // (Overlay buttons only get clicks on the top screen: the pop closes this inventory.)
            if (auto* c = currentClient()) c->getClientSceneStack()->schedulePopScreen(1);
            startSwitch(kAskBackpack);
        }, ctr("lp.backpack.button.backpack_tip", "Open the backpack"));
        static bool reported = false;
        reportOnce(reported, "backpack", r, s);
        a.y += kButtonH + kButtonGap;
    }
    auto* p = currentPlayer();
    if (p && bp::isItem(p->getSelectedItem(), bp::kSeedPouch)) {
        Rect insert{a.x, a.y, kButtonW, kButtonH};
        o.button(insert, ctr("lp.backpack.button.insert", "Insert"), [] {
            log().info("backpack client: pouch insert");
            net::sendToServer(bp::PouchActionMsg{"insert"});
        }, ctr("lp.backpack.button.insert_tip", "Put matching seeds into the pouch"));
        Rect take{a.x, a.y + kButtonH + kButtonGap, kButtonW, kButtonH};
        o.button(take, ctr("lp.backpack.button.take", "Take"), [] {
            log().info("backpack client: pouch take");
            net::sendToServer(bp::PouchActionMsg{"take"});
        }, ctr("lp.backpack.button.take_tip", "Take one stack out of the pouch"));
        static bool reportedInsert = false, reportedTake = false;
        reportOnce(reportedInsert, "insert", insert, s);
        reportOnce(reportedTake, "take", take, s);
    }
});

// Screen switches wait for the HUD to be on top. The game shows no inventory the server opens by
// itself: once the backpack screen is gone the client opens its inventory (the server lets an
// inventory request through right after the switch).
LP_REGISTER_HUD([](DrawContext&) {
    int state = gSwitchState;
    if (state == kIdle) return;
    auto* c = currentClient();
    auto* p = currentPlayer();
    if (std::chrono::steady_clock::now().time_since_epoch().count() > gSwitchDeadline) {
        if (gSwitchState.compare_exchange_strong(state, kIdle))
            log().warn("backpack client: could not switch screens (state {}, top screen {})", state, topScreen());
        return;
    }
    if (state == kAwaitClose || !c || !p || topScreen() != "hud_screen") return;
    if (!gSwitchState.compare_exchange_strong(state, kIdle)) return;
    if (state == kAskBackpack) {
        // The request the E key sends: the server answers it with the backpack screen. (A screen
        // the server opens without a request from the client is not shown.) "backpack" first: it
        // cancels the server's pass for the inventory request of a previous "Inventory" click.
        net::sendToServer(bp::BackpackSwitchMsg{"backpack"});
        if (auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::Interact)) {
            auto& interact      = static_cast<InteractPacket&>(*pkt);
            interact.mAction    = InteractPacketPayload::Action::OpenInventory;
            interact.mTargetId  = p->getRuntimeID();
            interact.mPos       = std::nullopt;
            interact.sendToServer();
            log().info("backpack client: asked for the backpack");
        }
        return;
    }
    p->openInventory();
    log().info("backpack client: opened the inventory");
});

LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
    if (!bp::isItem(item, bp::kSeedPouch)) return;
    auto c = bp::pouchContents(item);
    lines.push_back("§7" + ctr("lp.backpack.pouch_count", "{}/{}", c.count, bp::cfg().maxItems));
});

class BackpackClientModule {
public:
    void install() {
        mScreen = net::onClient<bp::BackpackScreenMsg>([](bp::BackpackScreenMsg const& m) {
            gBackpackScreen = m.open;
            int awaiting = kAwaitClose;
            if (!m.open) gSwitchState.compare_exchange_strong(awaiting, kOpenInventory);
            log().info("backpack client: screen {}", m.open ? "open" : "closed");
        });
        // A new connection (another world / server) starts without an open backpack screen.
        net::onServerModReady([] {
            gBackpackScreen = false;
            gSwitchState    = kIdle;
        });
    }
    void uninstall() { net::removeHandler(mScreen); }

private:
    net::HandlerId mScreen = 0;
};

} // namespace lp::client::backpack

LP_REGISTER_MODULE(lp::client::backpack::BackpackClientModule);

#endif // LP_CLIENT
