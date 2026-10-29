#include "feature/worldsurface/Common.h"

#include <algorithm>
#include <mutex>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"

#include "game/BlockStates.h"
#include "mod/Config.h"
#include "mod/Log.h"

namespace lp::feature::worldsurface {

namespace bs = game::blockstates;

Config const& cfg() { return config::get<Config>(); }

// ---- random --------------------------------------------------------------------------------------

namespace {
uint64_t splitmix(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z          = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z          = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
} // namespace

Rng::Rng(uint64_t seed) {
    for (auto& v : s) v = splitmix(seed);
}

uint64_t Rng::next() {
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t      = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

int Rng::nextInt(int bound) {
    if (bound <= 0) return 0;
    return static_cast<int>((next() >> 33) % static_cast<uint64_t>(bound));
}

int Rng::nextInt(int lo, int hi) { return hi <= lo ? lo : lo + nextInt(hi - lo); }

int Rng::between(int lo, int hi) { return hi <= lo ? lo : lo + nextInt(hi - lo + 1); }

double Rng::nextDouble() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

float Rng::nextFloat() { return static_cast<float>(next() >> 40) * 0x1.0p-24f; }

bool Rng::nextBool() { return (next() >> 63) != 0; }

uint64_t chunkSeed(uint64_t worldSeed, int cx, int cz, uint64_t salt) {
    uint64_t x = worldSeed ^ (salt * 0xD1B54A32D192ED03ull);
    x ^= static_cast<uint64_t>(static_cast<uint32_t>(cx)) * 0x9E3779B97F4A7C15ull;
    x ^= static_cast<uint64_t>(static_cast<uint32_t>(cz)) * 0xC2B2AE3D27D4EB4Full;
    return splitmix(x);
}

// ---- biomes / dimensions ---------------------------------------------------------------------------

namespace {
std::unordered_map<std::string, std::string> const& biomeMap() {
    static std::unordered_map<std::string, std::string> const m{
        {"snowy_plains",               "ice_plains"              },
        {"ice_spikes",                 "ice_plains_spikes"       },
        {"old_growth_birch_forest",    "birch_forest_mutated"    },
        {"dark_forest",                "roofed_forest"           },
        {"windswept_forest",           "extreme_hills_plus_trees"},
        {"windswept_hills",            "extreme_hills"           },
        {"windswept_gravelly_hills",   "extreme_hills_mutated"   },
        {"windswept_savanna",          "savanna_mutated"         },
        {"swamp",                      "swampland"               },
        {"wooded_badlands",            "mesa_plateau_stone"      },
        {"badlands",                   "mesa"                    },
        {"eroded_badlands",            "mesa_bryce"              },
        {"old_growth_spruce_taiga",    "redwood_taiga_mutated"   },
        {"old_growth_pine_taiga",      "mega_taiga"              },
        {"snowy_taiga",                "cold_taiga"              },
        {"sparse_jungle",              "jungle_edge"             },
        {"snowy_beach",                "cold_beach"              },
        {"stony_shore",                "stone_beach"             },
        {"mushroom_fields",            "mushroom_island"         },
        {"savanna_plateau",            "savanna_plateau"         },
        {"sunflower_plains",           "sunflower_plains"        },
        {"flower_forest",              "flower_forest"           },
    };
    return m;
}

std::unordered_map<std::string, std::vector<std::string>> const& tagMap() {
    static std::unordered_map<std::string, std::vector<std::string>> const m{
        {"minecraft:is_forest",
         {"minecraft:forest",
          "minecraft:flower_forest",
          "minecraft:birch_forest",
          "minecraft:old_growth_birch_forest",
          "minecraft:dark_forest",
          "minecraft:grove"}                                                                                  },
        {"c:is_plains",          {"minecraft:plains", "minecraft:sunflower_plains"}                           },
        {"minecraft:is_taiga",
         {"minecraft:taiga", "minecraft:snowy_taiga", "minecraft:old_growth_pine_taiga", "minecraft:old_growth_spruce_taiga"}},
        {"minecraft:is_jungle",  {"minecraft:jungle", "minecraft:sparse_jungle", "minecraft:bamboo_jungle"}    },
        {"minecraft:is_savanna", {"minecraft:savanna", "minecraft:savanna_plateau", "minecraft:windswept_savanna"}},
        {"minecraft:is_badlands", {"minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands"}},
        {"minecraft:is_ocean",
         {"minecraft:ocean",
          "minecraft:deep_ocean",
          "minecraft:warm_ocean",
          "minecraft:lukewarm_ocean",
          "minecraft:deep_lukewarm_ocean",
          "minecraft:cold_ocean",
          "minecraft:deep_cold_ocean",
          "minecraft:frozen_ocean",
          "minecraft:deep_frozen_ocean"}                                                                      },
        {"minecraft:is_mountain",
         {"minecraft:meadow",
          "minecraft:frozen_peaks",
          "minecraft:jagged_peaks",
          "minecraft:stony_peaks",
          "minecraft:snowy_slopes",
          "minecraft:cherry_grove"}                                                                           },
        {"c:is_snowy",
         {"minecraft:snowy_plains",
          "minecraft:ice_spikes",
          "minecraft:snowy_taiga",
          "minecraft:snowy_beach",
          "minecraft:grove",
          "minecraft:snowy_slopes",
          "minecraft:jagged_peaks",
          "minecraft:frozen_peaks"}                                                                           },
        {"c:is_swamp",           {"minecraft:swamp", "minecraft:mangrove_swamp"}                              },
    };
    return m;
}
} // namespace

std::string bedrockBiome(std::string_view javaId) {
    auto colon = javaId.find(':');
    auto path  = std::string(colon == std::string_view::npos ? javaId : javaId.substr(colon + 1));
    auto it    = biomeMap().find(path);
    return it == biomeMap().end() ? path : it->second;
}

std::string biomeName(Biome const& biome) {
    auto const& n = biome.mHash->getString();
    return n.starts_with("minecraft:") ? n.substr(10) : n;
}

std::vector<std::string> biomesOfTag(std::string_view javaTag) {
    std::string key(javaTag);
    if (key.starts_with('#')) key.erase(0, 1);
    if (key.find(':') == std::string::npos) key = "minecraft:" + key;
    std::vector<std::string> out;
    if (auto it = tagMap().find(key); it != tagMap().end())
        for (auto const& b : it->second) {
            out.push_back(bedrockBiome(b));
            out.push_back(javaPath(b));
        }
    return out;
}

std::string javaPath(std::string_view javaId) {
    auto colon = javaId.find(':');
    return std::string(colon == std::string_view::npos ? javaId : javaId.substr(colon + 1));
}

std::unordered_set<std::string> bedrockBiomeSet(std::vector<std::string> const& javaIds) {
    // both the legacy Bedrock name and the Java path: newer Bedrock versions use Java's names
    std::unordered_set<std::string> out;
    for (auto const& id : javaIds) {
        out.insert(bedrockBiome(id));
        out.insert(javaPath(id));
    }
    return out;
}

std::string_view dimensionName(int dim) {
    switch (dim) {
    case 0:
        return "minecraft:overworld";
    case 1:
        return "minecraft:the_nether";
    case 2:
        return "minecraft:the_end";
    default:
        return "";
    }
}

bool dimensionAllowed(DimensionConfig const& c, int dim) {
    auto name     = dimensionName(dim);
    bool contains = std::find(c.dimensions.begin(), c.dimensions.end(), name) != c.dimensions.end();
    return contains != c.isBlacklist;
}

bool BiomeFilter::allows(std::string const& b) const {
    bool inTags   = tagBiomes.contains(b);
    bool inBiomes = biomes.contains(b);
    return (inTags != tagsBlacklist) && (inBiomes != biomesBlacklist);
}

BiomeFilter resolve(CompoundBiomeConfig const& c) {
    BiomeFilter f;
    f.tagsBlacklist = c.tags.isBlacklist;
    for (auto const& t : c.tags.biomeTags)
        for (auto& b : biomesOfTag(t)) f.tagBiomes.insert(std::move(b));
    f.biomesBlacklist = c.biomes.isBlacklist;
    f.biomes          = bedrockBiomeSet(c.biomes.biomes);
    return f;
}

// ---- block classification -----------------------------------------------------------------------

std::string const& nameOf(Block const& block) { return block.getTypeName(); }

namespace {
bool inSet(Block const& block, std::unordered_set<std::string> const& set) { return set.contains(nameOf(block)); }
} // namespace

bool isDirt(Block const& block) {
    static std::unordered_set<std::string> const s{
        "minecraft:dirt",
        "minecraft:grass_block",
        "minecraft:grass",
        "minecraft:podzol",
        "minecraft:coarse_dirt",
        "minecraft:mycelium",
        "minecraft:dirt_with_roots",
        "minecraft:moss_block",
        "minecraft:mud",
        "minecraft:muddy_mangrove_roots",
    };
    return inSet(block, s);
}

bool isStone(Block const& block) {
    static std::unordered_set<std::string> const s{
        "minecraft:stone",
        "minecraft:granite",
        "minecraft:diorite",
        "minecraft:andesite",
        "minecraft:deepslate",
        "minecraft:tuff",
    };
    return inSet(block, s);
}

bool isStoneBased(Block const& block) {
    static HashedString const stoneTag("stone");
    if (block.isAir()) return false;
    if (block.hasTag(stoneTag)) return true;
    auto const& n = nameOf(block);
    if (!n.starts_with("minecraft:")) return false;
    static constexpr std::string_view words[] = {
        "stone", "deepslate", "tuff", "granite", "diorite", "andesite", "calcite", "_ore", "basalt",
        "blackstone", "netherrack", "bedrock", "obsidian", "dripstone_block", "end_stone", "brick", "terracotta",
        "prismarine", "magma", "nylium",
    };
    for (auto w : words)
        if (n.find(w) != std::string::npos) {
            // a few "stone"-named non-stone blocks
            if (n.find("button") != std::string::npos || n.find("pressure_plate") != std::string::npos
                || n.find("redstone_wire") != std::string::npos || n.find("redstone_torch") != std::string::npos)
                return false;
            return true;
        }
    return false;
}

bool isSmallFlower(Block const& block) {
    static std::unordered_set<std::string> const s{
        "minecraft:dandelion",
        "minecraft:yellow_flower",
        "minecraft:poppy",
        "minecraft:red_flower",
        "minecraft:blue_orchid",
        "minecraft:allium",
        "minecraft:azure_bluet",
        "minecraft:red_tulip",
        "minecraft:orange_tulip",
        "minecraft:white_tulip",
        "minecraft:pink_tulip",
        "minecraft:oxeye_daisy",
        "minecraft:cornflower",
        "minecraft:lily_of_the_valley",
        "minecraft:wither_rose",
        "minecraft:torchflower",
    };
    return inSet(block, s);
}

bool isFlower(Block const& block) {
    static std::unordered_set<std::string> const s{
        "minecraft:sunflower",
        "minecraft:lilac",
        "minecraft:rose_bush",
        "minecraft:peony",
        "minecraft:double_plant",
        "minecraft:azalea_leaves_flowered",
        "minecraft:flowering_azalea",
        "minecraft:cherry_leaves",
        "minecraft:pink_petals",
        "minecraft:mangrove_propagule",
        "minecraft:spore_blossom",
        "minecraft:chorus_flower",
        "minecraft:pitcher_plant",
    };
    return isSmallFlower(block) || inSet(block, s);
}

bool isWater(Block const& block) {
    auto const& n = nameOf(block);
    return n == "minecraft:water" || n == "minecraft:flowing_water";
}

bool isLiquid(Block const& block) {
    auto const& n = nameOf(block);
    return isWater(block) || n == "minecraft:lava" || n == "minecraft:flowing_lava";
}

bool isAirBlock(Block const& block) { return block.isAir(); }

bool replaceable(BlockSource& region, BlockPos const& pos, Block const& block) {
    return block.isAir() || block.getBlockType().canBeBuiltOver(block, region, pos);
}

bool sturdy(Block const& block, int face) { return block.canProvideFullSupport(static_cast<uchar>(face)); }

bool genSet(BlockSource& region, BlockPos const& pos, Block const& block) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    BlockChangeContext ctx{};
    return region.setBlock(pos, block, 2, nullptr, ctx);
}

bool liveSet(BlockSource& region, BlockPos const& pos, Block const& block) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    BlockChangeContext ctx{};
    return region.setBlock(pos, block, 3, nullptr, ctx);
}

std::pair<int, int> facingStep(int facing) {
    switch (facing & 3) {
    case 0:
        return {0, 1};
    case 1:
        return {-1, 0};
    case 2:
        return {0, -1};
    default:
        return {1, 0};
    }
}

char const* facingName(int facing) {
    static char const* const n[] = {"south", "west", "north", "east"};
    return n[facing & 3];
}

// ---- palette ---------------------------------------------------------------------------------------

namespace {
std::mutex                      gStateMutex;
std::shared_ptr<GenState const> gState = std::make_shared<GenState const>();

Block const* firstOf(std::vector<std::pair<std::string, bs::Changes>> const& candidates) {
    for (auto const& [id, changes] : candidates)
        if (auto const* b = bs::of(id, changes)) return b;
    return nullptr;
}
} // namespace

std::shared_ptr<GenState const> genState() {
    std::lock_guard lock(gStateMutex);
    return gState;
}

void rebuildGenState() {
    auto  st = std::make_shared<GenState>();
    auto& c  = st->config;
    c        = cfg();
    st->air  = bs::of("minecraft:air");

    // FairyRings
    st->forestBiomes = bedrockBiomeSet(c.fairyRingsForestBiomes);
    st->plainsBiomes = bedrockBiomeSet(c.fairyRingsPlainsBiomes);
    for (auto const& id : c.oresRaw) {
        if (auto const* b = bs::of(id)) st->ores.push_back(b);
        else log().warn("worldsurface: fairy ring ore {} does not exist", id);
    }
    for (auto const* f :
         {"minecraft:dandelion",
          "minecraft:poppy",
          "minecraft:blue_orchid",
          "minecraft:allium",
          "minecraft:azure_bluet",
          "minecraft:red_tulip",
          "minecraft:orange_tulip",
          "minecraft:white_tulip",
          "minecraft:pink_tulip",
          "minecraft:oxeye_daisy",
          "minecraft:cornflower",
          "minecraft:lily_of_the_valley",
          "minecraft:pink_petals"}) {
        if (auto const* b = bs::of(f)) st->flowers[f] = b;
    }

    // FallenLogs
    for (auto const& entry : c.biomeTags) {
        auto eq = entry.find('=');
        if (eq == std::string::npos) {
            log().warn("worldsurface: bad fallen log entry '{}' (expected tag=block)", entry);
            continue;
        }
        std::string tag = entry.substr(0, eq), logId = entry.substr(eq + 1);
        if (auto colon = tag.find(':'); colon != std::string::npos && tag.substr(0, colon) == "quark") tag = tag.substr(colon + 1);
        GenState::LogEntry e;
        e.log = logId;
        if (auto it = c.fallenLogsBiomeTagMembers.find(tag); it != c.fallenLogsBiomeTagMembers.end())
            e.biomes = bedrockBiomeSet(it->second);
        else
            for (auto& b : biomesOfTag(tag)) e.biomes.insert(std::move(b));
        e.axis = {
            firstOf({{logId, {{"pillar_axis", std::string("x")}}}, {logId, {{"minecraft:block_face", std::string("east")}}}}),
            firstOf({{logId, {{"pillar_axis", std::string("z")}}}, {logId, {{"minecraft:block_face", std::string("south")}}}})
        };
        if (!e.axis[0]) {
            log().warn("worldsurface: fallen log block {} does not exist", logId);
            continue;
        }
        // U6's hollow logs: lepton:hollow_<wood>_log (lepton:pillar -> minecraft:block_face)
        auto colon  = logId.find(':');
        auto path   = colon == std::string::npos ? logId : logId.substr(colon + 1);
        auto hollow = "lepton:hollow_" + path;
        e.hollowAxis = {
            firstOf({{hollow, {{"minecraft:block_face", std::string("east")}}}, {hollow, {{"pillar_axis", std::string("x")}}}}),
            firstOf({{hollow, {{"minecraft:block_face", std::string("south")}}}, {hollow, {{"pillar_axis", std::string("z")}}}})
        };
        st->logs.push_back(std::move(e));
    }
    st->reducedBiomes = bedrockBiomeSet(c.fallenLogsReducedDensityBiomes);
    st->fern          = firstOf({{"minecraft:fern", {}}, {"minecraft:tallgrass", {{"tall_grass_type", std::string("fern")}}}});
    st->mossCarpet    = bs::of("minecraft:moss_carpet");
    for (int i = 0; i < 4; ++i) st->vine[i] = bs::of("minecraft:vine", {{"vine_direction_bits", 1 << i}});

    // MonsterBox
    st->monsterBox = bs::of("lepton:monster_box");

    // GlimmeringWeald
    st->glowShroom   = bs::of("lepton:glow_shroom");
    st->lichenGrowth = bs::of("lepton:glow_lichen_growth");
    st->shroomCap    = bs::of(
        "lepton:glow_shroom_block",
        {{"lepton:north", true}, {"lepton:east", true}, {"lepton:south", true}, {"lepton:west", true}, {"lepton:up", true}, {"lepton:down", false}}
    );
    st->shroomStem = bs::of(
        "lepton:glow_shroom_stem",
        {{"lepton:north", true}, {"lepton:east", true}, {"lepton:south", true}, {"lepton:west", true}, {"lepton:up", false}, {"lepton:down", false}}
    );
    for (int f = 0; f < 4; ++f) st->ring[f] = bs::of("lepton:glow_shroom_ring", {{"minecraft:block_face", std::string(facingName(f))}});
    // multi_face_direction_bits: down 1, up 2, south 4, west 8, north 16, east 32 (by FacingID order below)
    static constexpr int lichenBits[6] = {1, 2, 16, 4, 8, 32};
    for (int f = 0; f < 6; ++f) st->glowLichen[f] = bs::of("minecraft:glow_lichen", {{"multi_face_direction_bits", lichenBits[f]}});
    st->lapisOre          = bs::of("minecraft:lapis_ore");
    st->deepslateLapisOre = bs::of("minecraft:deepslate_lapis_ore");

    // CherryGroveWaterPetals
    st->petalBiomes = resolve(c.biomes);
    for (int amount = 1; amount <= 4; ++amount) {
        for (int f = 0; f < 4; ++f) {
            std::string dir = facingName(f);
            st->petals[amount][f] = firstOf({
                {"lepton:water_pink_petals", {{"lepton:amount", amount}, {"minecraft:cardinal_direction", dir}}},
                {"lepton:water_pink_petals", {{"lepton:growth", amount - 1}, {"minecraft:cardinal_direction", dir}}},
                {"lepton:water_pink_petals", {{"lepton:amount", amount}, {"lepton:facing", dir}}},
            });
        }
    }
    st->cherryCarpet = bs::of("lepton:cherry_leaf_carpet");

    std::lock_guard lock(gStateMutex);
    gState = std::move(st);
}

} // namespace lp::feature::worldsurface
