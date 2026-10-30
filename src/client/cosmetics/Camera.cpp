// Camera (client): screenshot mode with filters, rulers, borders, overlays (docs/spec/cosmetics.md §1).
#ifdef LP_CLIENT

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelData.h"

#include "client/Client.h"
#include "client/cosmetics/Filter.h"
#include "client/ui/Ui.h"
#include "feature/cosmetics/Cosmetics.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::cosmetics {

namespace cos = lp::feature::cosmetics;
namespace ui  = lp::client::ui;

// ---- date format (Java SimpleDateFormat subset) ---------------------------------------------------

std::string formatDate(std::string const& pattern, std::tm const& t) {
    static char const* const kMonths[]{"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    static char const* const kDays[]{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    auto pad = [](int v, size_t n) {
        auto s = std::to_string(v);
        return s.size() < n ? std::string(n - s.size(), '0') + s : s;
    };
    std::string out;
    for (size_t i = 0; i < pattern.size();) {
        char c = pattern[i];
        if (c == '\'') { // quoted text, '' = a quote
            size_t j = i + 1;
            if (j < pattern.size() && pattern[j] == '\'') {
                out += '\'';
                i += 2;
                continue;
            }
            while (j < pattern.size() && pattern[j] != '\'') out += pattern[j++];
            i = j + 1;
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(c))) {
            out += c;
            ++i;
            continue;
        }
        size_t n = 0;
        while (i + n < pattern.size() && pattern[i + n] == c) ++n;
        i += n;
        switch (c) {
        case 'y': out += n == 2 ? pad((t.tm_year + 1900) % 100, 2) : pad(t.tm_year + 1900, n); break;
        case 'M':
            if (n >= 4) out += kMonths[t.tm_mon];
            else if (n == 3) out += std::string(kMonths[t.tm_mon]).substr(0, 3);
            else out += pad(t.tm_mon + 1, n);
            break;
        case 'd': out += pad(t.tm_mday, n); break;
        case 'H': out += pad(t.tm_hour, n); break;
        case 'h': out += pad(t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, n); break;
        case 'm': out += pad(t.tm_min, n); break;
        case 's': out += pad(t.tm_sec, n); break;
        case 'S': out += pad(0, n); break;
        case 'a': out += t.tm_hour < 12 ? "AM" : "PM"; break;
        case 'E': out += n >= 4 ? std::string(kDays[t.tm_wday]) : std::string(kDays[t.tm_wday]).substr(0, 3); break;
        case 'D': out += pad(t.tm_yday + 1, n); break;
        default: return {}; // unknown letter: invalid pattern
        }
    }
    return out;
}

/// The date text of the Date overlay ("MM/dd/yyyy" when `dateFormat` is invalid, like Quark's check).
std::string dateText(std::tm const& t) {
    auto s = formatDate(cos::config().dateFormat, t);
    return s.empty() && !cos::config().dateFormat.empty() ? formatDate("MM/dd/yyyy", t) : s;
}

namespace {

constexpr int kRulers   = 4;
constexpr int kBorders  = 6;
constexpr int kOverlays = 5;

struct State {
    bool cameraMode = false;
    int  shader = 0, rulers = 0, borders = 0, overlay = 0;
    int  hideFrames = 0;   ///< frames left without the settings HUD (shot in progress)
    bool queueShot  = false;
    bool savedHideGui = false;
} gState;

int cycle(int cur, int max, bool back) {
    int v = cur + (back ? -1 : 1);
    return v < 0 ? max - 1 : (v >= max ? 0 : v);
}

IOptionRegistry* options() {
    auto* c = client::currentClient();
    return c ? &c->getOptions() : nullptr;
}

void applyFilter() { filter::set(gState.cameraMode ? filter::preset(gState.shader) : std::nullopt); }

void setCameraMode(bool on) {
    if (gState.cameraMode == on) return;
    gState.cameraMode = on;
    if (auto* o = options()) {
        if (on) gState.savedHideGui = o->getHideHud();
        o->setHideGUI(on ? true : gState.savedHideGui);
    }
    applyFilter();
    log().info("cosmetics: camera mode {}", on ? "on" : "off");
}

std::string keyName(int vk) {
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x6F);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, static_cast<char>(vk));
    switch (vk) {
    case 0x0D: return "ENTER";
    case 0x20: return "SPACE";
    case 0x09: return "TAB";
    default: return "?";
    }
}

