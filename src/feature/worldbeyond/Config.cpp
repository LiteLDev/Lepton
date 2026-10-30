#include "feature/worldbeyond/Config.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>

#include "nlohmann/json.hpp"

#include "mod/Config.h"

LP_REGISTER_CONFIG("worldbeyond", lp::feature::worldbeyond::WorldBeyondConfig);

namespace lp::feature::worldbeyond {

namespace {

bool contains(std::vector<std::string> const& list, std::string_view v) {
    return std::find(list.begin(), list.end(), v) != list.end();
}

/// Java biome tags Lepton can answer for a dimension.
bool hasTag(std::string_view tag, int dimId) {
    auto is = [&](std::string_view suffix) {
        return tag == std::string("minecraft:") + std::string(suffix) || tag == std::string("c:") + std::string(suffix);
    };
    switch (dimId) {
    case 0:
        return is("is_overworld");
    case 1:
        return is("is_nether");
    case 2:
        return is("is_end");
    default:
        return false;
    }
}

struct LanternCache {
    std::mutex mutex;
    bool       loaded  = false;
    bool       enabled = true;
};
LanternCache& lantern() {
    static LanternCache c;
    return c;
}

} // namespace

bool DimensionConfig::canSpawnHere(int dimId) const { return contains(dimensions, dimensionName(dimId)) != isBlacklist; }

bool CompoundBiomeConfig::canSpawn(std::string_view biome, int dimId) const {
    // BiomeTagConfig: the biome must (not) carry one of the tags; StrictBiomeConfig: by name
    bool anyTag = std::any_of(tags.tags.begin(), tags.tags.end(), [&](auto const& t) { return hasTag(t, dimId); });
    bool tagOk  = anyTag != tags.isBlacklist;
    bool nameOk = contains(biomes.biomes, biome) != biomes.isBlacklist;
    return tagOk && nameOk;
}

namespace {
/// Published snapshots: the current one behind an atomic pointer (lock-free reads from every
/// thread); earlier ones stay alive (a reload or a selftest change publishes a new one, so callers
/// may keep references across calls).
struct Snapshot {
    std::mutex                                            mutex;
    std::vector<std::unique_ptr<WorldBeyondConfig const>> all;
    std::atomic<WorldBeyondConfig const*>                 current{nullptr};
};
Snapshot& snapshot() {
    static Snapshot s;
    return s;
}
} // namespace

WorldBeyondConfig const& config() {
    auto& s = snapshot();
    if (auto const* c = s.current.load(std::memory_order_acquire)) return *c;
    publishConfig();
    return *s.current.load(std::memory_order_acquire);
}

void publishConfig() {
    auto            fresh = std::make_unique<WorldBeyondConfig const>(lp::config::get<WorldBeyondConfig>());
    auto&           s     = snapshot();
    std::lock_guard lock(s.mutex);
    s.current.store(fresh.get(), std::memory_order_release);
    s.all.push_back(std::move(fresh));
}

std::string dimensionName(int dimId) {
    switch (dimId) {
    case 0:
        return "minecraft:overworld";
    case 1:
        return "minecraft:the_nether";
    case 2:
        return "minecraft:the_end";
    default:
        return "lepton:dimension_" + std::to_string(dimId);
    }
}

bool blazeLanternEnabled() {
    auto&           c = lantern();
    std::lock_guard lock(c.mutex);
    if (!c.loaded) {
        c.loaded  = true;
        c.enabled = true;
        // U8 (CompressedBlocks) owns `config/storage.json`; read by key so this unit does not
        // depend on U8's code. A missing file or key means Quark's default (enabled).
        std::ifstream in(lp::config::configDir() / "storage.json");
        if (in) {
            auto j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_object()) {
                c.enabled = j.value("enableCompressedBlocks", true) && j.value("enableBlazeLantern", true);
            }
        }
    }
    return c.enabled;
}

void resetCaches() {
    auto&           c = lantern();
    std::lock_guard lock(c.mutex);
    c.loaded = false;
}

} // namespace lp::feature::worldbeyond
