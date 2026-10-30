#pragma once

// Small server-side helpers of the enchanting unit.

#include <functional>
#include <string>

#include "core/GlobalPos.h"

class BlockSource;
class ItemStack;
class Player;
class Vec3;

namespace lp::feature::enchanting {

[[nodiscard]] Player* playerByUuid(std::string const& uuid);
[[nodiscard]] std::string uuidOf(Player const& player);
/// Puts `stack` into the player's inventory; what doesn't fit is dropped at the player.
void giveOrDrop(Player& player, ItemStack stack);
/// Spawns `stack` as an item entity at the centre of `pos`.
void dropAt(BlockSource& region, core::GlobalPos const& pos, ItemStack const& stack);
/// Runs `fn` on the server thread after `ticks` ticks.
void later(int ticks, std::function<void()> fn);
/// Plays a level sound event at the centre of `pos`.
void playSound(core::GlobalPos const& pos, int levelSoundEvent, float pitch = 1.0f);
/// Squared distance from the player's feet to the centre of `pos` (huge if other dimension).
[[nodiscard]] double distanceSq(Player const& player, core::GlobalPos const& pos);

} // namespace lp::feature::enchanting
