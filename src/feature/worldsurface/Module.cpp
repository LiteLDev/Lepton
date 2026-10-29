// U28 worldsurface module: config, the chunk decoration hooks that run the generators, the server
// tick (scheduler, monster boxes, Glimmering Weald), world lifecycle and the /quark sub-commands.
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/LevelSeed64.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/levelgen/v1/NetherGenerator.h"
#include "mc/world/level/levelgen/v1/OverworldGeneratorMultinoise.h"
#include "mc/world/level/levelgen/v1/TheEndGenerator.h"

#include "feature/worldsurface/Climate.h"
#include "feature/worldsurface/Common.h"
#include "feature/worldsurface/Config.h"
#include "feature/worldsurface/Gen.h"
#include "feature/worldsurface/Runtime.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("worldsurface", lp::feature::worldsurface::Config);

namespace lp::feature::worldsurface {

// ---- scheduling ------------------------------------------------------------------------------------

namespace {
struct Task {
    uint64_t              due;
    std::function<void()> fn;
};
uint64_t          gTick = 0;
std::vector<Task> gTasks;
} // namespace

uint64_t tickCount() { return gTick; }

void later(int ticks, std::function<void()> fn) { gTasks.push_back({gTick + static_cast<uint64_t>(std::max(ticks, 1)), std::move(fn)}); }

void runCommand(std::string const& command, int dim) {
    auto level = ll::service::getLevel();
    if (!level) return;
    ServerCommandOrigin origin("lepton-worldsurface", level->asServer(), CommandPermissionLevel::Owner, DimensionType(dim));
    ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
}

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

// ---- generation ----------------------------------------------------------------------------------------

namespace {

std::mutex gResolveMutex;

/// The generation state; resolves the custom blocks when the world's packs were not loaded at the
/// last rebuild (client: chunks generate before "world ready").
std::shared_ptr<GenState const> stateForGen() {
    auto st = genState();
    if (st->monsterBox || !st->config.enableMonsterBox) return st;
    // at most one retry per 5 seconds (a missing behaviour pack must not rebuild per chunk)
    static std::atomic<int64_t> lastTry{0};
    int64_t now  = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t last = lastTry.load();
    if (now - last < 5000 || !lastTry.compare_exchange_strong(last, now)) return st;
    std::lock_guard lock(gResolveMutex);
    st = genState();
    if (!st->monsterBox) {
        rebuildGenState(st->config, /*quiet=*/true);
        st = genState();
    }
    return st;
}

void runChunk(BlockSource& region, LevelChunk& lc, int dim, OverworldGeneratorMultinoise const* overworld) {
    try {
        auto        st = stateForGen();
        auto const& c  = st->config;
        ChunkPos    cp = *lc.mPosition;
        int         x0 = cp.x * 16, z0 = cp.z * 16;
        uint64_t    seed = region.getLevelSeed64().mValue;
        gen::stats().chunks++;
        std::vector<BlockPos> boxes;

        auto make = [&](Rng& rng) {
            gen::Ctx ctx{region, *st, rng, dim, {}, &boxes};
            return ctx;
        };

        // ---- UNDERGROUND_DECORATION: Glimmering Weald features, monster boxes ----
        if (c.enableGlimmeringWeald && overworld && dim == 0
            && climate::chunkMayContain(*overworld, x0, z0, region.getMinHeight(), 64)) {
            Rng                              rng(chunkSeed(seed, cp.x, cp.z, 1));
            auto                             ctx = make(rng);
            std::unordered_map<int64_t, bool> cache;
            ctx.isGlimmering = [&](BlockPos const& p) {
                int64_t key = (static_cast<int64_t>(p.x >> 2) & 0xFFFFF) | ((static_cast<int64_t>(p.z >> 2) & 0xFFFFF) << 20)
                            | ((static_cast<int64_t>(p.y >> 2) & 0xFFF) << 40);
                if (auto it = cache.find(key); it != cache.end()) return it->second;
                bool v     = climate::isGlimmeringWeald(*overworld, &region, p);
                cache[key] = v;
                return v;
            };
            gen::glimmeringChunk(ctx, x0, z0);
        }
        if (c.enableMonsterBox && dimensionAllowed(c.monsterBoxDimensions, dim)) {
            Rng  rng(chunkSeed(seed, cp.x, cp.z, 2));
            auto ctx = make(rng);
            gen::monsterBoxChunk(ctx, x0, z0);
        }
        // ---- TOP_LAYER_MODIFICATION: fairy rings, fallen logs, water petals ----
        if (c.enableFairyRings && dimensionAllowed(c.fairyRingsDimensions, dim)) {
            Rng  rng(chunkSeed(seed, cp.x, cp.z, 3));
            auto ctx = make(rng);
            gen::fairyRingsChunk(ctx, x0, z0);
        }
        if (c.enableFallenLogs && dimensionAllowed(c.fallenLogsDimensions, dim)) {
            Rng  rng(chunkSeed(seed, cp.x, cp.z, 4));
            auto ctx = make(rng);
            gen::fallenLogsChunk(ctx, x0, z0);
        }
        if (c.enableCherryGroveWaterPetals && dimensionAllowed(c.cherryGroveWaterPetalsDimensions, dim)) {
            Rng  rng(chunkSeed(seed, cp.x, cp.z, 5));
            auto ctx = make(rng);
            gen::petalsChunk(ctx, x0, z0);
        }
        box::queueGenerated(dim, boxes);
    } catch (std::exception const& e) {
        log().error("worldsurface: chunk generation failed: {}", e.what());
    } catch (...) {
        log().error("worldsurface: chunk generation failed");
    }
}

LL_TYPE_INSTANCE_HOOK(
    OverworldDecorateHook,
    ll::memory::HookPriority::Normal,
    OverworldGeneratorMultinoise,
    &OverworldGeneratorMultinoise::$decorateWorldGenPostProcess,
    void,
    Biome const& biome,
    LevelChunk&  lc,
    BlockSource& source,
    Random&      random
) {
    origin(biome, lc, source, random);
    climate::setGenerator(this);
    runChunk(source, lc, static_cast<int>(source.getDimensionId()), this);
}

LL_TYPE_INSTANCE_HOOK(
    NetherDecorateHook,
    ll::memory::HookPriority::Normal,
    NetherGenerator,
    &NetherGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const& biome,
    LevelChunk&  lc,
    BlockSource& source,
    Random&      random
) {
    origin(biome, lc, source, random);
    runChunk(source, lc, static_cast<int>(source.getDimensionId()), nullptr);
}

LL_TYPE_INSTANCE_HOOK(
    EndDecorateHook,
    ll::memory::HookPriority::Normal,
    TheEndGenerator,
    &TheEndGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const& biome,
    LevelChunk&  lc,
    BlockSource& source,
    Random&      random
) {
    origin(biome, lc, source, random);
    runChunk(source, lc, static_cast<int>(source.getDimensionId()), nullptr);
}

// ---- tick ----------------------------------------------------------------------------------------------

void onTick(ll::event::ServerLevelTickEvent&) {
    ++gTick;
    if (!gTasks.empty()) {
        std::vector<Task> due;
        for (auto it = gTasks.begin(); it != gTasks.end();) {
            if (it->due <= gTick) {
                due.push_back(std::move(*it));
                it = gTasks.erase(it);
            } else ++it;
        }
        for (auto& t : due) {
            try {
                t.fn();
            } catch (std::exception const& e) {
                log().error("worldsurface: task failed: {}", e.what());
            }
        }
    }
    try {
        box::tick();
        glimmer::tick();
    } catch (std::exception const& e) {
        log().error("worldsurface: tick failed: {}", e.what());
    }
}

// ---- commands ------------------------------------------------------------------------------------------

std::string fmtPos(BlockPos const& p) { return std::format("{} {} {}", p.x, p.y, p.z); }

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("locate").text("glimmering_weald").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
        auto const& biome = tr("biome.quark.glimmering_weald", "Glimmering Weald");
        auto const* gen   = climate::generator();
        if (!gen) return output.error(tr("lp.worldsurface.locate.unsupported", "{0} needs a normal (non-flat) Overworld", biome));
        Vec3     w = origin.getWorldPosition();
        BlockPos from{static_cast<int>(std::floor(w.x)), static_cast<int>(std::floor(w.y)), static_cast<int>(std::floor(w.z))};
        auto     found = climate::locate(*gen, from, 6400, 64, -56, 64);
        if (!found) return output.error(tr("lp.worldsurface.locate.none", "Could not find a {0} within {1} blocks", biome, 6400));
        int dist = static_cast<int>(std::sqrt(double(found->x - from.x) * (found->x - from.x) + double(found->z - from.z) * (found->z - from.z)));
        log().info("worldsurface: located glimmering_weald at {}", fmtPos(*found));
        output.success(tr("lp.worldsurface.locate.found", "The nearest {0} is at {1} ({2} blocks away)", biome, fmtPos(*found), dist));
    });
    cmd.overload().text("locate").text("monster_box").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
        auto const& what = tr("lp.worldsurface.monster_box", "Monster Box");
        Vec3        w    = origin.getWorldPosition();
        int         dim  = static_cast<int>(origin.getDimension() ? origin.getDimension()->getDimensionId() : DimensionType(0));
        std::optional<core::GlobalPos> best;
        double                          bestD = 1e18;
        for (auto const& p : box::registry()) {
            if (p.dim != dim) continue;
            double dx = p.x - w.x, dy = p.y - w.y, dz = p.z - w.z, d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) {
                bestD = d;
                best  = p;
            }
        }
        if (!best) return output.error(tr("lp.worldsurface.locate.none", "Could not find a {0} within {1} blocks", what, "-"));
        auto pos = game::blockPos(*best);
        log().info("worldsurface: located monster_box at {}", fmtPos(pos));
        output.success(tr("lp.worldsurface.locate.found", "The nearest {0} is at {1} ({2} blocks away)", what, fmtPos(pos), static_cast<int>(std::sqrt(bestD))));
    });
    cmd.overload().text("worldgen").text("stats").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
        auto& s    = gen::stats();
        auto  line = std::format(
            "worldsurface gen: chunks={} fairyRings={} fallenLogs={} monsterBoxes={} petalPatches={} glimmeringChunks={} hugeShrooms={} glowShrooms={} lapisVeins={} boxesKnown={}",
            s.chunks.load(),
            s.fairyRings.load(),
            s.fallenLogs.load(),
            s.monsterBoxes.load(),
            s.petalPatches.load(),
            s.glimmeringChunks.load(),
            s.hugeShrooms.load(),
            s.glowShrooms.load(),
            s.lapisVeins.load(),
            box::registry().size()
        );
        log().info("{}", line);
        output.success(line);
    });
});

} // namespace

class WorldsurfaceModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        box::install();
        glimmer::install();
        OverworldDecorateHook::hook();
        NetherDecorateHook::hook();
        EndDecorateHook::hook();
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] {
                rebuildGenState();
                box::onWorldReady();
            });
            game::db::onWorldSave([] { box::onWorldSave(); });
            game::db::onWorldClosed([] {
                box::onWorldClosed();
                glimmer::onWorldClosed();
                climate::reset();
                gTasks.clear();
            });
            config::onReload([] { rebuildGenState(); });
        }
        if (game::db::available()) {
            rebuildGenState();
            box::onWorldReady();
        }
        log().debug("worldsurface: installed");
    }
    void uninstall() {
        OverworldDecorateHook::unhook();
        NetherDecorateHook::unhook();
        EndDecorateHook::unhook();
        glimmer::uninstall();
        box::uninstall();
        box::onWorldSave();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        gTasks.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::feature::worldsurface

LP_REGISTER_MODULE(lp::feature::worldsurface::WorldsurfaceModule);
