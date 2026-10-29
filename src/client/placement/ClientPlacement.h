#pragma once

// U17 placement, client mod (client build only): state shared by the client parts.

#ifdef LP_CLIENT

#include <optional>
#include <string>

#include "client/ui/Ui.h"
#include "feature/placement/Placement.h"

namespace lp::client::placement {

using feature::placement::LockProfile;
using feature::placement::ReachTarget;

/// The rotation lock shown on the HUD (what this client sent to the server).
[[nodiscard]] std::optional<LockProfile> clientLock();
/// The variant this client places ("" = the original).
[[nodiscard]] std::string const& clientVariant();
/// Sets the variant; `sync` sends it to the server (Quark's PlaceVariantUpdateMessage).
void setClientVariant(std::string const& variant, bool sync);
/// The reacharound target of the last client tick.
[[nodiscard]] std::optional<ReachTarget> currentReachTarget();

/// The display name of a Windows virtual-key code ("K", "R", "F6", ...).
[[nodiscard]] std::string keyName(int vk);
/// The display name of the first key bound to a Lepton keybind ("" when unbound).
[[nodiscard]] std::string boundKeyName(std::string const& keybind);

/// Draws a small pixel icon: '#' = `color`, '+' = `shade`, anything else transparent; one UI
/// unit per character.
void drawIcon(ui::DrawContext& dc, std::initializer_list<std::string_view> rows, float x, float y, ui::Color color, ui::Color shade = ui::colors::black);

/// The HUD is hidden (F1) or the player is a spectator.
[[nodiscard]] bool hudHidden();

} // namespace lp::client::placement

#endif // LP_CLIENT
