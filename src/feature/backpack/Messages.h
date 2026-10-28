#pragma once

// Companion-protocol messages of U14 (registered in Messages.cpp).

#include <string>
#include <string_view>

namespace lp::feature::backpack {

/// Server -> client: the player's backpack screen opened / closed (the client draws its buttons).
struct BackpackScreenMsg {
    static constexpr std::string_view kId = "backpack:screen";
    bool open = false;
};

/// Client -> server: switch between the backpack screen and the normal inventory.
/// `target` = "inventory" or "backpack".
struct BackpackSwitchMsg {
    static constexpr std::string_view kId = "backpack:switch";
    std::string target;
};

/// Client -> server: seed pouch in the selected hotbar slot: "insert" (absorb from the inventory)
/// or "take" (take one stack out).
struct PouchActionMsg {
    static constexpr std::string_view kId = "backpack:pouch";
    std::string action;
};

} // namespace lp::feature::backpack
