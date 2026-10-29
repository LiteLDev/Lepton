#pragma once

// U19 PatTheDogs (server side). See PatTheDogs.cpp.

#include <cstdint>

class Actor;
class Player;

namespace lp::feature::emotes::pat {

enum class PetResult {
    None,    ///< not a petting interaction: the vanilla interaction goes on
    Pet,     ///< pet (heart, sound): the vanilla interaction is cancelled
    Refused, ///< a dog that can't be pet right now (wild, or pet < 1 s ago): cancelled anyway
};

/// Quark's PlayerInteract.EntityInteract handler: `player` uses an empty main hand on `target`.
PetResult tryPet(Player& player, Actor& target);

/// Ticks since the dog was last pet (0 for wild dogs).
[[nodiscard]] std::int64_t timeSinceLastPet(Actor const& dog);
/// More than 20 ticks since the last pet.
[[nodiscard]] bool canPet(Actor const& dog);
/// `dogsWantLove` > 0 and more than that many ticks since the last pet.
[[nodiscard]] bool needsPets(Actor const& dog);
/// Records "pet now" (only when `dogsWantLove` > 0, as Quark).
void setPetTime(Actor const& dog);

namespace testing {
void         setLastPet(Actor const& dog, std::int64_t tick);
void         clearLastPet(Actor const& dog);
std::int64_t currentTick();
} // namespace testing

} // namespace lp::feature::emotes::pat
