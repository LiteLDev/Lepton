#include "feature/enchanting/Enchants.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/deps/nbt/ShortTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"

#include "game/Items.h"

namespace lp::feature::enchanting {

namespace {

struct Known {
    char const* english;
    int         javaWeight;
};

/// Java names and weights (rarity) of the vanilla enchantments, by Bedrock id path.
std::unordered_map<std::string, Known> const& known() {
    static std::unordered_map<std::string, Known> const k = {
        {"protection",            {"Protection", 10}           },
        {"fire_protection",       {"Fire Protection", 5}       },
        {"feather_falling",       {"Feather Falling", 5}       },
        {"blast_protection",      {"Blast Protection", 2}      },
        {"projectile_protection", {"Projectile Protection", 5} },
        {"thorns",                {"Thorns", 1}                },
        {"respiration",           {"Respiration", 2}           },
        {"depth_strider",         {"Depth Strider", 2}         },
        {"aqua_affinity",         {"Aqua Affinity", 2}         },
        {"sharpness",             {"Sharpness", 10}            },
        {"smite",                 {"Smite", 5}                 },
        {"bane_of_arthropods",    {"Bane of Arthropods", 5}    },
        {"knockback",             {"Knockback", 5}             },
        {"fire_aspect",           {"Fire Aspect", 2}           },
        {"looting",               {"Looting", 2}               },
        {"efficiency",            {"Efficiency", 10}           },
        {"silk_touch",            {"Silk Touch", 1}            },
        {"unbreaking",            {"Unbreaking", 5}            },
        {"fortune",               {"Fortune", 2}               },
        {"power",                 {"Power", 10}                },
        {"punch",                 {"Punch", 2}                 },
        {"flame",                 {"Flame", 2}                 },
        {"infinity",              {"Infinity", 1}              },
        {"luck_of_the_sea",       {"Luck of the Sea", 2}       },
        {"lure",                  {"Lure", 2}                  },
        {"frost_walker",          {"Frost Walker", 2}          },
        {"mending",               {"Mending", 2}               },
        {"binding",               {"Curse of Binding", 1}      },
        {"vanishing",             {"Curse of Vanishing", 1}    },
        {"impaling",              {"Impaling", 2}              },
        {"riptide",               {"Riptide", 2}               },
        {"loyalty",               {"Loyalty", 5}               },
        {"channeling",            {"Channeling", 1}            },
        {"multishot",             {"Multishot", 2}             },
        {"piercing",              {"Piercing", 10}             },
        {"quick_charge",          {"Quick Charge", 5}          },
        {"soul_speed",            {"Soul Speed", 1}            },
        {"swift_sneak",           {"Swift Sneak", 1}           },
        {"wind_burst",            {"Wind Burst", 2}            },
        {"density",               {"Density", 5}               },
        {"breach",                {"Breach", 2}                },
        {"lunge",                 {"Lunge", 5}                 },
    };
    return k;
}

/// Java ids that differ from Bedrock's.
std::string aliasOf(std::string path) {
    if (path == "binding_curse") return "binding";
    if (path == "vanishing_curse") return "vanishing";
    return path;
}

int javaWeightFromFrequency(Enchant::Frequency f) {
    switch (f) {
    case Enchant::Frequency::Common: return 10;
    case Enchant::Frequency::Uncommon: return 5;
    case Enchant::Frequency::Rare: return 2;
    default: return 1;
    }
}

std::string pathOf(std::string_view id) {
    std::string s(id);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (auto c = s.find(':'); c != std::string::npos) s = s.substr(c + 1);
    return s;
}

std::unique_ptr<CompoundTag> userDataCopy(ItemStackBase const& stack) {
    if (stack.mUserData) return stack.mUserData->clone();
    return std::make_unique<CompoundTag>();
}

} // namespace

std::vector<EnchantDef> const& registry() {
    static std::vector<EnchantDef> defs;
    if (!defs.empty()) return defs;
    auto& all = Enchant::mEnchants();
    for (auto const& ptr : all) {
        if (!ptr) continue;
        auto const& e = *ptr;
        EnchantDef  d;
        d.type = static_cast<int>(static_cast<uchar>(e.mEnchantType));
        std::string path = pathOf(e.mStringId->getString());
        if (path.empty()) continue;
        d.id           = "minecraft:" + path;
        d.minLevel     = e.getMinLevel();
        d.maxLevel     = e.getMaxLevel();
        auto k         = known().find(path);
        d.weight       = k != known().end() ? k->second.javaWeight : javaWeightFromFrequency(e.mFrequency);
        d.treasure     = e.isTreasureOnly();
        d.discoverable = e.isDiscoverable();
        d.primarySlots   = e.mPrimarySlots;
        d.secondarySlots = e.mSecondarySlots;
        d.minCost.assign(static_cast<size_t>(std::max(d.maxLevel, 1)) + 1, 0);
        d.maxCost.assign(static_cast<size_t>(std::max(d.maxLevel, 1)) + 1, 0);
        for (int l = 1; l <= d.maxLevel; ++l) {
            d.minCost[l] = e.getMinCost(l);
            d.maxCost[l] = e.getMaxCost(l);
        }
        defs.push_back(std::move(d));
    }
    // Exclusive sets (both directions).
    for (auto& d : defs) {
        Enchant const* e = nullptr;
        for (auto const& ptr : all)
            if (ptr && static_cast<int>(static_cast<uchar>(ptr->mEnchantType)) == d.type) e = ptr.get();
        if (!e) continue;
        for (auto const& o : defs)
            if (o.type != d.type && !e->isCompatibleWith(static_cast<Enchant::Type>(o.type))) d.incompatible.push_back(o.type);
    }
    std::sort(defs.begin(), defs.end(), [](EnchantDef const& a, EnchantDef const& b) { return a.type < b.type; });
    return defs;
}

EnchantDef const* defOf(int type) {
    for (auto const& d : registry())
        if (d.type == type) return &d;
    return nullptr;
}

EnchantDef const* defById(std::string_view id) {
    auto path = aliasOf(pathOf(id));
    if (path.empty()) return nullptr;
    for (auto const& d : registry())
        if (d.id == "minecraft:" + path) return &d;
    return nullptr;
}

std::string normalizeId(std::string_view id) {
    auto const* d = defById(id);
    return d ? d->id : std::string{};
}

std::vector<std::string> normalizeIds(std::vector<std::string> const& ids) {
    std::vector<std::string> out;
    for (auto const& id : ids)
        if (auto n = normalizeId(id); !n.empty()) out.push_back(n);
    return out;
}

std::string nameKey(EnchantDef const& e) { return "lp.enchanting.enchant." + pathOf(e.id); }

std::string englishName(EnchantDef const& e) {
    auto path = pathOf(e.id);
    if (auto k = known().find(path); k != known().end()) return k->second.english;
    std::string s = path;
    bool        up = true;
    for (auto& c : s) {
        if (c == '_') {
            c  = ' ';
            up = true;
        } else if (up) {
            c  = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            up = false;
        }
    }
    return s;
}

std::string roman(int n) {
    static constexpr std::pair<int, char const*> k[] = {
        {1000, "M"},
        {900,  "CM"},
        {500,  "D"},
        {400,  "CD"},
        {100,  "C"},
        {90,   "XC"},
        {50,   "L"},
        {40,   "XL"},
        {10,   "X"},
        {9,    "IX"},
        {5,    "V"},
        {4,    "IV"},
        {1,    "I"},
    };
    if (n <= 0) return std::to_string(n);
    std::string out;
    for (auto const& [v, s] : k)
        while (n >= v) {
            out += s;
            n   -= v;
        }
    return out;
}

// ---- items -------------------------------------------------------------------------------------

std::map<int, int> enchantsOf(ItemStackBase const& stack) {
    std::map<int, int> out;
    if (game::isEmpty(stack) || !stack.mUserData || !stack.mUserData->contains("ench")) return out;
    auto const& v = (*stack.mUserData)["ench"];
    if (!v.hold(Tag::List)) return out;
    for (auto const& entry : v.get<ListTag>()) {
        if (!entry || entry->getId() != Tag::Compound) continue;
        auto const& c = static_cast<CompoundTag const&>(*entry);
        if (!c.contains("id") || !c.contains("lvl")) continue;
        auto const& id  = c["id"];
        auto const& lvl = c["lvl"];
        int         t   = id.hold(Tag::Short) ? static_cast<int>(id.get<ShortTag>().data) : -1;
        int         l   = lvl.hold(Tag::Short) ? static_cast<int>(lvl.get<ShortTag>().data) : 0;
        if (t >= 0 && l > 0) out[t] = std::max(out[t], l);
    }
    return out;
}

void setEnchants(ItemStackBase& stack, std::map<int, int> const& enchants) {
    auto    tag = userDataCopy(stack);
    ListTag list;
    for (auto const& [type, level] : enchants) {
        CompoundTag e;
        e["id"]  = ShortTag{static_cast<short>(type)};
        e["lvl"] = ShortTag{static_cast<short>(level)};
        list.add(e.copy());
    }
    if (enchants.empty()) tag->erase("ench");
    else (*tag)["ench"] = std::move(list);
    stack.setUserData(std::move(tag));
}

std::optional<std::string> stringTag(ItemStackBase const& stack, std::string const& key) {
    if (game::isEmpty(stack) || !stack.mUserData || !stack.mUserData->contains(key)) return std::nullopt;
    auto const& v = (*stack.mUserData)[key];
    if (!v.hold(Tag::String)) return std::nullopt;
    return static_cast<std::string const&>(v.get<StringTag>());
}

void setStringTag(ItemStackBase& stack, std::string const& key, std::string const& value) {
    auto tag    = userDataCopy(stack);
    (*tag)[key] = value;
    stack.setUserData(std::move(tag));
}

void removeTag(ItemStackBase& stack, std::string const& key) {
    if (!stack.mUserData || !stack.mUserData->contains(key)) return;
    auto tag = userDataCopy(stack);
    tag->erase(key);
    if (tag->empty()) stack.setUserData(nullptr);
    else stack.setUserData(std::move(tag));
}

int enchantSlotOf(ItemStackBase const& stack) {
    auto const* item = stack.mItem.get();
    return item ? item->getEnchantSlot() : 0;
}

int enchantValueOf(ItemStackBase const& stack) {
    auto const* item = stack.mItem.get();
    return item ? item->getEnchantValue() : 0;
}

bool applicable(ItemStackBase const& stack, EnchantDef const& e) {
    int slot = enchantSlotOf(stack);
    return slot != 0 && (slot & (e.primarySlots | e.secondarySlots)) != 0;
}

bool isBook(ItemStackBase const& stack) { return game::itemId(stack) == "minecraft:book"; }

bool isMatrixTarget(ItemStackBase const& stack) {
    if (game::isEmpty(stack) || stack.mCount != 1) return false;
    if (isBook(stack)) return true;
    if (stack.getMaxStackSize() != 1) return false;
    if (!enchantsOf(stack).empty()) return false;
    return enchantSlotOf(stack) != 0;
}

bool isOverlevel(ItemStackBase const& stack) {
    for (auto const& [type, level] : enchantsOf(stack))
        if (auto const* d = defOf(type); d && level > d->maxLevel) return true;
    return false;
}

// ---- players -----------------------------------------------------------------------------------

int playerLevel(Player const& player) {
    auto ref = player.getAttribute(Player::LEVEL());
    auto const* inst = ref.mPtr;
    return inst ? static_cast<int>(inst->mCurrentValue) : 0;
}

void addLevels(Player& player, int levels) {
    if (levels != 0) player.addLevels(levels);
}

bool creative(Player const& player) { return player.isCreative(); }

} // namespace lp::feature::enchanting
