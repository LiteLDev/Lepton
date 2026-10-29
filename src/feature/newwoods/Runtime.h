#pragma once

// Internal wiring of the newwoods sources (not used by other units).

#include "feature/newwoods/JavaRandom.h"

class Level;

namespace lp::feature::newwoods {

/// Random source of the server thread (gameplay rolls: drops, growth, bone meal).
[[nodiscard]] JavaRandom& runtimeRandom();
/// True on the server thread (recorded each server tick).
[[nodiscard]] bool onServerThread();

void leafParticlesTick(Level& level);
/// Ticks that fired off the server thread, replayed on it.
void runDeferredTicks();

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
