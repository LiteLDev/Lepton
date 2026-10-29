#pragma once

// `config/frames.json` (U24): Quark's GlassItemFrame, DyeableItemFrames, MoreBannerLayers and
// MapWashing options. See docs/spec/frames.md §5.

namespace lp::feature::frames {

struct FramesConfig {
    int version = 1;

    // ---- GlassItemFrame (building) -------------------------------------------------------------
    bool enableGlassItemFrame       = true;
    bool glassItemFramesUpdateMaps  = true;
    /// Set to true for faster map updates. Default is every 3s
    bool glassItemFramesUpdateMapsEveryTick = false;
    /// The scale at which items render in the Glass Item Frame. To match the vanilla Item Frame
    /// size, set to 1.0
    double itemRenderScale = 1.5;

    // ---- DyeableItemFrames (tweaks) ------------------------------------------------------------
    bool enableDyeableItemFrames = true;

    // ---- MoreBannerLayers (tweaks) -------------------------------------------------------------
    bool enableMoreBannerLayers = true;
    /// 1..16 (Quark); Bedrock draws at most 6 layers, so the effective limit is min(layerLimit, 6).
    int layerLimit = 16;

    // ---- MapWashing (tweaks) -------------------------------------------------------------------
    bool enableMapWashing = true;
};

} // namespace lp::feature::frames
