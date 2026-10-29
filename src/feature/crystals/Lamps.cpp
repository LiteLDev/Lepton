// U12 crystals / RainbowLamps: crystal lamps lit by redstone (Java RainbowLampBlock: turn on at
// once, turn off 4 ticks after losing power), light level from the config.

#include <map>

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"

#include "feature/crystals/Common.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Module.h"

namespace lp::feature::crystals {

namespace bs = game::blockstates;

namespace {
bool gInstalled = false;
bool lampsOn() { return gInstalled && cfg().enableRainbowLamps; }

std::map<core::GlobalPos, uint64_t>& pendingOff() {
    static std::map<core::GlobalPos, uint64_t> m;
    return m;
}

/// Applies `lightLevel` to every lit lamp permutation (server lighting; the pack says 15).
void applyLightLevel() {
    int level = std::clamp(cfg().lightLevel, 0, 15);
    for (int c = 0; c < static_cast<int>(kColors.size()); ++c) {
        auto const* lit = bs::of(lampId(c), {{"lepton:lit", true}});
        if (!lit) continue;
        const_cast<Block&>(*lit).mDirectData->mLightEmission = Brightness{static_cast<uchar>(level)};
    }
}

void lampTick() {
    if (!lampsOn()) return;
    auto now = currentTick();
    std::vector<core::GlobalPos> list(tracked(Kind::Lamp).begin(), tracked(Kind::Lamp).end());
    for (auto const& gp : list) {
        auto* region = game::regionOf(gp.dim);
        if (!region) continue;
        BlockPos bp{gp.x, gp.y, gp.z};
        if (!region->hasChunksAt(bp, 0, false)) continue;
        auto const& b = region->getBlock(bp);
        if (!isLamp(b)) {
            untrack(Kind::Lamp, gp);
            continue;
        }
        bool lit     = bs::getBool(b, "lepton:lit").value_or(false);
        bool powered = redstonePower(*region, bp) > 0;
        if (powered == lit) {
            pendingOff().erase(gp);
            continue;
        }
        if (powered) {
            if (auto const* n = bs::with(b, {{"lepton:lit", true}})) setBlock(*region, bp, *n);
        } else {
            auto [it, inserted] = pendingOff().try_emplace(gp, now + 4);
            if (!inserted && it->second <= now) {
                pendingOff().erase(it);
                if (auto const* n = bs::with(b, {{"lepton:lit", false}})) setBlock(*region, bp, *n);
            }
        }
    }
}

class LampsModule {
public:
    void install() {
        gInstalled = true;
        static bool once = false;
        if (!once) {
            once = true;
            tickListeners().push_back([] { lampTick(); });
            config::onReload([] { applyLightLevel(); });
            game::db::onWorldReady([] { applyLightLevel(); });
            game::db::onWorldClosed([] { pendingOff().clear(); });
        }
        applyLightLevel();
    }
    void uninstall() {
        gInstalled = false;
        pendingOff().clear();
    }
};
} // namespace

} // namespace lp::feature::crystals

LP_REGISTER_MODULE(lp::feature::crystals::LampsModule);
