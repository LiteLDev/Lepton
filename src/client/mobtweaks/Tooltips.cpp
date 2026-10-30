// U26 mobtweaks, client part: tooltip lines for GoldToolsHaveFortune (baked enchantments) and
// DiamondRepair (Quark's JEI repair hints). Spec: docs/spec/mobtweaks.md §4.

#ifdef LP_CLIENT

#include <set>
#include <string>
#include <vector>

#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/mobtweaks/Items.h"
#include "feature/mobtweaks/MobTweaks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::client::mobtweaks {

namespace mt = lp::feature::mobtweaks;

namespace {

/// One log line per item and kind (the tooltip is rebuilt every frame), for the e2e scenario.
void logOnce(char const* kind, std::string const& id) {
    static std::set<std::string> seen;
    if (seen.insert(std::string(kind) + id).second) log().info("mobtweaks: tooltip {} {}", kind, id);
}

int realLevel(ItemStackBase const& stack, int type) {
    if (!stack.mUserData) return 0;
    for (auto const& e : stack.constructItemEnchantsFromUserData().getAllEnchants())
        if (static_cast<int>(e.mEnchantType) == type) return e.mLevel;
    return 0;
}

void bakedLines(ItemStackBase const& stack, std::vector<std::string>& lines) {
    auto const& c = mt::cfg();
    if (!c.enableGoldToolsHaveFortune || !c.displayBakedEnchantmentsInTooltip) return;
    auto const all = mt::bakedEnchantments();
    auto        it  = all.find(game::itemId(stack));
    if (it == all.end()) return;
    for (auto const& [type, level] : it->second) {
        int real  = realLevel(stack, type);
        int baked = mt::effectiveBakedLevel(type, stack, real);
        if (baked <= real) continue; // the real enchantment line already shows the effective level
        auto name = EnchantUtils::getEnchantNameAndLevel(static_cast<Enchant::Type>(type), baked);
        lines.push_back(std::string("§7") + (c.italicTooltip ? "§o" : "") + name);
        logOnce("baked", it->first);
    }
}

void repairLines(ItemStackBase const& stack, std::vector<std::string>& lines) {
    auto const& c = mt::cfg();
    if (!c.enableDiamondRepair || !c.enableJeiHints) return;
    auto id = game::itemId(stack);
    for (auto const& u : mt::unrepairableItems()) {
        if (u == id) {
            lines.push_back("§8" + ctr("quark.jei.hint.repair_item_removed",
                                       "This item has been changed to not be repairable in an Anvil using repair materials."));
            logOnce("repair", id);
            return;
        }
    }
    auto const changes = mt::repairChanges();
    auto        it      = changes.find(id);
    if (it == changes.end()) return;
    std::string names;
    for (size_t i = 0; i < it->second.size(); ++i) {
        auto item = game::makeItem(it->second[i]);
        if (i) names += ", ";
        names += game::isEmpty(item) ? it->second[i] : item.getName();
    }
    if (it->second.size() == 1) {
        lines.push_back("§8" + ctr("quark.jei.hint.repair_item_changed",
                                   "This item's repair material in an Anvil has been changed to {}.", names));
    } else {
        lines.push_back("§8" + ctr("quark.jei.hint.repair_item_changed_multiple",
                                   "This item's repair material in an Anvil has been changed to one of {}.", names));
    }
    logOnce("repair", id);
}

} // namespace

} // namespace lp::client::mobtweaks

// e2e aid: the screen positions of the crafting and anvil slots (once per opening), so the client
// scenario can drive the elytra duplication and an anvil rename with real clicks.
LP_REGISTER_SCREEN_OVERLAY("*", [](lp::client::ui::Overlay&, lp::client::ui::ScreenInfo const& s) {
    static bool logged = false;
    if (s.frames <= 1) logged = false;
    if (!s.settled || logged) return;
    if (s.name.find("crafting") == std::string::npos && s.name.find("anvil") == std::string::npos) return;
    logged = true;
    for (auto const& slot : s.slots) {
        if (slot.collection == "inventory_items") continue;
        lp::log().info("mobtweaks: slot {} {}:{} at {:.4f},{:.4f}", s.name, slot.collection, slot.index,
                       (slot.rect.x + slot.rect.w / 2) / s.size.x, (slot.rect.y + slot.rect.h / 2) / s.size.y);
    }
    lp::log().info("mobtweaks: screen {} unit {:.5f},{:.5f}", s.name, 1.0f / s.size.x, 1.0f / s.size.y);
});

LP_REGISTER_TOOLTIP([](ItemStackBase const& stack, std::vector<std::string>& lines) {
    if (lp::game::isEmpty(stack)) return;
    lp::client::mobtweaks::bakedLines(stack, lines);
    lp::client::mobtweaks::repairLines(stack, lines);
});

#endif // LP_CLIENT
