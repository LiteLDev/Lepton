#pragma once

// Camera filters and screenshots (client build): a full-screen pixel-shader pass over the swap
// chain image right before the game presents it (Direct3D 12), and a read-back of that image.

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace lp::client::cosmetics::filter {

/// One filter. `mode` 0: colour matrix (`rows`: out = rows * (r, g, b) + w); 1: focus on `hue`
/// (degrees, other colours desaturated); 2: black and white threshold. `mosaic` > 1 averages
/// blocks of that many pixels, `levels` > 1 posterizes to that many levels per channel.
struct Params {
    std::array<std::array<float, 4>, 3> rows{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
    int                                 mode   = 0;
    float                               mosaic = 0;
    float                               levels = 0;
    float                               hue    = 0;
};

/// Quark's camera filters by index (0 = none, 1..19 as in the camera's filter list).
[[nodiscard]] std::optional<Params> preset(int index);
/// Number of filters (including "none").
inline constexpr int kCount = 20;
/// Lang key suffix of a filter ("none", "grayscale", ...).
[[nodiscard]] char const* name(int index);

/// Sets the active filter (nullopt = none). Thread-safe.
void set(std::optional<Params> params);
/// Saves the next presented frame (after the filter) as a PNG. `done(ok, path)` runs on a worker
/// thread. False if the renderer is not supported (no Direct3D 12 swap chain seen).
bool requestCapture(std::filesystem::path path, std::function<void(bool, std::string const&)> done);
/// True once the present hook has seen a Direct3D 12 swap chain.
[[nodiscard]] bool available();
/// Installs the present hook (idempotent). Called when camera mode is first used.
void ensureInstalled();
/// Removes the hooks (mod disable).
void shutdown();

} // namespace lp::client::cosmetics::filter
