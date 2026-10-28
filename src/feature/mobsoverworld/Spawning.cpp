// Spawning: the behaviour pack's spawn rules are written from the config at mod load (Quark reads
// EntitySpawnConfig at startup too), and every spawn of a Lepton mob is checked natively (module
// enabled, Quark's spawn predicates).

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "nlohmann/json.hpp"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/BrightnessPair.h"

#include "feature/mobsoverworld/Common.h"
#include "feature/mobsoverworld/Config.h"
#include "feature/mobsoverworld/Features.h"
#include "game/Blocks.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace lp::feature::mobsoverworld::spawning {

namespace {

using nlohmann::json;
using nlohmann::ordered_json;

constexpr std::string_view kStoneBlocks[] = {
    "minecraft:stone",       "minecraft:deepslate",    "minecraft:tuff",
    "minecraft:granite",     "minecraft:diorite",      "minecraft:andesite",
    "minecraft:calcite",     "minecraft:cobbled_deepslate", "minecraft:cobblestone",
    "minecraft:smooth_basalt", "minecraft:dripstone_block", "minecraft:mossy_cobblestone",
    "minecraft:polished_granite", "minecraft:polished_diorite", "minecraft:polished_andesite",
    "minecraft:polished_deepslate", "minecraft:infested_stone", "minecraft:infested_deepslate",
};

std::string tagOfDimension(std::string const& dim) {
    if (dim == "minecraft:overworld") return "overworld";
    if (dim == "minecraft:the_nether") return "nether";
    if (dim == "minecraft:the_end") return "the_end";
    auto p = dim.find(':');
    return p == std::string::npos ? dim : dim.substr(p + 1);
}

std::string stripNamespace(std::string const& id) {
    auto p = id.find(':');
    return p == std::string::npos ? id : id.substr(p + 1);
}

ordered_json biomeFilter(SpawnConfig const& sc, DimensionConfig const* dims, bool enabled) {
    if (!enabled || sc.spawnWeight <= 0) {
        return ordered_json{{"test", "has_biome_tag"}, {"value", "lepton_never_spawns"}};
    }
    std::vector<std::string> tags = sc.biomes.biomeTags;
    for (auto const& b : sc.biomes.biomes) tags.push_back(stripNamespace(b));
    ordered_json terms = ordered_json::array();
    ordered_json list  = ordered_json::array();
    for (auto const& t : tags) {
        ordered_json term{{"test", "has_biome_tag"}, {"value", t}};
        if (sc.biomes.isBlacklist) term["operator"] = "!=";
        list.push_back(term);
    }
    if (!list.empty()) {
        terms.push_back(sc.biomes.isBlacklist ? ordered_json{{"all_of", list}} : ordered_json{{"any_of", list}});
    }
    if (dims) {
        ordered_json d = ordered_json::array();
        for (auto const& dim : dims->dimensions) {
            ordered_json term{{"test", "has_biome_tag"}, {"value", tagOfDimension(dim)}};
            if (dims->isBlacklist) term["operator"] = "!=";
            d.push_back(term);
        }
        if (!d.empty()) terms.push_back(dims->isBlacklist ? ordered_json{{"all_of", d}} : ordered_json{{"any_of", d}});
    }
    if (terms.empty()) return ordered_json{{"test", "has_biome_tag"}, {"operator", "!=" }, {"value", "lepton_never_spawns"}};
    return ordered_json{{"all_of", terms}};
}

ordered_json rule(std::string_view id, std::string_view population, ordered_json condition) {
    return ordered_json{{"format_version", "1.8.0"},
                        {"minecraft:spawn_rules",
                         {{"description", {{"identifier", std::string(id)}, {"population_control", std::string(population)}}},
                          {"conditions", ordered_json::array({std::move(condition)})}}}};
}

void writeJson(std::filesystem::path const& path, ordered_json const& doc) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << doc.dump(2) << "\n";
}

} // namespace

