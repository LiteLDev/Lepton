// U23 gadgets, client mod part (client build): the abacus count and the pathfinder's quill HUD, the
// tiny potato name label, tooltips, and the compass / clock needles of CompassesWorkEverywhere.
// See docs/spec/gadgets.md.

#ifdef LP_CLIENT

#include <chrono>
#include <cmath>
#include <mutex>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/item/AbstractCompassItem.h"
#include "mc/world/item/ClockItem.h"
#include "mc/world/item/CompassSpriteCalculator.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/gadgets/Gadgets.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace lp::client::gadgets {

using namespace lp::feature::gadgets;
namespace ui = lp::client::ui;

namespace {

struct State {
    int         abacus = -1;
    bool        searching = false;
    std::string potatoName;
    std::string potatoExtra;
};
State gState; // client thread only

long long clientTicks() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() / 50;
}

// ---- HUD ---------------------------------------------------------------------------------------------------

void drawAbacus(ui::DrawContext& dc) {
    if (gState.abacus < 0 || !cfg().enableAbacus) return;
    auto  size = dc.screenSize();
    float x    = size.x / 2 + 10;
    float y    = size.y / 2 - 7;
    dc.item(game::makeItem(kAbacus), x, y, 1, false);
    std::string text = gState.abacus < kAbacusMax ? std::to_string(gState.abacus + 1) : std::to_string(kAbacusMax) + "+";
    dc.text(text, x + 17, y + 5, ui::colors::white);
}

void drawQuill(ui::DrawContext& dc) {
    auto const& c = cfg();
    if (!gState.searching || !c.drawHud || !c.enablePathfinderMaps) return;
    auto        size  = dc.screenSize();
    auto        ticks = clientTicks();
    std::string text  = ctr("quark.misc.quill_searching", "Searching");
    for (int i = 0; i < (ticks / 10) % 4; ++i) text += '.';
    float x = 5;
    float y = c.hudOnTop ? 20 : size.y - 15;
    dc.text(text, x, y, ui::colors::white);
    double t  = static_cast<double>(ticks) * 0.1;
    float  ix = x + static_cast<float>((std::sin(t) + 1) * 20);
    float  iy = y - 15;
    if (std::cos(t) >= 0) iy += static_cast<float>(std::sin(t * 8) - 1);
    dc.item(game::makeItem(kQuill), ix, iy - 1, 1, false);
}

void drawPotatoName(ui::DrawContext& dc) {
    if (gState.potatoName.empty() || !cfg().enableTinyPotato) return;
    auto size = dc.screenSize();
    dc.text(gState.potatoName, size.x / 2, size.y / 2 + 10, ui::colors::white, 1, ui::Align::Center);
    if (!gState.potatoExtra.empty())
        dc.text(gState.potatoExtra, size.x / 2, size.y / 2 + 20, ui::colors::gray, 0.75f, ui::Align::Center);
}

// ---- compasses and clocks ------------------------------------------------------------------------------

bool hasFlag(ItemStack const* s, char const* key) { return s && getBool(*s, key); }

LL_TYPE_INSTANCE_HOOK(
    CompassFrameHook,
    ll::memory::HookPriority::Normal,
    AbstractCompassItem,
    &AbstractCompassItem::$getAnimationFrameFor,
    int,
    Mob*             holder,
    bool             a,
    ItemStack const* stack,
    bool             b
) {
    auto const& c = cfg();
    if (!c.enableCompassesWorkEverywhere || !stack || game::itemId(*stack) != "minecraft:compass")
        return origin(holder, a, stack, b);
    if (c.enableCompassNerf && !hasFlag(stack, compasses::kCompassCalculated)) return 0;
    if (!holder) return origin(holder, a, stack, b);
    int                     dim = static_cast<int>(holder->getDimensionId());
    std::optional<BlockPos> target;
    if (dim == 2 && c.enableEnd) target = BlockPos(0, 0, 0);
    else if (dim == 1 && c.enableNether && hasFlag(stack, compasses::kPosSet))
        target = BlockPos(getInt(*stack, compasses::kNetherX).value_or(0), 0, getInt(*stack, compasses::kNetherZ).value_or(0));
    if (!target) return origin(holder, a, stack, b);
    auto* getCalc = *mGetSpriteCalculator;
    if (!getCalc) return origin(holder, a, stack, b);
    auto& calc = getCalc(*holder);
    auto  pos  = holder->getPosition();
    return calc.updateFromPosition(&holder->getDimensionBlockSourceConst(), *target, pos.x, pos.z, holder->getRotation().y, false, false, false);
}

