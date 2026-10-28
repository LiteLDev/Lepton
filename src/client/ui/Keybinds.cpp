// Keybinds (client build): LeviLamina KeyRegistry entries "lepton.<name>" (shown in the game's
// Controls settings as `key.lepton.<name>`; the game saves remaps in its own options), with the
// default keys taken from config/keybinds.json (written with the registration defaults).

#include <map>
#include <string>
#include <vector>

#include "mod/Config.h"

namespace lp::client::ui {

/// `config/keybinds.json`: default key codes per keybind name (Windows virtual-key codes; an
/// empty list = unbound). Read at game start.
struct KeybindConfig {
    int                                     version = 1;
    std::map<std::string, std::vector<int>> keys;
};

} // namespace lp::client::ui

#ifdef LP_CLIENT

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/input/KeyRegistry.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("keybinds", lp::client::ui::KeybindConfig);

namespace lp::client::ui {

namespace {
std::map<std::string, std::vector<int>>& bound() {
    static std::map<std::string, std::vector<int>> m;
    return m;
}

Keybind const* find(std::string const& name) {
    for (auto const& k : KeybindRegistry::entries())
        if (k.name == name) return &k;
    return nullptr;
}

void fire(Keybind const& k, bool down) {
    try {
        if (down && k.onDown) k.onDown();
        if (!down && k.onUp) k.onUp();
    } catch (std::exception const& e) {
        log().error("keybind {} failed: {}", k.name, e.what());
    }
}
} // namespace

std::vector<int> keysOf(std::string const& name) {
    auto it = bound().find(name);
    return it == bound().end() ? std::vector<int>{} : it->second;
}

bool pressKeybind(std::string const& name) {
    auto const* k = find(name);
    if (!k) return false;
    log().info("keybind {} pressed", name);
    fire(*k, true);
    fire(*k, false);
    return true;
}

class KeybindModule {
public:
    /// At mod load: the game builds its key mappings from the registry when it starts.
    void load() {
        auto& cfg     = config::mut<KeybindConfig>();
        bool  changed = false;
        for (auto const& k : KeybindRegistry::entries()) {
            auto it = cfg.keys.find(k.name);
            if (it == cfg.keys.end()) {
                it      = cfg.keys.emplace(k.name, k.defaultKey ? std::vector<int>{k.defaultKey} : std::vector<int>{}).first;
                changed = true;
            }
            bound()[k.name] = it->second;
            auto& handle    = ll::input::KeyRegistry::getInstance().getOrCreateKey("lepton." + k.name, it->second, true);
            auto  name      = k.name;
            handle.registerButtonDownHandler([name](FocusImpact, IClientInstance&) {
                if (auto const* kb = find(name)) {
                    log().info("keybind {} pressed", name);
                    fire(*kb, true);
                }
            });
            handle.registerButtonUpHandler([name](FocusImpact, IClientInstance&) {
                if (auto const* kb = find(name)) fire(*kb, false);
            });
        }
        if (changed) config::saveAll();
    }

    void install() {
        // Keybinds with `inScreens`: the game's mapping only fires in gameplay, so screens are
        // served from the raw key events.
        mListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
            if (topScreen() == "hud_screen") return;
            for (auto const& k : KeybindRegistry::entries()) {
                if (!k.inScreens) continue;
                for (int code : keysOf(k.name))
                    if (code == ev.keyCode()) fire(k, ev.isDown());
            }
        });
    }
    void uninstall() { ll::event::EventBus::getInstance().removeListener(mListener); }

private:
    ll::event::ListenerPtr mListener;
};

} // namespace lp::client::ui

LP_REGISTER_MODULE_ORDERED(lp::client::ui::KeybindModule, lp::ModuleOrder::Services);

#endif // LP_CLIENT
