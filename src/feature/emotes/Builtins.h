#pragma once

// The emotes that ship with Quark 4.1-488, as emote scripts (see EmoteScript.h). Pure C++.

#include <string>
#include <string_view>
#include <vector>

#include "feature/emotes/EmoteScript.h"

namespace lp::feature::emotes {

struct BuiltinEmote {
    std::string_view id;     ///< "wave"
    bool             patreon; ///< one of Quark's patreon emotes (tier 1 or 2)
    std::string_view script;
};

/// Every built-in emote: the 12 default ones in Quark's order, then the 6 patreon ones.
[[nodiscard]] std::vector<BuiltinEmote> const& builtinEmotes();
/// The built-in emote with that id, or nullptr.
[[nodiscard]] BuiltinEmote const* findBuiltin(std::string_view id);
/// The compiled built-in emote (compiled once), or nullptr.
[[nodiscard]] script::Emote const* compiledBuiltin(std::string_view id);
/// Quark's default `enabledEmotes`.
[[nodiscard]] std::vector<std::string> defaultEnabledEmotes();

} // namespace lp::feature::emotes
