// SpawnerReplacer (experimental): spawners of a configured mob switch to another mob.

#include <atomic>
#include <map>
#include <mutex>

#include "ll/api/memory/Hook.h"

#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/MobSpawnerBlockActor.h"

#include "feature/misctweaks/Internal.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace lp::feature::misctweaks {

std::string normalizeId(std::string_view id) {
    std::string s;
    for (char ch : id)
        if (ch != ' ') s += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (!s.empty() && s.find(':') == std::string::npos) s = "minecraft:" + s;
    return s;
}

namespace {

std::mutex                         gMapMutex;
std::map<std::string, std::string> gMap;
std::vector<std::string>           gMapSource; // the config list the map was built from

void rebuildIfNeeded() {
    auto const& list = cfg().replaceMobs;
    if (list == gMapSource) return;
    gMapSource = list;
    gMap.clear();
    for (auto const& entry : list) {
        auto comma = entry.find(',');
        if (comma == std::string::npos || entry.find(',', comma + 1) != std::string::npos) continue;
        auto from = normalizeId(std::string_view(entry).substr(0, comma));
        auto to   = normalizeId(std::string_view(entry).substr(comma + 1));
        if (!from.empty() && !to.empty()) gMap[from] = to;
    }
}

} // namespace

std::string spawnerReplacement(std::string_view from) {
    std::lock_guard lock(gMapMutex);
    rebuildIfNeeded();
    auto it = gMap.find(normalizeId(from));
    return it == gMap.end() ? std::string{} : it->second;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    SpawnerTickHook,
    ll::memory::HookPriority::Normal,
    MobSpawnerBlockActor,
    &MobSpawnerBlockActor::$tick,
    void,
    BlockSource& region
) {
    auto const& c = cfg();
    if (c.enableSpawnerReplacer && !c.replaceMobs.empty() && !region.getLevel().isClientSide()) {
        if (auto* spawner = mSpawner.get()) {
            auto const& def  = spawner->mActorDefId.get();
            std::string from = def.mNamespace.get().empty() ? def.mIdentifier.get() : def.mNamespace.get() + ":" + def.mIdentifier.get();
            if (auto to = spawnerReplacement(from); !to.empty() && to != normalizeId(from)) {
                setMob(region, ActorDefinitionIdentifier(to), nullptr);
                auto const& p = mPosition.get();
                log().info("misctweaks: spawner at {} {} {} {} -> {}", p.x, p.y, p.z, from, to);
            }
        }
    }
    origin(region);
}

} // namespace

void installSpawners() { SpawnerTickHook::hook(); }
void uninstallSpawners() { SpawnerTickHook::unhook(); }

} // namespace lp::feature::misctweaks
