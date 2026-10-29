#pragma once

// U23 gadgets: Abacus, PathfinderMaps, AmbientDiscs, EndermoshMusicDisc, CompassesWorkEverywhere,
// TinyPotato. Behaviour reference: docs/spec/gadgets.md.
//
// This header holds the config section, the companion-protocol messages and the small helpers
// the module files share. Game types are forward declared so the client files can include it.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Player;
class ItemStack;
class ItemStackBase;
class CompoundTag;
class Vec3;
class BlockPos;

namespace lp::feature::gadgets {

// ---- config (config/gadgets.json) ----------------------------------------------------------------

struct Rgba {
    double r = 0, g = 0, b = 0, a = 0.4;
};

/// One Pathfinder Map trade (Quark's TradeInfo: enabled, level, minPrice, maxPrice + biome, colour).
struct TradeEntry {
    std::string biome;
    bool        enabled  = true;
    int         level    = 4;
    int         minPrice = 8;
    int         maxPrice = 14;
    std::string color    = "FFFFFFFF"; ///< ARGB hex
};

std::vector<TradeEntry> defaultTrades();

struct GadgetsConfig {
    int version = 1;

    bool enableAbacus   = true;
    Rgba highlightColor = {};

    bool                     enablePathfinderMaps        = true;
    std::vector<std::string> customs                     = {};
    bool                     applyDefaultTrades          = true;
    int                      pathfindersQuillSpeed       = 32;
    bool                     multiThreaded               = true;
    bool                     allowRetrying               = true;
    int                      searchRadius                = 6400;
    int                      xpFromTrade                 = 5;
    bool                     addToCartographer           = true;
    bool                     addToWanderingTraderForced  = true;
    bool                     addToWanderingTraderGeneric = false;
    bool                     addToWanderingTraderRare    = false;
    bool                     drawHud                     = true;
    bool                     hudOnTop                    = false;
    std::vector<TradeEntry>  builtinTrades               = defaultTrades();

    bool   enableAmbientDiscs = true;
    bool   dropOnSpiderKill   = true;
    double volume             = 3;

    bool enableEndermoshMusicDisc            = true;
    bool playEndermoshDuringEnderdragonFight = false;
    bool addToEndCityLoot                    = true;
    int  lootWeight                          = 5;
    int  lootQuality                         = 1;

    bool enableCompassesWorkEverywhere = true;
    bool enableCompassNerf             = true;
    bool enableClockNerf               = true;
    bool enableNether                  = true;
    bool enableEnd                     = true;

    bool enableTinyPotato       = true;
    bool neverUseHeartOfDiamond = false;
};

[[nodiscard]] GadgetsConfig const& cfg();

// ---- companion protocol ------------------------------------------------------------------------

/// Server -> mod client: what the held abacus shows (count < 0: nothing).
struct AbacusMessage {
    static constexpr std::string_view kId = "gadgets:abacus";
    int count = -1; ///< clamped Manhattan distance (0..48), -1 = nothing to show
};

/// Server -> mod client: a pathfinder's quill is searching (or stopped).
struct QuillMessage {
    static constexpr std::string_view kId = "gadgets:quill";
    bool        searching = false;
    std::string biome;      ///< Java id
    int         color = -1; ///< ARGB
};

/// Server -> mod client: the name of the tiny potato the player looks at ("" = none).
struct PotatoNameMessage {
    static constexpr std::string_view kId = "gadgets:potato_name";
    std::string name;
    std::string extra; ///< "[WIP]" / "(soon)" line, or ""
};

// ---- ids ---------------------------------------------------------------------------------------

inline constexpr std::string_view kAbacus    = "lepton:abacus";
inline constexpr std::string_view kQuill     = "lepton:pathfinders_quill";
inline constexpr std::string_view kPotato    = "lepton:tiny_potato";
inline constexpr std::string_view kEndermosh = "lepton:music_disc_endermosh";
inline constexpr char const*      kAmbientNames[] = {"drips", "ocean", "rain", "wind", "fire", "clock", "crickets", "chatter"};
inline constexpr int              kAbacusMax      = 48;

/// "lepton:music_disc_<name>" -> "<name>" for the eight ambience discs, else "".
[[nodiscard]] std::string ambientNameOf(std::string_view itemId);

// ---- shared helpers (server thread; Gadgets.cpp) -------------------------------------------------

/// Current server tick counter of the unit (advances in ServerLevelTickEvent).
[[nodiscard]] uint64_t now();
/// Runs `fn` after `ticks` server ticks (>= 1).
void later(int ticks, std::function<void()> fn);
/// The online player with this unique id.
[[nodiscard]] Player* playerById(int64_t id);

/// Java `displayClientMessage(msg, true)`: the action bar.
void actionBar(Player& player, std::string const& text);
/// Plays a resource-pack sound to one player at `pos`.
void playSoundTo(Player& player, std::string const& name, Vec3 const& pos, float volume = 1, float pitch = 1);
/// Plays to every player of `dim` within `range` blocks of `pos`.
void playSoundNear(int dim, std::string const& name, Vec3 const& pos, float volume, float pitch, float range);
void stopSoundTo(Player& player, std::string const& name);
/// Spawns a particle effect for everyone in `dim` (`Level::spawnParticleEffect`).
void particle(int dim, std::string const& effect, Vec3 const& pos);

/// The stack's user data, created when missing.
CompoundTag& userData(ItemStackBase& stack);
[[nodiscard]] std::optional<int>         getInt(ItemStackBase const& stack, std::string_view key);
[[nodiscard]] std::optional<std::string> getString(ItemStackBase const& stack, std::string_view key);
[[nodiscard]] bool                       getBool(ItemStackBase const& stack, std::string_view key);
void                                     putInt(ItemStackBase& stack, std::string const& key, int value);
void                                     putString(ItemStackBase& stack, std::string const& key, std::string const& value);
void                                     putBool(ItemStackBase& stack, std::string const& key, bool value);
void                                     removeKey(ItemStackBase& stack, std::string_view key);

/// Removes the world's recipes with these ids (Bedrock recipe identifiers, "lepton:abacus").
int removeRecipes(std::vector<std::string> const& ids);

// ---- module entry points (called by the unit module in Gadgets.cpp) -------------------------------

namespace abacus {
void install();
void uninstall();
/// Binds / unbinds the held abacus (Quark `AbacusItem.useOn`).
void onUse(Player& player, BlockPos const& pos);
void tick();
[[nodiscard]] int countFor(ItemStackBase const& abacus, BlockPos const& target, bool targetIsAir);
} // namespace abacus

namespace pathfinder {
void install();
void uninstall();
void tick();
void onUseItem(Player& player, ItemStack const& item);
void onWorldClosed();
} // namespace pathfinder

namespace discs {
void install();
void uninstall();
void tick();
} // namespace discs

namespace compasses {
void tick();
} // namespace compasses

namespace potato {
void install();
void uninstall();
void tick();
void onWorldReady();
void onWorldSave();
void onWorldClosed();
} // namespace potato

} // namespace lp::feature::gadgets
