// U26 mobtweaks, client part: tooltip lines for GoldToolsHaveFortune (baked enchantments) and
// DiamondRepair (Quark's JEI repair hints). Spec: docs/spec/mobtweaks.md §4.

#ifdef LP_CLIENT

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

namespace lp::client::mobtweaks {

namespace mt = lp::feature::mobtweaks;

namespace {

int realLevel(ItemStackBase const& stack, int type) {
    if (!stack.mUserData) return 0;
    for (auto const& e : stack.constructItemEnchantsFromUserData().getAllEnchants())
        if (static_cast<int>(e.mEnchantType) == type) return e.mLevel;
    return 0;
}

void bakedLines(ItemStackBase const& stack, std::vector<std::string>& lines) {
    auto const& c = mt::cfg();
    if (!c.enableGoldToolsHaveFortune || !c.displayBakedEnchantmentsInTooltip) return;
    auto const& all = mt::bakedEnchantments();
    auto        it  = all.find(game::itemId(stack));
    if (it == all.end()) return;
    for (auto const& [type, level] : it->second) {
        int real  = realLevel(stack, type);
        int baked = mt::effectiveBakedLevel(type, stack, real);
        if (baked <= real) continue; // the real enchantment line already shows the effective level
        auto name = EnchantUtils::getEnchantNameAndLevel(static_cast<Enchant::Type>(type), baked);
        lines.push_back(std::string("§7") + (c.italicTooltip ? "§o" : "") + name);
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
            return;
        }
    }
    auto const& changes = mt::repairChanges();
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
}

} // namespace

} // namespace lp::client::mobtweaks

LP_REGISTER_TOOLTIP([](ItemStackBase const& stack, std::vector<std::string>& lines) {
    if (lp::game::isEmpty(stack)) return;
    lp::client::mobtweaks::bakedLines(stack, lines);
    lp::client::mobtweaks::repairLines(stack, lines);
});

#endif // LP_CLIENT
