// U12 crystals: shared parts (config, ids, block classes, position tracker, redstone, particles).

#include "feature/crystals/Common.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("crystals", lp::feature::crystals::CrystalsConfig);

namespace lp::feature::crystals {

namespace bs = game::blockstates;

CrystalsConfig const& cfg() { return config::get<CrystalsConfig>(); }

// ---- ids --------------------------------------------------------------------------------------------

std::string corundumId(int c) { return "lepton:" + std::string(kColors[c].name) + "_corundum"; }
std::string waxedCorundumId(int c) { return "lepton:waxed_" + std::string(kColors[c].name) + "_corundum"; }
std::string clusterId(int c) { return "lepton:" + std::string(kColors[c].name) + "_corundum_cluster"; }
std::string paneId(int c) { return "lepton:" + std::string(kColors[c].name) + "_corundum_pane"; }
std::string lampId(int c) { return "lepton:" + std::string(kColors[c].name) + "_crystal_lamp"; }
std::vector<std::string> corundumIds() {
    std::vector<std::string> out;
    for (int i = 0; i < static_cast<int>(kColors.size()); ++i) out.push_back(corundumId(i));
    return out;
}

// ---- classification ---------------------------------------------------------------------------------

bool hasTag(Block const& b, char const* tag) { return b.hasTag(HashedString(tag)); }

int colorOf(Block const& b) {
    auto const& name = b.getTypeName();
    if (!name.starts_with("lepton:")) return -1;
    for (int i = 0; i < static_cast<int>(kColors.size()); ++i) {
        std::string tag = "lepton:crystal_color_" + std::string(kColors[i].name);
        if (b.hasTag(HashedString(tag))) return i;
    }
    return -1;
}
bool isCorundum(Block const& b) { return hasTag(b, "lepton:corundum"); }
bool isWaxedCorundum(Block const& b) { return hasTag(b, "lepton:waxed_corundum"); }
bool isCluster(Block const& b) { return hasTag(b, "lepton:corundum_cluster"); }
bool isPane(Block const& b) { return hasTag(b, "lepton:corundum_pane"); }
bool isLamp(Block const& b) { return hasTag(b, "lepton:crystal_lamp"); }
bool isMagnet(Block const& b) { return b.getTypeName() == "lepton:magnet"; }
bool isMagnetized(Block const& b) { return b.getTypeName() == "lepton:magnetized_block"; }

std::array<int, 3> facingStep(int face) {
    switch (face) {
    case 0:
        return {0, -1, 0};
    case 1:
        return {0, 1, 0};
    case 2:
        return {0, 0, -1};
    case 3:
        return {0, 0, 1};
    case 4:
        return {-1, 0, 0};
    case 5:
        return {1, 0, 0};
    default:
        return {0, 0, 0};
    }
}
int oppositeFace(int face) { return face < 0 ? -1 : (face ^ 1); }
int faceFromName(std::string_view n) {
    static constexpr std::string_view kNames[6] = {"down", "up", "north", "south", "west", "east"};
    for (int i = 0; i < 6; ++i)
        if (kNames[i] == n) return i;
    return -1;
}
std::string_view faceToName(int face) {
    static constexpr std::string_view kNames[6] = {"down", "up", "north", "south", "west", "east"};
    return face >= 0 && face < 6 ? kNames[face] : std::string_view{};
}
int clusterFacing(Block const& b) {
    if (!isCluster(b)) return -1;
    return faceFromName(bs::getString(b, "minecraft:block_face").value_or("up"));
}
BlockPos offset(BlockPos const& pos, int face, int n) {
    auto s = facingStep(face);
    return {pos.x + s[0] * n, pos.y + s[1] * n, pos.z + s[2] * n};
}

std::optional<uint32_t> beaconColorOf(Block const& b) {
    auto const& name = b.getTypeName();
    if (name.starts_with("lepton:")) {
        int c = colorOf(b);
        if (c < 0) return std::nullopt;
        if (isCorundum(b) || isWaxedCorundum(b) || isPane(b)) return kColors[c].beacon;
        if (isLamp(b)) {
            if (bs::getBool(b, "lepton:lit").value_or(false)) return kColors[c].beacon;
            return std::nullopt;
        }
        return std::nullopt;
    }
    // vanilla stained glass / panes: Java DyeColor texture diffuse colours
    static std::map<std::string, uint32_t> const kDye{
        {"white", 0xf9fffe},   {"orange", 0xf9801d}, {"magenta", 0xc74ebd}, {"light_blue", 0x3ab3da},
        {"yellow", 0xfed83d},  {"lime", 0x80c71f},   {"pink", 0xf38baa},    {"gray", 0x474f52},
        {"light_gray", 0x9d9d97}, {"cyan", 0x169c9c}, {"purple", 0x8932b8},  {"blue", 0x3c44aa},
        {"brown", 0x835432},   {"green", 0x5e7c16},  {"red", 0xb02e26},     {"black", 0x1d1d21},
    };
    auto strip = [&](std::string_view suffix) -> std::optional<uint32_t> {
        if (!name.ends_with(suffix)) return std::nullopt;
        auto mid = name.substr(10, name.size() - 10 - suffix.size()); // after "minecraft:"
        if (auto it = kDye.find(std::string(mid)); it != kDye.end()) return it->second;
        return std::nullopt;
    };
    if (!name.starts_with("minecraft:")) return std::nullopt;
    if (auto c = strip("_stained_glass_pane")) return c;
    if (auto c = strip("_stained_glass")) return c;
    return std::nullopt;
}

// ---- java.util.Random -------------------------------------------------------------------------------

int32_t JavaRandom::nextInt(int32_t bound) {
    if (bound <= 0) return 0;
    if ((bound & -bound) == bound) return static_cast<int32_t>((static_cast<int64_t>(bound) * next(31)) >> 31);
    int32_t bits, val;
    do {
        bits = next(31);
        val  = bits % bound;
    } while (bits - val + (bound - 1) < 0);
    return val;
}

int32_t JavaRandom::nextInt(int32_t origin, int32_t bound) {
    int32_t r = nextInt();
    if (origin < bound) {
        int32_t n = bound - origin, m = n - 1;
        if ((n & m) == 0) {
            r = (r & m) + origin;
        } else if (n > 0) {
            for (int32_t u = static_cast<int32_t>(static_cast<uint32_t>(r) >> 1);
                 u + m - (r = u % n) < 0;
                 u = static_cast<int32_t>(static_cast<uint32_t>(nextInt()) >> 1)) {}
            r += origin;
        } else {
            while (r < origin || r >= bound) r = nextInt();
        }
    }
    return r;
}

int32_t javaHash(std::string_view s) {
    uint32_t h = 0;
    for (unsigned char ch : s) h = 31 * h + ch;
    return static_cast<int32_t>(h);
}
int32_t javaHash(int x, int y, int z) {
    uint32_t h = static_cast<uint32_t>(y) + static_cast<uint32_t>(z) * 31u;
    return static_cast<int32_t>(h * 31u + static_cast<uint32_t>(x));
}

// ---- tracker -----------------------------------------------------------------------------------------

namespace {
struct TrackState {
    std::array<std::set<core::GlobalPos>, static_cast<size_t>(Kind::Count)> sets;
    std::array<bool, static_cast<size_t>(Kind::Count)>                      dirty{};
    bool                                                                    loaded = false;
};
TrackState& ts() {
    static TrackState s;
    return s;
}
constexpr char const* kKindKey[] = {"lp:crystals:corundum", "lp:crystals:lamps", "lp:crystals:magnets"};

void loadTracked() {
    auto& s = ts();
    for (auto& set : s.sets) set.clear();
    s.dirty.fill(false);
    for (size_t k = 0; k < s.sets.size(); ++k) {
        auto j = game::db::load(kKindKey[k]);
        if (!j || !j->is_array()) continue;
        for (auto const& e : *j)
            if (e.is_array() && e.size() == 4)
                s.sets[k].insert({e[0].get<int>(), e[1].get<int>(), e[2].get<int>(), e[3].get<int>()});
    }
    s.loaded = true;
}
void saveTracked() {
    auto& s = ts();
    if (!s.loaded) return;
    for (size_t k = 0; k < s.sets.size(); ++k) {
        if (!s.dirty[k]) continue;
        auto arr = nlohmann::json::array();
        for (auto const& p : s.sets[k]) arr.push_back({p.dim, p.x, p.y, p.z});
        game::db::save(kKindKey[k], arr);
        s.dirty[k] = false;
    }
}
} // namespace

std::set<core::GlobalPos> const& tracked(Kind kind) { return ts().sets[static_cast<size_t>(kind)]; }
void track(Kind kind, core::GlobalPos const& pos) {
    if (ts().sets[static_cast<size_t>(kind)].insert(pos).second) ts().dirty[static_cast<size_t>(kind)] = true;
}
void untrack(Kind kind, core::GlobalPos const& pos) {
    if (ts().sets[static_cast<size_t>(kind)].erase(pos)) ts().dirty[static_cast<size_t>(kind)] = true;
}
std::optional<Kind> kindOf(Block const& b) {
    if (isCorundum(b)) return Kind::Corundum;
    if (isLamp(b)) return Kind::Lamp;
    if (isMagnet(b)) return Kind::Magnet;
    return std::nullopt;
}

// ---- redstone -----------------------------------------------------------------------------------------

namespace {
struct PowerCache {
    uint64_t                                tick = ~0ULL;
    std::map<std::tuple<int, int, int, int>, int> strength; // (dim, x, y, z) -> consumer strength
};
PowerCache& powerCache() {
    static PowerCache c;
    return c;
}
uint64_t& tickCounter() {
    static uint64_t t = 0;
    return t;
}

/// Power the block at `pos` gets from its neighbours (used when it has no circuit component).
int neighbourPower(BlockSource& region, BlockPos const& pos) {
    int best = 0;
    for (int f = 0; f < 6; ++f) {
        auto        n    = offset(pos, f);
        auto const& b    = region.getBlock(n);
        auto const& name = b.getTypeName();
        int         p    = 0;
        if (name == "minecraft:redstone_block" || name == "minecraft:redstone_torch") p = 15;
        else if (name == "minecraft:lever" && bs::getBool(b, "open_bit").value_or(false)) p = 15;
        else if (name.find("button") != std::string::npos && bs::getBool(b, "button_pressed_bit").value_or(false)) p = 15;
        else if (name == "minecraft:powered_repeater") p = 15;
        else if (name == "minecraft:observer" && bs::getBool(b, "powered_bit").value_or(false)) p = 15;
        else if (name != "minecraft:redstone_lamp" && name != "minecraft:lit_redstone_lamp")
            p = bs::getInt(b, "redstone_signal").value_or(0);
        if (hasTag(b, "lepton:button") || hasTag(b, "lepton:pressure_plate"))
            if (bs::getBool(b, "lepton:pressed").value_or(false)) p = 15;
        best = std::max(best, p);
    }
    return best;
}
} // namespace

int redstonePower(BlockSource& region, BlockPos const& pos) {
    auto& cache = powerCache();
    int   dim   = static_cast<int>(region.getDimensionId());
    if (cache.tick != tickCounter()) {
        cache.tick = tickCounter();
        cache.strength.clear();
        if (auto level = ll::service::getLevel()) {
            for (int d = 0; d < 3; ++d) {
                auto dimension = level->getDimension(d).lock();
                if (!dimension) continue;
                CircuitSystem* sys = dimension->mCircuitSystem.get();
                if (!sys) continue;
                // iterate (never hash): the game's BlockPos hash is not ours
                for (auto const& [p, comp] : sys->mSceneGraph->mAllComponents) {
                    if (comp) cache.strength[{d, p.x, p.y, p.z}] = comp->getStrength();
                }
            }
        }
    }
    if (auto it = cache.strength.find({dim, pos.x, pos.y, pos.z}); it != cache.strength.end())
        return std::clamp(it->second, 0, 15);
    return neighbourPower(region, pos);
}

// ---- world helpers ------------------------------------------------------------------------------------

bool setBlock(BlockSource& region, BlockPos const& pos, Block const& block) { return game::setBlock(region, pos, block); }

bool destroyBlock(BlockSource& region, BlockPos const& pos, bool drop) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    BlockChangeContext ctx{};
    return level->destroyBlock(region, pos, drop, ctx);
}

