#pragma once

class Actor;
class EconomyTradeableComponent;

namespace lp::feature::misctweaks {

/// Quark's reroll attempt on `comp`'s offers. Returns how many trades changed.
int attemptReroll(EconomyTradeableComponent& comp);

} // namespace lp::feature::misctweaks
