#pragma once

// U18 cosmetics: GreenerGrass, VariantAnimalTextures, BucketsShowInhabitants, SoulCandles,
// WoolShutsUpMinecarts, UsesForCurses, MoreVillagers, Camera, and GrabChickens' carried-chicken render.
// Spec: docs/spec/cosmetics.md. Server parts live here; client parts in src/client/cosmetics/.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Actor;
class ItemStackBase;

namespace lp::feature::cosmetics {

/// Quark's ConvulsionMatrixConfig: three output rows of three input weights.
struct ColorMatrix {
    std::vector<double> r;
    std::vector<double> g;
    std::vector<double> b;
};

struct CosmeticsConfig {
    int version = 1;

    bool                     enableGreenerGrass = true;
    bool                     affectLeaves       = true;
    bool                     affectWater        = false;
    std::vector<std::string> blockList{
        "minecraft:large_fern",
        "minecraft:tall_grass",
        "minecraft:grass_block",
        "minecraft:fern",
        "minecraft:short_grass",
        "minecraft:potted_fern",
        "minecraft:sugar_cane",
        "environmental:giant_tall_grass",
        "grassslabs:grass_carpet",
        "grassslabs:grass_slab",
        "grassslabs:grass_stairs",
    };
    std::vector<std::string> leavesList{
        "minecraft:spruce_leaves",
        "minecraft:birch_leaves",
        "minecraft:oak_leaves",
        "minecraft:jungle_leaves",
        "minecraft:acacia_leaves",
        "minecraft:dark_oak_leaves",
        "atmospheric:rosewood_leaves",
        "atmospheric:morado_leaves",
        "atmospheric:yucca_leaves",
        "autumnity:maple_leaves",
        "environmental:willow_leaves",
        "environmental:hanging_willow_leaves",
        "minecraft:vine",
        "valhelsia_structures:hanging_vines",
    };
    ColorMatrix colorMatrix{{0.89, 0.00, 0.00}, {0.00, 1.11, 0.00}, {0.00, 0.00, 0.89}};
    ColorMatrix waterMatrix{{0.86, 0.00, 0.00}, {0.00, 1.00, 0.22}, {0.00, 0.00, 1.22}};

    bool enableVariantAnimalTextures = true;
    bool enableCow                   = true;
    bool enablePig                   = true;
    bool enableChicken               = true;
    bool enableAllay                 = true;
    bool enableShinyRabbit           = true;
    bool enableShinyLlama            = true;
    bool enableShinyDolphin          = true;
    bool enableShinySlime            = true;
    bool enableShinyArmadillo        = true;
    bool enableShinyFrog             = true;
    bool enableLGBTBees              = true;
    bool everyBeeIsLGBT              = false;
    int  shinyAnimalChance           = 2048;
    bool shinySparkles               = true;

    bool enableBucketsShowInhabitants = true;
    bool showAxolotls                 = true;
    bool showCrabs                    = true;
    bool showTropicalFish             = true;
    bool showShinySlime               = true;

    bool enableSoulCandles = true;

    bool enableWoolShutsUpMinecarts = true;

    bool enableUsesForCurses             = true;
    bool vanishPumpkinOverlay            = true;
    bool bindArmorStandsWithPlayerHeads  = true;

    bool enableMoreVillagers = true;
    bool oceanVillager       = true;
    bool beachVillager       = true;

