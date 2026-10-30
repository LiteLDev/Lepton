#pragma once

// Quark's GlassItemFrame sign attachment rule (GlassItemFrame#updateIsOnSign), as a pure function
// of the frame's direction and the sign behind it. Directions use Java's order (= Bedrock's
// facing_direction): 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.

#include <cmath>

namespace lp::feature::frames {

enum class SignAttachment : int {
    NotAttached      = 0,
    StandingInFront  = 1,
    StandingBehind   = 2,
    WallSign         = 3,
    HangingFromWall  = 4,
    HangingInFront   = 5,
    HangingBehind    = 6,
};

enum class SignKind : int {
    None,
    Standing,       ///< standing sign (16 rotations)
    CeilingHanging, ///< hanging sign under a block (16 rotations)
    Wall,           ///< wall sign (facing)
    WallHanging,    ///< hanging sign on a wall (facing)
};

struct SignInfo {
    SignKind kind     = SignKind::None;
    int      rotation = 0; ///< 0..15 for standing / ceiling hanging signs
    int      facing   = 0; ///< direction for wall / wall hanging signs
};

[[nodiscard]] constexpr int oppositeDir(int d) { return d ^ 1; }

/// The attachment of a frame facing `facing` whose support block is `sign`.
[[nodiscard]] inline SignAttachment signAttachment(int facing, SignInfo const& sign) {
    if (facing <= 1) return SignAttachment::NotAttached; // vertical axis
    switch (sign.kind) {
    case SignKind::Standing:
    case SignKind::CeilingHanging: {
        static constexpr double angles[6]{0, 0, 0, 180, -90, 90};
        double ours   = angles[oppositeDir(facing)];
        double signed_ = sign.rotation / 16.0 * 360.0;
        if (signed_ > 180) signed_ -= 360;
        double diff = ours - signed_;
        double abs  = std::abs(diff);
        if (diff < 0) abs -= 0.01; // Quark: two frames equidistant in different directions don't both attach
        bool standing = sign.kind == SignKind::Standing;
        if (abs < 45) return standing ? SignAttachment::StandingInFront : SignAttachment::HangingInFront;
        if (abs >= 135 && abs < 225) return standing ? SignAttachment::StandingBehind : SignAttachment::HangingBehind;
        return SignAttachment::NotAttached;
    }
    case SignKind::Wall:
        return sign.facing == facing ? SignAttachment::WallSign : SignAttachment::NotAttached;
    case SignKind::WallHanging:
        return sign.facing == facing || sign.facing == oppositeDir(facing) ? SignAttachment::HangingFromWall
                                                                            : SignAttachment::NotAttached;
    default:
        return SignAttachment::NotAttached;
    }
}

} // namespace lp::feature::frames
