// ClimateControlRemover (experimental): disabled climate parameters stop influencing biome choice.

#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/world/level/biome/BiomeNoiseTarget.h"
#include "mc/world/level/biome/ClimateParameters.h"
#include "mc/world/level/biome/ParameterList.h"
#include "mc/world/level/biome/climate_utils/Parameter.h"

#include "feature/misctweaks/Climate.h"
#include "feature/misctweaks/Internal.h"
#include "mod/Log.h"

namespace lp::feature::misctweaks {

bool neutralizeClimate(ClimateParameters& p) {
    auto const& c = cfg();
    if (!c.enableClimateControlRemover) return false;
    ClimateUtils::Parameter dummy(int64{0}, int64{0});
    auto&                   space   = p.mParameterSpace.get();
    bool                    changed = false;
    auto                    apply   = [&](bool disabled, auto& field, size_t index) {
        if (!disabled) return;
        field        = dummy;
        space[index] = dummy;
        changed      = true;
    };
    // Parameter space order (as Java's Climate.ParameterPoint): temperature, humidity,
    // continentalness, erosion, depth, weirdness, offset.
    apply(c.disableTemperature, p.mTemperature.get(), 0);
    apply(c.disableHumidity, p.mHumidity.get(), 1);
    apply(c.disableContinentalness, p.mContinentalness.get(), 2);
    apply(c.disableErosion, p.mErosion.get(), 3);
    apply(c.disableDepth, p.mDepth.get(), 4);
    apply(c.disableWeirdness, p.mWeirdness.get(), 5);
    if (c.disableOffset) {
        p.mOffset = int64{0};
        space[6]  = dummy;
        changed   = true;
    }
    return changed;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    ParameterListCtorHook,
    ll::memory::HookPriority::Normal,
    ParameterList,
    &ParameterList::$ctor,
    void*,
    std::vector<BiomeNoiseTarget> const& targets
) {
    if (!cfg().enableClimateControlRemover) return origin(targets);
    std::vector<BiomeNoiseTarget> copy = targets;
    int                           n    = 0;
    for (auto& t : copy)
        if (neutralizeClimate(t.climateParameters.get())) ++n;
    log().info("misctweaks: climate control remover adjusted {} biome targets", n);
    return origin(copy);
}

} // namespace

void installClimate() { ParameterListCtorHook::hook(); }
void uninstallClimate() { ParameterListCtorHook::unhook(); }

} // namespace lp::feature::misctweaks
