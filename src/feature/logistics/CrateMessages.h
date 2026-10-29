#pragma once

// Companion-protocol messages of the crate screen (server <-> client mod).

#include <string_view>

namespace lp::feature::logistics {

/// Server -> client: the crate the player looks at (sent on open, on every change and on close).
struct CrateView {
    static constexpr std::string_view kId = "logistics:crate_view";
    bool open   = false;
    int  total  = 0;   ///< items in the crate
    int  max    = 640; ///< config maxItems
    int  stacks = 0;   ///< occupied stacks
    int  scroll = 0;   ///< first shown stack index (multiple of 9)
};

/// Client -> server: scroll the open crate by one row (Quark's ScrollCrateMessage).
struct CrateScroll {
    static constexpr std::string_view kId = "logistics:crate_scroll";
    bool down = true;
};

} // namespace lp::feature::logistics
