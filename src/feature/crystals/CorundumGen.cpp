// U12 crystals / Corundum cave generation: a port of Zeta's ClusterBasedGenerator /
// MultiChunkFeatureGenerator / ClusterShape (Perlin simplex noise, java.util.Random seeding) with
// Quark's UndergroundStyleGenerator and CorundumStyle, run in Bedrock's chunk post-processing.

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/DividedPos2d.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/source/BiomeArea.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/v1/NetherGenerator.h"
#include "mc/world/level/levelgen/v1/OverworldGeneratorMultinoise.h"
#include "mc/world/level/levelgen/v1/TheEndGenerator.h"

#include "feature/crystals/Common.h"
#include "feature/crystals/Noise.h"
#include "game/BlockStates.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::crystals {

namespace bs = game::blockstates;

namespace {

bool gInstalled = false;

// ---- Zeta ClusterShape ----------------------------------------------------------------------------

struct Shape {
    BlockPos src;
    double   rx, ry, rz;
    bool inside(int x, int y, int z, PerlinSimplexNoise const& noise) const {
        double dx = (x - src.x) / rx, dy = (y - src.y) / ry, dz = (z - src.z) / rz;
        double r  = dx * dx + dy * dy + dz * dz;
        if (r > 1) return false;
        r            = std::sqrt(r);
        double phi   = std::atan2(dz, dx);
        double theta = r == 0 ? 0 : std::acos(dy / r);
        double xn = phi + src.x, yn = theta + src.z;
        double n      = noise.getValue(xn, yn, false);
        double cutoff = 0.75 * 3.14159265358979323846;
        if (phi > cutoff) {
            double n0 = noise.getValue(-3.14159265358979323846 + src.x, yn, false);
            double t  = (phi - cutoff) / (3.14159265358979323846 - cutoff);
            n         = n + t * (n0 - n);
        }
        return (n + 0.5 - r) > 0;
    }
};

Shape shapeAround(BlockPos const& src, GenerationSettings const& g) {
    JavaRandom r(31LL * (31LL * (31 + src.x) + src.y) + src.z);
    auto       var = [&](int v) { return v > 0 ? r.nextInt(v) : 0; };
    int        rx  = g.horizontalSize + var(g.horizontalVariation);
    int        ry  = g.verticalSize + var(g.verticalVariation);
    int        rz  = g.horizontalSize + var(g.horizontalVariation);
    return {src, static_cast<double>(std::max(rx, 1)), static_cast<double>(std::max(ry, 1)), static_cast<double>(std::max(rz, 1))};
}

PerlinSimplexNoise const& shapeNoise() {
    static std::unique_ptr<PerlinSimplexNoise> noise;
    if (!noise) noise = std::make_unique<PerlinSimplexNoise>(static_cast<int64_t>(javaHash("corundum")), -4, 4);
    return *noise;
}

// ---- biome / dimension filters ----------------------------------------------------------------------

bool nameMatchesTag(std::string_view biome, std::string_view tag) {
    if (tag.starts_with("minecraft:")) tag.remove_prefix(10);
    if (tag.starts_with("c:")) tag.remove_prefix(2);
    if (tag.starts_with("is_")) tag.remove_prefix(3);
    static std::vector<std::pair<std::string_view, std::vector<std::string_view>>> const kTags{
        {"ocean", {"ocean"}},          {"deep_ocean", {"deep_"}},       {"beach", {"beach", "stone_shore"}},
        {"river", {"river"}},          {"mountain", {"peaks", "slopes", "mountain", "meadow", "grove"}},
        {"badlands", {"mesa", "badlands"}}, {"jungle", {"jungle"}},     {"taiga", {"taiga"}},
        {"forest", {"forest", "birch", "grove"}}, {"savanna", {"savanna"}}, {"hill", {"hills"}},
        {"nether", {"hell", "nether", "crimson", "warped", "basalt", "soulsand"}}, {"end", {"the_end"}},
    };
    for (auto const& [t, subs] : kTags) {
        if (t != tag) continue;
        if (t == "deep_ocean") return biome.find("deep_") != std::string_view::npos && biome.find("ocean") != std::string_view::npos;
        for (auto s : subs)
            if (biome.find(s) != std::string_view::npos) return true;
        return false;
    }
    return biome.find(tag) != std::string_view::npos;
}

bool biomeAllowed(std::string biome, BiomesConfig const& c) {
    if (auto p = biome.find(':'); p != std::string::npos) biome = biome.substr(p + 1);
    bool tagMatch = false;
    for (auto const& t : c.tags.tags)
        if (nameMatchesTag(biome, t)) tagMatch = true;
    // tags: a whitelist needs a match, a blacklist needs none (same for names)
    bool tagsOk = c.tags.isBlacklist ? !tagMatch : tagMatch;
    bool nameMatch = false;
    for (auto n : c.biomes.biomes) {
        if (auto p = n.find(':'); p != std::string::npos) n = n.substr(p + 1);
        if (n == biome) nameMatch = true;
    }
    bool namesOk = c.biomes.isBlacklist ? !nameMatch : nameMatch;
    return tagsOk && namesOk;
}

bool dimensionAllowed(std::string_view dim, DimensionsConfig const& c) {
    bool in = std::find(c.dimensions.begin(), c.dimensions.end(), dim) != c.dimensions.end();
    return in != c.isBlacklist;
}

// ---- Quark UndergroundStyle / CorundumStyle --------------------------------------------------------

bool isReplaceableStone(Block const& b) {
    // Quark #quark:underground_biome_replaceable = #minecraft:base_stone_overworld + netherrack + end stone
    static std::array<std::string_view, 8> const kIds{
        "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff",
        "minecraft:deepslate", "minecraft:netherrack", "minecraft:end_stone"};
    auto const& n = b.getTypeName();
    for (auto id : kIds)
        if (n == id) return true;
    return false;
}
bool canBeReplaced(BlockSource& region, BlockPos const& pos, Block const& b) {
    return b.isAir() || b.getBlockType().canBeBuiltOver(b, region, pos);
}
bool occludes(Block const& b) { return !b.isAir() && static_cast<bool>(b.mCachedComponentData->mIsSolid); }
bool isWater(Block const& b) {
    auto const& n = b.getTypeName();
    return n == "minecraft:water" || n == "minecraft:flowing_water";
}

struct Gen {
    BlockSource&              region;
    BlockPos                  source;
    JavaRandom&               random;
    CrystalsConfig const&     c;
    Block const*              stone;
    std::array<Block const*, 9> crystals{};

