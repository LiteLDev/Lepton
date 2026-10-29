#ifdef LP_CLIENT

#include "client/hud/Painter.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/actor/ItemRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"

namespace lp::client::hud {

namespace {
std::atomic<float> gSpace{4.f};

mce::Color mc(Color c) { return mce::Color(c.r, c.g, c.b, c.a); }
} // namespace

float spaceWidth() { return gSpace.load(); }
void  setSpaceWidth(float w) {
    if (w > 0) gSpace.store(w);
}

void Painter::imagePart(
    std::string const& path,
    float              x,
    float              y,
    float              w,
    float              h,
    float              u,
    float              v,
    float              uw,
    float              vh,
    Color              tint
) {
    auto        tex  = mRc.getTexture(ResourceLocation(Core::PathView(path)), false);
    auto const& data = tex.mClientTexture.get();
    if (!data) return;
    glm::vec2 pos{x, y}, size{w, h}, uv{u, v}, uvSize{uw, vh};
    mRc.drawImage(data->mClientTexture.get(), pos, size, uv, uvSize, false);
    static HashedString const kMaterial("ui_textured_and_glcolor_sprite");
    mRc.flushImages(mc(tint), tint.a, kMaterial);
}

void Painter::image(std::string const& path, float x, float y, float w, float h, Color tint) {
    imagePart(path, x, y, w, h, 0, 0, 1, 1, tint);
}

void Painter::fill(float x, float y, float w, float h, Color c) {
    mRc.fillRectangle(RectangleArea{x, x + w, y, y + h}, mc(c), c.a);
}

void Painter::text(std::string_view s, float x, float y, Color c, float scale, bool shadow) {
    auto* client = client::currentClient();
    if (s.empty() || !client) return;
    float            w = textWidth(s, scale) + 2;
    RectangleArea    area{x, x + w, y, y + 10 * scale};
    TextMeasureData  tmd{scale, 0.f, shadow, true, false, ::ui::TextAlignment::Left};
    CaretMeasureData caret{-1, false};
    auto             font = client->getFontHandle();
    mRc.drawText(font.getFont(), area, std::string(s), mc(c), c.a, ::ui::TextAlignment::Left, tmd, caret);
    mText = true;
}

float Painter::textWidth(std::string_view s, float scale) const {
    auto* client = client::currentClient();
    if (!client) return 0;
    auto font = client->getFontHandle();
    return static_cast<float>(mRc.getLineLength(font.getFont(), std::string(s), scale, false));
}

void Painter::item(ItemStack const& stack, float x, float y, float scale, int countText, bool durability) {
    auto* client = client::currentClient();
    if (stack.isNull() || !client) return;
    if (auto* renderer = client->getItemRenderer()) {
        BaseActorRenderContext ctx(mRc.mScreenContext, mRc.mClient, client->getMinecraftGame());
        renderer->renderGuiItemNew(ctx, stack, 0, x, y, false, 1.f, 1.f, scale, 0);
    }
    if (durability) {
        int   w = 0;
        Color c;
        if (durabilityBar(stack, w, c)) {
            fill(x + 2 * scale, y + 13 * scale, 13 * scale, 2 * scale, ui::colors::black);
            fill(x + 2 * scale, y + 13 * scale, static_cast<float>(w) * scale, 1 * scale, c);
        }
    }
    if (countText > 1) {
        auto n = std::to_string(countText);
        text(n, x + 17 * scale - textWidth(n, scale), y + 9 * scale, ui::colors::white, scale);
    }
}

void Painter::flush() {
    if (!mText) return;
    mRc.flushText(0.f, std::nullopt);
    mText = false;
}

ui::Vec screenPos(UIControl const& c) {
    if (!c.mCachedPositionDirty) {
        auto const& p = c.mCachedPosition.get();
        return {p.x, p.y};
    }
    ui::Vec v;
    for (UIControl const* cur = &c; cur;) {
        auto const& rel  = cur->mParentRelativePosition.get();
        v.x             += rel.x;
        v.y             += rel.y;
        auto parent      = cur->mParent.lock();
        cur              = parent.get();
    }
    return v;
}

bool isHudView(ScreenView const& view) {
    auto const& tree = view.mVisualTree.get();
    if (!tree) return false;
    // Root control names are "<namespace>.<name>" ("hud.hud_screen").
    std::string_view name = tree->mRootControlName.get();
    return name == "hud_screen" || name.ends_with(".hud_screen");
}

ui::Vec uiScreenSize() {
    auto* c = client::currentClient();
    if (!c) return {};
    auto const& s = c->getGuiData()->mScreenSizeData.get().clientUIScreenSize.get();
    return {s.x, s.y};
}

bool durabilityBar(ItemStackBase const& stack, int& width, Color& color) {
    auto* item = stack.mItem.get();
    if (stack.isNull() || !item || !stack.isDamageableItem() || !stack.isDamaged()) return false;
    int max = item->getMaxDamage();
    if (max <= 0) return false;
    int dmg = std::clamp<int>(stack.getDamageValue(), 0, max);
    width   = static_cast<int>(std::lround(13.0 - dmg * 13.0 / max));
    float f = std::max(0.f, static_cast<float>(max - dmg) / max);
    // HSV(f / 3, 1, 1) -> RGB, as Java's bar.
    float h = f / 3.f * 6.f;
    float x = 1 - std::fabs(std::fmod(h, 2.f) - 1);
    color   = h < 1 ? Color{1, x, 0, 1} : Color{x, 1, 0, 1};
    return true;
}

} // namespace lp::client::hud

#endif // LP_CLIENT
