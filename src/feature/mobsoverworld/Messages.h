#pragma once

// Companion-protocol messages of the mobsoverworld unit.

#include <string_view>

namespace lp::feature::mobsoverworld {

/// Server -> mod client: the local player's Resilience effect (ticks 0 = ended).
struct EffectMessage {
    static constexpr std::string_view kId = "mobsoverworld:effect";
    int                               amplifier = 0;
    int                               ticks     = 0;
};

} // namespace lp::feature::mobsoverworld
