#pragma once

// Entity helpers (server thread): spawn behaviour-pack entities (`assets/pack/bp/entities/`),
// look them up by unique id, query and despawn them.
//
// Never keep `Actor*` across ticks: store `entities::idOf(actor)` (the persistent unique id,
// saved with the entity) and look it up again with `entities::find(id)` (nullptr when the entity
// is gone or its chunk is not loaded).
//
//     auto* crab = lp::game::entities::spawn(region, "lepton:crab", pos);
//     int64_t id = lp::game::entities::idOf(*crab);
//     ...
//     if (auto* e = lp::game::entities::find(id)) lp::game::entities::triggerEvent(*e, "lepton:dance");

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Actor;
class AABB;
class BlockSource;
class Player;
class Vec3;

namespace lp::game::entities {

/// Spawns `type` ("lepton:crab", "minecraft:pig", optionally "type<init_event>") at `pos`.
/// Returns nullptr when the type is unknown or the area is not loaded.
Actor* spawn(BlockSource& region, std::string_view type, Vec3 const& pos, Actor* summoner = nullptr);
/// Same in dimension `dim` (0 overworld, 1 nether, 2 end).
Actor* spawn(int dim, std::string_view type, Vec3 const& pos, Actor* summoner = nullptr);

/// The persistent unique id (`ActorUniqueID::rawID`).
[[nodiscard]] int64_t idOf(Actor const& actor);
/// The loaded entity with that unique id (players included), or nullptr.
[[nodiscard]] Actor* find(int64_t uniqueId);
/// The entity's type id ("minecraft:pig", "lepton:crab").
[[nodiscard]] std::string typeOf(Actor const& actor);

/// Loaded entities in `box` (optionally only of `type`).
[[nodiscard]] std::vector<Actor*> inBox(BlockSource& region, AABB const& box, std::string_view type = {});
/// Every loaded entity of `type` (in dimension `dim`, -1 = all).
[[nodiscard]] std::vector<Actor*> ofType(std::string_view type, int dim = -1);

/// Runs a behaviour-pack event ("minecraft:entity_born", "lepton:...") on the entity.
bool triggerEvent(Actor& actor, std::string const& event);

/// Removes the entity without death animation or drops.
void despawn(Actor& actor);

} // namespace lp::game::entities
