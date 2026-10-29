// U16 hud: selftests of the client parts (client build, local worlds): tooltip lines and keybinds.

#ifdef LP_CLIENT

#include <algorithm>
#include <string>

#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/hud/HudConfig.h"
#include "game/Items.h"
#include "selftest/SelfTest.h"

namespace lp::client::hud {
std::string improvedTooltip(ItemStackBase const& item, std::string vanilla, int* components);
bool        autoWalking();
void        toggleAutoWalk();
} // namespace lp::client::hud

namespace {
using lp::selftest::Context;

bool has(std::string const& s, std::string_view part) { return s.find(part) != std::string::npos; }

LP_REGISTER_SELFTEST("hud.tooltip_lines", [](Context& ctx) {
    namespace hud = lp::client::hud;
    ctx.require(lp::client::currentPlayer() != nullptr, "a local player");
    auto const& conf = lp::feature::hud::cfg();
    int         n    = 0;

    // A sword: the attribute component replaces the vanilla modifier line.
    auto sword = hud::improvedTooltip(lp::game::makeItem("minecraft:diamond_sword"),
                                      "Diamond Sword\n\n\xC2\xA7" "9+7 Attack Damage", &n);
    if (conf.attributeTooltips) {
        ctx.check(n == 1, "the sword gets an attribute component");
        ctx.info("sword tooltip: " + sword);
    }

    // Food: shanks + the saturation tier line.
    auto beef = hud::improvedTooltip(lp::game::makeItem("minecraft:cooked_beef"), "Steak", &n);
    ctx.check(n == (conf.foodTooltips ? 1 : 0), "steak gets a food component");
    if (conf.showSaturation) ctx.check(beef.find('\n') != std::string::npos, "steak gets a saturation line");

    // A shulker box: a grid component; the vanilla content lines are removed.
    auto box = lp::game::itemFromSnbt(
        R"({Name:"minecraft:blue_shulker_box",Count:1b,Damage:0s,tag:{Items:[{Name:"minecraft:diamond",Count:5b,Damage:0s,Slot:0b}]}})");
    ctx.require(!lp::game::isEmpty(box), "shulker box created");
    auto boxText = hud::improvedTooltip(box, "Blue Shulker Box\n" + box.getName() + " x0\nDiamond x5", &n);
    if (conf.shulkerTooltips && !conf.shulkerBoxRequireShift) {
        ctx.check(n == 1, "the shulker box gets a preview component");
        ctx.check(!has(boxText, "Diamond x5"), "the vanilla content line is removed");
    }

    // Stone: nothing to add.
    auto stone = hud::improvedTooltip(lp::game::makeItem("minecraft:stone"), "Stone", &n);
    ctx.check(n == 0 && stone == "Stone", "stone keeps the vanilla tooltip");
});

LP_REGISTER_SELFTEST("hud.keybinds", [](Context& ctx) {
    namespace ui  = lp::client::ui;
    namespace hud = lp::client::hud;
    bool autorun = false, back = false;
    for (auto const& k : ui::KeybindRegistry::entries()) {
        autorun = autorun || (k.name == "autorun" && k.defaultKey == ui::Key::Unbound);
        back    = back || (k.name == "back" && k.inScreens);
    }
    ctx.check(autorun, "key.lepton.autorun is registered (unbound by default)");
    ctx.check(back, "key.lepton.back is registered and works in screens");
    if (!lp::feature::hud::cfg().enableAutoWalkKeybind) return;
    bool before = hud::autoWalking();
    hud::toggleAutoWalk();
    ctx.check(hud::autoWalking() != before, "the autorun keybind toggles auto-walk");
    hud::toggleAutoWalk();
    ctx.check(hud::autoWalking() == before, "pressing it again toggles it back");
});
} // namespace

#endif // LP_CLIENT
