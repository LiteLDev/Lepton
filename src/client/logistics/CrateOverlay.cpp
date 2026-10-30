// Crate screen for mod clients (Quark's CrateScreen): the item count ("total/max") and a scroll bar
// with scroll buttons next to the crate's 6x9 window. The window itself is the vanilla double-chest
// screen served by the server (game/VirtualContainer); scrolling asks the server to move it.

#ifdef LP_CLIENT

#include <algorithm>

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/logistics/CrateMessages.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::client::logistics {

using feature::logistics::CrateScroll;
using feature::logistics::CrateView;
namespace ui = lp::client::ui;

namespace {

CrateView gView;
bool      gReported = false;
bool      gShown    = false; ///< the crate's chest screen was on top since the view opened

/// CrateMenu#fullyOccupiedRows.
int fullRows(int stacks) { return std::max(0, (stacks - 1) / 9); }

void scroll(bool down) {
    net::sendToServer(CrateScroll{down});
    log().info("crate overlay: scroll {}", down ? "down" : "up");
}

} // namespace

bool gDispenserSeen = false; ///< a dispenser screen (the feeding trough) was logged since the last HUD frame

// Back in the game (no screen) or in another world: no crate is open any more.
LP_REGISTER_HUD([](ui::DrawContext&) {
    if (ui::topScreen() != "hud_screen") return;
    if (gView.open && gShown) gView = {};
    gDispenserSeen = false;
});

// One log line per dispenser screen shown (the feeding trough's 3x3 screen): e2e scenarios wait for it.
LP_REGISTER_SCREEN_OVERLAY("*dispenser*", [](ui::Overlay&, ui::ScreenInfo const& s) {
    if (gDispenserSeen) return;
    gDispenserSeen = true;
    log().info("logistics: dispenser screen shown ({}, {} slots)", s.name, s.slots.size());
});

LP_REGISTER_SCREEN_OVERLAY("*chest_screen", [](ui::Overlay& o, ui::ScreenInfo const& s) {
    if (!gView.open) return;
    // bounds of the crate window's slots
    bool     any = false;
    ui::Rect r;
    for (auto const& slot : s.slots) {
        if (slot.collection != "container_items") continue;
        if (!any) r = slot.rect;
        float x1 = std::max(r.x + r.w, slot.rect.x + slot.rect.w), y1 = std::max(r.y + r.h, slot.rect.y + slot.rect.h);
        r.x = std::min(r.x, slot.rect.x);
        r.y = std::min(r.y, slot.rect.y);
        r.w = x1 - r.x;
        r.h = y1 - r.y;
        any = true;
    }
    if (!any) return;
    gShown   = true;
    auto& dc = o.draw();
    // "total/max" above the window, right aligned left of the close button (Quark: on the title line)
    auto count = ctr("lp.logistics.crate.count", "{}/{}", gView.total, gView.max);
    dc.text(count, r.x + r.w - 16, r.y - 10, ui::Color::rgb(0x404040), 1, ui::Align::Right, false);
    // scroll bar: a track right of the window with a thumb and two buttons
    int      rows  = fullRows(gView.stacks);
    float    x     = r.x + r.w + 4;
    ui::Rect track{x, r.y + 14, 12, r.h - 28};
    dc.fill(track, ui::Color::rgb(0x8b8b8b));
    dc.outline(track, ui::Color::rgb(0x373737));
    float frac   = rows > 0 ? static_cast<float>(gView.scroll / 9) / static_cast<float>(rows) : 0.0f;
    float thumbH = 15;
    ui::Rect thumb{x, track.y + (track.h - thumbH) * std::clamp(frac, 0.0f, 1.0f), 12, thumbH};
    dc.fill(thumb, rows > 0 ? ui::Color::rgb(0xc6c6c6) : ui::Color::rgb(0x9a9a9a));
    dc.outline(thumb, ui::Color::rgb(0x555555));
    ui::Rect up{x, r.y, 12, 12}, down{x, r.y + r.h - 12, 12, 12};
    o.button(up, "^", [] { scroll(false); }, ctr("lp.logistics.crate.scroll_up", "Scroll up"));
    o.button(down, "v", [] { scroll(true); }, ctr("lp.logistics.crate.scroll_down", "Scroll down"));
    if (!gReported && s.settled) { // positions are final once the opening animation ended
        gReported = true;
        log().info("crate overlay: drawn on {} ({} items, {} stacks)", s.name, gView.total, gView.stacks);
        log().info("crate overlay: down button at {:.4f},{:.4f}", (down.x + 6) / s.size.x, (down.y + 6) / s.size.y);
        log().info("crate overlay: up button at {:.4f},{:.4f}", (up.x + 6) / s.size.x, (up.y + 6) / s.size.y);
    }
});

class CrateOverlayModule {
public:
    void install() {
        mHandler = net::onClient<CrateView>([](CrateView const& v) {
            if (v.open && !gView.open) gReported = gShown = false;
            gView = v;
        });
    }
    void uninstall() {
        net::removeHandler(mHandler);
        gView = {};
    }

private:
    net::HandlerId mHandler = 0;
};

} // namespace lp::client::logistics

LP_REGISTER_MODULE(lp::client::logistics::CrateOverlayModule);

#endif // LP_CLIENT
