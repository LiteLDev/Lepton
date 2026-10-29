#pragma once

// Companion-protocol messages of U15 (registered in Messages.cpp). Slot references use the
// game's UI collection names ("hotbar_items", "inventory_items",
// "combined_hotbar_and_inventory_items", "container_items", ...) and the index within them; the
// server resolves and validates them (see Common.h `resolveSlot`).

#include <string>
#include <string_view>

namespace lp::feature::inventory {

/// InventorySorting: sort the player inventory (`forcePlayer`) or the open container.
struct SortRequest {
    static constexpr std::string_view kId = "inventory:sort";
    bool forcePlayer = true;
};

/// EasyTransferring: insert (restock = false) / extract (restock = true), filtered when `smart`.
struct TransferRequest {
    static constexpr std::string_view kId = "inventory:transfer";
    bool smart   = false;
    bool restock = false;
};

/// EasyTransferring shift lock: move the stack of a slot to the other inventory.
struct QuickMoveRequest {
    static constexpr std::string_view kId = "inventory:quick_move";
    std::string collection;
    int         slot = 0;
};

/// HotbarChanger: swap the hotbar with row `bar` (1..3).
struct ChangeHotbarRequest {
    static constexpr std::string_view kId = "inventory:change_hotbar";
    int bar = 0;
};

/// ExpandedItemInteractions: the interaction click (right click, or left with invertClicks) on a
/// slot, with whatever the cursor holds.
struct SlotInteractRequest {
    static constexpr std::string_view kId = "inventory:slot_interact";
    std::string collection;
    int         slot = 0;
};

/// ExpandedItemInteractions: mouse wheel over a bundle.
struct ScrollBundleRequest {
    static constexpr std::string_view kId = "inventory:scroll_bundle";
    std::string collection;
    int         slot  = 0;
    double      delta = 0;
    /// Dynamic container id of the bundle (the client reads it; BDS cannot derive it).
    int dynamicId = -1;
};

/// ItemSharing: share the item of a slot.
struct ShareItemRequest {
    static constexpr std::string_view kId = "inventory:share";
    std::string collection;
    int         slot = 0;
};

/// ItemSharing, server -> mod clients: an item was shared (the chat line is sent as text too).
struct SharedItemNotice {
    static constexpr std::string_view kId = "inventory:shared";
    std::string sender;
    std::string senderUuid;
    std::string item; ///< SNBT
    std::string text; ///< the chat line
};

} // namespace lp::feature::inventory
