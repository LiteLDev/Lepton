// U29 worldbeyond: module entry, world generation hooks (docs/spec/worldbeyond.md).

#include <atomic>

#include "ll/api/memory/Hook.h"

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/levelgen/feature/NetherSpringFeature.h"
#include "mc/world/level/levelgen/v1/NetherGenerator.h"
#include "mc/world/level/levelgen/v1/OverworldGeneratorMultinoise.h"
#include "mc/world/level/levelgen/v1/TheEndGenerator.h"

#include "feature/worldbeyond/Blocks.h"
#include "feature/worldbeyond/Chorus.h"
#include "feature/worldbeyond/Config.h"
#include "feature/worldbeyond/Generators.h"
#include "feature/worldbeyond/Myalite.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::worldbeyond {

namespace {

std::atomic<bool> gLoggedDecoration[3]{};

void decorate(int dimId, LevelChunk& lc, BlockSource& source, TheEndGenerator const* endGen) {
    if (source.getLevel().isClientSide()) return;
    auto const& cp   = lc.mPosition.get();
    auto        seed = static_cast<int64_t>(source.getILevel().getLevelSeed64().mValue);
    decorateChunk(source, dimId, cp.x, cp.z, seed, endGen);
    if (dimId >= 0 && dimId < 3 && !gLoggedDecoration[dimId].exchange(true)) {
        log().info("worldbeyond: decorating chunks of dimension {}", dimId);
    }
}

LL_TYPE_INSTANCE_HOOK(
    EndDecorateHook,
    ll::memory::HookPriority::Normal,
    TheEndGenerator,
    &TheEndGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  source,
    Random&       random
) {
    origin(biome, lc, source, random);
    // The Nether and End generators share this function: act only in this hook's dimension.
    if (static_cast<int>(source.getDimensionId()) != 2) return;
    decorate(2, lc, source, this);
}

LL_TYPE_INSTANCE_HOOK(
    NetherDecorateHook,
    ll::memory::HookPriority::Normal,
    NetherGenerator,
    &NetherGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  source,
    Random&       random
) {
    origin(biome, lc, source, random);
    // The Nether and End generators share this function: act only in this hook's dimension.
    if (static_cast<int>(source.getDimensionId()) != 1) return;
    decorate(1, lc, source, nullptr);
}

LL_TYPE_INSTANCE_HOOK(
    OverworldDecorateHook,
    ll::memory::HookPriority::Normal,
    OverworldGeneratorMultinoise,
    &OverworldGeneratorMultinoise::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  source,
    Random&       random
) {
    origin(biome, lc, source, random);
    // The Nether and End generators share this function: act only in this hook's dimension.
    if (static_cast<int>(source.getDimensionId()) != 0) return;
    decorate(0, lc, source, nullptr);
}

/// NoMoreLavaPockets: Bedrock places the Nether lava springs (hidden in netherrack and open on one
/// side, Java's springs without `requiresBlockBelow`) with this feature.
LL_TYPE_INSTANCE_HOOK(
    NetherSpringHook,
    ll::memory::HookPriority::Normal,
    NetherSpringFeature,
    &NetherSpringFeature::$place,
    bool,
    BlockSource&    region,
    BlockPos const& pos,
    Random&         random
) {
    if (config().enableNoMoreLavaPockets && blocks().isLava(this->mBlock)) {
        countSpringRemoved();
        return false;
    }
    return origin(region, pos, random);
}

} // namespace

class WorldBeyondModule {
public:
    void install() {
        publishConfig();
        static bool registered = false;
        if (!registered) {
            registered = true;
            lp::config::onReload([] {
                publishConfig();
                resetCaches();
            });
            game::db::onWorldReady([] { resetBlocks(); });
            game::db::onWorldClosed([] { resetBlocks(); });
        }
        EndDecorateHook::hook();
        NetherDecorateHook::hook();
        OverworldDecorateHook::hook();
        NetherSpringHook::hook();
        installChorus();
        installMyalite();
    }

    void uninstall() {
        uninstallMyalite();
        uninstallChorus();
        NetherSpringHook::unhook();
        OverworldDecorateHook::unhook();
        NetherDecorateHook::unhook();
        EndDecorateHook::unhook();
    }
};

} // namespace lp::feature::worldbeyond

LP_REGISTER_MODULE(lp::feature::worldbeyond::WorldBeyondModule);
