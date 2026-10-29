#ifdef LP_CLIENT

#include "client/ui/Ui.h"

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/gui/screens/models/ClientInstanceModel.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "core/Glob.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::ui {

namespace {

// ---- registries (filled at static init and in install()) ---------------------------------------

std::vector<HudFn>& huds() {
    static std::vector<HudFn> v;
    return v;
}
std::vector<std::pair<std::string, OverlayFn>>& overlays() {
    static std::vector<std::pair<std::string, OverlayFn>> v;
    return v;
}
std::vector<TooltipFn>& tooltips() {
    static std::vector<TooltipFn> v;
    return v;
}

// ---- input state (window thread writes, render thread reads) ------------------------------------

struct Region {
    Rect rect;
    int  widget = -1; ///< index into the frame's callbacks; -1 = text field (see fieldId)
    std::string fieldId;
};

struct InputEvent {
    enum class Kind { Click, CustomClick, CustomKey, FieldChar, FieldKey } kind;
    int         widget = -1;
    std::string fieldId;
    Vec         pos;
    int         button = 0;
    int         key    = 0;
    char        ch     = 0;
};

std::mutex                gInputMutex;
std::string               gTopScreen = "hud_screen"; ///< last screen rendered in the last frame
std::vector<Region>       gRegions;                  ///< regions of gTopScreen's overlays
std::deque<InputEvent>    gEvents;
std::string               gFocusedField;
Vec                       gMouse;
bool                      gCustomOnTop = false;
bool                      gShiftDown   = false;

// ---- render-thread state ------------------------------------------------------------------------

std::vector<std::function<void()>>          gCallbacks;     ///< callbacks of the last overlay frame
std::map<std::string, std::string>          gFields;        ///< text field contents by id
std::map<std::string, std::function<void(std::string const&)>> gFieldChange;
std::shared_ptr<CustomScreen>               gCustom;
bool                                        gCustomPushed = false;
int                                         gCustomSeenFrames = 0;
std::map<std::string, int>                  gSeenFrames; ///< frames rendered per screen name (tree dump after 20)

float invGuiScale() {
    auto* c = client::currentClient();
    if (!c) return 1.f;
    try {
        return c->getGuiData()->mInvGuiScale;
    } catch (...) {
        return 1.f;
    }
}

bool isCustomScreenName(std::string const& name) { return name.find("custom_screen") != std::string::npos; }

char keyToChar(int key, bool shift) {
    if (key >= 'A' && key <= 'Z') return static_cast<char>(shift ? key : key - 'A' + 'a');
    if (key >= '0' && key <= '9') return static_cast<char>(key);
    if (key == 0x20) return ' ';
    if (key == 0xBD) return shift ? '_' : '-';
    if (key == 0xBE) return '.';
    return 0;
}

// ---- control tree helpers -----------------------------------------------------------------------

/// Read-only: never refresh the game's cached positions (doing it mid-render, before the layout
/// pass, leaves stale positions marked clean and breaks the game's own hit testing).
Vec positionOf(UIControl const& c) {
    if (!c.mCachedPositionDirty) {
        auto const& p = c.mCachedPosition.get();
        return {p.x, p.y};
    }
    Vec v;
    for (UIControl const* cur = &c; cur;) {
        auto const& rel = cur->mParentRelativePosition.get();
        v.x += rel.x;
        v.y += rel.y;
        auto parent = cur->mParent.lock();
        cur         = parent.get(); // the tree owns its controls; the pointer stays valid this frame
    }
    return v;
}

void collectSlots(UIControl& c, std::vector<Slot>& out, std::set<std::pair<std::string, int>>& seen, int depth) {
    if (depth > 64) return;
    auto const& size = c.mSize.get();
    if (size.x >= 8 && size.y >= 8 && size.x <= 40 && size.y <= 40) {
        c.findAllCollectionIndices([&](std::string const& name, int index) {
            if (name.find("item") == std::string::npos) return;
            if (!seen.insert({name, index}).second) return;
            auto p = positionOf(c);
            out.push_back(Slot{name, index, Rect{p.x, p.y, size.x, size.y}});
        });
    }
    for (auto const& child : c.mChildren.get())
        if (child) collectSlots(*child, out, seen, depth + 1);
}

void dumpControl(UIControl& c, int depth, std::string& out) {
    if (depth > 40) return;
    auto p    = positionOf(c);
    auto size = c.mSize.get();
    std::string idx;
    c.findAllCollectionIndices([&](std::string const& name, int index) { idx += " [" + name + ":" + std::to_string(index) + "]"; });
    out += std::string(static_cast<size_t>(depth) * 2, ' ') + c.mName.get() + " @" + std::to_string(static_cast<int>(p.x)) + ","
         + std::to_string(static_cast<int>(p.y)) + " " + std::to_string(static_cast<int>(size.x)) + "x"
         + std::to_string(static_cast<int>(size.y)) + idx + "\n";
    for (auto const& child : c.mChildren.get())
        if (child) dumpControl(*child, depth + 1, out);
}

std::string screenNameOf(ScreenView const& view) {
    auto const& tree = view.mVisualTree.get();
    return tree ? tree->mRootControlName.get() : std::string{};
}

} // namespace

