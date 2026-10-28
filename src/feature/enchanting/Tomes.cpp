#include "feature/enchanting/Tomes.h"

#include <algorithm>
#include <random>

#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "feature/enchanting/Config.h"
#include "feature/enchanting/Enchants.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::enchanting {

namespace {
std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int randInt(int bound) { return bound <= 0 ? 0 : std::uniform_int_distribution<int>(0, bound - 1)(rng()); }

std::string tablePath(std::string id) {
    if (auto c = id.find(':'); c != std::string::npos) id = id.substr(c + 1);
    return id;
}

bool tableMatches(std::string const& dir, std::string const& id) {
    auto path = tablePath(id);
    if (path.empty()) return false;
    std::string suffix = "/" + path + ".json";
    if (dir.size() >= suffix.size() && dir.compare(dir.size() - suffix.size(), suffix.size(), suffix) == 0) return true;
    return dir == path || dir == path + ".json" || dir == "loot_tables/" + path + ".json";
}
} // namespace

// ---- tomes -------------------------------------------------------------------------------------

std::vector<int> validTomeEnchants() {
    std::vector<int> out;
    for (auto const& name : cfg().enchantNames) {
        auto const* d = defById(name);
        if (!d) continue; // unknown (e.g. Java-only sweeping_edge): skipped, like Quark logs and skips
        if (cfg().sanityCheck && d->maxLevel == 1) continue;
        if (std::find(out.begin(), out.end(), d->type) == out.end()) out.push_back(d->type);
    }
    return out;
}

ItemStack makeTome(int type) {
    auto stack = game::makeItem(kTomeId, 1);
    if (game::isEmpty(stack)) return stack;
    if (auto const* d = defOf(type)) setEnchants(stack, {{type, d->maxLevel}});
    return stack;
}

bool isTome(ItemStackBase const& stack) { return game::itemId(stack) == kTomeId; }

std::optional<int> tomeEnchant(ItemStackBase const& stack) {
    if (!isTome(stack)) return std::nullopt;
    auto e = enchantsOf(stack);
    if (e.empty()) return std::nullopt;
    return e.begin()->first;
}

std::optional<Craft> anvilCraft(ItemStack const& left, ItemStack const& right) {
    if (!cfg().enableAncientTomes) return std::nullopt;
    if (game::isEmpty(left) || game::isEmpty(right)) return std::nullopt;
    auto const lid         = game::itemId(left);
    bool       isLeftBook  = lid == "minecraft:enchanted_book";
    bool       isLeftTome  = isTome(left);
    bool       isRightBook = game::itemId(right) == "minecraft:enchanted_book";
    bool       isRightTome = isTome(right);
    bool       cannotCombineWithBook = !cfg().combineWithBooks && isLeftBook;
    if ((isLeftBook || lid != game::itemId(right)) && (!isRightBook && !isRightTome)) return std::nullopt;

    std::map<int, int> out;
    int                cost = 0;
    auto upgrade = [&](int type, int level) { out[type] = std::max(out[type], level); };

    // Tomes keep their enchantment apart (Quark's TOME_ENCHANTMENTS component).
    auto leftEnch       = isLeftTome ? std::map<int, int>{} : enchantsOf(left);
    bool leftEnchExists = !leftEnch.empty();
    bool leftOver = false, rightOver = false;
    for (auto const& [t, l] : leftEnch) {
        if (auto const* d = defOf(t); d && l > d->maxLevel) leftOver = true;
        upgrade(t, l);
        cost += l;
    }
    auto rightEnch = isRightTome ? std::map<int, int>{} : enchantsOf(right);
    for (auto const& [t, l] : rightEnch) {
        for (auto const& [held, hl] : out) {
            auto const* hd = defOf(held);
            if (hd && held != t && !hd->compatibleWith(t)) return std::nullopt;
        }
        if (auto const* d = defOf(t); d && l > d->maxLevel) rightOver = true;
        upgrade(t, l);
        cost += l;
    }

    std::string kind = "overlevel";
    if (isRightTome) {
        if (cannotCombineWithBook) return std::nullopt;
        auto te = tomeEnchant(right);
        if (!te) return std::nullopt;
        auto const* d   = defOf(*te);
        int         cur = out.contains(*te) ? out[*te] : 0;
        if (!d || !leftEnchExists || cur <= 0 || cur >= d->maxLevel + cfg().maxLimitBreakLevels) return std::nullopt;
        int newLevel    = cur + 1;
        int breakLevels = newLevel - d->maxLevel;
        out[*te]        = newLevel;
        cost = newLevel > d->maxLevel ? cfg().normalUpgradeCost + breakLevels * cfg().limitBreakUpgradeExtraCost : cfg().normalUpgradeCost;
        kind = "tome";
    } else if (!leftOver && !rightOver) {
        return std::nullopt;
    } else {
        cost += left.getBaseRepairCost() + right.getBaseRepairCost();
    }

    Craft c;
    c.kind     = kind;
    c.result   = left;
    c.result.set(1);
    setEnchants(c.result, out);
    c.cost     = cost;
    c.usesTome = isRightTome || isLeftTome;
    return c;
}