std::string worldName() {
    auto* c = client::currentClient();
    if (!c) return "N/A";
    auto* level = c->getLevel();
    if (!level) return "N/A";
    auto const& name = level->getLevelData().mLevelName;
    return name->empty() ? std::string("N/A") : std::string(*name);
}

void showMessage(std::string const& text) {
    auto* c = client::currentClient();
    if (!c) return;
    c->getGuiData()->displayClientMessage(text, std::nullopt, false);
}

void shoot() {
    auto  now  = std::chrono::system_clock::now();
    auto  tt   = std::chrono::system_clock::to_time_t(now);
    std::tm t{};
    localtime_s(&t, &tt);
    char name[64];
    std::strftime(name, sizeof(name), "%Y-%m-%d_%H.%M.%S.png", &t);
    auto dir = lp::config::configDir().parent_path() / "screenshots";
    auto path = dir / name;
    bool queued = filter::requestCapture(path, [](bool ok, std::string const& p) {
        log().info("cosmetics: screenshot {} {}", ok ? "saved" : "failed", p);
    });
    if (queued) showMessage(ctr("lp.cosmetics.camera.saved", "Saved screenshot as {}", std::string(name)));
    else log().warn("cosmetics: screenshot not supported by this renderer");
}

// ---- HUD ------------------------------------------------------------------------------------------

constexpr ui::Color kRuler = ui::Color::rgb(0x000000, 0x33 / 255.f);

void vruler(ui::DrawContext& dc, float x0, float y0, float x, float h) { dc.fill({x0 + x, y0, 1, h}, kRuler); }
void hruler(ui::DrawContext& dc, float x0, float y0, float y, float w) { dc.fill({x0, y0 + y, w, 1}, kRuler); }

/// The 16x16 camera icon of the settings HUD (original pixel art).
void drawCameraIcon(ui::DrawContext& dc, float x, float y) {
    static constexpr char const* kRows[16]{
        "................", "................", "..........kkk...", "...kkkk..kyyyk..", "..kddddkkkkkkk..", ".kggggggggggggk.",
        ".kgllgkkkkkgggk.", ".kgggkbbbbbkggk.", ".kgggkbccbbkggk.", ".kgggkbcbbbkggk.", ".kgggkbbbbbkggk.", ".kggggkkkkkgggk.",
        ".kddddddddddddk.", "..kkkkkkkkkkkk..", "................", "................",
    };
    for (int r = 0; r < 16; ++r)
        for (int col = 0; col < 16; ++col) {
            ui::Color c;
            switch (kRows[r][col]) {
            case 'k': c = ui::Color::rgb(0x1c1c22); break;
            case 'd': c = ui::Color::rgb(0x3a3a44); break;
            case 'g': c = ui::Color::rgb(0x5c5e6a); break;
            case 'l': c = ui::Color::rgb(0xa8acb8); break;
            case 'b': c = ui::Color::rgb(0x2a3c66); break;
            case 'c': c = ui::Color::rgb(0x8fb4ff); break;
            case 'y': c = ui::Color::rgb(0xffd84a); break;
            default: continue;
            }
            dc.fill({x + col, y + r, 1, 1}, c);
        }
}

