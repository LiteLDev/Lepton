// UsesForCurses, vanishPumpkinOverlay (client): a worn carved pumpkin with Curse of Vanishing draws
// no pumpkin blur (docs/spec/cosmetics.md §3). The overlay is drawn by native code from a cached
// texture, so the texture itself is swapped: while the condition holds, `textures/misc/pumpkinblur`
// is replaced in place by a transparent image of the same size, and restored afterwards.
#ifdef LP_CLIENT

#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/cosmetics/Cosmetics.h"
#include "mod/Log.h"

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

ResourceLocation const& blurLocation() {
    static ResourceLocation const loc(Core::PathView("textures/misc/pumpkinblur"));
    return loc;
}

struct State {
    std::optional<cg::ImageBuffer> original; ///< the overlay as loaded by the game
    bool                           hidden = false;
} gState;

void setHidden(bool hide) {
    auto* client = client::currentClient();
    if (!client) return;
    auto group = client->getTextureGroup();
    if (!group) return;
    if (!gState.original) {
        cg::ImageBuffer* loaded = group->getCachedImageOrLoadSync(blurLocation(), false);
        if (!loaded) return;
        gState.original.emplace(*loaded);
    }
    cg::ImageBuffer image(*gState.original);
    if (hide) {
        auto& blob = *image.mStorage;
        if (blob.size()) std::memset(blob.data(), 0, blob.size());
    }
    group->updateTextureInPlace(blurLocation(), std::move(image));
    gState.hidden = hide;
    log().info("cosmetics: pumpkin overlay {}", hide ? "hidden" : "shown");
}

LP_REGISTER_HUD([](ui::DrawContext&) {
    try {
        bool want = hidePumpkinOverlay();
        if (want != gState.hidden) setHidden(want);
    } catch (std::exception const& e) {
        log().error("cosmetics: pumpkin overlay: {}", e.what());
        gState.hidden = hidePumpkinOverlay(); // don't retry every frame
    }
});

} // namespace

} // namespace lp::client::cosmetics

#endif // LP_CLIENT
