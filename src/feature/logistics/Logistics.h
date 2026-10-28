#pragma once

// U11 logistics: Chute, Pipes, FeedingTrough and Crate (Quark 4.1-488 automation / oddities).
// Shared declarations of the unit (config, block ids, faces, item ports).

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "core/GlobalPos.h"

class BlockPos;
class BlockSource;
class Container;
class ItemStack;
class Player;
class Vec3;

namespace lp::game::vc {}

namespace lp::feature::logistics {

namespace vc = lp::game::vc;

// ---- config (config/logistics.json) ---------------------------------------------------------------

struct LogisticsConfig {
    int version = 1;
    // Chute
    bool enableChute = true;
    // Pipes
    bool enablePipes        = true;
    int  pipeSpeed          = 5;    ///< how long it takes an item to cross a pipe (bigger = slower)
    int  maxPipeItems       = 16;   ///< 0 = no limit; a pipe holding more breaks
    bool doPipesWhoosh      = true; ///< sounds when pipes eject / absorb items
    bool enableEncasedPipes = true;
    bool renderPipeItems    = true;
    bool emitVibrations     = true;
    // FeedingTrough
    bool   enableFeedingTrough = true;
    int    cooldown            = 30;
    int    maxAnimals          = 32;
    double loveChance          = 0.333333333;
    double range               = 10;
    double lookChance          = 0.015;
    // Crate
    bool enableCrate = true;
    int  maxItems    = 640;
};

[[nodiscard]] LogisticsConfig const& cfg();

// ---- ids --------------------------------------------------------------------------------------------

inline constexpr char const* kChute         = "lepton:chute";
inline constexpr char const* kPipe          = "lepton:pipe";
inline constexpr char const* kEncasedPipe   = "lepton:encased_pipe";
inline constexpr char const* kFeedingTrough = "lepton:feeding_trough";
inline constexpr char const* kCrate         = "lepton:crate";
inline constexpr char const* kPipeItem      = "lepton:pipe_item"; ///< display entity of items in pipes
inline constexpr char const* kGrate         = "lepton:grate";     ///< U5 (by id only)

[[nodiscard]] inline bool isPipe(std::string_view id) { return id == kPipe || id == kEncasedPipe; }

// ---- faces (Bedrock FacingID = Java Direction ordinal) ---------------------------------------------

enum Face : int { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };
inline constexpr std::array<int, 6> kFaces{Down, Up, North, South, West, East};
inline constexpr std::array<char const*, 6> kFaceNames{"down", "up", "north", "south", "west", "east"};
inline constexpr std::array<std::array<int, 3>, 6> kFaceStep{{{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}}};
[[nodiscard]] constexpr int  opposite(int f) { return f ^ 1; }
[[nodiscard]] constexpr int  axisOf(int f) { return f / 2; } ///< 0 y, 1 z, 2 x
[[nodiscard]] core::GlobalPos offset(core::GlobalPos const& p, int face);
[[nodiscard]] BlockPos         offset(BlockPos const& p, int face);

/// Inverse of `GlobalPos::key()` ("dim:x:y:z").
[[nodiscard]] core::GlobalPos parseKey(std::string_view key);
/// Block id at a global position ("" when not loaded).
[[nodiscard]] std::string idAt(core::GlobalPos const& pos);
/// Redstone next to the block (game/Shapes approximation).
[[nodiscard]] bool powered(BlockSource& region, BlockPos const& pos);
/// Spawns an item entity with the given motion (no random spread).
void spawnItemAt(BlockSource& region, ItemStack const& stack, Vec3 const& pos, Vec3 const& motion, bool pickupDelay);
/// Plays a level sound event / named sound at a position for everyone nearby.
void playSoundAt(BlockSource& region, Vec3 const& pos, std::string const& sound, float volume = 1, float pitch = 1);

// ---- item ports ---------------------------------------------------------------------------------
//
// Custom blocks have no block entity, so hoppers, droppers and pipes would not see them as
// containers. A port gives a block position a container face: Lepton hooks the game's container
// lookups (Hopper::_getContainerInBlock, DropperBlockActor::getContainerAt) and returns a
// `Container` proxy that forwards to the port. Proxies live until the next server tick.

class Port {
public:
    virtual ~Port() = default;
    [[nodiscard]] virtual int              size() const = 0;
    [[nodiscard]] virtual ItemStack const& get(int slot) const = 0;
    virtual void                           set(int slot, ItemStack const& item) = 0;
    /// `face`: the face of this block the item comes through.
    [[nodiscard]] virtual bool canPushIn(int slot, int face, ItemStack const& item) const;
    [[nodiscard]] virtual bool canPullOut(int slot, int face, ItemStack const& item) const;
    [[nodiscard]] virtual int  maxStack() const { return 64; }
    /// Items of `item` that slot `slot` can still take (crates cap their total).
    [[nodiscard]] virtual int room(int slot, ItemStack const& item) const;
};

using PortFactory = std::function<std::unique_ptr<Port>(core::GlobalPos const&)>;
void registerPort(std::string const& blockId, PortFactory factory);
void unregisterPort(std::string const& blockId);

/// The container at `pos`: a vanilla block entity / entity container (as droppers see it) or a
/// Lepton port. nullptr if none.
[[nodiscard]] Container* containerAt(BlockSource& region, BlockPos const& pos);
/// Only Lepton ports (nullptr for anything else).
[[nodiscard]] Container* portAt(BlockSource& region, BlockPos const& pos);

/// Inserts as much of `stack` as fits into `c` through `face` (the face of the target block the
/// item enters by), merging first. `stack` keeps the rest. Returns the amount moved.
int insertThroughFace(Container& c, ItemStack& stack, int face, bool simulate = false);

// ---- per-module hooks used across files ----------------------------------------------------------

namespace pipes {
/// Passes an item into the pipe at `pos` through `face`. False if there is no enabled pipe.
bool passIn(core::GlobalPos const& pos, ItemStack const& stack, int face);
[[nodiscard]] bool isEnabledPipe(core::GlobalPos const& pos);
/// Items currently travelling in the pipe at `pos`.
[[nodiscard]] int itemCount(core::GlobalPos const& pos);
/// Recomputes the connection states of the pipe at `pos` (and nothing else).
void refresh(BlockSource& region, BlockPos const& pos);
} // namespace pipes

namespace trough {
/// Opens the trough's screen for `player` (as using the block does).
bool open(Player& player, core::GlobalPos const& pos);
} // namespace trough

} // namespace lp::feature::logistics