void renderHud(ui::DrawContext& dc) {
    if (!gState.cameraMode) return;
    if (!client::inLevel()) {
        setCameraMode(false);
        return;
    }
    auto  size    = dc.screenSize();
    float twidth  = size.x, theight = size.y;
    float width = twidth, height = theight;
    float padH = 0, padV = 0;
    ui::Color padColor = ui::colors::black;
    double    target   = -1;
    switch (gState.borders) {
    case 1: target = 1; break;
    case 2: target = 4.0 / 3.0; break;
    case 3: target = 16.0 / 9.0; break;
    case 4: target = 21.0 / 9.0; break;
    case 5: {
        float border = static_cast<float>(static_cast<int>(20.0 * (static_cast<double>(twidth * theight) / 518400)));
        padH = padV = border;
        padColor    = ui::colors::white;
        break;
    }
    default: break;
    }
    if (target > 0) {
        double cur = width / height;
        if (cur > target) padH = std::floor((width - static_cast<float>(height * target)) / 2);
        else if (cur < target) padV = std::floor((height - static_cast<float>(width / target)) / 2);
    }
    width -= padH * 2;
    height -= padV * 2;
    if (padH > 0) {
        dc.fill({0, 0, padH, theight}, padColor);
        dc.fill({twidth - padH, 0, padH, theight}, padColor);
    }
    if (padV > 0) {
        dc.fill({0, 0, twidth, padV}, padColor);
        dc.fill({0, theight - padV, twidth, padV}, padColor);
    }

    // Overlay.
    std::string text;
    bool        shadow = true;
    float       scale  = 2;
    ui::Color   color  = ui::colors::white;
    float       ox = -1, oy = -1;
    auto*       player = client::currentPlayer();
    switch (gState.overlay) {
    case 1: {
        auto    tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm t{};
        localtime_s(&t, &tt);
        text  = dateText(t);
        color = ui::Color::rgb(0xf77700);
        break;
    }
    case 2:
        text  = ctr("quark.camera.greetings", "Greetings from {}!", worldName());
        ox    = padH + 20;
        oy    = padV + 20;
        scale = 3;
        color = ui::Color::rgb(0xef5425);
        break;
    case 3:
        text   = player ? player->getRealName() : std::string();
        scale  = 6;
        shadow = false;
        color  = ui::Color::rgb(0x000000, 0x44 / 255.f);
        break;
    case 4:
        text = player ? player->getSelectedItem().getHoverName() : std::string();
        ox   = twidth / 2 - dc.textWidth(text);
        oy   = padV + 40;
        break;
    default: break;
    }
    if (ox == -1) ox = twidth - padH - dc.textWidth(text) * static_cast<int>(scale) - 40;
    if (oy == -1) oy = theight - padV - 10 - 10 * static_cast<int>(scale);
    if (!text.empty()) dc.text(text, ox, oy, color, scale, ui::Align::Left, shadow);

    if (gState.hideFrames > 0) {
        // A shot is being taken: no rulers / settings, then grab this frame.
        if (gState.queueShot) {
            gState.queueShot = false;
            shoot();
        }
        --gState.hideFrames;
        return;
    }

    switch (gState.rulers) {
    case 1:
        vruler(dc, padH, padV, std::floor(width / 3), height);
        vruler(dc, padH, padV, std::floor(width / 3) * 2, height);
        hruler(dc, padH, padV, std::floor(height / 3), width);
        hruler(dc, padH, padV, std::floor(height / 3) * 2, width);
        break;
    case 2: {
        double p1 = 1 / 2.61, p2 = 1.61 / 2.61;
        vruler(dc, padH, padV, std::floor(static_cast<float>(width * p1)), height);
        vruler(dc, padH, padV, std::floor(static_cast<float>(width * p2)), height);
        hruler(dc, padH, padV, std::floor(static_cast<float>(height * p1)), width);
        hruler(dc, padH, padV, std::floor(static_cast<float>(height * p2)), width);
        break;
    }
    case 3:
        vruler(dc, padH, padV, std::floor(width / 2), height);
        hruler(dc, padH, padV, std::floor(height / 2), width);
        break;
    default: break;
    }

    float     left = 30, top = theight - 65;
    ui::Color gold = ui::Color::rgb(0xffaa00);
    auto line = [&](int n, std::string const& label, std::string const& value, float y) {
        std::string prefix = "[" + std::to_string(n) + "] ";
        dc.text(prefix, left, y);
        float x = left + dc.textWidth(prefix);
        dc.text(label, x, y);
        if (!value.empty()) dc.text(value, x + dc.textWidth(label), y, gold);
    };
    line(1, ctr("quark.camera.filter", "Filter: "), ctr(std::string("quark.camera.filter.") + filter::name(gState.shader), filter::name(gState.shader)), top);
    line(2, ctr("quark.camera.rulers", "Rulers: "), ctr("quark.camera.rulers" + std::to_string(gState.rulers), ""), top + 12);
    line(3, ctr("quark.camera.borders", "Borders: "), ctr("quark.camera.borders" + std::to_string(gState.borders), ""), top + 24);
    line(4, ctr("quark.camera.overlay", "Overlay: "), ctr("quark.camera.overlay" + std::to_string(gState.overlay), ""), top + 36);
    line(5, ctr("quark.camera.reset", "Reset Settings (Must Sneak)"), "", top + 48);
    auto header = ctr("quark.camera.header", "Camera Mode");
    dc.text(header, twidth / 2, 6, ui::Color::rgb(0x55ffff), 1, ui::Align::Center);
    auto keys = ui::keysOf("camera_mode");
    auto info = ctr("quark.camera.info", "[{} to exit, ENTER to shoot]", keyName(keys.empty() ? 0 : keys[0]));
    dc.text(info, twidth / 2, 16, ui::colors::white, 1, ui::Align::Center);
    drawCameraIcon(dc, left - 22, top + 18);
}