void writePackFiles() {
    auto bp = config::configDir().parent_path() / "behavior_packs" / "Lepton_BP";
    if (!std::filesystem::exists(bp / "manifest.json")) {
        log().warn("mobsoverworld: behaviour pack not found at {}, spawn rules not updated", bp.string());
        return;
    }
    auto const& c = config();
    try {
        ordered_json crab{
            {"minecraft:spawns_on_surface", ordered_json::object()},
            {"minecraft:spawns_on_block_filter", {"minecraft:sand", "minecraft:red_sand"}},
            {"minecraft:brightness_filter", {{"min", 9}, {"max", 15}, {"adjust_for_weather", false}}},
            {"minecraft:weight", {{"default", std::max(0, c.crabsSpawnConfig.spawnWeight)}}},
            {"minecraft:herd", {{"min_size", c.crabsSpawnConfig.minGroupSize}, {"max_size", c.crabsSpawnConfig.maxGroupSize}}},
            {"minecraft:biome_filter", biomeFilter(c.crabsSpawnConfig, nullptr, c.enableCrabs)},
        };
        writeJson(bp / "spawn_rules" / "lepton_crab.json", rule(kCrab, "animal", crab));

        ordered_json shiba{
            {"minecraft:spawns_on_surface", ordered_json::object()},
            {"minecraft:spawns_on_block_filter", {"minecraft:grass_block", "minecraft:snow_layer", "minecraft:snow"}},
            {"minecraft:brightness_filter", {{"min", 9}, {"max", 15}, {"adjust_for_weather", false}}},
            {"minecraft:weight", {{"default", std::max(0, c.shibaSpawnConfig.spawnWeight)}}},
            {"minecraft:herd", {{"min_size", c.shibaSpawnConfig.minGroupSize}, {"max_size", c.shibaSpawnConfig.maxGroupSize}}},
            {"minecraft:biome_filter", biomeFilter(c.shibaSpawnConfig, nullptr, c.enableShiba)},
        };
        writeJson(bp / "spawn_rules" / "lepton_shiba.json", rule(kShiba, "animal", shiba));

        ordered_json stone = ordered_json::array();
        for (auto s : kStoneBlocks) stone.push_back(std::string(s));
        ordered_json toretoise{
            {"minecraft:spawns_underground", ordered_json::object()},
            {"minecraft:spawns_on_block_filter", stone},
            {"minecraft:brightness_filter", {{"min", 0}, {"max", 7}, {"adjust_for_weather", true}}},
            {"minecraft:height_filter", {{"min", -64}, {"max", c.maxYLevel}}},
            {"minecraft:difficulty_filter", {{"min", "easy"}, {"max", "hard"}}},
            {"minecraft:weight", {{"default", std::max(0, c.toretoiseSpawnConfig.spawnWeight)}}},
            {"minecraft:herd", {{"min_size", c.toretoiseSpawnConfig.minGroupSize}, {"max_size", c.toretoiseSpawnConfig.maxGroupSize}}},
            {"minecraft:biome_filter", biomeFilter(c.toretoiseSpawnConfig, &c.dimensions, c.enableToretoise)},
        };
        writeJson(bp / "spawn_rules" / "lepton_toretoise.json", rule(kToretoise, "monster", toretoise));

        // The oretoise follows the configured foods.
        auto entity = bp / "entities" / "toretoise.json";
        if (std::filesystem::exists(entity)) {
            std::ifstream in(entity, std::ios::binary);
            auto          doc = ordered_json::parse(in, nullptr, false);
            in.close();
            if (!doc.is_discarded()) {
                ordered_json items = ordered_json::array();
                for (auto const& f : c.foods) items.push_back(f);
                auto& tempt = doc["minecraft:entity"]["components"]["minecraft:behavior.tempt"];
                if (tempt["items"] != items) {
                    tempt["items"] = items;
                    writeJson(entity, doc);
                }
            }
        }
        log().info("mobsoverworld: spawn rules written");
    } catch (std::exception const& e) {
        log().error("mobsoverworld: writing spawn rules failed: {}", e.what());
    }
}

bool allowed(std::string const& type, int dim, BlockPos const& pos, bool natural) {
    auto const& c = config();
    if (type == kCrab) {
        if (!c.enableCrabs) return false;
        if (!natural) return true;
        auto* region = game::regionOf(dim);
        if (!region) return true;
        BlockPos under{pos.x, pos.y - 1, pos.z};
        auto     id = game::blockIdAt(*region, under);
        if (id != "minecraft:sand" && id != "minecraft:red_sand") return false;
        auto light = region->getBrightnessPair(pos);
        return std::max(light.sky->mValue, light.block->mValue) > 8;
    }
    if (type == kShiba) return c.enableShiba;
    if (type == kToretoise) {
        if (!c.enableToretoise) return false;
        if (!natural) return true;
        if (difficulty() == 0 || pos.y > c.maxYLevel) return false;
        static constexpr std::string_view kDims[] = {"minecraft:overworld", "minecraft:the_nether", "minecraft:the_end"};
        std::string dimName = dim >= 0 && dim < 3 ? std::string(kDims[dim]) : "";
        bool listed = std::find(c.dimensions.dimensions.begin(), c.dimensions.dimensions.end(), dimName) != c.dimensions.dimensions.end();
        if (listed == c.dimensions.isBlacklist) return false;
        auto* region = game::regionOf(dim);
        if (!region) return true;
        auto id = game::blockIdAt(*region, BlockPos{pos.x, pos.y - 1, pos.z});
        return std::find(std::begin(kStoneBlocks), std::end(kStoneBlocks), id) != std::end(kStoneBlocks);
    }
    return true;
}

} // namespace lp::feature::mobsoverworld::spawning
