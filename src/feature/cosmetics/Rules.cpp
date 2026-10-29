// Pure rules of VariantAnimalTextures and GreenerGrass (docs/spec/cosmetics.md §1, §3).
#include "feature/cosmetics/Cosmetics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <regex>

#include "mod/Config.h"

LP_REGISTER_CONFIG("cosmetics", lp::feature::cosmetics::CosmeticsConfig);

namespace lp::feature::cosmetics {

CosmeticsConfig const& config() { return lp::config::get<CosmeticsConfig>(); }

int64_t mostBits(int64_t uniqueId) {
    // SplitMix64: Bedrock unique ids are sequential, Quark's UUID bits are random.
    uint64_t z = static_cast<uint64_t>(uniqueId) + 0x9E3779B97F4A7C15ull;
    z          = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z          = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return static_cast<int64_t>(z ^ (z >> 31));
}

double javaRandomDouble(int64_t seed) {
    constexpr uint64_t kMul  = 0x5DEECE66Dull;
    constexpr uint64_t kMask = (1ull << 48) - 1;
    uint64_t           s     = (static_cast<uint64_t>(seed) ^ kMul) & kMask;
    auto               next  = [&](int bits) {
        s = (s * kMul + 0xBull) & kMask;
        return static_cast<int64_t>(s >> (48 - bits));
    };
    int64_t hi = next(26);
    int64_t lo = next(27);
    return static_cast<double>((hi << 27) + lo) * 0x1.0p-53;
}

bool isShiny(int64_t most, int chance) { return chance > 0 && most % chance == 0; }

int variantSkin(int64_t most, int count) {
    if (count <= 0) return 0;
    // Quark: styles = [variant1..variantN, vanilla]; choice = |(int)(most % (N + 1))|.
    int choice = std::abs(static_cast<int>(most % (count + 1)));
    return choice < count ? choice + 1 : 0;
}

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
int beeIndex(std::string_view name) {
    for (size_t i = 0; i < kBeeVariants.size(); ++i)
        if (kBeeVariants[i] == name) return static_cast<int>(i);
    return -1;
}
} // namespace

int beeSkin(int64_t most, std::string const& customName, bool enabled, bool everyBeeIsLgbt) {
    if (!enabled) return 0;
    constexpr double kLgbtChance = 0.056;
    bool             lgbt        = everyBeeIsLgbt || javaRandomDouble(most) < kLgbtChance;
    if (customName.empty() && !lgbt) return 0;
    std::string name = lower(customName);
    if (beeIndex(name) < 0) {
        static std::regex const wire("wire(se|bee)gal");
        if (std::regex_match(customName, wire)) name = "enbee";
        else if (lgbt) {
            // Quark: `most % (size - 1)` (meant to skip helen; in fact the last skin never spawns).
            auto n = static_cast<int64_t>(kBeeVariants.size() - 1);
            name   = std::string(kBeeVariants[static_cast<size_t>(std::abs(static_cast<int>(most % n)))]);
        }
    }
    int idx = beeIndex(name);
    return idx < 0 ? 0 : idx + 1;
}

int extraSkins(std::string_view type) {
    if (type == "minecraft:cow") return 4;
    if (type == "minecraft:pig") return 3;
    if (type == "minecraft:chicken") return 6;
    if (type == "minecraft:allay") return 5;
    if (type == "minecraft:rabbit" || type == "minecraft:llama" || type == "minecraft:dolphin" || type == "minecraft:slime"
        || type == "minecraft:armadillo" || type == "minecraft:frog")
        return 0;
    return -1;
}

namespace {
bool typeEnabled(CosmeticsConfig const& c, std::string_view type) {
    if (type == "minecraft:cow") return c.enableCow;
    if (type == "minecraft:pig") return c.enablePig;
    if (type == "minecraft:chicken") return c.enableChicken;
    if (type == "minecraft:allay") return c.enableAllay;
    if (type == "minecraft:rabbit") return c.enableShinyRabbit;
    if (type == "minecraft:llama") return c.enableShinyLlama;
    if (type == "minecraft:dolphin") return c.enableShinyDolphin;
    if (type == "minecraft:slime") return c.enableShinySlime;
    if (type == "minecraft:armadillo") return c.enableShinyArmadillo;
    if (type == "minecraft:frog") return c.enableShinyFrog;
    return false;
}
} // namespace

int animalSkin(std::string_view type, int64_t uniqueId, std::string const& customName, bool /*isBaby*/) {
    auto const& c = config();
    if (!c.enableVariantAnimalTextures) return 0;
    int64_t most = mostBits(uniqueId);
    if (type == "minecraft:bee") return beeSkin(most, customName, c.enableLGBTBees, c.everyBeeIsLGBT);
    int extra = extraSkins(type);
    if (extra < 0 || !typeEnabled(c, type)) return 0;
    if (isShiny(most, c.shinyAnimalChance)) return kShinySkin;
    return variantSkin(most, extra);
}

bool sparkles(std::string_view type) {
    auto const& c = config();
    if (!c.enableVariantAnimalTextures || !c.shinySparkles) return false;
    if (type == "minecraft:slime") return false; // not in Quark's isSparkly list
    return extraSkins(type) >= 0 && typeEnabled(c, type);
}

int chickenOffsetCode(double offset) {
    offset = std::clamp(offset, -1.0, 1.0);
    return static_cast<int>(std::lround((offset + 1.0) * 100.0));
}

uint32_t convolve(ColorMatrix const& m, uint32_t argb) {
    auto row = [](std::vector<double> const& v, int i) { return i < static_cast<int>(v.size()) ? v[static_cast<size_t>(i)] : 0.0; };
    int  r   = (argb >> 16) & 0xFF;
    int  g   = (argb >> 8) & 0xFF;
    int  b   = argb & 0xFF;
    auto out = [&](std::vector<double> const& w) {
        int v = static_cast<int>(r * row(w, 0) + g * row(w, 1) + b * row(w, 2));
        return static_cast<uint32_t>(std::clamp(v, 0, 255));
    };
    return (argb & 0xFF000000u) | (out(m.r) << 16) | (out(m.g) << 8) | out(m.b);
}

std::string_view villagerTypeForBiome(std::string_view biome) {
    if (biome == "beach") return "beach";
    static constexpr std::string_view kOceans[]{
        "ocean",
        "deep_ocean",
        "warm_ocean",
        "deep_warm_ocean",
        "lukewarm_ocean",
        "deep_lukewarm_ocean",
        "cold_ocean",
        "deep_cold_ocean",
        "frozen_ocean",
        "deep_frozen_ocean",
        "legacy_frozen_ocean",
    };
    for (auto o : kOceans)
        if (biome == o) return "ocean";
    return {};
}

} // namespace lp::feature::cosmetics