LL_TYPE_INSTANCE_HOOK(
    ClockFrameHook,
    ll::memory::HookPriority::Normal,
    ClockItem,
    &ClockItem::$getAnimationFrameFor,
    int,
    Mob*             holder,
    bool             a,
    ItemStack const* stack,
    bool const       b
) {
    auto const& c = cfg();
    if (c.enableCompassesWorkEverywhere && c.enableClockNerf && stack && !hasFlag(stack, compasses::kClockCalculated)) return 0;
    return origin(holder, a, stack, b);
}

struct GadgetsClientModule {
    std::vector<net::HandlerId> mHandlers;
    ll::event::ListenerPtr      mExit;

    void install() {
        mHandlers.push_back(net::onClient<AbacusMessage>([](AbacusMessage const& m) { gState.abacus = m.count; }));
        mHandlers.push_back(net::onClient<QuillMessage>([](QuillMessage const& m) {
            gState.searching = m.searching;
            log().info("gadgets: client quill hud {}", m.searching ? "on" : "off");
        }));
        mHandlers.push_back(net::onClient<PotatoNameMessage>([](PotatoNameMessage const& m) {
            gState.potatoName  = m.name;
            gState.potatoExtra = m.extra;
        }));
        CompassFrameHook::hook();
        ClockFrameHook::hook();
        // the server only sends changes: start clean in every world
        mExit = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { gState = {}; });
    }
    void uninstall() {
        for (auto id : mHandlers) net::removeHandler(id);
        mHandlers.clear();
        CompassFrameHook::unhook();
        ClockFrameHook::unhook();
        if (mExit) ll::event::EventBus::getInstance().removeListener(mExit);
        mExit.reset();
        gState = {};
    }
};

} // namespace

LP_REGISTER_HUD([](ui::DrawContext& dc) {
    if (!net::serverHasMod()) return;
    drawAbacus(dc);
    drawQuill(dc);
    drawPotatoName(dc);
});

// Lore lines are written on stacks the server creates; stacks from the creative inventory have none.
LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
    auto id = game::itemId(item);
    if (id.empty() || !item.getCustomLore().empty()) return;
    if (id == kQuill) {
        lines.push_back("§7" + ctr("quark.misc.quill_blank", "Blank"));
        return;
    }
    std::string name = ambientNameOf(id);
    if (name.empty() && id == kEndermosh) name = "endermosh";
    if (!name.empty()) lines.push_back("§7" + ctr("item.quark.music_disc_" + name + ".desc", name));
});

LP_REGISTER_SELFTEST("gadgets.client_tooltips", [](selftest::Context& ctx) {
    auto disc = ui::tooltipFor(game::makeItem("lepton:music_disc_rain"));
    ctx.check(disc.find(ctr("item.quark.music_disc_rain.desc", "Rainy Mood")) != std::string::npos, "ambience discs show their description");
    auto mosh = ui::tooltipFor(game::makeItem(kEndermosh));
    ctx.check(mosh.find("Endermosh") != std::string::npos, "Endermosh shows its title");
    auto quill = ui::tooltipFor(game::makeItem(kQuill));
    ctx.check(quill.find(ctr("quark.misc.quill_blank", "Blank")) != std::string::npos, "a blank quill says Blank");
    ctx.info("tooltips: " + disc + " | " + mosh + " | " + quill);
});

} // namespace lp::client::gadgets

LP_REGISTER_MODULE(lp::client::gadgets::GadgetsClientModule);

#endif // LP_CLIENT