    Block const* cluster(int color, int face) const {
        return bs::of(clusterId(color), {{"minecraft:block_face", std::string(faceToName(face))}});
    }
    void set(BlockPos const& p, Block const& b) {
        bool water = isWater(region.getBlock(p));
        region.setBlock(p, b, 2, nullptr, BlockChangeContext{});
        if (water && isCluster(b)) {
            if (auto const* w = bs::of("minecraft:water")) region.setExtraBlock(p, *w, 2);
        }
        if (isCorundum(b)) track(Kind::Corundum, game::globalPos(region, p));
    }
    bool empty(BlockPos const& p) const { return region.getBlock(p).isAir(); }

    void makeCrystalAt(BlockPos const& p, int offsetFace, int color, double clusterChance) {
        if (random.nextDouble() < clusterChance) {
            if (auto const* cl = cluster(color, offsetFace)) set(p, *cl);
            return;
        }
        if (crystals[color]) set(p, *crystals[color]);
        for (int d = 0; d < 6; ++d) {
            auto cp = offset(p, d);
            if (empty(cp) && random.nextDouble() < c.crystalClusterOnSidesChance)
                if (auto const* cl = cluster(color, d)) set(cp, *cl);
        }
    }
    void makeCrystalIfApt(BlockPos const& pos, int offsetFace, int color) {
        BlockPos cp            = offset(pos, offsetFace);
        bool     hasHorizontal = false;
        for (int d = 2; d < 6; ++d)
            if (occludes(region.getBlock(offset(cp, d)))) {
                hasHorizontal = true;
                break;
            }
        if (!hasHorizontal) return;
        makeCrystalAt(cp, offsetFace, color, c.crystalClusterChance);
        if (random.nextDouble() < c.doubleCrystalChance) {
            cp = offset(cp, offsetFace);
            if (empty(cp)) makeCrystalAt(cp, offsetFace, color, 0);
        }
    }
    std::pair<int, int> colors() const {
        JavaRandom r(javaHash(source.x, source.y, source.z));
        int        hi    = r.nextInt(8);
        int        floor = r.nextInt(9);
        int        ceil  = hi;
        if (ceil >= floor) ++ceil;
        return {floor, ceil};
    }

