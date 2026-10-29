#pragma once

// U12 crystals: Corundum, RainbowLamps, BeaconRedirection and Magnets (Quark 4.1-488).
// Shared declarations of the unit: config, colours, block classification, the position tracker,
// redstone reading, particles and Java's random generator. See docs/spec/crystals.md.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/GlobalPos.h"

class Block;
class BlockPos;
class BlockSource;
class Player;
class Vec3;

namespace lp::feature::crystals {

// ---- config ---------------------------------------------------------------------------------------

struct DimensionsConfig {
    bool                     isBlacklist = false;
    std::vector<std::string> dimensions{"minecraft:overworld"};
};
struct BiomeTagsConfig {
    bool                     isBlacklist = true;
    std::vector<std::string> tags{"minecraft:is_ocean"};
};
struct BiomeNamesConfig {
    bool                     isBlacklist = true;
    std::vector<std::string> biomes{};
};
struct BiomesConfig {
    BiomeTagsConfig  tags{};
    BiomeNamesConfig biomes{};
};
/// Zeta's ClusterSizeConfig of Quark's UndergroundStyleConfig.
struct GenerationSettings {
    DimensionsConfig dimensions{};
    BiomesConfig     biomes{};
    int              rarity              = 400;
    int              minYLevel           = 0;
    int              maxYLevel           = 64;
    int              horizontalSize      = 36;
    int              verticalSize        = 14;
    int              horizontalVariation = 8;
    int              verticalVariation   = 6;
};

struct CrystalsConfig {
    int version = 1;

    // Corundum
    bool               enableCorundum = true;
    GenerationSettings generationSettings{};
    double             crystalChance               = 0.16;
    double             crystalClusterChance        = 0.2;
    double             crystalClusterOnSidesChance = 0.6;
    double             doubleCrystalChance         = 0.2;
    int                caveCrystalGrowthChance     = 5;
    bool               crystalsCraftRunes          = true;
    bool               enableCollateralMovement    = true;

    // RainbowLamps
    bool enableRainbowLamps = true;
    int  lightLevel         = 15;
    bool useCorundum        = true;

    // BeaconRedirection
    bool enableBeaconRedirection      = true;
    int  horizontalMoveLimit          = 64;
    bool allowTintedGlassTransparency = true;

