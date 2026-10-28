// U20 mobsoverworld, client side: the Resilience effect on the HUD (Bedrock's effect list only
// knows vanilla effects) and the effect lines of the Potion of Stability tooltips.

#ifdef LP_CLIENT

#include <chrono>
#include <string>
#include <vector>

#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/mobsoverworld/Messages.h"
#include "game/Items.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::client::mobsoverworld {

namespace {

using Clock = std::chrono::steady_clock;
using feature::mobsoverworld::EffectMessage;

struct Effect {
    int               amplifier = 0;
    Clock::time_point end{};
};
Effect gEffect;

int remainingTicks() {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(gEffect.end - Clock::now()).count();
    return left > 0 ? static_cast<int>(left / 50) : 0;
}

std::string formatTicks(int ticks) {
    int s = ticks / 20;
    return std::to_string(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string(s % 60);
}

std::string effectName(int amplifier) {
    std::string name = ctr("effect.quark.resilience", "Resilience");
    if (amplifier > 0) name += " " + ctr("lp.mobsoverworld.level_2", "II");
    return name;
}

void drawHud(ui::DrawContext& dc) {
    int ticks = remainingTicks();
    if (ticks <= 0 || !currentPlayer()) return;
    auto        size = dc.screenSize();
    std::string text = ctr("lp.mobsoverworld.effect_hud", "{0} {1}", effectName(gEffect.amplifier), formatTicks(ticks));
    float       w    = dc.textWidth(text) + 8;
    ui::Rect    r{size.x - w - 4, 4, w, 14};
    dc.fill(r, ui::colors::panel);
    dc.text(text, r.x + 4, r.y + 3, ui::Color::rgb(0x9a5a3c));
}

void tooltip(ItemStackBase const& item, std::vector<std::string>& lines) {
    std::string id = game::itemId(item);
    if (id.find("resilience_") == std::string::npos || !id.starts_with("lepton:")) return;
    int  duration  = 3600;
    int  amplifier = 0;
    if (id.starts_with("lepton:long_")) duration = 9600;
    if (id.starts_with("lepton:strong_")) {
        duration  = 1800;
        amplifier = 1;
    }
    if (id.find("lingering") != std::string::npos) duration /= 4;
    lines.push_back("§9" + ctr("lp.mobsoverworld.potion_tooltip", "Resilience{0} ({1})",
                                amplifier > 0 ? " " + ctr("lp.mobsoverworld.level_2", "II") : std::string(),
                                formatTicks(duration)));
    lines.push_back("§9" + ctr("lp.mobsoverworld.potion_attr", "+{0} Knockback Resistance",
                                amplifier > 0 ? std::string("1") : std::string("0.5")));
}

} // namespace

LP_REGISTER_HUD(drawHud);
LP_REGISTER_TOOLTIP(tooltip);

class MobsOverworldClient {
public:
    void install() {
        mHandler = net::onClient<EffectMessage>([](EffectMessage const& m) {
            gEffect.amplifier = m.amplifier;
            gEffect.end       = Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(m.ticks) * 50);
        });
    }
    void uninstall() { net::removeHandler(mHandler); }

private:
    net::HandlerId mHandler{};
};

} // namespace lp::client::mobsoverworld

LP_REGISTER_MODULE(lp::client::mobsoverworld::MobsOverworldClient);

#endif
