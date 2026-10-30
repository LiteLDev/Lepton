// SoulCandles (client): candles on soul-fire bases burn with soul flames (docs/spec/cosmetics.md §3).
// The vanilla candle tick runs as usual; while it runs, its particles are swapped.
#ifdef LP_CLIENT

#include <random>
#include <string>
#include <string_view>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/AbstractCandleBlock.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockAnimateTickData.h"

#include "mc/comprehensive/ParticleType.h"
#include "feature/cosmetics/Cosmetics.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::cosmetics {

namespace {

namespace cos = lp::feature::cosmetics;

thread_local bool tSoulCandle = false;

bool isSoulBase(std::string_view name) { return name == "minecraft:soul_sand" || name == "minecraft:soul_soil"; }
/// Java: a block with enchanting power (bookshelves) between the candle and the soul base.
bool givesEnchantPower(std::string_view name) {
    return name == "minecraft:bookshelf" || (name.starts_with("lepton:") && name.ends_with("_bookshelf"));
}

bool onSoulBase(BlockSource& region, BlockPos const& pos) {
    BlockPos below{pos.x, pos.y - 1, pos.z};
    auto     name = region.getBlock(below).getTypeName();
    if (!isSoulBase(name) && givesEnchantPower(name)) {
        below.y -= 1;
        name = region.getBlock(below).getTypeName();
    }
    return isSoulBase(name);
}

float random01() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    return std::uniform_real_distribution<float>(0.f, 1.f)(rng);
}

LL_TYPE_INSTANCE_HOOK(
    CandleTickHook,
    ll::memory::HookPriority::Normal,
    AbstractCandleBlock,
    &AbstractCandleBlock::$animateTickBedrockLegacy,
    void,
    BlockAnimateTickData const& data
) {
    auto const& c = cos::config();
    bool        soul = false;
    try {
        soul = c.enableSoulCandles && onSoulBase(data.mRegion, data.mPos);
    } catch (...) {}
    if (!soul) return origin(data);
    tSoulCandle = true;
    try {
        origin(data);
    } catch (...) {
        tSoulCandle = false;
        throw;
    }
    tSoulCandle = false;
}

LL_TYPE_INSTANCE_HOOK(
    AddParticleHook,
    ll::memory::HookPriority::Normal,
    Level,
    &Level::$addParticle,
    ::Particle*,
    ::ParticleType     type,
    ::Vec3 const&      pos,
    ::Vec3 const&      dir,
    int                data,
    ::CompoundTag const* tag,
    bool               isGlobal
) {
    if (tSoulCandle) {
        if (type == ParticleType::CandleFlame || type == ParticleType::Flame) {
            // The candle flame is a long-lived particle; the soul one keeps its size and lifetime
            // (lepton:soul_candle_flame in the resource pack) with the soul fire flame sprite.
            static HashedString const soulFlame("lepton:soul_candle_flame");

            static_cast<ILevel*>(this)->addParticleEffect(soulFlame, pos);
            return nullptr;
        }
        if (type == ParticleType::Smoke && random01() < 0.1f) type = ParticleType::Soul;
    }
    return origin(type, pos, dir, data, tag, isGlobal);
}

LL_TYPE_INSTANCE_HOOK(
    AddParticleEffectHook,
    ll::memory::HookPriority::Normal,
    Level,
    &Level::$addParticleEffect,
    void,
    ::HashedString const&      effect,
    ::Vec3 const&              pos,
    ::MolangVariableMap const& vars
) {
    if (tSoulCandle && !effect.getString().starts_with("lepton:")) {
        std::string_view name = effect.getString();
        if (name.find("candle_flame") != std::string_view::npos || name.find("basic_flame") != std::string_view::npos) {
            static HashedString const soulFlame("lepton:soul_candle_flame");
            return origin(soulFlame, pos, vars);
        }
        if (name.find("smoke") != std::string_view::npos && random01() < 0.1f) {
            static HashedString const soul("minecraft:soul_particle");
            return origin(soul, pos, vars);
        }
    }
    origin(effect, pos, vars);
}

class SoulCandles {
public:
    void install() {
        CandleTickHook::hook();
        AddParticleHook::hook();
        AddParticleEffectHook::hook();
    }
    void uninstall() {
        AddParticleEffectHook::unhook();
        AddParticleHook::unhook();
        CandleTickHook::unhook();
    }
};

} // namespace

} // namespace lp::client::cosmetics

LP_REGISTER_MODULE(lp::client::cosmetics::SoulCandles);

#endif // LP_CLIENT
