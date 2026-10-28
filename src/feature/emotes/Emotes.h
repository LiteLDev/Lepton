#pragma once

// U19 Emotes (server side + shared parts). Companion messages, the emote list and the server
// entry points (also used by the client build's integrated server and by the selftests).
//
// Flow (as Quark's RequestEmoteMessage / DoEmoteMessage):
//   mod client: keybind / chat-screen button -> EmoteRequest -> server
//   vanilla client: `/quark emote [name]` (a form without a name)
//   server: validates (module on, emote enabled, patron tier), then
//     - sends an AnimateEntityPacket ("animation.lepton.emote.<id>", baked into the resource
//       pack) to every client that sees the player, so vanilla and mod clients alike see the
//       emote on the player model;
//     - broadcasts EmotePlay to every mod client (HUD icon, emote sounds, custom emotes).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Player;

namespace lp::feature::emotes {

/// Client -> server: play an emote ("wave", "custom:<file name>").
struct EmoteRequest {
    static constexpr std::string_view kId = "emotes:request";
    std::string                       emote;
};

/// Server -> every mod client: `player` (unique id) plays `emote`; `tier` = the player's tier.
struct EmotePlay {
    static constexpr std::string_view kId = "emotes:play";
    std::string                       emote;
    std::int64_t                      player = 0;
    std::string                       playerName;
    int                               tier = 0;
};

/// Server -> mod client: the emotes the player may use (in menu order) and its tier.
struct EmoteList {
    static constexpr std::string_view kId = "emotes:list";
    bool                              enabled = true;
    std::vector<std::string>          emotes;
    int                               tier = 0;
};

/// The patron tier of a player (`emoteTiers` by lower-case name or XUID, else the default).
[[nodiscard]] int tierOf(Player const& player);
/// The built-in emotes the player may use, in menu order (enabled default emotes in config
/// order, then the patreon emotes their tier allows).
[[nodiscard]] std::vector<std::string> availableFor(Player const& player);
/// A custom emote id ("custom:" + a file name of letters, digits, '_', '-', '.', max 64).
[[nodiscard]] bool isValidCustomId(std::string_view id);

enum class PlayResult { Played, Disabled, Unknown, NotEnabled, TierTooLow };
/// Validates and plays an emote for the player (see the flow above).
PlayResult playEmote(Player& player, std::string const& emote);

/// The emote's display name for the player ("Wave"; custom emotes: their file name).
[[nodiscard]] std::string emoteName(Player const& player, std::string const& emote);
/// Vanilla-client menu: a form with every emote the player may use.
void openEmoteForm(Player& player);

namespace testing {
struct SentAnimation {
    std::int64_t player = 0;
    std::string  animation;
};
/// Animations sent since the last call (selftests).
[[nodiscard]] std::vector<SentAnimation> takeSentAnimations();
} // namespace testing

} // namespace lp::feature::emotes
