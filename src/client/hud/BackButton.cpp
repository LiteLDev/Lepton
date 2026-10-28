// U16 hud, BackButtonKeybind (client build): mouse button 5 (or the `back` keybind) presses the
// open screen's Back / Done / Cancel action, i.e. the scene's Escape handling.

#ifdef LP_CLIENT

#include <atomic>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/deps/input/enums/ButtonState.h"
#include "mc/deps/input/enums/RawInputType.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/hud/HudConfig.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::hud {

using lp::feature::hud::cfg;

namespace {

std::atomic<bool> gPending{false};

/// Runs on the client (render) thread.
void pressBack() {
    auto* c = client::currentClient();
    if (!c) return;
    auto name = c->getTopScreenName();
    if (name.empty() || name == "hud_screen") return; // in game: no screen to go back from
    auto* scene = c->getClientSceneStack()->getTopScene();
    if (!scene) return;
    constexpr int kEscape = 0x1B;
    scene->handleRawInputEvent(kEscape, RawInputType::Keyboard, ButtonState::Down, false);
    scene->handleRawInputEvent(kEscape, RawInputType::Keyboard, ButtonState::Up, false);
    log().info("hud: back button on {}", name);
}

/// The configured mouse button as LeviLamina's MouseAction (X1 = 5, X2 = 6), 0 = none.
int mouseAction() {
    switch (cfg().backMouseButton) {
    case 4: return 5;
    case 5: return 6;
    default: return 0;
    }
}

} // namespace

/// Queues a Back press (any thread); it runs on the next rendered frame.
void requestBack() {
    if (cfg().enableBackButtonKeybind) gPending = true;
}

class BackButtonModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mMouse    = bus.emplaceListener<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
            int action = mouseAction();
            if (action == 0 || ev.actionButtonId() != action || ev.buttonData() == 0) return;
            requestBack();
        });
        mFrame = bus.emplaceListener<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent&) {
            if (!gPending.exchange(false)) return;
            try {
                pressBack();
            } catch (std::exception const& e) {
                log().error("hud: back button failed: {}", e.what());
            }
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mMouse);
        bus.removeListener(mFrame);
    }

private:
    ll::event::ListenerPtr mMouse;
    ll::event::ListenerPtr mFrame;
};

} // namespace lp::client::hud

LP_REGISTER_MODULE(lp::client::hud::BackButtonModule);
// Keyboard binding of Back (unbound by default; Quark's default is the mouse button above).
static ::lp::client::ui::detail::StaticHook lp_hud_back_keybind([] {
    ::lp::client::ui::Keybind k{"back", ::lp::client::ui::Key::Unbound, [] { lp::client::hud::requestBack(); }, {}};
    k.inScreens = true;
    ::lp::client::ui::KeybindRegistry::add(std::move(k));
});

#endif // LP_CLIENT