// ---- DrawContext ----------------------------------------------------------------------------------

struct DrawContext::Impl {
    MinecraftUIRenderContext& rc;
    ScreenView&               view;
    FontHandle                font;
    bool                      textQueued = false;
};

Vec DrawContext::screenSize() const {
    auto const& s = mImpl.view.mSize.get();
    return {s.x, s.y};
}

void DrawContext::text(std::string_view s, float x, float y, Color color, float scale, Align align, bool shadow) {
    if (s.empty()) return;
    float         w = textWidth(s, scale) + 2;
    float         left = align == Align::Left ? x : align == Align::Center ? x - w / 2 : x - w;
    RectangleArea area{left, left + w, y, y + 10 * scale};
    auto          a  = align == Align::Left ? ::ui::TextAlignment::Left : align == Align::Center ? ::ui::TextAlignment::Center : ::ui::TextAlignment::Right;
    TextMeasureData  tmd{scale, 0.f, shadow, true, false, a};
    CaretMeasureData caret{-1, false};
    mImpl.rc.drawText(mImpl.font.getFont(), area, std::string(s), mce::Color(color.r, color.g, color.b, color.a), color.a, a, tmd, caret);
    mImpl.textQueued = true;
}

float DrawContext::textWidth(std::string_view s, float scale) const {
    return static_cast<float>(mImpl.rc.getLineLength(mImpl.font.getFont(), std::string(s), scale, false));
}

void DrawContext::fill(Rect const& r, Color color) {
    mImpl.rc.fillRectangle(RectangleArea{r.x, r.x + r.w, r.y, r.y + r.h}, mce::Color(color.r, color.g, color.b, color.a), color.a);
}

void DrawContext::outline(Rect const& r, Color color, int thickness) {
    mImpl.rc.drawRectangle(RectangleArea{r.x, r.x + r.w, r.y, r.y + r.h}, mce::Color(color.r, color.g, color.b, color.a), color.a, thickness);
}

void DrawContext::item(ItemStack const& stack, float x, float y, float scale, bool decorations) {
    if (stack.isNull()) return;
    auto* c = client::currentClient();
    if (!c) return;
    auto* renderer = c->getItemRenderer();
    if (!renderer) return;
    BaseActorRenderContext ctx(mImpl.rc.mScreenContext, mImpl.rc.mClient, c->getMinecraftGame());
    renderer->renderGuiItemNew(ctx, stack, 0, x, y, false, 1.f, 1.f, scale, 0);
    if (decorations && stack.mCount > 1) {
        auto n = std::to_string(static_cast<int>(stack.mCount));
        text(n, x + 17 * scale, y + 9 * scale, colors::white, scale, Align::Right);
    }
}

void DrawContext::flush() {
    if (!mImpl.textQueued) return;
    mImpl.rc.flushText(0.f, std::nullopt);
    mImpl.textQueued = false;
}

// ---- Overlay ---------------------------------------------------------------------------------------

struct Overlay::Impl {
    DrawContext&                        dc;
    std::vector<Region>&                regions;
    std::vector<std::function<void()>>& callbacks;
    Vec                                 mouse;
    std::string                         focused;
};