LP_REGISTER_HUD(renderHud);

LP_REGISTER_KEYBIND("camera_mode", 0x7B /* F12 */, [] {
    if (!cos::config().enableCamera || !client::inLevel()) return;
    setCameraMode(!gState.cameraMode);
});

// ---- keys -----------------------------------------------------------------------------------------

void onKey(ll::event::KeyInputEvent& ev) {
    if (!gState.cameraMode || !ev.isDown() || ui::topScreen() != "hud_screen") return;
    auto* player = client::currentPlayer();
    if (!player) return;
    bool sneak    = player->isSneaking();
    bool affected = false;
    switch (ev.keyCode()) {
    case '1': gState.shader = cycle(gState.shader, filter::kCount, sneak), affected = true; break;
    case '2': gState.rulers = cycle(gState.rulers, kRulers, sneak), affected = true; break;
    case '3': gState.borders = cycle(gState.borders, kBorders, sneak), affected = true; break;
    case '4': gState.overlay = cycle(gState.overlay, kOverlays, sneak), affected = true; break;
    case '5':
        if (sneak) {
            gState.shader = gState.rulers = gState.borders = gState.overlay = 0;
            affected = true;
        }
        break;
    case 0x0D: // Enter
        if (gState.hideFrames > 0 && sneak) {
            gState.hideFrames = 0;
            gState.queueShot  = false;
        } else if (gState.hideFrames == 0) {
            if (auto* c = client::currentClient()) c->playUI("lepton.camera.shutter", 1.f, 1.f);
            gState.queueShot  = true;
            gState.hideFrames = 10;
        }
        ev.cancel();
        return;
    default: return;
    }
    // The number keys would also select a hotbar slot (Quark restores the slot afterwards).
    ev.cancel();
    if (affected) {
        applyFilter();
        log().info("cosmetics: camera filter={} rulers={} borders={} overlay={}", filter::name(gState.shader), gState.rulers, gState.borders, gState.overlay);
    }
}

/// The vanilla HUD (hotbar, chat, ...) is not drawn in camera mode; Lepton's HUD hooks still run.
void onBeforeUi(ll::event::BeforeUIRenderEvent& ev) {
    if (!gState.cameraMode) return;
    auto const& tree = ev.screenView().mVisualTree.get();
    if (!tree) return;
    std::string const& name = tree->mRootControlName.get();
    if (name.size() >= 10 && name.compare(name.size() - 10, 10, "hud_screen") == 0) ev.cancel();
}

class Camera {
public:
    void install() {
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>(onKey);
        mUiListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::BeforeUIRenderEvent>(onBeforeUi);
        // The present hooks go in at game start, without suspending the other threads (patching
        // them later with suspended threads stalled the game in the client e2e). They do no work
        // until camera mode sets a filter or asks for a shot.
        if (cos::config().enableCamera) filter::ensureInstalled();
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mUiListener);
        ll::event::EventBus::getInstance().removeListener(mListener);
        setCameraMode(false);
        filter::shutdown();
    }

private:
    ll::event::ListenerPtr mListener;
    ll::event::ListenerPtr mUiListener;
};

} // namespace

} // namespace lp::client::cosmetics

LP_REGISTER_MODULE(lp::client::cosmetics::Camera);

#endif // LP_CLIENT
