// U16 hud, UsageTicker (client build): counts of the held items / ammo / armour above the hotbar.

#ifdef LP_CLIENT

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/hud/Painter.h"
#include "client/ui/Ui.h"
#include "feature/hud/HudConfig.h"
#include "feature/hud/Logic.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::hud {

using namespace lp::feature::hud;

namespace {

struct Element {
    TickerSlot slot;
    TickerAnim anim;
    ItemStack  currStack;
    ItemStack  currReal;
    int        currCount = 0;
};

std::vector<Element>                  gElements;
std::chrono::steady_clock::time_point gLastTick = std::chrono::steady_clock::now();
bool                                  gLogged   = false;

std::vector<TickerSlot> wantedSlots() {
    auto const&             c = cfg();
    std::vector<TickerSlot> s;
    if (c.enableMainHand) s.push_back(TickerSlot::MainHand);
    if (c.enableOffHand) s.push_back(TickerSlot::OffHand);
    if (c.enableArmor)
        for (auto a : {TickerSlot::Head, TickerSlot::Chest, TickerSlot::Legs, TickerSlot::Feet}) s.push_back(a);
    return s;
}

void syncElements() {
    auto want = wantedSlots();
    bool same = want.size() == gElements.size();
    for (size_t i = 0; same && i < want.size(); ++i) same = gElements[i].slot == want[i];
    if (same) return;
    gElements.clear();
    for (auto s : want) gElements.push_back(Element{s});
}

void onTick() {
    gLastTick = std::chrono::steady_clock::now();
    if (!cfg().enableUsageTicker) {
        gElements.clear();
        return;
    }
    syncElements();
    auto* player = client::currentPlayer();
    if (!player) return;
    for (auto& e : gElements) {
        ItemStack real      = tickerSource(*player, e.slot);
        int       count     = tickerCount(*player, real);
        ItemStack displayed = logicalStack(*player, e.slot, real, count);
        bool      changed   = tickerChanged(real, e.currReal, count, e.currCount)
                     || tickerChanged(displayed, e.currStack, count, e.currCount);
        bool wasVisible = e.anim.visible();
        e.anim.tick(game::isEmpty(displayed), changed);
        if (!wasVisible && e.anim.visible() && !gLogged) {
            gLogged = true;
            log().info("hud: usage ticker shows {}", game::itemId(displayed));
        }
        e.currCount = count;
        e.currStack = std::move(displayed);
        e.currReal  = std::move(real);
    }
}

void draw(ui::DrawContext& dc) {
    if (!cfg().enableUsageTicker || gElements.empty()) return;
    auto* c      = client::currentClient();
    auto* player = client::currentPlayer();
    if (!c || !player || c->getOptions().getHideHud()) return;
    float partial = std::chrono::duration<float>(std::chrono::steady_clock::now() - gLastTick).count() * 20.f;
    partial       = std::clamp(partial, 0.f, 1.f);
    auto  size    = dc.screenSize();
    bool  offEmpty = game::isEmpty(player->getOffhandSlot());
    auto const& conf = cfg();
    for (auto const& e : gElements) {
        if (!e.anim.visible()) continue;
        auto shown = tickerStack(*player, e.slot);
        if (game::isEmpty(shown.stack)) continue;
        float x = std::floor(tickerX(size.x, e.slot, conf.invert, offEmpty, conf.shiftLeft, conf.shiftRight));
        float y = std::floor(size.y - e.anim.offset(partial));
        dc.item(shown.stack, x, y, 1, false);
        int   w = 0;
        Color col;
        if (durabilityBar(shown.stack, w, col)) {
            dc.fill({x + 2, y + 13, 13, 2}, ui::colors::black);
            dc.fill({x + 2, y + 13, static_cast<float>(w), 1}, col);
        }
        if (shown.count > 1) {
            auto n = std::to_string(shown.count);
            dc.text(n, x + 17, y + 9, ui::colors::white, 1, ui::Align::Right);
        }
    }
}

} // namespace

class UsageTickerModule {
public:
    void install() {
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>(
            [](ll::event::ClientLevelTickEvent&) {
                try {
                    onTick();
                } catch (std::exception const& e) {
                    log().error("hud: usage ticker tick failed: {}", e.what());
                }
            }
        );
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        gElements.clear();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace lp::client::hud

LP_REGISTER_MODULE(lp::client::hud::UsageTickerModule);
LP_REGISTER_HUD([](lp::client::ui::DrawContext& dc) { lp::client::hud::draw(dc); });

#endif // LP_CLIENT
