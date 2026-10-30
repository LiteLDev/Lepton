#include "feature/inventory/Search.h"

#include <functional>
#include <memory>
#include <regex>

#include "mc/deps/shared_types/item/CreativeItemCategory.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/alchemy/Potion.h"

#include "feature/inventory/Common.h"
#include "game/Items.h"
#include "mod/I18n.h"

namespace lp::feature::inventory {

namespace {

std::string trim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string_view::npos) return {};
    size_t e = s.find_last_not_of(" \t");
    return std::string(s.substr(b, e - b + 1));
}

using Matcher = std::function<bool(std::string const&, std::string const&)>;

std::string creativeCategoryName(ItemStack const& s) {
    auto const* item = s.mItem.get();
    if (!item) return {};
    using C = SharedTypes::CreativeItemCategory;
    switch (item->mCreativeCategory) {
    case C::Construction: return tr("lp.inventory.tab.construction", "Construction");
    case C::Nature: return tr("lp.inventory.tab.nature", "Nature");
    case C::Equipment: return tr("lp.inventory.tab.equipment", "Equipment");
    case C::Items: return tr("lp.inventory.tab.items", "Items");
    default: return {};
    }
}

bool isPotion(std::string const& id) {
    return id == "minecraft:potion" || id == "minecraft:splash_potion" || id == "minecraft:lingering_potion"
        || id == "minecraft:arrow";
}

bool matchWith(ItemStack const& stack, std::string const& search, Matcher const& matcher, int depth);

bool matchStack(ItemStack const& stack, std::string search, int depth) {
    if (search.empty()) return true;
    if (game::isEmpty(stack)) return false;
    Matcher matcher = [](std::string const& hay, std::string const& needle) { return hay.find(needle) != std::string::npos; };
    if (search.size() >= 3 && search.front() == '"' && search.back() == '"') {
        search  = search.substr(1, search.size() - 2);
        matcher = [](std::string const& hay, std::string const& needle) { return hay == needle; };
    }
    if (search.size() >= 3 && search.front() == '/' && search.back() == '/') {
        search  = search.substr(1, search.size() - 2);
        // Compiled once per search text (per thread: the client renders, the server sorts).
        thread_local std::string                 lastText;
        thread_local std::shared_ptr<std::regex> lastRe;
        if (!lastRe || lastText != search) {
            lastText = search;
            try {
                lastRe = std::make_shared<std::regex>(search);
            } catch (std::regex_error const&) {
                lastRe = nullptr;
            }
        }
        if (!lastRe) return false; // a bad expression matches nothing
        matcher = [re = lastRe](std::string const& hay, std::string const&) {
            try {
                return std::regex_search(hay, *re);
            } catch (std::regex_error const&) {
                return false; // too complex for this text
            }
        };
    }
    return matchWith(stack, search, matcher, depth);
}

bool matchWith(ItemStack const& stack, std::string const& search, Matcher const& matcher, int depth) {
    // Shulker boxes match by their contents too (the original search text, as Quark recurses).
    if (isShulkerBox(stack) && depth < 4)
        for (auto const& inner : shulkerContents(stack))
            if (!game::isEmpty(inner) && matchWith(inner, search, matcher, depth + 1)) return true;

    // Enchantments (and stored enchantments of books).
    for (auto const& [id, level] : enchantments(stack))
        if (matcher(normalizeSearch(enchantDisplay(id, level)), search)) return true;

    // Potion effects.
    auto id = game::itemId(stack);
    if (isPotion(id)) {
        int aux = stack.getAuxValue();
        if (id == "minecraft:arrow") --aux; // tipped arrows: aux = potion id + 1 (0 = plain arrow)
        if (aux >= 0 && aux < 64)
            if (auto const& potion = Potion::mPotionsById()[static_cast<size_t>(aux)])
                for (auto const& e : *potion->mEffects)
                    if (matcher(normalizeSearch(e.getDisplayName()), search)) return true;
    }

    // Creative tab.
    if (auto tab = normalizeSearch(creativeCategoryName(stack)); !tab.empty() && matcher(tab, search)) return true;

    // Mod name.
    auto ns = id.substr(0, id.find(':'));
    if (ns == "minecraft" && matcher("minecraft", search)) return true;
    if (ns == "lepton" && (matcher("lepton", search) || matcher("quark", search))) return true;

    return matcher(normalizeSearch(displayName(stack)), search);
}

} // namespace

std::string normalizeSearch(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7) { // §
            i += 2;                                                                            // skip the code too
            continue;
        }
        out.push_back(c < 0x80 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c));
    }
    return trim(out);
}

bool namesMatch(ItemStack const& stack, std::string_view search) { return matchStack(stack, normalizeSearch(search), 0); }

} // namespace lp::feature::inventory
