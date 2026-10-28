#include "feature/hud/Logic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/components/IFoodItemComponent.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"

#include "feature/hud/HudConfig.h"
#include "game/Items.h"

namespace lp::feature::hud {

namespace {
using ArmorSlot = SharedTypes::Legacy::ArmorSlot;

ArmorSlot armorSlotOf(TickerSlot s) {
    switch (s) {
    case TickerSlot::Head: return ArmorSlot::Head;
    case TickerSlot::Chest: return ArmorSlot::Torso;
    case TickerSlot::Legs: return ArmorSlot::Legs;
    default: return ArmorSlot::Feet;
    }
}

bool isBlockItem(ItemStackBase const& s) { return s.mBlock != nullptr; }

bool sameStack(ItemStackBase const& a, ItemStackBase const& b) {
    if (game::isEmpty(a) || game::isEmpty(b)) return false;
    return a.matchesItem(b);
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
} // namespace

// ---- UsageTicker ------------------------------------------------------------------------------

int slotIndex(TickerSlot s) {
    switch (s) {
    case TickerSlot::MainHand: return 0;
    case TickerSlot::OffHand: return 1;
    case TickerSlot::Feet: return 0;
    case TickerSlot::Legs: return 1;
    case TickerSlot::Chest: return 2;
    case TickerSlot::Head: return 3;
    }
    return 0;
}

void TickerAnim::tick(bool shownEmpty, bool changed) {
    if (shownEmpty) {
        liveTicks = 0;
    } else if (changed) {
        bool done         = liveTicks == 0;
        bool animatingIn  = liveTicks > kMaxTime - kAnimTime;
        bool animatingOut = liveTicks < kAnimTime && !done;
        if (animatingOut) liveTicks = kMaxTime - liveTicks;
        else if (!animatingIn) liveTicks = done ? kMaxTime : kMaxTime - kAnimTime;
    } else if (liveTicks > 0) {
        --liveTicks;
    }
}

float TickerAnim::offset(float partial) const {
    if (liveTicks <= 0) return 0;
    float p;
    if (liveTicks < kAnimTime) p = std::max(0.f, liveTicks - partial) / kAnimTime;
    else p = std::min(static_cast<float>(kAnimTime), (kMaxTime - liveTicks) + partial) / kAnimTime;
    return -p * (p - 2) * 20.f;
}

ItemStack const& tickerSource(Player const& player, TickerSlot slot) {
    switch (slot) {
    case TickerSlot::MainHand: return player.getSelectedItem();
    case TickerSlot::OffHand: return player.getOffhandSlot();
    default: return player.getArmor(armorSlotOf(slot));
    }
}

int tickerCount(Player const& player, ItemStackBase const& display) {
    if (game::isEmpty(display)) return 0;
    int val = 1;
    if (display.isStackable() || isBlockItem(display)) {
        int   total = 0;
        auto& inv   = player.getInventory();
        for (int i = 0; i < inv.getContainerSize(); ++i) {
            auto const& at = inv.getItem(i);
            if (sameStack(at, display)) total += at.mCount;
        }
        for (auto s : {ArmorSlot::Head, ArmorSlot::Torso, ArmorSlot::Legs, ArmorSlot::Feet}) {
            auto const& at = player.getArmor(s);
            if (sameStack(at, display)) total += at.mCount;
        }
        if (auto const& off = player.getOffhandSlot(); sameStack(off, display)) total += off.mCount;
        val = std::max(total, static_cast<int>(display.mCount));
    }
    return val;
}

std::optional<ItemStack> projectileFor(Player const& player, ItemStackBase const& weapon) {
    auto id = game::itemId(weapon);
    bool bow = id == "minecraft:bow", crossbow = id == "minecraft:crossbow";
    if (!bow && !crossbow) return std::nullopt;
    if (bow && EnchantUtils::getEnchantLevel(Enchant::Type::Infinity, weapon) > 0) return std::nullopt;
    auto isAmmo = [&](ItemStackBase const& s, bool held) {
        auto sid = game::itemId(s);
        return sid == "minecraft:arrow" || (crossbow && held && sid == "minecraft:firework_rocket");
    };
    // Held projectiles first (off hand, then the main hand), then the inventory in slot order.
    if (auto const& off = player.getOffhandSlot(); isAmmo(off, true)) return ItemStack(off);
    auto& inv = player.getInventory();
    for (int i = 0; i < inv.getContainerSize(); ++i)
        if (auto const& at = inv.getItem(i); isAmmo(at, false)) return ItemStack(at);
    return ItemStack{};
}

ItemStack logicalStack(Player const& player, TickerSlot slot, ItemStack const& stack, int count) {
    if (game::isEmpty(stack)) return ItemStack{};
    if (auto proj = projectileFor(player, stack)) return *proj;
    if (!stack.isStackable() && !isBlockItem(stack) && !isArmorSlot(slot)) return ItemStack{};
    if (stack.isStackable() && count == stack.mCount) return ItemStack{};
    return stack;
}

TickerStack tickerStack(Player const& player, TickerSlot slot) {
    auto const& real  = tickerSource(player, slot);
    int         count = tickerCount(player, real);
    auto        shown = logicalStack(player, slot, real, count);
    if (game::isEmpty(shown)) return {};
    if (!sameStack(shown, real)) count = tickerCount(player, shown);
    return {std::move(shown), count};
}

bool tickerChanged(ItemStackBase const& now, ItemStackBase const& before, int countNow, int countBefore) {
    if (now.mItem.get() != before.mItem.get()) return true;
    if (now.isDamageableItem() && now.getDamageValue() != before.getDamageValue()) return true;
    return countNow != countBefore;
}

float tickerX(float width, TickerSlot slot, bool invert, bool offhandEmpty, int shiftLeft, int shiftRight) {
    float x      = width / 2;
    bool  armor  = isArmorSlot(slot);
    bool  right  = armor != invert; // main arm is always the right one on Bedrock
    int   slots  = armor ? 4 : 2;
    int   index  = slots - slotIndex(slot) - 1;
    float mul    = right ? 1.f : -1.f;
    int   barW   = 190;
    if (!right && !offhandEmpty) barW += 58;
    x += (barW / 2.f) * mul + index * 20;
    if (!right) x += -slots * 20 + shiftLeft;
    else x += shiftRight;
    return x;
}

// ---- ElytraIndicator ---------------------------------------------------------------------------

bool canElytraFly(ItemStackBase const& chest) {
    if (game::itemId(chest) != "minecraft:elytra") return false;
    auto* item = chest.mItem.get();
    if (!item) return false;
    return chest.getDamageValue() < item->getMaxDamage() - 1;
}

// ---- food --------------------------------------------------------------------------------------

std::optional<FoodInfo> foodInfo(ItemStackBase const& stack) {
    if (game::isEmpty(stack)) return std::nullopt;
    auto* item = stack.mItem.get();
    if (!item) return std::nullopt;
    auto id = game::itemId(stack);
    if (id == "minecraft:ominous_bottle") return std::nullopt;
    auto* food = item->getFood();
    if (!food) return std::nullopt;
    FoodInfo info;
    info.nutrition  = food->getNutrition();
    info.saturation = std::min(20.f, info.nutrition * food->getSaturationModifier() * 2.f);
    // Bedrock's food effects are part of the items' code: the vanilla foods with a harmful effect.
    static constexpr std::array<std::string_view, 5> kHarmful{
        "minecraft:rotten_flesh", "minecraft:spider_eye", "minecraft:poisonous_potato", "minecraft:pufferfish",
        "minecraft:chicken"};
    info.poison = std::find(kHarmful.begin(), kHarmful.end(), id) != kHarmful.end();
    if (id == "minecraft:suspicious_stew") {
        // Aux values with a harmful effect: tulips (weakness), azure bluet (blindness), lily of the
        // valley (poison), wither rose (wither), open / closed eyeblossom (blindness / nausea).
        int aux     = stack.getAuxValue();
        info.poison = aux == 2 || aux == 3 || aux == 4 || aux == 9 || aux == 11 || aux == 12;
    }
    return info;
}

int saturationTier(float s) {
    if (s >= 19) return 5;
    if (s < 10) {
        if (s >= 8) return 1;
        if (s >= 6) return 2;
        if (s >= 2) return 3;
        return 4;
    }
    return 0;
}

FoodRow foodRow(int nutrition, int divisor, int compressionThreshold) {
    FoodRow r;
    if (nutrition <= 0) return r;
    int  count = static_cast<int>(std::ceil(static_cast<double>(nutrition) / std::max(1, divisor)));
    bool fract = nutrition % 2 != 0;
    r.width    = count * 9;
    r.shanks   = count;
    if (count > compressionThreshold) {
        r.shanks = 1;
        if (fract) --count;
        r.text = "x" + std::to_string(count) + (fract ? ".5" : "");
    }
    r.halfLast = fract;
    return r;
}

// ---- fuel ------------------------------------------------------------------------------------

int fuelTicks(ItemStackBase const& stack) {
    if (game::isEmpty(stack)) return 0;
    return static_cast<int>(std::lround(FurnaceBlockActor::getBurnDuration(stack, 200.f)));
}

std::string fuelText(int ticks, int divisor) {
    float items = static_cast<float>(ticks) / static_cast<float>(std::max(1, divisor));
    char  buf[32];
    if (items - static_cast<int>(items) == 0) std::snprintf(buf, sizeof buf, "x%.0f", items);
    else std::snprintf(buf, sizeof buf, "x%.1f", items);
    return buf;
}

// ---- shulker boxes ---------------------------------------------------------------------------

bool isShulkerBox(ItemStackBase const& stack) {
    auto id = game::itemId(stack);
    return id.size() > 11 && id.ends_with("shulker_box");
}

std::vector<ItemStack> shulkerContents(ItemStackBase const& stack) {
    std::vector<std::pair<int, ItemStack>> slots;
    if (!isShulkerBox(stack) || !stack.mUserData) return {};
    auto const& tag = *stack.mUserData;
    if (!tag.contains("Items")) return {};
    auto const& items = tag.at("Items");
    if (!items.is_array()) return {};
    int order = 0;
    for (auto const& entry : items.get<ListTag>()) {
        if (!entry || !entry.hold(Tag::Type::Compound)) continue;
        auto const& c    = static_cast<CompoundTag const&>(*entry);
        int         slot = order++;
        if (c.contains("Slot") && c.at("Slot").is_number_integer()) slot = static_cast<int>(static_cast<int64>(c.at("Slot")));
        auto is = ItemStack::fromTag(c);
        if (!game::isEmpty(is)) slots.emplace_back(slot, std::move(is));
    }
    std::stable_sort(slots.begin(), slots.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<ItemStack> out;
    out.reserve(slots.size());
    for (auto& [s, is] : slots) out.push_back(std::move(is));
    return out;
}

std::pair<int, int> shulkerDims(int size) {
    static constexpr std::array<std::pair<int, int>, 7> kRatios{
        {{1, 1}, {9, 3}, {9, 5}, {9, 6}, {9, 8}, {9, 9}, {12, 9}}};
    for (auto const& r : kRatios)
        if (r.first * r.second == size) return r;
    if (size <= 0) return {0, 0};
    return {std::min(size, 9), 1 + (size - 1) / 9};
}

std::optional<int> shulkerColor(ItemStackBase const& stack) {
    static std::map<std::string, int, std::less<>> const kDyes{
        {"white", 0xF9FFFE},  {"orange", 0xF9801D}, {"magenta", 0xC74EBD}, {"light_blue", 0x3AB3DA},
        {"yellow", 0xFED83D}, {"lime", 0x80C71F},   {"pink", 0xF38BAA},    {"gray", 0x474F52},
        {"light_gray", 0x9D9D97}, {"silver", 0x9D9D97}, {"cyan", 0x169C9C}, {"purple", 0x8932B8},
        {"blue", 0x3C44AA},   {"brown", 0x835432},  {"green", 0x5E7C16},   {"red", 0xB02E26},
        {"black", 0x1D1D21}};
    auto id = game::itemId(stack);
    if (!startsWith(id, "minecraft:") || !id.ends_with("_shulker_box")) return std::nullopt;
    auto color = std::string_view(id).substr(10, id.size() - 10 - 12);
    if (auto it = kDyes.find(color); it != kDyes.end()) return it->second;
    return std::nullopt;
}

// ---- enchanted books ---------------------------------------------------------------------------

std::vector<BookEnchant> bookEnchants(ItemStackBase const& stack) {
    std::vector<BookEnchant> out;
    if (game::itemId(stack) != "minecraft:enchanted_book") return out;
    for (auto const& e : stack.constructItemEnchantsFromUserData().getAllEnchants()) {
        auto type = static_cast<Enchant::Type>(e.mEnchantType);
        out.push_back({static_cast<int>(type), e.mLevel, EnchantUtils::getEnchantNameAndLevel(type, e.mLevel)});
    }
    return out;
}

namespace {
Enchant const* enchantOf(int type) {
    auto& all = Enchant::mEnchants();
    if (type < 0 || type >= static_cast<int>(all.size())) return nullptr;
    return all[static_cast<size_t>(type)].get();
}

std::string stripNamespace(std::string_view s) {
    if (auto p = s.find(':'); p != std::string_view::npos) return std::string(s.substr(p + 1));
    return std::string(s);
}
} // namespace

std::vector<std::string> enchantItems(int enchantType) {
    std::vector<std::string> out;
    auto const*              e = enchantOf(enchantType);
    if (!e) return out;
    for (auto const& id : cfg().enchantingStacks) {
        auto stack = game::makeItem(id);
        auto* item = stack.mItem.get();
        if (game::isEmpty(stack) || !item) continue;
        if ((item->getEnchantSlot() & e->mPrimarySlots) == 0) continue;
        if (item->getEnchantValue() <= 0) continue;
        out.push_back(id);
    }
    // "enchant_id=item1,item2": the enchantment by its string id ("minecraft:mending" or "mending").
    std::string ours = stripNamespace(e->mStringId->getString());
    for (auto const& line : cfg().enchantingAdditionalStacks) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (stripNamespace(std::string_view(line).substr(0, eq)) != ours) continue;
        std::string_view rest = std::string_view(line).substr(eq + 1);
        while (!rest.empty()) {
            auto comma = rest.find(',');
            auto id    = std::string(rest.substr(0, comma));
            if (!id.empty() && !game::isEmpty(game::makeItem(id))) out.push_back(id);
            if (comma == std::string_view::npos) break;
            rest = rest.substr(comma + 1);
        }
    }
    return out;
}

// ---- attributes --------------------------------------------------------------------------------

TickerSlot equipmentSlotFor(ItemStackBase const& stack) {
    if (!stack.isArmorItem()) return TickerSlot::MainHand;
    switch (stack.getArmorSlot()) {
    case ArmorSlot::Head: return TickerSlot::Head;
    case ArmorSlot::Torso: return TickerSlot::Chest;
    case ArmorSlot::Legs: return TickerSlot::Legs;
    case ArmorSlot::Feet: return TickerSlot::Feet;
    default: return TickerSlot::Chest;
    }
}

std::vector<Attribute> attributesOf(ItemStackBase const& stack) {
    std::vector<Attribute> out;
    auto*                  item = stack.mItem.get();
    if (game::isEmpty(stack) || !item) return out;
    auto add = [&](std::string id, SlotGroup g, double amount, double base) {
        for (auto& a : out) {
            if (a.id == id && a.group == g && a.op == AttrOp::AddValue) {
                a.amount += amount;
                return;
            }
        }
        out.push_back({std::move(id), g, AttrOp::AddValue, amount, base});
    };
    if (stack.isArmorItem()) {
        SlotGroup g = SlotGroup::Chest;
        switch (equipmentSlotFor(stack)) {
        case TickerSlot::Head: g = SlotGroup::Head; break;
        case TickerSlot::Legs: g = SlotGroup::Legs; break;
        case TickerSlot::Feet: g = SlotGroup::Feet; break;
        default: break;
        }
        if (int armor = item->getArmorValue(); armor > 0) add("armor", g, armor, 0);
        if (int tough = item->getToughnessValue(); tough > 0) add("armor_toughness", g, tough, 0);
        if (game::itemId(stack).find("netherite_") != std::string::npos) add("knockback_resistance", g, 0.1, 0);
    }
    if (int dmg = item->getAttackDamage(); dmg > 0 && !stack.isArmorItem()) add("attack_damage", SlotGroup::MainHand, dmg, 1);
    return out;
}

std::string formatNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

std::pair<std::string, bool> formatAttribute(Attribute const& a) {
    switch (a.op) {
    case AttrOp::AddValue: return {formatNumber(a.amount + a.base), false};
    case AttrOp::AddMultipliedBase: return {(a.amount > 0 ? "+" : "") + formatNumber(a.amount * 100) + "%", false};
    case AttrOp::AddMultipliedTotal: return {formatNumber(a.amount) + "x", a.amount < 1};
    }
    return {formatNumber(a.amount), a.amount < 0};
}

} // namespace lp::feature::hud
