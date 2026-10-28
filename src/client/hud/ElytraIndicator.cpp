// U16 hud, ElytraIndicator (client build): an elytra icon at the left end of the armour bar; the
// bar moves 8 px right and keeps 9 icons while the armour is below 20.

#ifdef LP_CLIENT

#include <array>

#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HudArmorRenderer.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/item/ItemStack.h"

#include "client/Client.h"
#include "client/hud/Painter.h"
#include "feature/hud/HudConfig.h"
#include "feature/hud/Logic.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::client::hud {

using lp::feature::hud::cfg;

namespace {

bool gLogged = false;

bool elytraActive() {
    if (!cfg().enableElytraIndicator) return false;
    auto* player = client::currentPlayer();
    if (!player) return false;
    return lp::feature::hud::canElytraFly(player->getArmor(SharedTypes::Legacy::ArmorSlot::Torso));
}

LL_TYPE_INSTANCE_HOOK(
    ArmorBarHook,
    ll::memory::HookPriority::Normal,
    HudArmorRenderer,
    &HudArmorRenderer::$render,
    void,
    MinecraftUIRenderContext& rc,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    if (!elytraActive()) {
        origin(rc, client, owner, pass);
        return;
    }
    auto* player = client::currentPlayer();
    int   armor  = player->getArmorValue();
    float shift  = armor >= 20 ? 0.f : 8.f;

    // Shift the icons and drop the 10th while shifted (Quark's getArmorLimit), then restore.
    auto savedPos   = mIconPosition.get();
    auto savedFull  = mNumFullIcons;
    auto savedHalf  = mNumHalfIcons;
    auto savedEmpty = mNumEmptyIcons;
    if (shift != 0) {
        for (auto& p : mIconPosition.get()) p.x += shift;
        if (mNumFullIcons + mNumHalfIcons + mNumEmptyIcons >= 10) {
            if (mNumEmptyIcons > 0) --mNumEmptyIcons;
            else if (mNumHalfIcons > 0) --mNumHalfIcons;
            else if (mNumFullIcons > 0) --mNumFullIcons;
        }
    }
    origin(rc, client, owner, pass);
    mIconPosition.get() = savedPos;
    mNumFullIcons  = savedFull;
    mNumHalfIcons  = savedHalf;
    mNumEmptyIcons = savedEmpty;

    auto  base = screenPos(owner);
    float x    = base.x + savedPos[0].x - 8 + shift;
    float y    = base.y + savedPos[0].y;
    if (!gLogged) {
        gLogged = true;
        log().info(
            "hud: elytra indicator drawn (armor {}, shift {}, owner {:.1f},{:.1f} size {:.1f}x{:.1f}, icon0 {:.1f},{:.1f}, icon1 "
            "{:.1f},{:.1f}, icons {}/{}/{})",
            armor, shift, base.x, base.y, owner.mSize.get().x, owner.mSize.get().y, savedPos[0].x, savedPos[0].y, savedPos[1].x,
            savedPos[1].y, savedFull, savedHalf, savedEmpty
        );
    }
    Painter p(rc);
    p.image("textures/lepton/hud/elytra", x, y, 9, 9);
}

} // namespace

class ElytraIndicatorModule {
public:
    void install() { ArmorBarHook::hook(); }
    void uninstall() { ArmorBarHook::unhook(); }
};

} // namespace lp::client::hud

LP_REGISTER_MODULE(lp::client::hud::ElytraIndicatorModule);

#endif // LP_CLIENT
