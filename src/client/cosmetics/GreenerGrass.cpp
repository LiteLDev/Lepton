// GreenerGrass (client): grass, foliage and water tints go through Quark's colour matrices
// (docs/spec/cosmetics.md §3). Hooks the block tessellation tint resolvers.
#ifdef LP_CLIENT

#include <algorithm>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/block/tessellation_pipeline/helper/client_block_pipeline/PositionAgnosticTintResolver.h"
#include "mc/client/renderer/block/tessellation_pipeline/helper/client_block_pipeline/PositionDependentTintResolver.h"
#include "mc/deps/core/math/Color.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/TintMethod.h"

#include "feature/cosmetics/Cosmetics.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::cosmetics {

namespace {

namespace cos = lp::feature::cosmetics;

enum class Kind : uint8_t { None, Grass, Leaves };

/// Block type -> kind, rebuilt when the config is reloaded. Meshing runs on worker threads.
struct Classifier {
    std::shared_mutex                               mutex;
    std::unordered_map<BlockType const*, Kind>      cache;
    std::unordered_set<std::string>                 grass, leaves;
    std::atomic<bool>                               dirty{true};

    void reload() {
        std::unique_lock lock(mutex);
        auto const& c = cos::config();
        grass         = {c.blockList.begin(), c.blockList.end()};
        leaves        = {c.leavesList.begin(), c.leavesList.end()};
        cache.clear();
        dirty = false;
    }

    Kind kindOf(Block const& block) {
        if (dirty) reload();
        BlockType const* type = &block.getBlockType();
        {
            std::shared_lock lock(mutex);
            auto             it = cache.find(type);
            if (it != cache.end()) return it->second;
        }
        std::unique_lock lock(mutex);
        auto const&      name = block.getTypeName();
        Kind             k    = grass.contains(name) ? Kind::Grass : leaves.contains(name) ? Kind::Leaves : Kind::None;
        cache.emplace(type, k);
        return k;
    }
};

Classifier& classifier() {
    static Classifier c;
    return c;
}

mce::Color apply(cos::ColorMatrix const& m, mce::Color const& in) {
    auto to8 = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    uint32_t argb = 0xFF000000u | (to8(in.r) << 16) | (to8(in.g) << 8) | to8(in.b);
    uint32_t out  = cos::convolve(m, argb);
    mce::Color c  = in;
    c.r           = ((out >> 16) & 0xFF) / 255.f;
    c.g           = ((out >> 8) & 0xFF) / 255.f;
    c.b           = (out & 0xFF) / 255.f;
    return c;
}

mce::Color adjust(Block const& block, TintMethod method, mce::Color const& color) {
    auto const& c = cos::config();
    if (!c.enableGreenerGrass) return color;
    if (method == TintMethod::Water) return c.affectWater ? apply(c.waterMatrix, color) : color;
    if (method == TintMethod::None) return color;
    switch (classifier().kindOf(block)) {
    case Kind::Grass:
        return apply(c.colorMatrix, color);
    case Kind::Leaves:
        return c.affectLeaves ? apply(c.colorMatrix, color) : color;
    default:
        return color;
    }
}

LL_TYPE_INSTANCE_HOOK(
    DependentTintHook,
    ll::memory::HookPriority::Normal,
    ClientBlockPipeline::PositionDependentTintResolver,
    &ClientBlockPipeline::PositionDependentTintResolver::$_get,
    mce::Color,
    Block const&    block,
    BlockPos const& pos,
    TintMethod      method,
    BiomeTintCache* cache
) {
    return adjust(block, method, origin(block, pos, method, cache));
}

LL_TYPE_INSTANCE_HOOK(
    AgnosticTintHook,
    ll::memory::HookPriority::Normal,
    ClientBlockPipeline::PositionAgnosticTintResolver,
    &ClientBlockPipeline::PositionAgnosticTintResolver::$_get,
    mce::Color,
    Block const&    block,
    BlockPos const& pos,
    TintMethod      method,
    BiomeTintCache* cache
) {
    return adjust(block, method, origin(block, pos, method, cache));
}

class GreenerGrass {
public:
    void install() {
        lp::config::onReload([] { classifier().dirty = true; });
        DependentTintHook::hook();
        AgnosticTintHook::hook();
    }
    void uninstall() {
        AgnosticTintHook::unhook();
        DependentTintHook::unhook();
    }
};

} // namespace

} // namespace lp::client::cosmetics

LP_REGISTER_MODULE(lp::client::cosmetics::GreenerGrass);

#endif // LP_CLIENT
