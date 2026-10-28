#pragma once

// Client UI toolkit (client build only; include it from files wrapped in `#ifdef LP_CLIENT`).
//
// How it works: Bedrock's screens are JSON-UI scenes. Lepton does not edit them; it draws on top
// of them natively. LeviLamina's `AfterUIRenderEvent` runs after every ScreenView rendered a
// frame, with its `MinecraftUIRenderContext`; we identify the screen by the name of its visual
// tree root (`hud_screen`, `inventory_screen`, `small_chest_screen`, `large_chest_screen`, ...,
// see `ui::screenName`) and call the registered hooks with a `DrawContext` (text, rectangles,
// item icons in UI coordinates). Mouse and keyboard events (LeviLamina `MouseInputEvent` /
// `KeyInputEvent`) are routed to the widgets of the top screen first; a click on a widget is
// cancelled for the game. Custom screens (radial menus, grids) are pushed as an empty JSON-UI
// scene (`lepton.custom_screen` in the resource pack) that frees the mouse; the screen object
// draws everything itself. Keybinds use LeviLamina's KeyRegistry (listed and rebindable in the
// game's Controls settings) with defaults from `config/keybinds.json`. Tooltip lines are appended
// in the game's hover-text builder (`ClientInstanceModel::getFormattedHoverText`).
//
// Coordinates: UI units (the game's GUI-scaled pixels), origin top-left of the screen.
// Threads: every callback runs on the client (render) thread.
//
//     LP_REGISTER_HUD([](lp::client::ui::DrawContext& dc) { dc.text("Hello", 4, 4); });
//
//     LP_REGISTER_KEYBIND("emote_wheel", lp::client::ui::Key::B, [] { ui::openScreen(std::make_shared<EmoteWheel>()); });
//
//     LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
//         if (isFood(item)) lines.push_back("§7Hunger: 3");
//     });
//
//     LP_REGISTER_SCREEN_OVERLAY("*chest_screen", [](ui::Overlay& o, ui::ScreenInfo const& s) {
//         o.button({s.size.x / 2 + 90, s.size.y / 2 - 80, 20, 12}, "Sort", [] { net::sendToServer(Sort{}); });
//         for (auto const& slot : s.slots) if (slot.collection == "container_items" && slot.index == 0)
//             o.greyOut(slot.rect);
//     });

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mod/Registry.h"

class ItemStack;
class ItemStackBase;

namespace lp::client::ui {

struct Vec {
    float x = 0, y = 0;
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

/// RGBA, 0..1.
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    static constexpr Color rgb(int hex, float alpha = 1) {
        return {((hex >> 16) & 0xff) / 255.f, ((hex >> 8) & 0xff) / 255.f, (hex & 0xff) / 255.f, alpha};
    }
};

namespace colors {
inline constexpr Color white{1, 1, 1, 1};
inline constexpr Color black{0, 0, 0, 1};
inline constexpr Color shade{0, 0, 0, 0.5f};           ///< grey-out overlay
inline constexpr Color panel{0.1f, 0.1f, 0.1f, 0.75f}; ///< HUD panel background
inline constexpr Color highlight{1, 1, 1, 0.25f};
inline constexpr Color yellow = Color::rgb(0xffff55);
inline constexpr Color gray   = Color::rgb(0xaaaaaa);
} // namespace colors

enum class Align { Left, Center, Right };

/// Drawing on the current screen for one frame. Only valid inside a hook call.
class DrawContext {
public:
    struct Impl;
    explicit DrawContext(Impl& impl) : mImpl(impl) {}

    /// Size of the screen in UI units.
    [[nodiscard]] Vec screenSize() const;
    /// Text with the game font (`§` colour codes work). `scale` 1 = normal size (8 units high).
    void text(std::string_view s, float x, float y, Color color = colors::white, float scale = 1, Align align = Align::Left, bool shadow = true);
    /// Width of `s` in UI units at `scale`.
    [[nodiscard]] float textWidth(std::string_view s, float scale = 1) const;
    void fill(Rect const& r, Color color);
    void outline(Rect const& r, Color color, int thickness = 1);
    /// An item icon (16x16 at scale 1) with its count / durability like an inventory slot when
    /// `decorations` is true.
    void item(ItemStack const& stack, float x, float y, float scale = 1, bool decorations = true);
    /// Draws the queued text now (text is otherwise flushed at the end of the hook).
    void flush();

private:
    Impl& mImpl;
};

/// One item slot of a container screen (found in the screen's control tree).
struct Slot {
    std::string collection; ///< "container_items", "inventory_items", "hotbar_items", ...
    int         index = 0;
    Rect        rect;
};

struct ScreenInfo {
    std::string       name; ///< visual tree root name, e.g. "inventory_screen"
    Vec               size; ///< UI units
    std::vector<Slot> slots;
};

/// Name of the top screen ("hud_screen" when nothing is open).
[[nodiscard]] std::string topScreen();

// ---- HUD ----------------------------------------------------------------------------------------

using HudFn = std::function<void(DrawContext&)>;
/// Draws on the HUD (in game, no screen open or a screen that shows the HUD behind it).
void addHud(HudFn fn);

// ---- screen overlays ----------------------------------------------------------------------------

/// Widgets an overlay adds to a screen this frame (immediate mode: rebuilt every frame).
class Overlay {
public:
    struct Impl;
    explicit Overlay(Impl& impl) : mImpl(impl) {}

