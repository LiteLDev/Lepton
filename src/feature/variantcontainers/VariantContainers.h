#pragma once

// U9 variantcontainers: Quark's VariantChests and VariantFurnaces (docs/spec/variantcontainers.md).
//
// Chests are custom blocks (tags lepton:variant_chest / lepton:variant_trapped_chest) whose items
// live in game/VirtualContainer: a single chest under its own position (27 slots), a double chest
// under its "first" half (Java type right, the half on the player's left; 54 slots). Furnaces are
// custom blocks that get the vanilla furnace block actor (BlockPatch.cpp).

#include <optional>
#include <string>
#include <vector>

#include "mc/world/level/BlockPos.h"

#include "core/GlobalPos.h"

class Block;
class BlockSource;
class Container;
class ItemStack;
class Player;

namespace lp::feature::variantcontainers {

struct Config {
    int                      version                     = 1;
    bool                     enableVariantChests         = true;
    bool                     enableRevertingWoodenChests = true;
    bool                     replaceWorldgenChests       = true;
    std::vector<std::string> structureChests{};
    bool                     enableVariantFurnaces = true;
};
[[nodiscard]] Config const& cfg();

// ---- directions (Java 2D data values: 0 S, 1 W, 2 N, 3 E) -----------------------------------------

[[nodiscard]] inline int cw(int f) { return (f + 1) & 3; }  ///< Java Direction#getClockWise
[[nodiscard]] inline int ccw(int f) { return (f + 3) & 3; } ///< Java Direction#getCounterClockWise
[[nodiscard]] inline int opp(int f) { return (f + 2) & 3; }
[[nodiscard]] char const* dirName(int f);  ///< "south", "west", "north", "east"
[[nodiscard]] int         dirIndex(std::string_view name); ///< -1 when unknown
[[nodiscard]] BlockPos    step(BlockPos const& pos, int f);

// ---- chests -----------------------------------------------------------------------------------------

enum class ChestType { Single, Left, Right };

struct ChestState {
    std::string id;      ///< block id
    int         facing;  ///< Java FACING (the front), = opposite of minecraft:cardinal_direction
    ChestType   type;
    int         lid;     ///< 0..4
    bool        trapped;
};

inline constexpr int kChestSlots = 27;

[[nodiscard]] bool                      isVariantChest(Block const& block);
[[nodiscard]] std::optional<ChestState> chestState(Block const& block);
/// The block with the given chest states changed (nullptr if not a chest / invalid).
[[nodiscard]] Block const* withChest(Block const& block, std::optional<int> facing, std::optional<ChestType> type,
    std::optional<int> lid = std::nullopt, std::optional<int> power = std::nullopt);
/// Direction of the partner of a double chest half (Java ChestBlock#getConnectedDirection).
[[nodiscard]] int connectedDir(ChestState const& s);
/// The partner position when `pos` holds a valid double chest half whose partner points back.
[[nodiscard]] std::optional<BlockPos> partnerOf(BlockSource& region, BlockPos const& pos);
/// Where the items of the chest at `pos` are stored and how many slots it has (27 or 54).
struct ChestStorage {
    core::GlobalPos key;
    int             size = kChestSlots;
    std::optional<BlockPos> second; ///< the other half of a double chest
};
[[nodiscard]] std::optional<ChestStorage> chestStorage(BlockSource& region, BlockPos const& pos);
/// Rolls a pending structure loot table into the chest (first open, hopper, comparator, break).
void ensureLoot(BlockSource& region, BlockPos const& pos, Player* opener = nullptr);
/// Comparator signal of the chest at `pos` (whole double chest).
[[nodiscard]] int chestComparatorSignal(BlockSource& region, BlockPos const& pos);
/// Container the game's hoppers / droppers / comparators see for a chest (nullptr: not a chest).
[[nodiscard]] Container* chestContainerAt(BlockSource& region, BlockPos const& pos);
/// Signals comparators next to the chest (both halves) that its contents changed.
void notifyChestChanged(BlockSource& region, BlockPos const& pos);
/// Opens the chest for the player (blocked / busy checks included). True if it opens.
bool openChest(Player& player, BlockPos const& pos);
/// Remembers a loot table to roll into the chest at `pos` (structure chests).
void setPendingLoot(core::GlobalPos const& pos, std::string const& table, int seed);
/// Number of players viewing the chest at `pos` (either half).
[[nodiscard]] int chestViewers(BlockSource& region, BlockPos const& pos);

// ---- furnaces ---------------------------------------------------------------------------------------

[[nodiscard]] bool isVariantFurnace(Block const& block);
[[nodiscard]] int  furnaceComparatorSignal(BlockSource& region, BlockPos const& pos);
/// Called by the patched block type when a variant furnace is removed (drops items and XP).
void onFurnaceRemoved(BlockSource& region, BlockPos const& pos);
/// Opens the vanilla furnace screen of the variant furnace at `pos`. True if it opens.
bool openFurnace(Player& player, BlockPos const& pos);

// ---- block type patch (BlockPatch.cpp) --------------------------------------------------------------

/// Gives our block types their native behaviour (comparator signal, the furnace block actor).
/// Idempotent; done when the blocks register and again when a world is ready.
void patchBlockTypes();
/// True once the furnace block types carry the vanilla furnace block actor.
[[nodiscard]] bool furnacesPatched();

// ---- config gates (Gating.cpp) ----------------------------------------------------------------------

/// Recipe `lepton:<name>` exists under the current config (true for recipes of other units).
[[nodiscard]] bool recipeAllowed(std::string_view name);
/// Item `id` appears in the creative inventory under the current config.
[[nodiscard]] bool itemAllowed(std::string_view id);

// ---- shared helpers ---------------------------------------------------------------------------------

void playSoundAt(BlockSource& region, float x, float y, float z, std::string const& sound, float volume, float pitch);
void spawnItem(BlockSource& region, ItemStack const& stack, float x, float y, float z);
/// The block ids of every chest / trapped chest of the module (generated list).
[[nodiscard]] std::vector<std::string> const& chestIds();
[[nodiscard]] std::vector<std::string> const& furnaceIds();
/// The materials Quark has chests for (vanilla ones; other units add theirs by tag).
[[nodiscard]] std::vector<std::string> const& chestMaterials();
[[nodiscard]] bool isWoodenMaterial(std::string_view material);

} // namespace lp::feature::variantcontainers