    void fill(BlockPos const& pos) {
        auto const& state = region.getBlock(pos);
        if (!isReplaceableStone(state)) return; // also excludes unbreakable blocks
        auto const& up   = region.getBlock(offset(pos, 1));
        auto const& down = region.getBlock(offset(pos, 0));
        if (canBeReplaced(region, offset(pos, 1), up)) {
            if (random.nextDouble() < c.crystalChance) makeCrystalIfApt(pos, 1, colors().first);
        } else if (canBeReplaced(region, offset(pos, 0), down)) {
            if (random.nextDouble() < c.crystalChance) makeCrystalIfApt(pos, 0, colors().second);
        } else {
            for (int d = 2; d < 6; ++d) {
                auto        np = offset(pos, d);
                auto const& nb = region.getBlock(np);
                if ((&nb != &state && nb.isAir()) || canBeReplaced(region, np, nb)) {
                    if (stone && &state != stone) region.setBlock(pos, *stone, 2, nullptr, BlockChangeContext{});
                    break;
                }
            }
        }
    }
};

/// Generates the corundum caves for chunk (cx, cz). `gen` answers biome and surface queries for
/// sources outside the loaded area.
template <class Generator>
void generateChunk(Generator const& gen, BlockSource& region, int cx, int cz, std::string_view dimName) {
    auto const& c = cfg();
    auto const& g = c.generationSettings;
    if (!gInstalled || !c.enableCorundum || g.rarity <= 0) return;
    if (!dimensionAllowed(dimName, g.dimensions)) return;
    int radius = g.horizontalSize + g.horizontalVariation;
    if (radius <= 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    int64_t    seed = static_cast<int64_t>(level->getSeed());
    int64_t    xor_ = static_cast<int64_t>(javaHash("corundum"));
    JavaRandom worldRandom(seed);
    int64_t    xSeed = worldRandom.nextLong(), zSeed = worldRandom.nextLong();
    JavaRandom our(static_cast<int64_t>(static_cast<uint64_t>(xSeed) * static_cast<uint64_t>(cx)
                                        + static_cast<uint64_t>(zSeed) * static_cast<uint64_t>(cz))
                   ^ seed ^ xor_);
    int chunkRadius = static_cast<int>(std::ceil(radius / 16.0));
    int minY = region.getMinHeight(), maxY = region.getMaxHeight();

    Gen ctx{region, {}, our, c, bs::of("minecraft:stone"), {}};
    for (int i = 0; i < 9; ++i) ctx.crystals[i] = bs::of(corundumId(i));
    if (!ctx.crystals[0]) return; // behavior pack missing

    for (int x = cx - chunkRadius; x <= cx + chunkRadius; ++x) {
        for (int z = cz - chunkRadius; z <= cz + chunkRadius; ++z) {
            JavaRandom cr(static_cast<int64_t>(static_cast<uint64_t>(xSeed) * static_cast<uint64_t>(x)
                                               + static_cast<uint64_t>(zSeed) * static_cast<uint64_t>(z))
                          ^ seed ^ xor_);
            if (cr.nextInt(g.rarity) != 0) continue;
            int sx = (x << 4) + cr.nextInt(16);
            int sy = g.maxYLevel > g.minYLevel ? cr.nextInt(g.minYLevel, g.maxYLevel) : g.minYLevel;
            int sz = (z << 4) + cr.nextInt(16);
            // biome at the top of the world, and only underground sources
            BoundingBox box;
            box.min = BlockPos{sx, maxY - 1, sz};
            box.max = BlockPos{sx, maxY - 1, sz};
            auto area = gen.getBiomeArea(box, 1);
            auto const& biomes = area.mBiomes.get();
            if (biomes.empty() || !biomes[0] || !biomeAllowed(biomes[0]->mHash->getString(), g.biomes)) continue;
            auto surface = gen.getPreliminarySurfaceLevel(DividedPos2d<4>(sx >> 2, sz >> 2));
            if (surface && *surface < sy) continue;

            BlockPos src{sx, sy, sz};
            ctx.source  = src;
            Shape shape = shapeAround(src, g);
            int   lo    = std::max(minY + 1, static_cast<int>(std::floor(src.y - shape.ry)));
            int   hi    = std::min(maxY - 1, static_cast<int>(std::ceil(src.y + shape.ry)));
            for (int bx = 0; bx < 16; ++bx) {
                for (int y = lo; y < hi; ++y) {
                    for (int bz = 0; bz < 16; ++bz) {
                        int      wx = (cx << 4) + bx, wz = (cz << 4) + bz;
                        BlockPos p{wx, y, wz};
                        if (region.getHeightmapPos(p).y <= y) continue; // canPlaceAt: below the surface
                        if (!shape.inside(wx, y, wz, shapeNoise())) continue;
                        ctx.fill(p);
                    }
                }
            }
        }
    }
}

LL_TYPE_INSTANCE_HOOK(
    OverworldPostProcessHook,
    ll::memory::HookPriority::Normal,
    OverworldGeneratorMultinoise,
    &OverworldGeneratorMultinoise::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  region,
    Random&       random
) {
    origin(biome, lc, region, random);
    auto const& cp = lc.mPosition.get();
    generateChunk(*this, region, cp.x, cp.z, "minecraft:overworld");
}

LL_TYPE_INSTANCE_HOOK(
    NetherPostProcessHook,
    ll::memory::HookPriority::Normal,
    NetherGenerator,
    &NetherGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  region,
    Random&       random
) {
    origin(biome, lc, region, random);
    auto const& cp = lc.mPosition.get();
    generateChunk(*this, region, cp.x, cp.z, "minecraft:the_nether");
}

LL_TYPE_INSTANCE_HOOK(
    EndPostProcessHook,
    ll::memory::HookPriority::Normal,
    TheEndGenerator,
    &TheEndGenerator::$decorateWorldGenPostProcess,
    void,
    Biome const&  biome,
    LevelChunk&   lc,
    BlockSource&  region,
    Random&       random
) {
    origin(biome, lc, region, random);
    auto const& cp = lc.mPosition.get();
    generateChunk(*this, region, cp.x, cp.z, "minecraft:the_end");
}

class CorundumGenModule {
public:
    void install() {
        gInstalled = true;
        OverworldPostProcessHook::hook();
        NetherPostProcessHook::hook();
        EndPostProcessHook::hook();
    }
    void uninstall() {
        gInstalled = false;
        OverworldPostProcessHook::unhook();
        NetherPostProcessHook::unhook();
        EndPostProcessHook::unhook();
    }
};

} // namespace

// ---- selftest seams ---------------------------------------------------------------------------------

bool caveShapeContains(BlockPos const& src, BlockPos const& p) {
    return shapeAround(src, cfg().generationSettings).inside(p.x, p.y, p.z, shapeNoise());
}
std::pair<int, int> caveColors(BlockPos const& src) {
    JavaRandom r(javaHash(src.x, src.y, src.z));
    int        hi = r.nextInt(8), floor = r.nextInt(9);
    return {floor, hi >= floor ? hi + 1 : hi};
}
bool caveBiomeAllowed(std::string const& biome) { return biomeAllowed(biome, cfg().generationSettings.biomes); }

} // namespace lp::feature::crystals

LP_REGISTER_MODULE(lp::feature::crystals::CorundumGenModule);