    [[nodiscard]] DrawContext& draw();
    /// A clickable button. `onClick` runs on the client thread after the click.
    void button(Rect const& r, std::string_view label, std::function<void()> onClick, std::string_view tooltip = {});
    /// A one-line text field. `id` keeps its text and focus across frames; returns the text.
    std::string const& textField(std::string const& id, Rect const& r, std::string_view placeholder = {},
                                 std::function<void(std::string const&)> onChange = {});
    /// Darkens a rectangle (e.g. a slot the player can't use).
    void greyOut(Rect const& r, Color color = colors::shade);
    /// The mouse position this frame (UI units), for custom hover effects.
    [[nodiscard]] Vec mouse() const;

private:
    Impl& mImpl;
};

using OverlayFn = std::function<void(Overlay&, ScreenInfo const&)>;
/// Calls `fn` every frame while a screen whose name matches `screenGlob` (`*`, `?`) is on top.
void addScreenOverlay(std::string screenGlob, OverlayFn fn);

// ---- tooltips -----------------------------------------------------------------------------------

using TooltipFn = std::function<void(ItemStackBase const&, std::vector<std::string>& lines)>;
/// Appends lines to item tooltips (every screen that shows hover text).
void addTooltip(TooltipFn fn);
/// The tooltip text the game would show for `item` (with Lepton's lines).
[[nodiscard]] std::string tooltipFor(ItemStackBase const& item);

// ---- keybinds -----------------------------------------------------------------------------------

/// Windows virtual-key codes, as used by Bedrock's keyboard mapping.
namespace Key {
inline constexpr int Tab = 0x09, Enter = 0x0D, Shift = 0x10, Ctrl = 0x11, Alt = 0x12, Escape = 0x1B, Space = 0x20;
inline constexpr int A = 'A', B = 'B', C = 'C', G = 'G', H = 'H', J = 'J', K = 'K', L = 'L', M = 'M', N = 'N', O = 'O',
                     P = 'P', R = 'R', U = 'U', V = 'V', X = 'X', Y = 'Y', Z = 'Z';
inline constexpr int F6 = 0x75, F7 = 0x76, F8 = 0x77, F9 = 0x78, F10 = 0x79;
inline constexpr int Unbound = 0;
} // namespace Key

struct Keybind {
    std::string           name;       ///< unique, e.g. "emote_wheel" (shown as key.lepton.<name>)
    int                   defaultKey; ///< `Key::...`; `Key::Unbound` = no default
    std::function<void()> onDown;
    std::function<void()> onUp;
    /// Also fires while a screen is open (default: only in game, like Quark's keybinds).
    bool inScreens = false;
};
using KeybindRegistry = StaticRegistry<Keybind>;
/// Key codes currently bound to `name` (from config/keybinds.json or the registration default).
[[nodiscard]] std::vector<int> keysOf(std::string const& name);
/// Simulates a press of a registered keybind (tests, scripted input).
bool pressKeybind(std::string const& name);

// ---- custom screens -----------------------------------------------------------------------------

/// A full screen drawn by Lepton (radial menus, grids). Open with `openScreen`; Esc closes it.
class CustomScreen {
public:
    virtual ~CustomScreen() = default;
    virtual void render(DrawContext& dc, Vec mouse) = 0;
    /// Mouse button pressed (0 left, 1 right, 2 middle) at `pos`.
    virtual void onClick(Vec pos, int button) { (void)pos, (void)button; }
    /// Key pressed (virtual-key code). Return true to consume it.
    virtual bool onKey(int key) { return (void)key, false; }
    virtual void onClose() {}
    /// Asks Lepton to close this screen after the frame.
    void close() { mCloseRequested = true; }
    [[nodiscard]] bool closeRequested() const { return mCloseRequested; }

private:
    bool mCloseRequested = false;
};
/// Pushes `screen` on top of the game's screen stack. False when not in a world.
bool openScreen(std::shared_ptr<CustomScreen> screen);
/// The open custom screen, if it is on top.
[[nodiscard]] std::shared_ptr<CustomScreen> currentScreen();
/// Name of the JSON-UI scene used for custom screens.
inline constexpr char const* kCustomScreenName = "lepton.custom_screen";

// ---- registration helpers -------------------------------------------------------------------

namespace detail {
struct StaticHook {
    template <class F>
    explicit StaticHook(F f) {
        f();
    }
};
} // namespace detail

} // namespace lp::client::ui

/// Static registration (any .cpp of the client build).
#define LP_REGISTER_HUD(...)                                                                                           \
    static ::lp::client::ui::detail::StaticHook LP_UNIQUE(lp_hud_)([] { ::lp::client::ui::addHud(__VA_ARGS__); })
#define LP_REGISTER_SCREEN_OVERLAY(glob, ...)                                                                          \
    static ::lp::client::ui::detail::StaticHook LP_UNIQUE(lp_overlay_)([] {                                          \
        ::lp::client::ui::addScreenOverlay(glob, __VA_ARGS__);                                                         \
    })
#define LP_REGISTER_TOOLTIP(...)                                                                                       \
    static ::lp::client::ui::detail::StaticHook LP_UNIQUE(lp_tooltip_)([] {                                          \
        ::lp::client::ui::addTooltip(__VA_ARGS__);                                                                     \
    })
/// `LP_REGISTER_KEYBIND("name", lp::client::ui::Key::X, onDown[, onUp])`.
#define LP_REGISTER_KEYBIND(name, key, ...)                                                                           \
    static ::lp::client::ui::detail::StaticHook LP_UNIQUE(lp_keybind_)([] {                                           \
        ::lp::client::ui::KeybindRegistry::add(::lp::client::ui::Keybind{name, key, __VA_ARGS__});                    \
    })
