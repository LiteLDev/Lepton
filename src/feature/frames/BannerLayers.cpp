// U24 MoreBannerLayers (Quark tweaks). Quark lets the loom (and the banner copy recipe and the
// tooltip) go up to `layerLimit` layers. Bedrock's banner renderer draws at most 6 layers, so
// the effective limit is min(layerLimit, 6): lower limits are enforced in the loom
// (CraftHandlerLoom) and in the crafting-table pattern recipe (BannerAddPatternRecipe). See
// docs/spec/frames.md §3.3 and §7.3.

#include "feature/frames/Frames.h"

#include <algorithm>

#include "ll/api/memory/Hook.h"

#include "mc/world/PlayerUIContainer.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/CraftingContainer.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerBase.h"
#include "mc/world/inventory/network/crafting/CraftHandlerLoom.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftBase.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftHandler.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/BannerAddPatternRecipe.h"
#include "mc/world/item/crafting/CraftingContext.h"
#include "mc/world/level/block/actor/BannerBlockActor.h"

#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::frames::banners {

namespace {
constexpr int kBedrockMaxLayers = 6; // the banner renderer draws 6 pattern layers

int patternsOf(ItemStack const& stack) {
    if (game::isEmpty(stack) || game::itemId(stack) != "minecraft:banner") return 0;
    return BannerBlockActor::getPatternCount(stack.mUserData.get());
}
} // namespace

int effectiveLimit() {
    if (!cfg().enableMoreBannerLayers) return kBedrockMaxLayers;
    return std::min(std::clamp(cfg().layerLimit, 1, 16), kBedrockMaxLayers);
}

bool mayAddPattern(int patterns) { return patterns < effectiveLimit(); }

namespace {

LL_TYPE_INSTANCE_HOOK(
    LoomCraftHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerLoom,
    &CraftHandlerLoom::$_handleCraftAction,
    ItemStackNetResult,
    ItemStackRequestActionCraftBase const& requestAction
) {
    if (effectiveLimit() < kBedrockMaxLayers) {
        Player& player = this->mCraftRequestHandler.mPlayer;
        auto const& banner = player.mPlayerUIContainer->getItem(static_cast<int>(PlayerUISlot::LoomInput));
        int  n      = patternsOf(banner);
        if (!mayAddPattern(n)) {
            log().info("frames: loom refused a banner with {} layers (limit {})", n, effectiveLimit());
            return ItemStackNetResult::InvalidCraftRequest;
        }
    }
    return origin(requestAction);
}

LL_TYPE_INSTANCE_HOOK(
    BannerRecipeHook,
    ll::memory::HookPriority::Normal,
    BannerAddPatternRecipe,
    &BannerAddPatternRecipe::$matches,
    bool,
    CraftingContainer const& craftSlots,
    CraftingContext const&   context
) {
    if (effectiveLimit() < kBedrockMaxLayers) {
        for (int i = 0; i < craftSlots.getContainerSize(); ++i)
            if (!mayAddPattern(patternsOf(craftSlots.getItem(i)))) return false;
    }
    return origin(craftSlots, context);
}

} // namespace

void install() {
    LoomCraftHook::hook();
    BannerRecipeHook::hook();
}

void uninstall() {
    BannerRecipeHook::unhook();
    LoomCraftHook::unhook();
}

} // namespace lp::feature::frames::banners
