// U7 newwoods: config, ids and the small per-item rules (fuel, compost, recipes, loot).

#include "feature/newwoods/NewWoods.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/block/Block.h"

#include "game/BlockStates.h"
#include "mod/Config.h"

LP_REGISTER_CONFIG("newwoods", lp::feature::newwoods::NewWoodsConfig);

namespace lp::feature::newwoods {

NewWoodsConfig const& config() { return lp::config::get<NewWoodsConfig>(); }

namespace {

constexpr std::string_view kWoods[] = {"azalea", "blossom", "ancient"};

Module moduleOfWood(std::string_view w) {
    if (w == "azalea") return Module::AzaleaWood;
    if (w == "blossom") return Module::BlossomTrees;
    return Module::AncientWood;
}

struct Ids {
    std::vector<std::string>                     blocks;
    std::unordered_map<std::string, Module>      module;
    std::unordered_map<std::string, std::string> kind; ///< id -> "log", "planks", "slab", ...
};

Ids const& ids() {
    static Ids const v = [] {
        Ids out;
        auto add = [&](std::string id, Module m, std::string kind) {
            out.blocks.push_back(id);
            out.module[id] = m;
            out.kind[id]   = std::move(kind);
        };
        for (auto w : kWoods) {
            std::string s(w);
            Module      m = moduleOfWood(w);
            add("lepton:" + s + "_log", m, "log");
            add("lepton:" + s + "_wood", m, "log");
            add("lepton:stripped_" + s + "_log", m, "log");
            add("lepton:stripped_" + s + "_wood", m, "log");
            add("lepton:" + s + "_planks", m, "planks");
            add("lepton:" + s + "_planks_stairs", m, "stairs");
            add("lepton:" + s + "_planks_slab", m, "slab");
            add("lepton:" + s + "_planks_vertical_slab", m, "slab");
            add("lepton:" + s + "_fence", m, "fence");
            add("lepton:" + s + "_fence_gate", m, "fence");
            add("lepton:" + s + "_door", m, "door");
            add("lepton:" + s + "_trapdoor", m, "trapdoor");
            add("lepton:" + s + "_button", m, "button");
            add("lepton:" + s + "_pressure_plate", m, "plate");
            add("lepton:vertical_" + s + "_planks", m, "planks");
            add("lepton:" + s + "_bookshelf", m, "bookshelf");
            add("lepton:" + s + "_ladder", m, "ladder");
            add("lepton:" + s + "_post", m, "post");
            add("lepton:stripped_" + s + "_post", m, "post");
            add("lepton:hollow_" + s + "_log", m, "hollow_log");
        }
        for (auto const& t : treeTypes()) {
            std::string n(t.name);
            add("lepton:" + n + "_leaves", t.module, "leaves");
            add("lepton:" + n + "_sapling", t.module, "sapling");
            add("lepton:" + n + "_hedge", t.module, "hedge");
            add("lepton:" + n + "_leaf_carpet", t.module, "leaf_carpet");
        }
        out.module["lepton:ancient_fruit"] = Module::AncientWood;
        out.kind["lepton:ancient_fruit"]   = "fruit";
        return out;
    }();
    return v;
}

std::string_view kindOf(std::string_view id) {
    auto const& k  = ids().kind;
    auto        it = k.find(std::string(id));
    return it == k.end() ? std::string_view{} : std::string_view(it->second);
}

} // namespace

Module moduleOfId(std::string_view id) {
    auto const& m  = ids().module;
    auto        it = m.find(std::string(id));
    return it == m.end() ? Module::None : it->second;
}

bool moduleEnabled(Module m) {
    auto const& c = config();
    switch (m) {
    case Module::AzaleaWood:
        return c.enableAzaleaWood;
    case Module::AncientWood:
        return c.enableAncientWood;
    case Module::BlossomTrees:
        return c.enableBlossomTrees;
    default:
        return true;
    }
}

std::vector<std::string> const& allBlockIds() { return ids().blocks; }

std::array<TreeType, 6> const& treeTypes() {
    static std::array<TreeType, 6> const t{{
        {"ancient", "lepton:ancient_sapling", "lepton:ancient_leaves", "lepton:ancient_log", true, Module::AncientWood},
        {"blue_blossom", "lepton:blue_blossom_sapling", "lepton:blue_blossom_leaves", "lepton:blossom_log", false, Module::BlossomTrees},
        {"lavender_blossom", "lepton:lavender_blossom_sapling", "lepton:lavender_blossom_leaves", "lepton:blossom_log", false, Module::BlossomTrees},
        {"orange_blossom", "lepton:orange_blossom_sapling", "lepton:orange_blossom_leaves", "lepton:blossom_log", false, Module::BlossomTrees},
        {"yellow_blossom", "lepton:yellow_blossom_sapling", "lepton:yellow_blossom_leaves", "lepton:blossom_log", false, Module::BlossomTrees},
        {"red_blossom", "lepton:red_blossom_sapling", "lepton:red_blossom_leaves", "lepton:blossom_log", false, Module::BlossomTrees},
    }};
    return t;
}

TreeType const* treeOfSapling(std::string_view saplingId) {
    for (auto const& t : treeTypes())
        if (t.sapling == saplingId) return &t;
    return nullptr;
}
TreeType const* treeOfLeaves(std::string_view leavesId) {
    for (auto const& t : treeTypes())
        if (t.leaves == leavesId) return &t;
    return nullptr;
}
TreeType const* treeByName(std::string_view name) {
    for (auto const& t : treeTypes())
        if (t.name == name) return &t;
    return nullptr;
}

// ---- fuel / compost (docs §2.1, §2.2) ---------------------------------------------------------------

int fuelTicks(std::string_view itemId) {
    auto m = moduleOfId(itemId);
    if (m == Module::None || !moduleEnabled(m)) return 0;
    auto k = kindOf(itemId);
    // Java AbstractFurnaceBlockEntity fuel: #logs, #planks, wooden stairs / fences / gates / trapdoors /
    // pressure plates 300, wooden slabs 150, wooden doors 200, wooden buttons 100; saplings 100 (Quark).
    // Ladders are U6's (lepton:ladder).
    if (k == "log" || k == "planks" || k == "stairs" || k == "fence" || k == "trapdoor" || k == "plate") return 300;
    if (k == "slab") return 150;
    if (k == "door") return 200;
    if (k == "button" || k == "sapling") return 100;
    return 0;
}

int compostChance(std::string_view itemId) {
    auto m = moduleOfId(itemId);
    if (m == Module::None || !moduleEnabled(m)) return -1;
    auto k = kindOf(itemId);
    if (k == "leaves" || k == "sapling") return 30;
    if (k == "fruit") return 65;
    return -1;
}

int fruitExperience() {
    auto const& c = config();
    if (!c.enableAncientWood || !c.ancientFruitGivesExp) return 0;
    return std::max(1, c.ancientFruitExpValue);
}

// ---- recipes / loot (docs §3.8, §3.10) ---------------------------------------------------------------

bool recipeDisabled(std::string const& recipeId, std::string const& result) {
    if (!recipeId.starts_with("lepton:")) return false;
    std::string_view path(recipeId);
    path.remove_prefix(7);
    if (path.starts_with("azalea_hedge_from_") || path.starts_with("flowering_azalea_hedge_from_"))
        // the hedges are U6's blocks: without them there is nothing to craft
        return !config().enableAzaleaWood || !game::blockstates::of(result);
    auto m = moduleOfId(result);
    return m != Module::None && !moduleEnabled(m);
}

std::optional<LootAddition> lootAddition(std::string_view dir) {
    auto const& c = config();
    if (!c.enableAncientWood) return std::nullopt;
    // tables are looked up with or without the ".json" suffix
    if (dir.ends_with(".json")) dir.remove_suffix(5);
    if (dir == "loot_tables/gameplay/entities/sniffer_seeds" && c.sniffingLootWeight > 0)
        return LootAddition{"lepton:ancient_sapling", c.sniffingLootWeight, c.sniffingLootQuality};
    if (dir == "loot_tables/chests/ancient_city" && c.ancientCityLootWeight > 0)
        return LootAddition{"lepton:ancient_sapling", c.ancientCityLootWeight, c.ancientCityLootQuality};
    return std::nullopt;
}

bool hasTag(Block const& block, std::string_view tag) {
    thread_local std::map<std::string, HashedString, std::less<>> cache;
    auto it = cache.find(tag);
    if (it == cache.end()) it = cache.emplace(std::string(tag), HashedString(std::string(tag))).first;
    return block.hasTag(it->second);
}

} // namespace lp::feature::newwoods