    // Magnets
    bool                     enableMagnets = true;
    std::vector<std::string> magneticDerivationList{
        "minecraft:iron_ingot",       "minecraft:copper_ingot",          "minecraft:exposed_copper",
        "minecraft:weathered_copper", "minecraft:oxidized_copper",       "minecraft:raw_iron",
        "minecraft:raw_copper",       "minecraft:iron_ore",              "minecraft:deepslate_iron_ore",
        "minecraft:copper_ore",       "minecraft:deepslate_copper_ore", "lepton:gravisand"};
    std::vector<std::string> magneticWhitelist{
        "minecraft:chipped_anvil",       "minecraft:damaged_anvil",       "minecraft:iron_horse_armor",
        "minecraft:chainmail_helmet",    "minecraft:chainmail_boots",     "minecraft:chainmail_leggings",
        "minecraft:chainmail_chestplate", "#minecraft:cauldrons"};
    std::vector<std::string> magneticBlacklist{
        "minecraft:tripwire_hook", "minecraft:map", "#minecraft:all_hanging_signs", "#minecraft:hanging_signs"};
    bool   usePreEndRecipe        = false;
    bool   usePistonLogic         = true;
    bool   affectEntities         = true;
    bool   affectsArmor           = true;
    double entitiesPullForce      = 0.18;
    bool   stoneCutterSilkTouch   = true;
    double stoneCutterMaxHardness = 3;
};

[[nodiscard]] CrystalsConfig const& cfg();

// ---- colours --------------------------------------------------------------------------------------

struct CrystalColor {
    std::string_view name;
    uint32_t         beacon; ///< 0xRRGGBB (Quark's CorundumColor.beaconColor)
};
inline constexpr std::array<CrystalColor, 9> kColors{{
    {"red", 0xff0000},
    {"orange", 0xff8000},
    {"yellow", 0xffff00},
    {"green", 0x00ff00},
    {"blue", 0x00ffff},
    {"indigo", 0x0000ff},
    {"violet", 0xff00ff},
    {"white", 0xffffff},
    {"black", 0x000000},
}};

[[nodiscard]] std::string corundumId(int color);       ///< lepton:<c>_corundum
[[nodiscard]] std::string waxedCorundumId(int color);  ///< lepton:waxed_<c>_corundum
[[nodiscard]] std::string clusterId(int color);        ///< lepton:<c>_corundum_cluster
[[nodiscard]] std::string paneId(int color);           ///< lepton:<c>_corundum_pane
[[nodiscard]] std::string lampId(int color);           ///< lepton:<c>_crystal_lamp
/// Ids of the nine unwaxed corundum blocks (Quark's #quark:corundum), for other units.
[[nodiscard]] std::vector<std::string> corundumIds();

// ---- block classification (by the generator's tags) ---------------------------------------------

[[nodiscard]] int  colorOf(Block const& b); ///< 0..8 for corundum/waxed/pane/cluster/lamp, -1 otherwise
[[nodiscard]] bool isCorundum(Block const& b);      ///< unwaxed corundum block
[[nodiscard]] bool isWaxedCorundum(Block const& b);
[[nodiscard]] bool isCluster(Block const& b);
[[nodiscard]] bool isPane(Block const& b);
[[nodiscard]] bool isLamp(Block const& b);
[[nodiscard]] bool isMagnet(Block const& b);
[[nodiscard]] bool isMagnetized(Block const& b);
[[nodiscard]] bool hasTag(Block const& b, char const* tag);

/// Bedrock FacingID: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
[[nodiscard]] std::array<int, 3> facingStep(int face);
[[nodiscard]] int                oppositeFace(int face);
[[nodiscard]] int                faceFromName(std::string_view name); ///< -1 if unknown
[[nodiscard]] std::string_view   faceToName(int face);
/// The pointing face of a corundum cluster (its minecraft:block_face), or -1.
[[nodiscard]] int clusterFacing(Block const& b);
[[nodiscard]] BlockPos offset(BlockPos const& pos, int face, int n = 1);

// ---- tracked positions --------------------------------------------------------------------------

/// Positions of the unit's ticking blocks (unwaxed corundum, lamps, magnets), kept in sync from
/// block changes and saved in the world DB (lp:crystals:<kind>).
enum class Kind { Corundum, Lamp, Magnet, Count };
[[nodiscard]] std::set<core::GlobalPos> const& tracked(Kind kind);
void                                         track(Kind kind, core::GlobalPos const& pos);
void                                         untrack(Kind kind, core::GlobalPos const& pos);
/// The tracker kind of a block (nullopt when not tracked).
[[nodiscard]] std::optional<Kind> kindOf(Block const& b);

// ---- redstone ---------------------------------------------------------------------------------------

/// The redstone power (0..15) a consumer block at `pos` receives (Java getBestNeighborSignal):
/// the circuit system's strength of its consumer component, or a neighbour scan when the block
/// has none.
[[nodiscard]] int redstonePower(BlockSource& region, BlockPos const& pos);

// ---- world helpers ------------------------------------------------------------------------------------

/// Sets a block with neighbour and client updates.
bool setBlock(BlockSource& region, BlockPos const& pos, Block const& block);
/// Destroys the block like Java's Level#destroyBlock(pos, drop).
bool destroyBlock(BlockSource& region, BlockPos const& pos, bool drop);
/// Sends a custom particle effect (RP particles/lepton_*.json) with molang variables to the players
/// near `pos` (within 64 blocks).
void spawnParticle(BlockSource& region, std::string const& effect, Vec3 const& pos,
                   std::vector<std::pair<std::string, float>> const& vars = {});
/// Plays a level sound event at the block.
void playSound(BlockSource& region, BlockPos const& pos, int levelSoundEvent, std::string const& blockId = {});
/// Java randomTickSpeed equivalent of the world (Bedrock gamerule randomtickspeed).
[[nodiscard]] int randomTickSpeed();

// ---- java.util.Random -----------------------------------------------------------------------------------

/// Bit-exact port of java.util.Random (and Minecraft's LegacyRandomSource).
class JavaRandom {
public:
    explicit JavaRandom(int64_t seed) { setSeed(seed); }
    void    setSeed(int64_t seed) { mSeed = (static_cast<uint64_t>(seed) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1); }
    int32_t next(int bits) {
        mSeed = (mSeed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
        return static_cast<int32_t>(static_cast<int64_t>(mSeed) >> (48 - bits));
    }
    int32_t nextInt() { return next(32); }
    int32_t nextInt(int32_t bound);
    /// Java 17+ RandomGenerator#nextInt(origin, bound) (used by java.util.Random).
    int32_t nextInt(int32_t origin, int32_t bound);
    int64_t nextLong() { return (static_cast<int64_t>(next(32)) << 32) + next(32); }
    double  nextDouble() {
        return static_cast<double>((static_cast<int64_t>(next(26)) << 27) + next(27)) * 0x1.0p-53;
    }
    float nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

private:
    uint64_t mSeed = 0;
};

/// Java String#hashCode.
[[nodiscard]] int32_t javaHash(std::string_view s);
/// Java Vec3i#hashCode.
[[nodiscard]] int32_t javaHash(int x, int y, int z);

// ---- module hooks shared between the unit's files -----------------------------------------------------

/// Quark's beacon colour multiplier of a block (getBeaconColorMultiplier): corundum, sheets, lit
/// lamps and stained glass. nullopt when the block does not tint beams.
[[nodiscard]] std::optional<uint32_t> beaconColorOf(Block const& b);

} // namespace lp::feature::crystals
