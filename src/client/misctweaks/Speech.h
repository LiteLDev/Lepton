#pragma once

// NarratorReadout's voice (client build): the Windows speech API on a worker thread.

#include <string>

namespace lp::client::misctweaks {

/// Speaks `utf8` asynchronously, interrupting what is being said (Java's `narrator.say(text, true)`).
/// Returns false when the speech API is unavailable.
bool say(std::string const& utf8);
/// Stops the worker thread (module uninstall).
void stopSpeech();

} // namespace lp::client::misctweaks
