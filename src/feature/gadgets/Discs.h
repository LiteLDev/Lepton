#pragma once

// Ambience discs / Endermosh internals shared with the selftests.

#include <string>

class ItemStack;
class Player;

namespace lp::core {
struct GlobalPos;
}

namespace lp::feature::gadgets::discs {

/// One of the eight ambience discs at random, with its description as lore.
[[nodiscard]] ItemStack randomAmbientDisc();
/// `lepton:music_disc_<name>` with its description as lore.
[[nodiscard]] ItemStack makeDisc(std::string const& name, Player const* player);
/// A jukebox at `pos` plays an ambience disc (its name in `name`).
[[nodiscard]] bool isTracked(core::GlobalPos const& pos, std::string* name = nullptr);
/// How many times the ambience at `pos` was restarted (-1: not tracked).
[[nodiscard]] int replaysAt(core::GlobalPos const& pos);
/// Test seam: makes the ambience at `pos` `ticks` older (to reach the next loop at once).
void ageForTest(core::GlobalPos const& pos, int ticks);

} // namespace lp::feature::gadgets::discs