DrawContext& Overlay::draw() { return mImpl.dc; }

void Overlay::button(Rect const& r, std::string_view label, std::function<void()> onClick, std::string_view tooltip) {
    bool hover = r.contains(mImpl.mouse.x, mImpl.mouse.y);
    mImpl.dc.fill(r, hover ? Color{0.45f, 0.45f, 0.55f, 0.95f} : Color{0.25f, 0.25f, 0.25f, 0.95f});
    mImpl.dc.outline(r, hover ? colors::white : Color{0, 0, 0, 1});
    mImpl.dc.text(label, r.x + r.w / 2, r.y + (r.h - 8) / 2, hover ? colors::yellow : colors::white, 1, Align::Center);
    if (hover && !tooltip.empty()) {
        float w = mImpl.dc.textWidth(tooltip) + 6;
        Rect  t{mImpl.mouse.x + 8, mImpl.mouse.y - 14, w, 12};
        mImpl.dc.fill(t, colors::panel);
        mImpl.dc.text(tooltip, t.x + 3, t.y + 2);
    }
    mImpl.regions.push_back(Region{r, static_cast<int>(mImpl.callbacks.size()), {}});
    mImpl.callbacks.push_back(std::move(onClick));
}

std::string const& Overlay::textField(std::string const& id, Rect const& r, std::string_view placeholder,
                                      std::function<void(std::string const&)> onChange) {
    auto& value   = gFields[id];
    bool  focused = mImpl.focused == id;
    mImpl.dc.fill(r, Color{0, 0, 0, 0.85f});
    mImpl.dc.outline(r, focused ? colors::white : colors::gray);
    if (value.empty() && !focused) mImpl.dc.text(placeholder, r.x + 3, r.y + (r.h - 8) / 2, colors::gray);
    else mImpl.dc.text(value + (focused ? "_" : ""), r.x + 3, r.y + (r.h - 8) / 2);
    mImpl.regions.push_back(Region{r, -1, id});
    if (onChange) gFieldChange[id] = std::move(onChange);
    return value;
}

void Overlay::greyOut(Rect const& r, Color color) { mImpl.dc.fill(r, color); }

// ---- public functions ---------------------------------------------------------------------------

std::string topScreen() {
    std::lock_guard lock(gInputMutex);
    return gTopScreen;
}

void addHud(HudFn fn) { huds().push_back(std::move(fn)); }
void addScreenOverlay(std::string screenGlob, OverlayFn fn) { overlays().emplace_back(std::move(screenGlob), std::move(fn)); }
void addTooltip(TooltipFn fn) { tooltips().push_back(std::move(fn)); }

std::shared_ptr<CustomScreen> currentScreen() { return gCustom; }

bool openScreen(std::shared_ptr<CustomScreen> screen) {
    auto* c = client::currentClient();
    if (!c || !client::inLevel() || !screen) return false;
    try {
        auto controller = std::make_shared<ScreenController>(false);
        auto scene      = c->getSceneFactory().createUIScene(kCustomScreenName, controller);
        if (!scene) {
            log().error("ui: the scene {} could not be created (resource pack missing?)", kCustomScreenName);
            return false;
        }
        if (gCustom) gCustom->onClose();
        gCustom           = std::move(screen);
        gCustomPushed     = true;
        gCustomSeenFrames = 0;
        c->getClientSceneStack()->pushScreen(scene, false);
        log().info("ui: custom screen opened");
        return true;
    } catch (std::exception const& e) {
        log().error("ui: opening a custom screen failed: {}", e.what());
        return false;
    }
}

