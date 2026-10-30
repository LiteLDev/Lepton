#pragma once

// Internal wiring of the newwoods sources (not used by other units).

#include <functional>
#include <memory>

#include "feature/newwoods/JavaRandom.h"

class Level;

namespace lp::feature::newwoods {

struct NewWoodsConfig;
/// The config as seen by the world generation threads: a copy published on the server thread at
/// world load and `/quark reload` (the live config may change under them).
[[nodiscard]] std::shared_ptr<NewWoodsConfig const> genConfig();
void                                              publishGenConfig();

/// Random source of the server thread (gameplay rolls: drops, growth, bone meal).
[[nodiscard]] JavaRandom& runtimeRandom();
/// True on the server thread (recorded each server tick).
[[nodiscard]] bool onServerThread();

void leafParticlesTick(Level& level);
/// Ticks that fired off the server thread, replayed on it.
void runDeferredTicks();

/// Runs `fn` on the server thread at the next tick (world edits after a cancelled interaction).
void defer(std::function<void()> fn);
void runDeferred();

void installPottedHooks();
void uninstallPottedHooks();
void installLeafHooks();
void uninstallLeafHooks();
void installSaplingHooks();
void uninstallSaplingHooks();
void installWorldGenHooks();
void uninstallWorldGenHooks();
void installLoadHooks();   ///< recipes, loot tables (at mod load)
void installGameHooks();   ///< fuel, compost, eating, azalea growth
void uninstallGameHooks();

} // namespace lp::feature::newwoods
