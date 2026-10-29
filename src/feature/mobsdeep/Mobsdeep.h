#pragma once

// U21 mobsdeep: Quark's Stonelings, Forgotten, Foxhound and Wraith (docs/spec/mobsdeep.md).
// Internal interface shared by the module and its selftests.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "feature/mobsdeep/Config.h"

class Actor;
class Player;
class Vec3;

namespace lp::feature::mobsdeep {

inline constexpr std::string_view kStoneling = "lepton:stoneling";
inline constexpr std::string_view kForgotten = "lepton:forgotten";
inline constexpr std::string_view kFoxhound  = "lepton:foxhound";
inline constexpr std::string_view kWraith    = "lepton:wraith";
inline constexpr std::string_view kSoulBead  = "lepton:soul_bead";

inline constexpr std::string_view kDiamondHeart = "lepton:diamond_heart";
inline constexpr std::string_view kForgottenHat = "lepton:forgotten_hat";
inline constexpr std::string_view kSoulBeadItem = "lepton:soul_bead";

/// Stoneling variants in Quark's order (index = the `lepton:variant` property).
struct StonelingVariant {
    std::string_view                name;
    std::array<std::string_view, 2> blocks; ///< second may be empty
};
inline constexpr std::array<StonelingVariant, 11> kVariants{{
    {"stone", {"minecraft:cobblestone", "minecraft:stone"}},
    {"andesite", {"minecraft:andesite", "minecraft:polished_andesite"}},
    {"diorite", {"minecraft:diorite", "minecraft:polished_diorite"}},
    {"granite", {"minecraft:granite", "minecraft:polished_granite"}},
    {"limestone", {"lepton:limestone", "lepton:polished_limestone"}},
    {"calcite", {"minecraft:calcite", ""}},
    {"shale", {"lepton:shale", "lepton:polished_shale"}},
    {"jasper", {"lepton:jasper", "lepton:polished_jasper"}},
    {"deepslate", {"minecraft:deepslate", "minecraft:polished_deepslate"}},
    {"tuff", {"minecraft:tuff", "minecraft:polished_tuff"}},
    {"dripstone", {"minecraft:dripstone_block", "lepton:polished_dripstone"}},
}};

/// The variant a block (or block item) id makes / feeds, -1 if none.
[[nodiscard]] int variantOf(std::string_view blockId);

/// A wraith voice (Bedrock sound names; empty = silent).
struct Voice {
    std::string idle, hurt, death;
};
/// Quark id ("entity.sheep.ambient", "minecraft:entity.frog.hurt", "quark:entity.stoneling.cry") ->
/// Bedrock sound name. Unknown ids are returned unchanged (they may already be Bedrock names).
[[nodiscard]] std::string bedrockSound(std::string_view javaId);
/// The usable entries of `wraithSounds` (exactly three '|'-separated parts), mapped to Bedrock.
[[nodiscard]] std::vector<Voice> wraithVoices();

/// Entity property / molang helpers.
[[nodiscard]] float molang(Actor& actor, std::string const& expr);
[[nodiscard]] bool  boolProp(Actor& actor, std::string_view prop);
[[nodiscard]] int   intProp(Actor& actor, std::string_view prop);

/// Plays a sound at a position for nearby players (PlaySoundPacket).
void playSoundAt(Actor& near, std::string const& name, Vec3 const& pos, float volume = 1.0f, float pitch = 1.0f);
/// Spawns a resource-pack particle effect at a position (all players in the dimension).
void particleAt(Actor& in, std::string const& effect, Vec3 const& pos);

/// The Forgotten's weapon rule: bow without target or when the target's blindness ends in < 20 ticks.
[[nodiscard]] bool shouldUseBow(Actor& forgotten);
/// Soul bead trail point after `t` ticks from `start` towards (tx, tz) (Quark's formula).
[[nodiscard]] Vec3 soulBeadPoint(Vec3 const& start, int targetX, int targetZ, int t);
/// Finds the nearest Nether Fortress within 100 chunks of `origin` in dimension `dim`.
[[nodiscard]] std::optional<std::pair<int, int>> findFortress(int dim, Vec3 const& origin);
/// Spawns a soul bead flying from `start` towards (x, z). Returns its unique id (0 on failure).
int64_t spawnSoulBead(Player& player, Vec3 const& start, int targetX, int targetZ);

namespace testing {
/// Forces the next taming rolls (nullopt = use config).
void forceTameChance(std::optional<double> chance);
/// The stored voice of a wraith (nullopt if none yet).
[[nodiscard]] std::optional<Voice> voiceOf(int64_t wraithId);
/// The age of a soul bead (-1 if unknown) / sets it.
[[nodiscard]] int soulBeadAge(int64_t beadId);
void setSoulBeadAge(int64_t beadId, int age);
/// Ticks until the tiny potato pops out of a foxhound (0 if none).
[[nodiscard]] int taterTicks(int64_t foxhoundId);
/// Pending skeleton -> forgotten decision used by the spawn hook.
[[nodiscard]] bool replacesSkeleton(double roll, float y, bool fromSpawner);
} // namespace testing

} // namespace lp::feature::mobsdeep