namespace {

void closeCustom(bool popScene) {
    if (!gCustom) return;
    auto screen = std::move(gCustom);
    gCustom     = nullptr;
    {
        std::lock_guard lock(gInputMutex);
        gCustomOnTop = false;
    }
    try {
        screen->onClose();
    } catch (std::exception const& e) {
        log().error("ui: custom screen onClose failed: {}", e.what());
    }
    if (popScene) {
        if (auto* c = client::currentClient()) c->getClientSceneStack()->schedulePopScreen(1);
    }
    log().info("ui: custom screen closed");
}

void processInput(std::deque<InputEvent> events) {
    for (auto& e : events) {
        try {
            switch (e.kind) {
            case InputEvent::Kind::Click:
                if (e.widget >= 0 && e.widget < static_cast<int>(gCallbacks.size()) && gCallbacks[e.widget]) gCallbacks[e.widget]();
                break;
            case InputEvent::Kind::CustomClick:
                if (gCustom) gCustom->onClick(e.pos, e.button);
                break;
            case InputEvent::Kind::CustomKey:
                // Escape is cancelled for the game (onKey below): Lepton pops the scene itself.
                if (gCustom && !gCustom->onKey(e.key) && e.key == Key::Escape) gCustom->close();
                break;
            case InputEvent::Kind::FieldChar: {
                auto& v = gFields[e.fieldId];
                if (v.size() < 64) v.push_back(e.ch);
                if (auto it = gFieldChange.find(e.fieldId); it != gFieldChange.end() && it->second) it->second(v);
                break;
            }
            case InputEvent::Kind::FieldKey: {
                auto& v = gFields[e.fieldId];
                if (e.key == 0x08 && !v.empty()) v.pop_back(); // backspace
                if (auto it = gFieldChange.find(e.fieldId); it != gFieldChange.end() && it->second) it->second(v);
                break;
            }
            }
        } catch (std::exception const& ex) {
            log().error("ui: input handler failed: {}", ex.what());
        }
    }
}

void onRender(ll::event::AfterUIRenderEvent& ev) {
    auto& view = ev.screenView();
    auto  name = screenNameOf(view);
    if (name.empty()) return;

    std::deque<InputEvent> events;
    Vec                    mouse;
    std::string            focused;
    {
        std::lock_guard lock(gInputMutex);
        events.swap(gEvents);
        mouse   = gMouse;
        focused = gFocusedField;
    }
    processInput(std::move(events));

    auto* c = client::currentClient();
    if (!c) return;
    DrawContext::Impl impl{ev.uiRenderContext(), view, c->getFontHandle()};
    DrawContext       dc(impl);

    bool                               isHud = name == "hud_screen";
    bool                               isCustom = isCustomScreenName(name);
    std::vector<Region>                regions;
    std::vector<std::function<void()>> callbacks;

    if (++gSeenFrames[name] == 20) { // after the layout settled
        std::string tree;
        if (auto const& t = view.mVisualTree.get(); t && t->mRootControl.get()) dumpControl(*t->mRootControl.get(), 0, tree);
        log().debug("ui: screen {} (top: {}) control tree:\n{}", name, c->getTopScreenName(), tree);
    }

    try {
        if (isHud) {
            for (auto const& fn : huds()) fn(dc);
        } else if (isCustom) {
            if (gCustom) {
                ++gCustomSeenFrames;
                gCustom->render(dc, mouse);
            }
        } else {
            ScreenInfo info{name, dc.screenSize(), {}};
            bool       slotsDone = false;
            for (auto const& [glob, fn] : overlays()) {
                if (!core::globMatch(glob, name)) continue;
                if (!slotsDone) {
                    std::set<std::pair<std::string, int>> seen;
                    if (auto const& t = view.mVisualTree.get(); t && t->mRootControl.get())
                        collectSlots(*t->mRootControl.get(), info.slots, seen, 0);
                    slotsDone = true;
                }
                Overlay::Impl oimpl{dc, regions, callbacks, mouse, focused};
                Overlay       o(oimpl);
                fn(o, info);
            }
        }
    } catch (std::exception const& e) {
        log().error("ui: draw hook for {} failed: {}", name, e.what());
    }
    dc.flush();

    // The custom screen object closed itself, or the game popped its scene (Esc).
    if (gCustom && gCustom->closeRequested()) closeCustom(true);
    else if (gCustom && isHud && gCustomSeenFrames > 0 && !isCustomScreenName(c->getClientSceneStack()->getScreenName()))
        closeCustom(false);

    // The HUD renders first in every frame, also below open screens: it only becomes the "top
    // screen" when no menu is shown, so input never sees a half-updated state.
    bool hudOnTop = isHud && !c->isShowingMenu();
    if (!isHud) gCallbacks = std::move(callbacks);
    std::lock_guard lock(gInputMutex);
    if (isHud && !hudOnTop) return;
    gTopScreen   = name;
    gCustomOnTop = isCustom && gCustom != nullptr;
    if (isHud) gRegions.clear();
    else gRegions = std::move(regions);
}

void onMouse(ll::event::MouseInputEvent& ev) {
    float inv = invGuiScale();
    Vec   pos{ev.x() * inv, ev.y() * inv};
    int   action = ev.actionButtonId();
    bool  down   = ev.buttonData() != 0;
    std::lock_guard lock(gInputMutex);
    gMouse = pos;
    if (action < 1 || action > 3) return; // move / wheel
    if (gTopScreen == "hud_screen" && !gCustomOnTop) return;
    if (gCustomOnTop) {
        if (down) gEvents.push_back(InputEvent{InputEvent::Kind::CustomClick, -1, {}, pos, action - 1});
        ev.cancel();
        return;
    }
    for (auto const& r : gRegions) {
        if (!r.rect.contains(pos.x, pos.y)) continue;
        if (down && action == 1) {
            if (r.widget >= 0) {
                gEvents.push_back(InputEvent{InputEvent::Kind::Click, r.widget});
                gFocusedField.clear();
            } else {
                gFocusedField = r.fieldId;
            }
        }
        ev.cancel();
        return;
    }
    if (down) gFocusedField.clear();
}

void onKey(ll::event::KeyInputEvent& ev) {
    int  key  = ev.keyCode();
    bool down = ev.isDown();
    std::lock_guard lock(gInputMutex);
    if (key == Key::Shift) gShiftDown = down;
    if (gCustomOnTop) {
        if (down) gEvents.push_back(InputEvent{InputEvent::Kind::CustomKey, -1, {}, {}, 0, key});
        ev.cancel();
        return;
    }
    if (gFocusedField.empty() || gTopScreen == "hud_screen") return;
    if (key == Key::Escape || key == Key::Enter) {
        if (down) gFocusedField.clear();
        ev.cancel();
        return;
    }
    if (down) {
        if (char ch = keyToChar(key, gShiftDown)) gEvents.push_back(InputEvent{InputEvent::Kind::FieldChar, -1, gFocusedField, {}, 0, key, ch});
        else gEvents.push_back(InputEvent{InputEvent::Kind::FieldKey, -1, gFocusedField, {}, 0, key});
    }
    if (key != Key::Shift) ev.cancel(); // typing must not trigger game keys (E closes the inventory)
}

} // namespace

