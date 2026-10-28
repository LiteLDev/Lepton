#pragma once

// U16 hud (client build): drawing inside the game's own UI renderers (tooltip box, armour bar),
// where the client/ui DrawContext is not available: textures from the resource pack, text and
// item icons on a MinecraftUIRenderContext. UI units.

#ifdef LP_CLIENT

#include <string>
#include <string_view>

#include "client/ui/Ui.h"

class MinecraftUIRenderContext;
class ItemStack;
class ItemStackBase;
class UIControl;

namespace lp::client::hud {

using ui::Color;

class Painter {
public:
    explicit Painter(MinecraftUIRenderContext& rc) : mRc(rc) {}
    ~Painter() { flush(); }
    Painter(Painter const&)            = delete;
    Painter& operator=(Painter const&) = delete;

    /// A whole resource-pack texture (`textures/lepton/hud/elytra`, no extension) stretched over
    /// the rectangle, multiplied by `tint`.
    void image(std::string const& path, float x, float y, float w, float h, Color tint = ui::colors::white);
    /// A part of a texture: `u, v, uw, vh` as fractions of the texture.
    void imagePart(std::string const& path, float x, float y, float w, float h, float u, float v, float uw, float vh,
                   Color tint = ui::colors::white);
    void fill(float x, float y, float w, float h, Color c);
    void text(std::string_view s, float x, float y, Color c = ui::colors::white, float scale = 1, bool shadow = true);
    [[nodiscard]] float textWidth(std::string_view s, float scale = 1) const;
    /// Item icon (16x16 * scale) with an optional count text and the durability bar.
    void item(ItemStack const& stack, float x, float y, float scale = 1, int countText = -1, bool durability = true);
    void flush();

private:
    MinecraftUIRenderContext& mRc;
    bool                      mText = false;
};

/// Java's item durability bar for damaged items: filled width (0..13) and colour. False when the
/// item shows no bar.
bool durabilityBar(ItemStackBase const& stack, int& width, Color& color);

/// Position of a control on the screen (read-only: the cached position, or the sum of the relative
/// positions while the cache is dirty, as client/ui does).
[[nodiscard]] ui::Vec screenPos(UIControl const& c);

/// Width of one space in the game font (measured on the first rendered frame; 4 before).
[[nodiscard]] float spaceWidth();
void                setSpaceWidth(float w);

} // namespace lp::client::hud

#endif // LP_CLIENT
