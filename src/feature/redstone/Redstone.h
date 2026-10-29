#pragma once

// U10 redstone: ChainsConnectBlocks, IronRod, PistonsMoveTileEntities, Gravisand, MetalButtons,
// ObsidianPlate, RedstoneRandomizer, EnderWatcher, DispensersPlaceBlocks. See docs/spec/redstone.md.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mc/world/level/BlockPos.h"

class Block;
class BlockSource;
class BlockType;

namespace lp::feature::redstone {

struct RedstoneConfig {
    int  version                       = 1;
    bool enableChainsConnectBlocks     = true;
    bool enableIronRod                 = true;
    bool usePreEndRecipe               = false;
    bool enablePistonsMoveTileEntities = true;
    bool enableChestsMovingTogether    = true;
    std::vector<std::string> renderBlacklist{"psi:programmer", "botania:starfield"};
    std::vector<std::string> movementBlacklist{
        "minecraft:spawner",
        "minecraft:trial_spawner",
        "minecraft:vault",
        "integrateddynamics:cable",
        "randomthings:blockbreaker",
        "minecraft:ender_chest",
        "minecraft:enchanting_table",
        "#minecraft:ceiling_hanging_signs",
        "#c:chests/trapped",
        "endergetic:bolloom_bud"
    };
    std::vector<std::string> namespaceMovementBlacklist{};
    std::vector<std::string> delayedUpdateList{"minecraft:dispenser", "minecraft:dropper"};
    int                      pistonPushLimit             = 12;
    bool                     enableGravisand             = true;
    bool                     enableMetalButtons          = true;
    bool                     enableIron                  = true;
    bool                     enableGold                  = true;
    bool                     enableObsidianPlate         = true;
    bool                     enableRedstoneRandomizer    = true;
    bool                     enableEnderWatcher          = true;
    bool                     enableDispensersPlaceBlocks = true;
    std::vector<std::string> blacklist{"minecraft:water", "minecraft:lava", "minecraft:fire"};
    bool                     wrapExistingBehaviors = true;
};

[[nodiscard]] RedstoneConfig const& cfg();
/// The unit's modules are installed (false after uninstall).
[[nodiscard]] bool installed();

// ---- ticks ---------------------------------------------------------------------------------------
/// Server ticks since the unit was installed.
[[nodiscard]] uint64_t now();
/// Runs `fn` on the server thread `delay` ticks from now (>= 1). Dropped when the world closes.
void later(uint64_t delay, std::function<void()> fn);
/// Per-tick listeners (run every server tick after the scheduled tasks).
void onTick(std::function<void()> fn);
/// True on the server thread (queued ticks can run on chunk-loading worker threads).
[[nodiscard]] bool onServerThread();

// ---- geometry --------------------------------------------------------------------------------------
/// Bedrock FacingID: 0 down, 1 up, 2 north (-z), 3 south (+z), 4 west (-x), 5 east (+x).
[[nodiscard]] BlockPos offset(BlockPos const& p, int face, int n = 1);
[[nodiscard]] constexpr int opposite(int face) { return face ^ 1; }
[[nodiscard]] int              faceFromName(std::string_view name); ///< -1 if unknown
[[nodiscard]] std::string_view faceToName(int face);
/// Horizontal rotations of FacingIDs (Java Direction#getClockWise seen from above).
[[nodiscard]] int clockwise(int face);
[[nodiscard]] int counterClockwise(int face);

// ---- blocks ------------------------------------------------------------------------------------------
[[nodiscard]] bool isServerRegion(BlockSource const& region);
[[nodiscard]] int  dimOf(BlockSource const& region);
/// The block has a non-empty collision box at `pos`.
[[nodiscard]] bool hasCollision(BlockSource& region, BlockPos const& pos, Block const& b);
/// Falling blocks can enter `b` (air, fire, liquids, replaceable plants).
[[nodiscard]] bool canFallThrough(BlockSource& region, BlockPos const& pos);
/// Destroys the block (Level::destroyBlock: particles, sound, drops when `drop`).
bool destroyBlock(BlockSource& region, BlockPos const& pos, bool drop);
/// Spawns an item entity at the centre of `pos`.
void dropItem(BlockSource& region, BlockPos const& pos, std::string_view id, int count = 1);
/// Sends a redstone-dust particle (`minecraft:redstone_wire_dust_particle`) at `pos`.
void dustParticle(BlockSource& region, float x, float y, float z);
/// Plays a level event (LevelEvent id) at `pos`.
void levelEvent(BlockSource& region, int event, BlockPos const& pos, int data = 0);

// ---- redstone --------------------------------------------------------------------------------------
/// Strength of the circuit component at `pos` (consumers: the power they receive), nullopt when the
/// position has none. Read from the dimension's circuit scene graph (cached per tick).
[[nodiscard]] std::optional<int> componentStrength(BlockSource& region, BlockPos const& pos);
/// The signal the block at `receiver + face` sends towards `receiver` (Java Level#getSignal(neighbour,
/// face)), in Bedrock terms: wire power, producers, diodes pointing at the receiver, powered blocks,
/// Lepton's randomizers and ender watchers.
[[nodiscard]] int signalFrom(BlockSource& region, BlockPos const& receiver, int face);
/// Java Level#hasNeighborSignal.
[[nodiscard]] bool hasNeighborSignal(BlockSource& region, BlockPos const& pos);

// ---- block type virtual tables ---------------------------------------------------------------------
/// Gives the block type its own copy of its virtual table with `slot` replaced by `fn` (idempotent).
/// `member` is a pointer to a virtual member function of BlockType, used to find the slot index.
bool patchVirtual(BlockType& type, void* memberThunk, void* fn);
/// Restores every patched block type's original virtual table.
void unpatchAll();

// ---- parts (installed by Common.cpp) -------------------------------------------------------------------
void installPistons();
void uninstallPistons();
void installGravisand();
void uninstallGravisand();
void onWorldReadyGravisand();
void installDispensers();
void uninstallDispensers();
/// Called from the Block::queuedTick hook for the unit's ticking blocks (server thread).
void randomizerTick(BlockSource& region, BlockPos const& pos, Block const& block);
void watcherTick(BlockSource& region, BlockPos const& pos, Block const& block);
/// Called from the Block::neighborChanged hook for gravisand / randomizers.
void gravisandNeighborChanged(BlockSource& region, BlockPos const& pos);

// ---- test hooks ----------------------------------------------------------------------------------------
namespace testing {
/// Forces the randomizer's next random choice (true = left) for the next `n` choices; -1 = random.
void forceRandomizerSide(int side);
/// The number of falling gravisand entities ticked last tick.
[[nodiscard]] int fallingGravisand();
/// Last piston resolution: whether the Lepton resolver was used, and its lists.
struct PistonTrace {
    bool                  usedLepton = false;
    bool                  result     = false;
    std::vector<BlockPos> push;
    std::vector<BlockPos> destroy;
};
[[nodiscard]] PistonTrace const& lastPiston();
/// Bedrock movement type of a block id (0 push_pull, 1 push, 2 popped, 3 immovable), -1 if unknown;
/// `blockEntity` is set when the block has a block entity.
[[nodiscard]] int movementTypeOf(std::string_view id, bool* blockEntity = nullptr);
/// The paired chest of the chest at `pos`, if any.
[[nodiscard]] std::optional<BlockPos> chestPartnerOf(BlockSource& region, BlockPos const& pos);
/// Dispenser placements done since install.
[[nodiscard]] int dispenserPlacements();
} // namespace testing

} // namespace lp::feature::redstone
