// U14 backpack, client mod part (docs/spec/backpack.md §4): buttons that switch between the
// backpack screen and the normal inventory, seed pouch Insert / Take buttons, the pouch tooltip line.

#ifdef LP_CLIENT

#include <atomic>

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

bool wearsBackpack() {
    auto* p = currentPlayer();
    return p && bp::isItem(p->getArmor(SharedTypes::Legacy::ArmorSlot::Torso), bp::kBackpack) && !p->isCreative();
}

void report(char const* what, Rect const& r, Vec size) {
    log().info("backpack client: {} button at {:.4f},{:.4f}", what, (r.x + r.w / 2) / size.x, (r.y + r.h / 2) / size.y);
}

} // namespace

LP_REGISTER_SCREEN_OVERLAY("*chest_screen", [](Overlay& o, ScreenInfo const& s) {
    if (!gBackpackScreen || !net::serverHasMod()) return;
    float cx = s.size.x / 2, cy = s.size.y / 2;
    Rect  r{cx - 88 - 62, cy - 80, 58, 16};
    o.button(r, ctr("lp.backpack.button.inventory", "Inventory"), [] {
        net::sendToServer(bp::BackpackSwitchMsg{"inventory"});
    }, ctr("lp.backpack.button.inventory_tip", "Armor and crafting"));
    static bool reported = false;
    if (!reported) {
        reported = true;
        report("inventory", r, s.size);
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
            net::sendToServer(bp::BackpackSwitchMsg{"backpack"});
        }, ctr("lp.backpack.button.backpack_tip", "Open the backpack"));
        static bool reported = false;
        if (!reported) {
            reported = true;
            report("backpack", r, s.size);
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
            log().info("backpack client: screen {}", m.open ? "open" : "closed");
        });
        // A new connection (another world / server) starts without an open backpack screen.
        net::onServerModReady([] { gBackpackScreen = false; });
    }
    void uninstall() { net::removeHandler(mScreen); }

private:
    net::HandlerId mScreen = 0;
};

} // namespace lp::client::backpack

LP_REGISTER_MODULE(lp::client::backpack::BackpackClientModule);

#endif // LP_CLIENT
