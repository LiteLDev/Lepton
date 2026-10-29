// WoolShutsUpMinecarts (client): a minecart over wool or a wool carpet makes no rolling sound
// (docs/spec/cosmetics.md §3). The cart's looping sounds are wrapped when they are registered.
#ifdef LP_CLIENT

#include <cmath>
#include <functional>
#include <string>
#include <string_view>

#include "ll/api/memory/Hook.h"

#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/sound/LoopingSoundState.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/Minecart.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/cosmetics/Cosmetics.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::cosmetics {

/// Wool and wool carpets (Java's `dampens_vibrations` block tag).
bool dampensVibrations(std::string_view name) {
    if (!name.starts_with("minecraft:")) return false;
    if (name.ends_with("_wool")) return true;
    return name.ends_with("_carpet") && name.find("moss") == std::string_view::npos;
}

/// Quark: the cart's box moved down by its own height touches a dampening block.
bool overWool(Actor& cart) {
    auto const& box    = cart.getAABB();
    auto&       region = cart.getDimensionBlockSource();
    double      h      = box.max.y - box.min.y;
    int         x0 = static_cast<int>(std::floor(box.min.x)), x1 = static_cast<int>(std::floor(box.max.x));
    int         y0 = static_cast<int>(std::floor(box.min.y - h)), y1 = static_cast<int>(std::floor(box.min.y));
    int         z0 = static_cast<int>(std::floor(box.min.z)), z1 = static_cast<int>(std::floor(box.max.z));
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (dampensVibrations(region.getBlock(BlockPos{x, y, z}).getTypeName())) return true;
    return false;
}

namespace {

namespace cos = lp::feature::cosmetics;

thread_local Minecart* tRegistering = nullptr;

LL_TYPE_INSTANCE_HOOK(RegisterCartSoundsHook, ll::memory::HookPriority::Normal, Minecart, &Minecart::_registerLoopingSounds, void) {
    tRegistering = this;
    try {
        origin();
    } catch (...) {
        tRegistering = nullptr;
        throw;
    }
    tRegistering = nullptr;
}

LL_TYPE_INSTANCE_HOOK(
    RegisterLoopHook,
    ll::memory::HookPriority::Normal,
    SoundEngine,
    &SoundEngine::$registerLoop,
    uint64,
    std::string const&                        name,
    std::function<void(::LoopingSoundState&)> getSoundState,
    float                                     fadeIn,
    float                                     fadeOut
) {
    if (Minecart* cart = tRegistering) {
        // The vanilla callback captures the cart too: the loop is unregistered before the cart dies.
        auto wrapped = [inner = std::move(getSoundState), cart](::LoopingSoundState& state) {
            if (inner) inner(state);
            try {
                if (cos::config().enableWoolShutsUpMinecarts && overWool(*cart)) {
                    // The state starts with the emitter position: put it far out of hearing range
                    // (a fixed point, so a callback that keeps the old position cannot drift).
                    *reinterpret_cast<Vec3*>(&state) = Vec3{0.f, 1.0e6f, 0.f};
                }
            } catch (...) {}
        };
        return origin(name, std::move(wrapped), fadeIn, fadeOut);
    }
    return origin(name, std::move(getSoundState), fadeIn, fadeOut);
}

class QuietMinecarts {
public:
    void install() {
        RegisterCartSoundsHook::hook();
        RegisterLoopHook::hook();
    }
    void uninstall() {
        RegisterLoopHook::unhook();
        RegisterCartSoundsHook::unhook();
    }
};

} // namespace

} // namespace lp::client::cosmetics

LP_REGISTER_MODULE(lp::client::cosmetics::QuietMinecarts);

#endif // LP_CLIENT