void addRandomCurse(ItemStack& stack) {
    std::vector<int> curses;
    for (auto const& d : registry())
        if (d.id == "minecraft:binding" || d.id == "minecraft:vanishing") curses.push_back(d.type);
    if (curses.empty()) return;
    int  curse = curses[static_cast<size_t>(randInt(static_cast<int>(curses.size())))];
    auto e     = enchantsOf(stack);
    e[curse]   = std::max(e[curse], 1);
    setEnchants(stack, e);
}

// ---- runes -------------------------------------------------------------------------------------

std::vector<std::string> const& runeColors() {
    static std::vector<std::string> const k = {"white", "orange", "magenta", "light_blue", "yellow", "lime",
                                               "pink",  "gray",   "light_gray", "cyan",    "purple", "blue",
                                               "brown", "green",  "red",     "black",      "rainbow", "blank"};
    return k;
}

std::string runeColorOf(ItemStack const& addition) {
    if (game::isEmpty(addition)) return "blank";
    auto id = game::itemId(addition);
    if (id == "minecraft:blaze_powder") return "rainbow";
    for (size_t i = 0; i < 16; ++i)
        if (id == "minecraft:" + runeColors()[i] + "_dye") return runeColors()[i];
    return {};
}

std::optional<std::string> appliedRune(ItemStackBase const& stack) {
    if (game::itemId(stack) == "minecraft:enchanted_book") {
        if (cfg().enableAncientTomes && cfg().overleveledBooksGlowRainbow && isOverlevel(stack)) return std::string("rainbow");
        return std::nullopt;
    }
    return stringTag(stack, kRuneTag);
}

bool canHaveRune(ItemStackBase const& stack) {
    if (game::isEmpty(stack)) return false;
    return !enchantsOf(stack).empty() || game::itemId(stack) == "minecraft:lodestone_compass";
}

std::optional<Craft> smithingCraft(ItemStack const& templ, ItemStack const& base, ItemStack const& addition) {
    if (!cfg().enableColorRunes) return std::nullopt;
    if (game::itemId(templ) != kRuneId || !canHaveRune(base)) return std::nullopt;
    auto color = runeColorOf(addition);
    if (color.empty()) return std::nullopt;
    if (appliedRune(base) == color) return std::nullopt;
    Craft c;
    c.kind   = "rune";
    c.result = base;
    c.result.set(1);
    setStringTag(c.result, kRuneTag, color);
    c.cost = 0;
    return c;
}

// ---- loot --------------------------------------------------------------------------------------

std::vector<LootExtra> lootExtrasFor(std::string const& dir) {
    std::vector<LootExtra> out;
    auto const&            c = cfg();
    if (c.enableAncientTomes && !validTomeEnchants().empty()) {
        for (auto const& entry : c.lootTables) {
            auto comma = entry.rfind(',');
            if (comma == std::string::npos) continue;
            int weight = 0;
            try {
                weight = std::stoi(entry.substr(comma + 1));
            } catch (...) {
                continue;
            }
            if (weight > 0 && tableMatches(dir, entry.substr(0, comma))) out.push_back({weight, "tome"});
        }
    }
    if (c.enableColorRunes) {
        std::pair<char const*, int> const runes[] = {
            {"chests/simple_dungeon",  c.dungeonWeight       },
            {"chests/nether_bridge",   c.netherFortressWeight},
            {"chests/jungle_temple",   c.jungleTempleWeight  },
            {"chests/desert_pyramid",  c.desertTempleWeight  },
        };
        for (auto const& [t, w] : runes)
            if (w > 0 && tableMatches(dir, t)) out.push_back({w, "rune"});
    }
    return out;
}

ItemStack makeLootItem(std::string const& what) {
    if (what == "rune") return game::makeItem(kRuneId, 1);
    auto valid = validTomeEnchants();
    if (valid.empty()) return {};
    return makeTome(valid[static_cast<size_t>(randInt(static_cast<int>(valid.size())))]);
}

} // namespace lp::feature::enchanting