void spawnParticle(BlockSource& region, std::string const& effect, Vec3 const& pos,
                   std::vector<std::pair<std::string, float>> const& vars) {
    // MolangVariableMap has no exported default constructor: an all-zero object is an empty map
    // (two empty vectors and a bool); fill it, copy it with the inline copy constructor, destroy it.
    struct alignas(MolangVariableMap) Raw {
        std::byte bytes[sizeof(MolangVariableMap)]{};
    } raw;
    auto& tmp = *std::launder(reinterpret_cast<MolangVariableMap*>(&raw));
    for (auto const& [k, v] : vars) {
        std::string name = "variable." + k;
        HashedString hs(name);
        tmp.setMolangVariable(hs.getHash(), hs.getString().c_str(), MolangScriptArg(v), false);
    }
    std::optional<MolangVariableMap> map(tmp);
    tmp.~MolangVariableMap();
    auto dim = static_cast<uchar>(static_cast<int>(region.getDimensionId()));
    SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(pos, effect, dim, std::move(map)));
    pkt.sendTo(BlockPos{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))},
               region.getDimensionId());
}

void playSound(BlockSource& region, BlockPos const& pos, int event, std::string const& blockId) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Player* any = nullptr;
    level->forEachPlayer([&](Player& p) {
        if (p.getDimensionId() == region.getDimensionId()) {
            any = &p;
            return false;
        }
        return true;
    });
    if (!any) return; // sound events need an actor identifier
    Vec3        at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    Block const* block = blockId.empty() ? &region.getBlock(pos) : bs::of(blockId);
    if (!block) block = &region.getBlock(pos);
    level->broadcastSoundEvent(region, static_cast<SharedTypes::Legacy::LevelSoundEvent>(event), at, *block,
                               ActorSoundIdentifier(*any), false, std::nullopt);
}

