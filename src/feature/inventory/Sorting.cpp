#include "feature/inventory/Sorting.h"

#include <algorithm>
#include <array>
#include <functional>
#include <string>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/Container.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/components/IFoodItemComponent.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/alchemy/Potion.h"

#include "feature/inventory/Common.h"
#include "game/Items.h"

namespace lp::feature::inventory {

namespace {

bool endsWith(std::string const& s, std::string_view suffix) {
    return s.size() >= suffix.size() && std::string_view(s).substr(s.size() - suffix.size()) == suffix;
}

constexpr std::array<std::string_view, 4> kRails{"minecraft:rail", "minecraft:golden_rail", "minecraft:detector_rail",
                                                 "minecraft:activator_rail"};

int railIndex(std::string const& id) {
    for (size_t i = 0; i < kRails.size(); ++i)
        if (kRails[i] == id) return static_cast<int>(i);
    return -1;
}

/// Java tier speed x 100 (wood 2, stone 4, iron 6, diamond 8, netherite 9, gold 12).
int tierSpeed(ItemStack const& s) {
    if (itemHasTag(s, "minecraft:golden_tier")) return 1200;
    if (itemHasTag(s, "minecraft:netherite_tier")) return 900;
    if (itemHasTag(s, "minecraft:diamond_tier")) return 800;
    if (itemHasTag(s, "minecraft:iron_tier")) return 600;
    if (itemHasTag(s, "minecraft:stone_tier")) return 400;
    if (itemHasTag(s, "minecraft:wooden_tier")) return 200;
    return 0;
}

/// Java tier attack damage bonus x 100 (wood 0, gold 0, stone 1, iron 2, diamond 3, netherite 4).
int tierAttack(ItemStack const& s) {
    if (itemHasTag(s, "minecraft:netherite_tier")) return 400;
    if (itemHasTag(s, "minecraft:diamond_tier")) return 300;
    if (itemHasTag(s, "minecraft:iron_tier")) return 200;
    if (itemHasTag(s, "minecraft:stone_tier")) return 100;
    return 0;
}

IFoodItemComponent* foodOf(ItemStack const& s) {
    auto const* item = s.mItem.get();
    return item && item->isFood() ? item->getFood() : nullptr;
}

int nutrition(ItemStack const& s) {
    auto* f = foodOf(s);
    return f ? f->getNutrition() : 0;
}

float saturation(ItemStack const& s) {
    auto* f = foodOf(s);
    return f ? std::min(20.f, f->getNutrition() * f->getSaturationModifier() * 2.f) : 0.f;
}

int potionPower(ItemStack const& s) {
    int aux = s.getAuxValue();
    if (aux < 0 || aux >= 64) return 0;
    auto const& potion = Potion::mPotionsById()[static_cast<size_t>(aux)];
    if (!potion) return 0;
    int total = 0;
    for (auto const& e : *potion->mEffects) total += e.mAmplifier * e.mDuration->mValue;
    return total;
}

using Cmp = std::function<int(ItemStack const&, ItemStack const&)>;

int damageCompare(ItemStack const& a, ItemStack const& b) { return damageOf(a) - damageOf(b); }
int enchantCompare(ItemStack const& a, ItemStack const& b) { return enchantPower(b) - enchantPower(a); }

int componentCompare(ItemStack const& a, ItemStack const& b) {
    bool ha = hasExtraData(a), hb = hasExtraData(b);
    if (hb && !ha) return -1;
    if (ha && !hb) return 1;
    if (!ha) return 0;
    auto sa = a.mUserData->toSnbt(SnbtFormat::Minimize, 0);
    auto sb = b.mUserData->toSnbt(SnbtFormat::Minimize, 0);
    auto ia = static_cast<int>(std::hash<std::string>{}(sa) & 0x7fffffff);
    auto ib = static_cast<int>(std::hash<std::string>{}(sb) & 0x7fffffff);
    return ib < ia ? -1 : ib > ia ? 1 : 0;
}

int fallbackCompare(ItemStack const& a, ItemStack const& b) {
    if (a.getId() != b.getId()) return a.getId() < b.getId() ? -1 : 1;
    if (int d = damageCompare(a, b)) return d;
    if (int c = static_cast<int>(b.mCount) - static_cast<int>(a.mCount)) return c;
    return componentCompare(a, b);
}

int joint(ItemStack const& a, ItemStack const& b, std::initializer_list<Cmp> cmps) {
    for (auto const& c : cmps)
        if (int r = c(a, b)) return r;
    return fallbackCompare(a, b);
}

std::string enchantNames(ItemStack const& s) {
    std::string out;
    for (auto const& [id, lvl] : enchantments(s)) out += id;
    return out;
}

int enchantedBookCompare(ItemStack const& a, ItemStack const& b, bool levels) {
    auto ea = enchantments(a), eb = enchantments(b);
    if (int d = static_cast<int>(eb.size()) - static_cast<int>(ea.size())) return d;
    if (int d = enchantNames(b).compare(enchantNames(a))) return d < 0 ? -1 : 1;
    if (levels)
        for (size_t i = 0; i < ea.size() && i < eb.size(); ++i)
            if (int d = eb[i].second - ea[i].second) return d;
    return 0;
}

int compareWithin(Category cat, ItemStack const& a, ItemStack const& b) {
    switch (cat) {
    case Category::Pickaxe:
    case Category::Shovel:
    case Category::Axe:
    case Category::GenericTool:
        return joint(a, b, {[](auto& x, auto& y) { return tierSpeed(y) - tierSpeed(x); }, enchantCompare, damageCompare});
    case Category::Sword:
        return joint(a, b, {[](auto& x, auto& y) { return tierAttack(y) - tierAttack(x); }, enchantCompare, damageCompare});
    case Category::Armor:
        // Java EquipmentSlot index: feet 0 .. head 3, higher first => head, chest, legs, feet.
        return joint(a, b, {[](auto& x, auto& y) { return armorSlotFor(x) - armorSlotFor(y); }, enchantCompare, damageCompare});
    case Category::Bow:
    case Category::Crossbow:
    case Category::Trident:
        return joint(a, b, {enchantCompare, damageCompare});
    case Category::Potion:
        return joint(a, b, {[](auto& x, auto& y) { return potionPower(y) - potionPower(x); },
                            [](auto& x, auto& y) { return static_cast<int>(y.getAuxValue()) - x.getAuxValue(); }});
    case Category::Food:
        return joint(a, b, {[](auto& x, auto& y) { return nutrition(y) - nutrition(x); },
                            [](auto& x, auto& y) { float d = saturation(y) - saturation(x); return (d > 0) - (d < 0); }});
    case Category::EnchantedBook:
        return joint(a, b, {[](auto& x, auto& y) { return enchantedBookCompare(x, y, true); }});
    case Category::AncientTome:
        return joint(a, b, {[](auto& x, auto& y) { return enchantedBookCompare(x, y, false); }});
    case Category::Rail:
        return joint(a, b, {[](auto& x, auto& y) { return railIndex(game::itemId(x)) - railIndex(game::itemId(y)); }});
    case Category::Torch:
    default:
        return fallbackCompare(a, b);
    }
}

} // namespace

Category categoryOf(ItemStack const& s) {
    auto id = game::itemId(s);
    if (id == "minecraft:torch") return Category::Torch;
    if (itemHasTag(s, "minecraft:is_pickaxe")) return Category::Pickaxe;
    if (itemHasTag(s, "minecraft:is_shovel")) return Category::Shovel;
    if (itemHasTag(s, "minecraft:is_axe")) return Category::Axe;
    if (itemHasTag(s, "minecraft:is_sword")) return Category::Sword;
    if (itemHasTag(s, "minecraft:is_hoe")) return Category::GenericTool;
    if (isArmorItem(s)) return Category::Armor;
    if (id == "minecraft:bow") return Category::Bow;
    if (id == "minecraft:crossbow") return Category::Crossbow;
    if (id == "minecraft:trident") return Category::Trident;
    if (id == "minecraft:arrow" || id == "minecraft:spectral_arrow") return Category::Arrows;
    if (id == "minecraft:potion" || id == "minecraft:splash_potion" || id == "minecraft:lingering_potion")
        return Category::Potion;
    if (id.find("minecart") != std::string::npos) return Category::Minecart;
    if (railIndex(id) >= 0) return Category::Rail;
    if (endsWith(id, "_dye")) return Category::Dye;
    if (foodOf(s)) return Category::Food;
    if (id == "minecraft:enchanted_book" && !enchantments(s).empty()) return Category::EnchantedBook;
    if (id == "lepton:ancient_tome" && !enchantments(s).empty()) return Category::AncientTome;
    if (s.mBlock == nullptr) return Category::Any;
    return Category::Block;
}

int compareStacks(ItemStack const& a, ItemStack const& b) {
    bool ea = game::isEmpty(a), eb = game::isEmpty(b);
    if (ea && eb) return 0;
    if (ea) return -1;
    if (eb) return 1;
    auto ca = categoryOf(a), cb = categoryOf(b);
    if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
    return compareWithin(ca, a, b);
}

void sortRange(Container& c, int begin, int end, std::vector<int> const& locked) {
    begin = std::clamp(begin, 0, c.getContainerSize());
    end   = std::clamp(end, begin, c.getContainerSize());
    auto isLocked = [&](int slot) { return std::find(locked.begin(), locked.end(), slot) != locked.end(); };

    std::vector<ItemStack> stacks;
    for (int i = begin; i < end; ++i) {
        if (isLocked(i)) continue;
        auto const& s = c.getItem(i);
        if (!game::isEmpty(s)) stacks.push_back(s);
    }
    // Merge: each stack absorbs later equal stacks up to its max size (Quark mergeStacks).
    for (size_t i = 0; i < stacks.size(); ++i) {
        auto& target = stacks[i];
        if (game::isEmpty(target)) continue;
        int max = std::max<int>(1, std::min<int>(target.getMaxStackSize(), c.getMaxStackSize()));
        for (size_t j = 0; j < stacks.size() && target.mCount < max; ++j) {
            if (j == i || game::isEmpty(stacks[j]) || !game::canMerge(target, stacks[j])) continue;
            int n = std::min<int>(max - target.mCount, stacks[j].mCount);
            target.set(target.mCount + n);
            if (n >= stacks[j].mCount) stacks[j] = ItemStack{};
            else stacks[j].set(stacks[j].mCount - n);
        }
    }
    std::erase_if(stacks, [](ItemStack const& s) { return game::isEmpty(s); });
    std::stable_sort(stacks.begin(), stacks.end(), [](ItemStack const& a, ItemStack const& b) { return compareStacks(a, b) < 0; });

    size_t k = 0;
    for (int i = begin; i < end; ++i) {
        if (isLocked(i)) continue;
        c.setItem(i, k < stacks.size() ? stacks[k++] : ItemStack{});
    }
    c.setContainerChanged(0);
}

} // namespace lp::feature::inventory
