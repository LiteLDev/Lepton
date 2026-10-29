#pragma once

// Companion-protocol messages of the enchanting unit (registered in Messages.cpp).

#include <string>
#include <string_view>

namespace lp::feature::enchanting {

/// Matrix operations (Quark's MatrixEnchanterOperationMessage). Client -> server.
enum Oper : int { OperAdd = 0, OperPlace = 1, OperRemove = 2, OperRotate = 3, OperMerge = 4 };

struct MatrixOpMsg {
    static constexpr std::string_view kId = "enchanting:op";
    int                               op   = 0;
    int                               arg0 = 0, arg1 = 0, arg2 = 0;
};

/// Slot moves on the matrix screen. Client -> server.
/// `action`: "put_item" / "put_lapis" (from inventory slot `slot`), "take_item", "take_lapis", "take_output".
struct MatrixSlotMsg {
    static constexpr std::string_view kId = "enchanting:slot";
    std::string                       action;
    int                               slot = -1;
};

/// The client closed the matrix screen. Client -> server.
struct MatrixCloseMsg {
    static constexpr std::string_view kId = "enchanting:close";
    int                               unused = 0;
};

/// Full state of the open matrix enchanter (JSON in `state`); `open = false` closes the screen.
/// Server -> client.
struct MatrixStateMsg {
    static constexpr std::string_view kId = "enchanting:state";
    bool                              open = true;
    std::string                       state;
    bool                              fresh = false; ///< the table was just opened (else: an update of an open screen)
};

/// A tome / rune combination is ready in the open anvil or smithing table. Server -> client.
/// `kind` "" = nothing to offer (hide the overlay).
struct CraftPreviewMsg {
    static constexpr std::string_view kId = "enchanting:craft_preview";
    std::string                       kind; ///< "tome", "overlevel", "rune" or ""
    std::string                       result; ///< SNBT of the result
    int                               cost       = 0;
    bool                              affordable = false;
};

/// Apply the offered combination. Client -> server.
struct CraftMsg {
    static constexpr std::string_view kId = "enchanting:craft";
    int                               unused = 0;
};

} // namespace lp::feature::enchanting