int randomTickSpeed() {
    auto level = ll::service::getLevel();
    if (!level) return 1;
    auto const& rules = level->getGameRules().mGameRules.get();
    auto        idx   = static_cast<size_t>(GameRules::GameRulesIndex::RandomTickSpeed);
    if (idx >= rules.size()) return 1;
    if (auto const* v = std::get_if<int>(&rules[idx].mValue.get())) return *v;
    return 1;
}

uint64_t currentTick() { return tickCounter(); }

// ---- module: tracker upkeep -------------------------------------------------------------------------

namespace {
class TrackerModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            auto gp = game::globalPos(region, ev.pos());
            if (auto k = kindOf(ev.previousBlock())) untrack(*k, gp);
            if (auto k = kindOf(ev.newBlock())) track(*k, gp);
            for (auto& fn : changeListeners()) fn(region, ev.pos(), ev.previousBlock(), ev.newBlock());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            ++tickCounter();
            for (auto& fn : tickListeners()) fn();
        }));
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] { loadTracked(); });
            game::db::onWorldSave([] { saveTracked(); });
            game::db::onWorldClosed([] {
                ts() = TrackState{};
                powerCache() = PowerCache{};
            });
        }
        if (game::db::available() && !ts().loaded) loadTracked();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        saveTracked();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};
} // namespace

std::vector<ChangeFn>& changeListeners() {
    static std::vector<ChangeFn> v;
    return v;
}
std::vector<std::function<void()>>& tickListeners() {
    static std::vector<std::function<void()>> v;
    return v;
}

} // namespace lp::feature::crystals

LP_REGISTER_MODULE_ORDERED(lp::feature::crystals::TrackerModule, lp::ModuleOrder::Feature - 1);
