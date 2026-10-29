// U16 hud, AutoWalkKeybind (client build): a keybind toggles walking forward; the forward key
// stops it; auto-jump is on while it runs; an animated "Auto-Walking" banner on the HUD.

#ifdef LP_CLIENT

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/input/ClientMoveInputHandler.h"
#include "mc/client/input/KeyboardRemappingLayout.h"
#include "mc/client/input/Keymapping.h"
#include "mc/client/input/RemappingLayout.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/input/MoveInputState.h"
#include "mc/deps/core/math/Vec3.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "client/hud/Painter.h"
#include "feature/hud/HudConfig.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace lp::client::hud {

using lp::feature::hud::cfg;

namespace {

std::atomic<bool> gAutorun{false};
std::atomic<bool> gReleaseForward{false}; ///< clear the forward flag we set (on the client thread)
std::atomic<int>  gPressedKey{0};         ///< last key pressed in game (window thread -> client thread)

std::vector<int> forwardKeys() {
    auto* c = client::currentClient();
    if (!c) return {};
    try {
        auto layout = c->getOptions().getCurrentKeyboardRemapping();
        if (!layout) return {};
        return layout->getKeymappingByAction("key.forward").mKeys;
    } catch (...) {
        return {};
    }
}

void setForward(bool on) {
    auto* c = client::currentClient();
    if (!c) return;
    auto* move = ClientMoveInputHandler::getMoveInput(*c);
    if (!move) return;
    auto idx = static_cast<size_t>(MoveInputState::Flag::Up);
    move->mRawInputState.get().mFlagValues.get().set(idx, on);
}

void stop(char const* why) {
    if (!gAutorun.exchange(false)) return;
    gReleaseForward = true;
    log().info("hud: auto-walk off ({})", why);
}

} // namespace

/// True while auto-walking (tests).
bool autoWalking() { return gAutorun.load(); }

void toggleAutoWalk() {
    if (!cfg().enableAutoWalkKeybind || !client::currentPlayer()) return;
    if (gAutorun) {
        stop("keybind");
        return;
    }
    gAutorun = true;
    log().info("hud: auto-walk on");
}

namespace {

LL_TYPE_INSTANCE_HOOK(AutoJumpHook, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::$isAutoJumpEnabled, bool) {
    // Quark turns auto-jump on while auto-walking when the step height is below 1 (always on
    // Bedrock: 0.5625) and restores the option afterwards; reporting it as on is the same.
    if (gAutorun.load() && cfg().enableAutoWalkKeybind) return true;
    return origin();
}

bool gWasRunning = false;
Vec3 gStart;

void onTick() {
    auto* player = client::currentPlayer();
    if (!player || !cfg().enableAutoWalkKeybind) {
        if (gAutorun) stop(player ? "disabled" : "left the world");
    }
    // The forward key stops auto-walk (Quark: keyUp.isDown()). The game itself keeps moving the
    // player while the key is held, so the forced flag is not cleared here.
    if (int key = gPressedKey.exchange(0); key != 0 && gAutorun) {
        auto keys = forwardKeys();
        if (std::find(keys.begin(), keys.end(), key) != keys.end() && gAutorun.exchange(false))
            log().info("hud: auto-walk off (forward key)");
    }
    if (gReleaseForward.exchange(false)) setForward(false);
    bool running = gAutorun.load();
    if (player && running && !gWasRunning) gStart = player->getPosition();
    if (player && !running && gWasRunning) {
        auto  pos = player->getPosition();
        float dx = pos.x - gStart.x, dz = pos.z - gStart.z;
        log().info("hud: auto-walked {:.1f} blocks", std::sqrt(dx * dx + dz * dz));
    }
    gWasRunning = running && player;
    if (running) setForward(true);
}

void drawBanner(Painter& p) {
    if (!gAutorun || !cfg().drawHud || !cfg().enableAutoWalkKeybind) return;
    auto* c = client::currentClient();
    if (!c || c->getOptions().getHideHud()) return;
    auto        message = ctr("quark.misc.autowalking", "Auto-Walking");
    float       w       = p.textWidth("OoO" + message + "oOo");
    float       x       = std::floor((uiScreenSize().x - w) / 2);
    float       y       = static_cast<float>(cfg().hudHeight);
    auto        ms      = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
    )
                    .count();
    std::string shown = (ms / 500) % 2 == 0 ? "OoO " + message + " oOo" : "oOo " + message + " OoO";
    p.text(shown, x, y, ui::colors::white);
}

} // namespace

class AutoWalkModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](ll::event::ClientLevelTickEvent&) { onTick(); });
        // Window thread: only remember the key; onTick (client thread) checks the binding.
        mKey = bus.emplaceListener<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
            if (gAutorun && ev.isDown() && ui::topScreen() == "hud_screen") gPressedKey = ev.keyCode();
        });
        mPaint = bus.emplaceListener<ll::event::AfterUIRenderEvent>([](ll::event::AfterUIRenderEvent& ev) {
            if (!gAutorun || !isHudView(ev.screenView())) return;
            Painter p(ev.uiRenderContext());
            drawBanner(p);
        });
        AutoJumpHook::hook();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mKey);
        bus.removeListener(mPaint);
        AutoJumpHook::unhook();
        if (gAutorun.exchange(false)) setForward(false); // do not leave the forced forward flag behind
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mKey;
    ll::event::ListenerPtr mPaint;
};

} // namespace lp::client::hud

LP_REGISTER_MODULE(lp::client::hud::AutoWalkModule);
LP_REGISTER_KEYBIND("autorun", lp::client::ui::Key::Unbound, [] { lp::client::hud::toggleAutoWalk(); });

#endif // LP_CLIENT
