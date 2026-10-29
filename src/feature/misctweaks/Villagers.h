#pragma once

#include <cstdint>

class Actor;
class EconomyTradeableComponent;

namespace lp::feature::misctweaks {

/// The villager's trade seed (created and stored in the world DB on first use).
uint32_t villagerSeed(Actor& villager);
/// Quark's reroll attempt on `comp`'s offers. Returns how many trades changed.
int attemptReroll(EconomyTradeableComponent& comp);

} // namespace lp::feature::misctweaks
