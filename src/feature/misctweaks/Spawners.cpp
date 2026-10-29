// SpawnerReplacer (experimental): spawners of a configured mob switch to another mob.

#include <atomic>
#include <map>
#include <mutex>

#include "ll/api/memory/Hook.h"

#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/level/BaseMobSpawner.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
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

std::string spawnerMob(MobSpawnerBlockActor const& spawner) {
    // Through save(): the spawner's member layout is not reliable in the SDK headers.
    auto        ctx = SaveContextFactory::createCloneSaveContext();
    CompoundTag tag;
    if (!ctx || !spawner.save(tag, *ctx) || !tag.contains("EntityIdentifier", Tag::String)) return {};
    return normalizeId(tag.at("EntityIdentifier").get<StringTag>());
}

namespace {

std::string mobOf(BaseMobSpawner& spawner) {
    CompoundTag tag;
    spawner.save(tag);
    if (!tag.contains("EntityIdentifier", Tag::String)) return {};
    return normalizeId(tag.at("EntityIdentifier").get<StringTag>());
}

// The game ticks a monster spawner block through its BaseMobSpawner (MobSpawnerBlockActor::tick
// resolves to the same code), so the hook sits there and finds the block actor by position. Only
// spawner blocks are replaced, as in Quark (not spawner minecarts or trial spawners).
LL_TYPE_INSTANCE_HOOK(
    SpawnerTickHook,
    ll::memory::HookPriority::Normal,
    BaseMobSpawner,
    &BaseMobSpawner::$tick,
    void,
    BlockSource& region
) {
    auto const& c = cfg();
    if (c.enableSpawnerReplacer && !c.replaceMobs.empty() && !region.getLevel().isClientSide()) {
        std::string from = mobOf(*this);
        if (auto to = spawnerReplacement(from); !from.empty() && !to.empty() && to != from) {
            BlockPos const pos = getPos();
            if (region.getBlock(pos).getTypeName() == "minecraft:mob_spawner") {
                if (auto* be = region.getBlockEntity(pos)) {
                    static_cast<MobSpawnerBlockActor*>(be)->setMob(region, ActorDefinitionIdentifier(to), nullptr);
                    log().info("misctweaks: spawner at {} {} {} {} -> {}", pos.x, pos.y, pos.z, from, to);
                }
            }
        }
    }
    origin(region);
}

} // namespace
void installSpawners() { SpawnerTickHook::hook(); }
void uninstallSpawners() { SpawnerTickHook::unhook(); }

} // namespace lp::feature::misctweaks