    bool        enableCamera = true;
    std::string dateFormat   = "MM/dd/yyyy";
};

[[nodiscard]] CosmeticsConfig const& config();

// ---- VariantAnimalTextures rules (pure) ---------------------------------------------------------

/// The 17 bee skins, in Quark's order. Random LGBT bees use `|m % 16|`, so the last one (`salmacibee`)
/// only comes from its name (Quark meant to exclude `helen`, but its `size() - 1` drops the last entry).
inline constexpr std::array<std::string_view, 17> kBeeVariants{
    "acebee", "agenbee", "arobee", "beefluid", "beesexual", "beequeer", "enbee",       "gaybee",     "interbee",
    "lesbeean", "panbee", "polysexbee", "transbee", "helen", "mlbeem", "polyamorbee", "salmacibee",
};

/// Skin id values written to the synced `SkinId` (read by the resource pack as `query.skin_id`).
inline constexpr int kShinySkin   = 31;
inline constexpr int kSkinMask    = 31;  ///< low 5 bits: the skin
inline constexpr int kCarriedFlag = 32;  ///< chicken carried by a player
inline constexpr int kSecondSeat  = 64;  ///< chicken on the player's second seat
inline constexpr int kOffsetUnit  = 128; ///< chicken offset code (0..200) * 128

/// Quark's "most significant bits" of an entity's UUID, derived from the Bedrock unique id.
[[nodiscard]] int64_t mostBits(int64_t uniqueId);
/// Java `new Random(seed).nextDouble()`.
[[nodiscard]] double javaRandomDouble(int64_t seed);
/// Quark's isShiny.
[[nodiscard]] bool isShiny(int64_t most, int chance);
/// Variant skin among `count` extra skins + vanilla: 0 = vanilla, 1..count = extra skin.
[[nodiscard]] int variantSkin(int64_t most, int count);
/// Bee skin (0 = vanilla, 1..17 = kBeeVariants index + 1) from the custom name and the id rule.
[[nodiscard]] int beeSkin(int64_t most, std::string const& customName, bool enabled, bool everyBeeIsLgbt);

/// Number of extra skins per animal type id ("minecraft:cow" -> 4); 0 = shiny only, -1 = none.
[[nodiscard]] int extraSkins(std::string_view type);
/// The skin the server writes for an animal (0 vanilla, 1..n variant, 31 shiny). Chickens without flags.
[[nodiscard]] int animalSkin(std::string_view type, int64_t uniqueId, std::string const& customName, bool isBaby);
/// True if the actor type sparkles when shiny (Quark's isSparkly list, honouring the keys).
[[nodiscard]] bool sparkles(std::string_view type);

/// Chicken offset code for the resource pack: round((offset + 1) * 100), offset clamped to -1..1.
[[nodiscard]] int chickenOffsetCode(double offset);

// ---- GreenerGrass (pure) ---------------------------------------------------------------------

/// Quark's ConvulsionMatrixConfig.convolve on 0xRRGGBB (alpha kept).
[[nodiscard]] uint32_t convolve(ColorMatrix const& m, uint32_t argb);

// ---- server state, used by tests ----------------------------------------------------------------

/// Runs the server pass on one actor now (skins, flags, villager types). Returns true if it applied.
bool applyTo(Actor& actor);
/// GrabChickens settings read from config/farming.json (U25): enabled and offset.
struct FarmingView {
    bool   enableGrabChickens = true;
    double chickenOffset      = 0.0;
};
[[nodiscard]] FarmingView farmingView();
/// Test seam: overrides the values read from config/farming.json (nullopt = read the file again).
void setFarmingViewForTests(std::optional<FarmingView> v);
/// True if `item` is a player head with Curse of Binding.
[[nodiscard]] bool isBoundPlayerHead(ItemStackBase const& item);
/// Biome rule of MoreVillagers: "beach", "ocean" or "" for the biome name.
[[nodiscard]] std::string_view villagerTypeForBiome(std::string_view biome);
/// Mark variant for a tagged villager (7 beach, 8 ocean), or -1.
[[nodiscard]] int villagerMarkVariant(Actor const& villager);

inline constexpr int kBeachMarkVariant = 7;
inline constexpr int kOceanMarkVariant = 8;
inline constexpr std::string_view kBeachTag = "lepton:villager_beach";
inline constexpr std::string_view kOceanTag = "lepton:villager_ocean";

/// Skins forced by `/quark cosmetics gallery` (unique id -> skin), kept until the world closes.
[[nodiscard]] std::unordered_map<int64_t, int>& forcedSkins();

/// The synced skin id of an actor (`query.skin_id`).
[[nodiscard]] int skinIdOf(Actor const& actor);
/// The synced mark variant (`query.mark_variant`).
[[nodiscard]] int markVariantOf(Actor const& actor);

} // namespace lp::feature::cosmetics