// ---- tooltips -------------------------------------------------------------------------------------

namespace {
std::string withTooltips(ItemStackBase const& item, std::string text) {
    if (tooltips().empty() || item.isNull()) return text;
    std::vector<std::string> lines;
    for (auto const& fn : tooltips()) {
        try {
            fn(item, lines);
        } catch (std::exception const& e) {
            log().error("ui: tooltip hook failed: {}", e.what());
        }
    }
    for (auto const& l : lines) text += "\n" + l;
    return text;
}

LL_TYPE_INSTANCE_HOOK(
    TooltipHook,
    ll::memory::HookPriority::Normal,
    ClientInstanceModel,
    &ClientInstanceModel::$getFormattedHoverText,
    std::string,
    ItemStackBase const& item,
    bool const           showCategory
) {
    return withTooltips(item, origin(item, showCategory));
}
} // namespace

std::string tooltipFor(ItemStackBase const& item) {
    // The game builds the vanilla part in the screen model; outside screens only Lepton's lines.
    return withTooltips(item, item.getName());
}

// ---- module ---------------------------------------------------------------------------------------

class UiModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::AfterUIRenderEvent>(onRender));
        mListeners.push_back(bus.emplaceListener<ll::event::MouseInputEvent>(onMouse));
        // High priority: widgets see keys before keybind listeners (which skip cancelled events).
        mListeners.push_back(bus.emplaceListener<ll::event::KeyInputEvent>(onKey, ll::event::EventPriority::High));
        TooltipHook::hook();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        TooltipHook::unhook();
        gCustom = nullptr;
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::client::ui

LP_REGISTER_MODULE_ORDERED(lp::client::ui::UiModule, lp::ModuleOrder::Services);

#endif // LP_CLIENT
