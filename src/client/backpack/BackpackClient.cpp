// U14 backpack, client mod part (docs/spec/backpack.md §4): buttons that switch between the
// backpack screen and the normal inventory, seed pouch Insert / Take buttons, the pouch tooltip line.

#ifdef LP_CLIENT

#include <atomic>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
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
/// open the inventory as soon as no screen is shown. Frames left before giving up in gSwitchFrames.
std::atomic<int> gSwitchState{0};
std::atomic<int> gSwitchFrames{0};

bool wearsBackpack() {
    auto* p = currentPlayer();
    return p && bp::isItem(p->getArmor(SharedTypes::Legacy::ArmorSlot::Torso), bp::kBackpack) && !p->isCreative();
}

void report(char const* what, Rect const& r, Vec size, std::string const& screen) {
    log().info("backpack client: {} button at {:.4f},{:.4f} ({})", what, (r.x + r.w / 2) / size.x, (r.y + r.h / 2) / size.y, screen);
}

} // namespace

LP_REGISTER_SCREEN_OVERLAY("*chest_screen", [](Overlay& o, ScreenInfo const& s) {
    if (!gBackpackScreen || !net::serverHasMod()) return;
    float cx = s.size.x / 2, cy = s.size.y / 2;
    Rect  r{cx - 88 - 62, cy - 80, 58, 16};
    o.button(r, ctr("lp.backpack.button.inventory", "Inventory"), [] {
        log().info("backpack client: switching to the inventory");
        gSwitchFrames = 600;
        gSwitchState  = 1;
        if (!net::sendToServer(bp::BackpackSwitchMsg{"inventory"})) gSwitchState = 0;
    }, ctr("lp.backpack.button.inventory_tip", "Armor and crafting"));
    static bool reported = false;
    if (!reported) {
        reported = true;
        report("inventory", r, s.size, s.name);
    }
});

LP_REGISTER_SCREEN_OVERLAY("inventory_screen", [](Overlay& o, ScreenInfo const& s) {
    gBackpackScreen = false;
    if (!net::serverHasMod()) return;
    float cx = s.size.x / 2, cy = s.size.y / 2;
    float y  = cy - 80;
    if (wearsBackpack()) {
        Rect r{cx - 88 - 62, y, 58, 16};
        o.button(r, ctr("lp.backpack.button.backpack", "Backpack"), [] {
            log().info("backpack client: switching to the backpack");
            net::sendToServer(bp::BackpackSwitchMsg{"backpack"});
        }, ctr("lp.backpack.button.backpack_tip", "Open the backpack"));
        static bool reported = false;
        if (!reported) {
            reported = true;
            report("backpack", r, s.size, s.name);
        }
        y += 20;
    }
    auto* p = currentPlayer();
    if (p && bp::isItem(p->getSelectedItem(), bp::kSeedPouch)) {
        o.button({cx - 88 - 62, y, 58, 16}, ctr("lp.backpack.button.insert", "Insert"), [] {
            net::sendToServer(bp::PouchActionMsg{"insert"});
        }, ctr("lp.backpack.button.insert_tip", "Put matching seeds into the pouch"));
        o.button({cx - 88 - 62, y + 20, 58, 16}, ctr("lp.backpack.button.take", "Take"), [] {
            net::sendToServer(bp::PouchActionMsg{"take"});
        }, ctr("lp.backpack.button.take_tip", "Take one stack out of the pouch"));
    }
});

// The game shows no inventory screen the server opens by itself: once the backpack screen is gone,
// the client asks for its inventory as the E key does (the server lets this request through).
LP_REGISTER_HUD([](DrawContext&) {
    int state = gSwitchState;
    if (state == 0) return;
    auto* c = currentClient();
    auto* p = currentPlayer();
    if (--gSwitchFrames <= 0) {
        gSwitchState = 0;
        log().warn("backpack client: could not open the inventory (state {}, menu {})", state, c && c->isShowingMenu());
        return;
    }
    if (state != 2 || !c || !p || c->isShowingMenu()) return;
    gSwitchState = 0;
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
            if (!m.open && gSwitchState == 1) gSwitchState = 2;
            log().info("backpack client: screen {}", m.open ? "open" : "closed");
        });
        // A new connection (another world / server) starts without an open backpack screen.
        net::onServerModReady([] {
            gBackpackScreen = false;
            gSwitchState    = 0;
        });
    }
    void uninstall() { net::removeHandler(mScreen); }

private:
    net::HandlerId mScreen = 0;
};

} // namespace lp::client::backpack

LP_REGISTER_MODULE(lp::client::backpack::BackpackClientModule);

#endif // LP_CLIENT
