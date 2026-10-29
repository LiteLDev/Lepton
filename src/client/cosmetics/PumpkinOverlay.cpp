// UsesForCurses, vanishPumpkinOverlay (client): a worn carved pumpkin with Curse of Vanishing draws
// no pumpkin blur (docs/spec/cosmetics.md §3). The overlay texture is looked up through the texture
// group; while the condition holds the lookup answers with a transparent texture.
#ifdef LP_CLIENT

#include <atomic>
#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"

#include "client/Client.h"
#include "feature/cosmetics/Cosmetics.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::cosmetics {

namespace cos = lp::feature::cosmetics;

/// True while the local player wears a carved pumpkin with Curse of Vanishing (and the option is on).
bool hidePumpkinOverlay() {
    auto const& c = cos::config();
    if (!c.enableUsesForCurses || !c.vanishPumpkinOverlay) return false;
    auto* player = client::currentPlayer();
    if (!player) return false;
    auto const& head = player->getArmor(SharedTypes::Legacy::ArmorSlot::Head);
    if (head.isNull() || head.getTypeName() != "minecraft:carved_pumpkin") return false;
    return EnchantUtils::getEnchantLevel(Enchant::Type::CurseOfVanishing, head) > 0;
}

namespace {

bool isPumpkinBlur(ResourceLocation const& loc) { return loc.mPath->value.find("misc/pumpkinblur") != std::string::npos; }

ResourceLocation const& emptyTexture() {
    static ResourceLocation const loc(Core::PathView("textures/lepton/cosmetics/empty"));
    return loc;
}

std::atomic<bool> gSeen{false};

void noteLookup(char const* where) {
    if (!gSeen.exchange(true)) log().debug("cosmetics: pumpkin overlay looked up through {}", where);
}

LL_TYPE_INSTANCE_HOOK(
    UiTextureHook,
    ll::memory::HookPriority::Normal,
    MinecraftUIRenderContext,
    &MinecraftUIRenderContext::$getTexture,
    ::mce::TexturePtr,
    ::ResourceLocation const& loc,
    bool                      forceReload
) {
    if (isPumpkinBlur(loc)) {
        noteLookup("the UI render context");
        if (hidePumpkinOverlay()) return origin(emptyTexture(), forceReload);
    }
    return origin(loc, forceReload);
}

LL_TYPE_INSTANCE_HOOK(
    GroupTextureHook,
    ll::memory::HookPriority::Normal,
    mce::TextureGroup,
    &mce::TextureGroup::$getTexture,
    ::mce::TexturePtr,
    ::ResourceLocation const&       loc,
    bool const                      forceReload,
    ::std::optional<uint>           order,
    ::cg::TextureSetLayerType const type
) {
    if (isPumpkinBlur(loc)) {
        noteLookup("the texture group");
        if (hidePumpkinOverlay()) return origin(emptyTexture(), forceReload, order, type);
    }
    return origin(loc, forceReload, order, type);
}

class PumpkinOverlay {
public:
    void install() {
        UiTextureHook::hook();
        GroupTextureHook::hook();
    }
    void uninstall() {
        GroupTextureHook::unhook();
        UiTextureHook::unhook();
    }
};

} // namespace

} // namespace lp::client::cosmetics

LP_REGISTER_MODULE(lp::client::cosmetics::PumpkinOverlay);

#endif // LP_CLIENT
