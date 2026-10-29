#include "feature/mobtweaks/MobTweaks.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <mutex>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"

#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("mobtweaks", lp::feature::mobtweaks::MobTweaksConfig);

namespace lp::feature::mobtweaks {

MobTweaksConfig const& cfg() { return config::get<MobTweaksConfig>(); }

namespace {

std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::vector<std::string> split(std::string const& s, char sep) {
    std::vector<std::string> out;
    size_t                   start = 0;
    while (true) {
        auto at = s.find(sep, start);
        out.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    return out;
}

/// "golden_pickaxe" -> "minecraft:golden_pickaxe"
std::string withNamespace(std::string id) {
    id = trim(std::move(id));
    if (!id.empty() && id.find(':') == std::string::npos) id = "minecraft:" + id;
    return id;
}

struct BakedSlot {
    std::string name;      ///< Bedrock enchantment string id ("fortune")
    int         level = 0;
    int         type  = -1; ///< resolved Enchant::Type, -2 = unknown id
};

struct Caches {
    std::unordered_map<std::string, BakedEnchants>               baked;     // resolved view (selftests, tooltip)
    std::unordered_map<uint64_t, std::vector<BakedSlot>>         bakedHash; // item full-name hash -> slots
    std::unordered_map<std::string, std::vector<std::string>>    repair;
    std::vector<std::string>                                     unrepairable;
    std::vector<std::pair<std::string, std::vector<BakedSlot>>>  bakedNamed; // for the resolved view
    std::atomic<bool>                                            resolved = false;
};

/// Resolution may run on the server thread (world ready) or the client thread (tooltips).
std::mutex& resolveMutex() {
    static std::mutex m;
    return m;
}

Caches& caches() {
    static Caches c;
    return c;
}

int resolveEnchant(std::string const& name) {
    auto t = EnchantUtils::getEnchantmentId(HashedString(name));
    if (t == Enchant::Type::InvalidEnchantment || t == Enchant::Type::NumEnchantments) return -2;
    return static_cast<int>(t);
}

void resolveAll() {
    auto& c = caches();
    if (c.resolved.load(std::memory_order_acquire)) return;
    std::lock_guard lock(resolveMutex());
    if (c.resolved.load(std::memory_order_relaxed)) return;
    if (resolveEnchant("fortune") < 0) return; // enchantments not registered yet: retry later
    c.baked.clear();
    c.bakedHash.clear();
    for (auto& [item, slots] : c.bakedNamed) {
        for (auto& s : slots) {
            if (s.type == -1) s.type = resolveEnchant(s.name);
            if (s.type < 0) {
                log().warn("mobtweaks: unknown enchantment '{}' in itemEnchantments", s.name);
                continue;
            }
            c.baked[item].emplace(s.type, s.level); // first entry wins (Quark: computeIfAbsent)
        }
        c.bakedHash[HashedString(item).getHash()] = slots;
    }
    c.resolved.store(true, std::memory_order_release);
}

} // namespace

std::optional<BakedEntry> parseBakedEntry(std::string const& entry) {
    auto pair = split(entry, '+');
    if (pair.size() != 2) return std::nullopt;
    auto ench = split(pair[1], '@');
    if (ench.size() != 2) return std::nullopt;
    BakedEntry out;
    out.item    = withNamespace(pair[0]);
    out.enchant = trim(ench[0]);
    if (auto colon = out.enchant.find(':'); colon != std::string::npos) out.enchant = out.enchant.substr(colon + 1);
    auto lvl = trim(ench[1]);
    auto res = std::from_chars(lvl.data(), lvl.data() + lvl.size(), out.level);
    if (res.ec != std::errc{} || out.item.empty() || out.enchant.empty()) return std::nullopt;
    return out;
}

std::optional<std::pair<std::string, std::vector<std::string>>> parseRepairEntry(std::string const& entry) {
    auto toks = split(entry, '=');
    if (toks.size() != 2) return std::nullopt;
    auto item = withNamespace(toks[0]);
    if (item.empty()) return std::nullopt;
    std::vector<std::string> repairs;
    for (auto& r : split(toks[1], ',')) {
        auto id = withNamespace(r);
        if (!id.empty()) repairs.push_back(id);
    }
    if (repairs.empty()) return std::nullopt;
    return std::make_pair(item, repairs);
}

void rebuildCaches() {
    auto&       c = caches();
    auto const& k = cfg();
    {
        std::lock_guard lock(resolveMutex());
        c.resolved.store(false);
        c.baked.clear();
        c.bakedHash.clear();
        c.repair.clear();
        c.unrepairable.clear();
        c.bakedNamed.clear();
    }

    std::vector<std::pair<std::string, std::vector<BakedSlot>>> named;
    auto slotsOf = [&](std::string const& item) -> std::vector<BakedSlot>& {
        for (auto& [n, s] : named)
            if (n == item) return s;
        named.emplace_back(item, std::vector<BakedSlot>{});
        return named.back().second;
    };
    auto put = [&](std::string const& item, std::string const& ench, int level) {
        auto& slots = slotsOf(item);
        for (auto const& s : slots)
            if (s.name == ench) return; // computeIfAbsent: the first value stays
        slots.push_back(BakedSlot{ench, level});
    };
    for (auto const& e : k.itemEnchantments) {
        if (auto p = parseBakedEntry(e)) put(p->item, p->enchant, p->level);
        else log().warn("mobtweaks: bad itemEnchantments entry '{}'", e);
    }
    if (k.fortuneLevel > 0) {
        // Java: every TieredItem of tier GOLD; the sword (tag counts_as_weapon...) gets Looting.
        for (auto id : {"minecraft:golden_pickaxe", "minecraft:golden_axe", "minecraft:golden_shovel", "minecraft:golden_hoe"})
            put(id, "fortune", k.fortuneLevel);
        put("minecraft:golden_sword", "looting", k.fortuneLevel);
    }
    c.bakedNamed = std::move(named);
    resolveAll();

    for (auto const& e : k.repairChangesList) {
        if (auto p = parseRepairEntry(e)) {
            auto& list = c.repair[p->first];
            for (auto& r : p->second)
                if (std::find(list.begin(), list.end(), r) == list.end()) list.push_back(r);
        } else {
            log().warn("mobtweaks: bad repairChangesList entry '{}'", e);
        }
    }
    for (auto const& e : k.unrepairableItemsList) {
        auto id = withNamespace(e);
        if (!id.empty()) c.unrepairable.push_back(id);
    }
}

std::unordered_map<std::string, BakedEnchants> const& bakedEnchantments() {
    resolveAll();
    return caches().baked;
}

int bakedLevel(int enchantType, ItemStackBase const& stack) {
    if (!cfg().enableGoldToolsHaveFortune) return 0;
    auto* item = stack.mItem.get();
    if (!item) return 0;
    auto& c = caches();
    if (!c.resolved.load(std::memory_order_acquire)) return 0; // resolved on config load / world ready
    auto it = c.bakedHash.find(item->mFullName->getHash());
    if (it == c.bakedHash.end()) return 0;
    for (auto const& s : it->second)
        if (s.type == enchantType) return s.level;
    return 0;
}

std::unordered_map<std::string, std::vector<std::string>> const& repairChanges() { return caches().repair; }
std::vector<std::string> const&                                   unrepairableItems() { return caches().unrepairable; }

std::optional<bool> repairOverride(std::string const& item, std::string const& repair) {
    if (!cfg().enableDiamondRepair) return std::nullopt;
    auto const& c = caches();
    if (std::find(c.unrepairable.begin(), c.unrepairable.end(), item) != c.unrepairable.end()) return false;
    if (auto it = c.repair.find(item); it != c.repair.end())
        return std::find(it->second.begin(), it->second.end(), repair) != it->second.end();
    return std::nullopt;
}

// ---- scheduler -----------------------------------------------------------------------------------

namespace {
struct Scheduler {
    uint64_t                                         tick = 0;
    std::multimap<uint64_t, std::function<void()>>   tasks;
};
Scheduler& sched() {
    static Scheduler s;
    return s;
}
} // namespace

void later(int ticks, std::function<void()> fn) {
    sched().tasks.emplace(sched().tick + static_cast<uint64_t>(std::max(ticks, 1)), std::move(fn));
}

uint64_t tickNow() { return sched().tick; }

class MobTweaksCore {
public:
    void install() {
        rebuildCaches();
        static bool registered = false;
        if (!registered) {
            registered = true;
            config::onReload([] { rebuildCaches(); });
            game::db::onWorldClosed([] { sched().tasks.clear(); });
            game::db::onWorldReady([] { resolveAll(); });
        }
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            auto& s = sched();
            ++s.tick;
            while (!s.tasks.empty() && s.tasks.begin()->first <= s.tick) {
                auto fn = std::move(s.tasks.begin()->second);
                s.tasks.erase(s.tasks.begin());
                try {
                    fn();
                } catch (std::exception const& e) {
                    log().error("mobtweaks: task failed: {}", e.what());
                }
            }
        });
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        mTick.reset();
        sched().tasks.clear();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace lp::feature::mobtweaks

LP_REGISTER_MODULE(lp::feature::mobtweaks::MobTweaksCore);
