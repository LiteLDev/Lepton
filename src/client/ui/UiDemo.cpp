// The UI toolkit demo (client build, off by default): `config/ui.json` {"demo": true} shows a HUD
// panel, an inventory overlay (button, text field, a greyed-out slot), an extra tooltip line and,
// on key J (`key.lepton.ui_demo`), a radial custom screen. It is the reference usage of
// client/ui/Ui.h and is exercised by the client e2e scenario `ui`.

#include "mod/Config.h"

namespace lp::client::ui {
struct UiConfig {
    int  version = 1;
    /// Shows the UI toolkit demo (developers / e2e only).
    bool demo = false;
};
} // namespace lp::client::ui

#ifdef LP_CLIENT

#include <cmath>
#include <numbers>

#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "selftest/SelfTest.h"

LP_REGISTER_CONFIG("ui", lp::client::ui::UiConfig);

namespace lp::client::ui {

namespace {

bool demo() { return config::get<UiConfig>().demo; }

/// A radial menu with 8 entries; click one to pick it.
class RadialDemo : public CustomScreen {
public:
    void render(DrawContext& dc, Vec mouse) override {
        auto  size = dc.screenSize();
        Vec   c{size.x / 2, size.y / 2};
        dc.fill({0, 0, size.x, size.y}, Color{0, 0, 0, 0.35f});
        dc.text(ctr("lp.ui.demo.radial", "Lepton radial demo"), c.x, c.y - 80, colors::yellow, 1.5f, Align::Center);
        mHover = -1;
        for (int i = 0; i < kCount; ++i) {
            Rect r = cell(c, i);
            bool hover = r.contains(mouse.x, mouse.y);
            if (hover) mHover = i;
            dc.fill(r, hover ? Color{0.4f, 0.4f, 0.7f, 0.9f} : colors::panel);
            dc.outline(r, hover ? colors::white : colors::gray);
            dc.text(std::to_string(i + 1), r.x + r.w / 2, r.y + 4, colors::white, 1, Align::Center);
        }
        dc.item(game::makeItem("minecraft:emerald", 3), c.x - 8, c.y - 8);
        if (!mReported) {
            mReported = true;
            Rect r    = cell(c, 0);
            log().info("ui demo: radial cell 1 at {:.4f},{:.4f}", (r.x + r.w / 2) / size.x, (r.y + r.h / 2) / size.y);
        }
    }
    void onClick(Vec pos, int button) override {
        if (button != 0) return;
        for (int i = 0; i < kCount; ++i) {
            if (!cell(lastCenter(), i).contains(pos.x, pos.y)) continue;
            log().info("ui demo: radial pick {}", i + 1);
            close();
            return;
        }
    }
    void onClose() override { log().info("ui demo: radial closed"); }

private:
    static constexpr int kCount = 8;
    int                  mHover    = -1;
    bool                 mReported = false;
    Vec                  mCenter;

    Rect cell(Vec c, int i) {
        mCenter     = c;
        double a    = i * 2 * std::numbers::pi / kCount - std::numbers::pi / 2;
        float  x    = c.x + static_cast<float>(std::cos(a) * 55);
        float  y    = c.y + static_cast<float>(std::sin(a) * 55);
        return {x - 14, y - 9, 28, 18};
    }
    [[nodiscard]] Vec lastCenter() const { return mCenter; }
};

} // namespace

LP_REGISTER_HUD([](DrawContext& dc) {
    if (!demo()) return;
    Rect panel{4, 40, 120, 30};
    dc.fill(panel, colors::panel);
    dc.outline(panel, colors::yellow);
    dc.item(game::makeItem("minecraft:diamond", 5), panel.x + 4, panel.y + 7);
    dc.text(ctr("lp.ui.demo.hud", "Lepton HUD demo"), panel.x + 24, panel.y + 5, colors::yellow);
    dc.text(ctr("lp.ui.demo.hud_sub", "text, rects, items"), panel.x + 24, panel.y + 17, colors::gray, 0.75f);
});

LP_REGISTER_SCREEN_OVERLAY("inventory_screen", [](Overlay& o, ScreenInfo const& s) {
    if (!demo()) return;
    float cx = s.size.x / 2, cy = s.size.y / 2;
    o.button({cx + 92, cy - 82, 40, 14}, ctr("lp.ui.demo.button", "Sort"), [] { log().info("ui demo: button clicked"); },
             ctr("lp.ui.demo.button_tip", "Lepton overlay button"));
    o.textField("ui_demo_search", {cx + 92, cy - 64, 70, 12}, ctr("lp.ui.demo.search", "Search..."),
                [](std::string const& text) { log().info("ui demo: search '{}'", text); });
    int greyed = 0;
    for (auto const& slot : s.slots) {
        if (slot.index != 0 || slot.collection.find("inventory") == std::string::npos) continue;
        o.greyOut(slot.rect);
        ++greyed;
    }
    static bool reported = false;
    if (!reported) {
        // Positions as fractions of the screen, for the e2e scenario's clicks.
        reported = true;
        log().info("ui demo: inventory overlay drawn ({} slots found, {} greyed)", s.slots.size(), greyed);
        log().info("ui demo: button at {:.4f},{:.4f}", (cx + 112) / s.size.x, (cy - 75) / s.size.y);
        log().info("ui demo: field at {:.4f},{:.4f}", (cx + 127) / s.size.x, (cy - 58) / s.size.y);
        for (auto const& slot : s.slots) {
            if (slot.index != 0 || slot.collection.find("hotbar") == std::string::npos) continue;
            log().info("ui demo: slot {}:0 at {:.4f},{:.4f}", slot.collection, (slot.rect.x + slot.rect.w / 2) / s.size.x,
                       (slot.rect.y + slot.rect.h / 2) / s.size.y);
            break;
        }
    }
});

LP_REGISTER_TOOLTIP([](ItemStackBase const&, std::vector<std::string>& lines) {
    if (demo()) lines.push_back("§9" + ctr("lp.ui.demo.tooltip", "Lepton tooltip line"));
});

LP_REGISTER_KEYBIND("ui_demo", Key::J, [] {
    if (!demo()) return;
    openScreen(std::make_shared<RadialDemo>());
});

// ---- selftests (client build; run in a local world) -------------------------------------------------

LP_REGISTER_SELFTEST("ui.keybinds", [](selftest::Context& ctx) {
    bool found = false;
    for (auto const& k : KeybindRegistry::entries()) found = found || k.name == "ui_demo";
    ctx.check(found, "the demo keybind is registered");
    auto keys = keysOf("ui_demo");
    ctx.check(!keys.empty(), "keybinds get their keys from config/keybinds.json");
    ctx.check(!pressKeybind("no_such_keybind"), "unknown keybinds are refused");
});

LP_REGISTER_SELFTEST("ui.tooltip", [](selftest::Context& ctx) {
    auto text = tooltipFor(game::makeItem("minecraft:diamond"));
    bool has  = text.find(ctr("lp.ui.demo.tooltip", "Lepton tooltip line")) != std::string::npos;
    ctx.check(has == demo(), "tooltip hooks add their lines (demo on) or nothing (demo off)");
    ctx.info("tooltip: " + text);
});

} // namespace lp::client::ui

#endif // LP_CLIENT
