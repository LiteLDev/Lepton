#pragma once

// U7 newwoods: Quark's AzaleaWood, AncientWood and BlossomTrees (docs/spec/newwoods.md).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "feature/newwoods/JavaRandom.h"

class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class ItemStackBase;

namespace lp::feature::newwoods {

// ---- config (docs §5) ----------------------------------------------------------------------------

struct DimensionsConfig {
    bool                     isBlacklist = false;
    std::vector<std::string> dimensions{"minecraft:overworld"};
};
struct BiomeTagsConfig {
    bool                     isBlacklist = false;
    std::vector<std::string> tags;
};
struct BiomeNamesConfig {
    bool                     isBlacklist = true;
    std::vector<std::string> biomes;
};
struct BiomesConfig {
    BiomeTagsConfig  tags;
    BiomeNamesConfig biomes;
};
/// Quark BlossomTreeConfig: dimensions, biome filter and rarity (1 in `rarity` chunks).
struct BlossomTreeConfig {
    DimensionsConfig dimensions;
    BiomesConfig     biomes;
    int              rarity = 100;
};

struct NewWoodsConfig {
    int  version                = 1;
    bool enableAzaleaWood       = true;
    bool replaceAzaleaTreeLog   = true;
    bool enableAncientWood      = true;
    bool ancientFruitGivesExp   = true;
    int  ancientFruitExpValue   = 10;
    int  ancientCityLootWeight  = 0;
    int  ancientCityLootQuality = 1;
    int  sniffingLootWeight     = 1;
    int  sniffingLootQuality    = 0;
    bool enableBlossomTrees     = true;
    BlossomTreeConfig blue{{}, {{false, {"c:is_snowy"}}, {}}, 200};
    BlossomTreeConfig lavender{{}, {{false, {"c:is_swamp"}}, {}}, 100};
    BlossomTreeConfig orange{{}, {{false, {"minecraft:is_savanna"}}, {}}, 100};
    BlossomTreeConfig yellow{{}, {{false, {"c:is_plains"}}, {}}, 200};
    BlossomTreeConfig red{{}, {{false, {"minecraft:is_badlands"}}, {}}, 30};
    bool dropLeafParticles = true;
};

[[nodiscard]] NewWoodsConfig const& config();

// ---- modules and ids -------------------------------------------------------------------------------

enum class Module { None, AzaleaWood, AncientWood, BlossomTrees };

/// The module a `lepton:` block / item id of this unit belongs to (None for other ids).
[[nodiscard]] Module moduleOfId(std::string_view id);
[[nodiscard]] bool   moduleEnabled(Module m);
/// Every block id this unit generates (docs §2).
[[nodiscard]] std::vector<std::string> const& allBlockIds();

// ---- trees -----------------------------------------------------------------------------------------

/// A tree of this unit: its sapling, leaves and log, and its shape.
struct TreeType {
    std::string_view name;    ///< "ancient", "blue_blossom", ...
    std::string_view sapling; ///< "lepton:ancient_sapling"
    std::string_view leaves;  ///< "lepton:ancient_leaves"
    std::string_view log;     ///< "lepton:ancient_log"
    bool             ancient; ///< ashen tree shape (else Java's fancy tree)
    Module           module;
};
[[nodiscard]] std::array<TreeType, 6> const& treeTypes();
[[nodiscard]] TreeType const*               treeOfSapling(std::string_view saplingId);
[[nodiscard]] TreeType const*               treeOfLeaves(std::string_view leavesId);
[[nodiscard]] TreeType const*               treeByName(std::string_view name);

/// Places the tree with its trunk at `root` (the sapling's position, which must be free). Java
/// TreeFeature#place: false (nothing placed) when it doesn't fit. Logs/leaves go only into air,
/// leaves and replaceable plants; blocks outside loaded chunks are skipped.
bool placeTree(BlockSource& region, BlockPos const& root, TreeType const& tree, JavaRandom& random, bool worldgen);

// ---- leaves (docs §3.1, §3.2) ---------------------------------------------------------------------

[[nodiscard]] bool isOurLeaves(Block const& b);
[[nodiscard]] bool isAnyLeaves(Block const& b);
[[nodiscard]] bool isLogLike(Block const& b);
/// Java LeavesBlock distance < 7: a log within 6 steps through leaves.
[[nodiscard]] bool leafSupported(BlockSource& region, BlockPos const& pos);
/// The Java loot of our leaves. `tool`: shears or silk touch; `fortune` 0..; `explosionRadius` > 0
/// when an explosion broke it.
[[nodiscard]] std::vector<std::pair<std::string, int>>
leafDrops(std::string_view leavesId, bool shearsOrSilk, int fortune, float explosionRadius, JavaRandom& random);
/// Marks leaves around `pos` for a support check (a log or leaf there changed).
void queueLeafCheck(BlockSource& region, BlockPos const& pos);
/// Runs pending leaf checks now (normally at the end of each tick). Returns how many leaves were
/// found unsupported.
int  runLeafChecks();
/// The block tick of an unsupported leaf: breaks it (with drops) if still unsupported.
void leafTick(BlockSource& region, BlockPos const& pos);

// ---- saplings (docs §3.3) ------------------------------------------------------------------------

[[nodiscard]] bool isOurSapling(Block const& b);
/// Java SaplingBlock#randomTick.
void saplingRandomTick(BlockSource& region, BlockPos const& pos, JavaRandom& random);
/// Java SaplingBlock#advanceTree: stage 0 -> 1, stage 1 -> grow. Returns true if something changed.
bool advanceSapling(BlockSource& region, BlockPos const& pos, JavaRandom& random);
/// Bone meal on a sapling (Java BoneMealItem#growCrop): 45% to advance, particles. Returns true when the
/// block is one of ours (the bone meal is used).
bool bonemealSapling(BlockSource& region, BlockPos const& pos, JavaRandom& random);
/// Schedules the next random-like block tick of a sapling / unsupported leaf.
void scheduleTick(BlockSource& region, BlockPos const& pos, Block const& block);
/// Mean ticks between two random ticks at the world's random tick speed (Java: 4096 / (3 * speed)).
[[nodiscard]] int maxRandomTickDelay();

// ---- world generation (docs §3.5, §3.6) -----------------------------------------------------------

/// Quark CompoundBiomeConfig#canSpawn for a Bedrock biome name.
[[nodiscard]] bool biomeAllowed(std::string const& biome, BiomesConfig const& c);
[[nodiscard]] bool dimensionAllowed(std::string_view dim, DimensionsConfig const& c);
/// The column (x, z) the blossom generator of `colour` (0..4) tries in chunk (cx, cz), and its
/// rarity roll; deterministic for a world seed.
struct BlossomRoll {
    int  x, z;
    bool rarityHit;
};
[[nodiscard]] BlossomRoll blossomRoll(uint64_t seed, int cx, int cz, int colour, int rarity);
/// Generates the blossom trees of chunk (cx, cz). Returns how many trees were placed.
int generateBlossomChunk(BlockSource& region, int cx, int cz, std::string_view dim, uint64_t seed);
/// Replaces the oak logs of generated azalea trees in chunk (cx, cz). Returns the logs replaced.
int replaceAzaleaTreesInChunk(BlockSource& region, int cx, int cz);
/// Replaces oak logs at `positions` (just grown from an azalea) by azalea logs.
int replaceAzaleaLogs(BlockSource& region, std::vector<BlockPos> const& positions);
/// Generation statistics since the server started ("chunks=.. trees=.. azalea=..").
[[nodiscard]] std::string genStats();

// ---- misc ------------------------------------------------------------------------------------------

/// Furnace burn time of our item in ticks (0 = not ours / not fuel).
[[nodiscard]] int fuelTicks(std::string_view itemId);
/// Composter chance in percent (-1 = not ours).
[[nodiscard]] int compostChance(std::string_view itemId);
/// Experience given by eating the Enchanted Fruit (0 = none).
[[nodiscard]] int fruitExperience();
/// Recipe gate: true when the recipe must not be loaded (its module is disabled).
[[nodiscard]] bool recipeDisabled(std::string const& recipeId, std::string const& result);
/// Loot additions (docs §3.8): the entry added to the table `dir`, or nullopt.
struct LootAddition {
    std::string item;
    int         weight;
    int         quality;
};
[[nodiscard]] std::optional<LootAddition> lootAddition(std::string_view dir);

/// Tags of a block by name.
[[nodiscard]] bool hasTag(Block const& block, std::string_view tag);

} // namespace lp::feature::newwoods
