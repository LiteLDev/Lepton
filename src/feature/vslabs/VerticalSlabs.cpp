// U1 VerticalSlabs: everything Quark's VerticalSlabsModule adds on top of the generated vertical
// slab blocks (docs/spec/vslabs.md §3).

#include "feature/vslabs/VSlabs.h"

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Random.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/global/LightningBolt.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/WallBlock.h"
#include "mc/world/level/block/WallConnectionType.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/block/components/BlockComponentDirectData.h"
#include "mc/world/level/storage/GameRule.h"
#include "mc/world/level/storage/GameRules.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("vslabs", lp::feature::vslabs::Config);

namespace lp::feature::vslabs {

namespace bs     = game::blockstates;
namespace shapes = game::shapes;
using SoundEvent = SharedTypes::Legacy::LevelSoundEvent;
using LevelEvt   = SharedTypes::Legacy::LevelEvent;

Config const& config() { return lp::config::get<Config>(); }

// ---- vertical slabs ---------------------------------------------------------------------------

bool isVerticalSlab(Block const& block) { return shapes::kindOf(block) == shapes::Kind::VerticalSlab; }

bool isVerticalSlabId(std::string_view id) {
    if (!id.starts_with("lepton:") || !id.ends_with("_vertical_slab")) return false;
    auto const* b = bs::of(id);
    return b && isVerticalSlab(*b);
}

std::string typeOf(Block const& block) {
    if (!isVerticalSlab(block)) return {};
    return bs::getString(block, "lepton:type").value_or("north");
}

std::optional<std::string> parentSlabOf(std::string_view vslabId) {
    constexpr std::string_view kPre = "lepton:", kSuf = "_vertical_slab";
    if (!vslabId.starts_with(kPre) || !vslabId.ends_with(kSuf)) return std::nullopt;
    auto path = std::string(vslabId.substr(kPre.size(), vslabId.size() - kPre.size() - kSuf.size()));
    std::string slab = path == "stone" ? "minecraft:normal_stone_slab" : "minecraft:" + path + "_slab";
    if (!bs::of(slab)) return std::nullopt; // not a vanilla material (another unit's vertical slab)
    return slab;
}

std::optional<bool> connectsTo(Block const& other, std::string_view face, bool wallOrPane) {
    if (!isVerticalSlab(other)) return std::nullopt;
    auto type = typeOf(other);
    if (type == "double") return true;                     // a full block
    if (face == shapes::opposite(type)) return true;       // the full back face (sturdy, as in Java)
    if (face == type) return false;                        // the open side
    // Quark: shouldWallConnect - walls and panes connect to a side whose axis differs from the slab's
    return wallOrPane && config().enableVerticalSlabs && config().allowSideConnections;
}

// ---- copper ------------------------------------------------------------------------------------

namespace {
constexpr std::array<std::string_view, 4> kStagePrefix = {"", "exposed_", "weathered_", "oxidized_"};

/// Vanilla weathering copper blocks, by path without the stage prefix.
bool isVanillaCopperBase(std::string_view path, bool unaffected) {
    static std::unordered_set<std::string_view> const kBases = {
        "cut_copper",  "cut_copper_slab", "double_cut_copper_slab", "cut_copper_stairs", "copper_door",
        "copper_trapdoor", "copper_grate", "copper_bulb", "chiseled_copper", "lightning_rod", "copper_chest",
        "copper_bars", "copper_chain", "copper_lantern", "copper_golem_statue",
    };
    if (unaffected && path == "copper_block") return true;
    if (!unaffected && path == "copper") return true;
    return kBases.contains(path);
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float nextFloat() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }
int   nextInt(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng()); }
} // namespace

std::optional<int> copperAge(std::string_view id) {
    if (auto st = copperSlabStage(id)) {
        if (st->second) return std::nullopt;
        return st->first;
    }
    if (!id.starts_with("minecraft:")) return std::nullopt;
    auto path = id.substr(10);
    if (path.find("copper") == std::string_view::npos && !path.ends_with("lightning_rod")) return std::nullopt;
    for (int age = 3; age >= 1; --age) {
        if (path.starts_with(kStagePrefix[age])) {
            return isVanillaCopperBase(path.substr(kStagePrefix[age].size()), false) ? std::optional(age) : std::nullopt;
        }
    }
    return isVanillaCopperBase(path, true) ? std::optional(0) : std::nullopt;
}

std::string copperSlabId(int age, bool waxed) {
    return std::string("lepton:") + (waxed ? "waxed_" : "") + std::string(kStagePrefix[age & 3])
         + "cut_copper_vertical_slab";
}

std::optional<std::pair<int, bool>> copperSlabStage(std::string_view id) {
    // cheap filter first: this runs for every block change and in the weathering scan
    if (!id.starts_with("lepton:") || !id.ends_with("cut_copper_vertical_slab")) return std::nullopt;
    for (int age = 0; age < 4; ++age) {
        for (bool waxed : {false, true}) {
            if (id == copperSlabId(age, waxed)) return std::pair{age, waxed};
        }
    }
    return std::nullopt;
}

namespace {
/// Replaces the copper slab at `pos` by `id`, keeping its lepton:type.
bool setCopper(BlockSource& region, BlockPos const& pos, std::string const& id) {
    auto const& cur = region.getBlock(pos);
    auto const* to  = bs::of(id);
    if (!to) return false;
    auto const* next = bs::copyStates(cur, *to);
    return next && game::setBlock(region, pos, *next);
}

Vec3 center(BlockPos const& pos) {
    return {static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
}
} // namespace

bool weatherOnce(BlockSource& region, BlockPos const& pos, bool force) {
    auto const& b   = region.getBlock(pos);
    auto        age = copperAge(b.getTypeName());
    if (!age || *age >= 3 || !copperSlabStage(b.getTypeName())) return false;
    // Java WeatheringCopper#onRandomTick: 0.05688889 per random tick, then ChangeOverTimeBlock
    if (!force && nextFloat() >= 0.05688889f) return false;
    int same = 0, older = 0;
    for (int dx = -4; dx <= 4; ++dx) {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dz = -4; dz <= 4; ++dz) {
                int d = std::abs(dx) + std::abs(dy) + std::abs(dz);
                if (d == 0 || d > 4) continue;
                BlockPos p{pos.x + dx, pos.y + dy, pos.z + dz};
                if (p.y < region.getMinHeight() || p.y >= region.getMaxHeight()) continue;
                auto other = copperAge(region.getBlock(p).getTypeName());
                if (!other) continue;
                if (*other < *age) return false; // a younger neighbour holds it back
                if (*other > *age) ++older;
                else ++same;
            }
        }
    }
    float f = static_cast<float>(older + 1) / static_cast<float>(older + same + 1);
    float g = f * f * (*age == 0 ? 0.75f : 1.f);
    if (!force && nextFloat() >= g) return false;
    return setCopper(region, pos, copperSlabId(*age + 1, false));
}

namespace {

// ---- scheduling (world edits after a cancelled interaction run on the next tick) ---------------

struct Task {
    uint64_t              due;
    std::function<void()> fn;
};
struct State {
    uint64_t                                                     tick = 0;
    std::vector<Task>                                            tasks;
    std::map<std::pair<int64_t, std::tuple<int, int, int>>, uint64_t> debounce;
    std::set<int64_t>                                            bolts; ///< lightning bolts already handled
    std::map<core::GlobalPos, uint64_t>                          pending; ///< weathering tick due (level tick)
};
State& st() {
    static State s;
    return s;
}
void later(std::function<void()> fn) { st().tasks.push_back({st().tick + 1, std::move(fn)}); }

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

void effects(BlockSource& region, BlockPos const& pos, LevelEvt event, SoundEvent sound, Player* by) {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->broadcastLevelEvent(event, center(pos), 0, nullptr);
    if (by) {
        level->broadcastSoundEvent(
            region,
            sound,
            center(pos),
            region.getBlock(pos),
            ActorSoundIdentifier(*by),
            false,
            std::nullopt
        );
    }
}

int randomTickSpeed(Level& level) {
    auto const& rules = level.getGameRules().mGameRules.get();
    auto        idx   = static_cast<size_t>(GameRules::GameRulesIndex::RandomTickSpeed);
    if (idx >= rules.size()) return 1;
    auto const& v = rules[idx].mValue.get();
    if (auto const* i = std::get_if<int>(&v)) return *i;
    return 1;
}

// ---- interaction: honeycomb waxes, axes scrape / remove wax (Quark: ToolInteractionHandler) ----

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !config().enableVerticalSlabs) return;
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.blockPos();
    auto  name   = region.getBlock(pos).getTypeName();
    auto  stage  = copperSlabStage(name);
    if (!stage) return;
    auto const& item   = ev.item();
    auto        itemId = item.isNull() ? std::string() : item.getTypeName();
    auto [age, waxed]  = *stage;

    std::string target;
    LevelEvt    event{};
    SoundEvent  sound{};
    bool        honeycomb = false;
    if (itemId == "minecraft:honeycomb" && !waxed) {
        target    = copperSlabId(age, true);
        event     = LevelEvt::WaxOn;
        sound     = SoundEvent::CopperWaxOn;
        honeycomb = true;
    } else if (itemId.ends_with("_axe") && waxed) {
        target = copperSlabId(age, false);
        event  = LevelEvt::WaxOff;
        sound  = SoundEvent::CopperWaxOff;
    } else if (itemId.ends_with("_axe") && age > 0) {
        target = copperSlabId(age - 1, false);
        event  = LevelEvt::Scrape;
        sound  = SoundEvent::Scrape;
    } else {
        return;
    }
    ev.cancel();
    auto id  = player.getOrCreateUniqueID().rawID;
    auto key = std::make_pair(id, std::make_tuple(pos.x, pos.y, pos.z));
    if (auto it = st().debounce.find(key); it != st().debounce.end() && st().tick - it->second < 5) return;
    st().debounce[key] = st().tick;
    int dim            = static_cast<int>(region.getDimensionId());
    later([dim, pos, id, name, target, event, sound, honeycomb] {
        auto* r = game::regionOf(dim);
        if (!r || r->getBlock(pos).getTypeName() != name) return;
        if (!setCopper(*r, pos, target)) return;
        auto* p = playerById(id);
        effects(*r, pos, event, sound, p);
        log().info("vslabs: {} -> {} at {} {} {}", name, target, pos.x, pos.y, pos.z);
        if (!p || p->isCreative()) return;
        ItemStack held = p->getSelectedItem();
        if (held.isNull()) return;
        if (honeycomb && held.getTypeName() == "minecraft:honeycomb") held.remove(1);
        else if (!honeycomb && held.getTypeName().ends_with("_axe")) held.hurtAndBreak(1, p);
        else return;
        p->setSelectedItem(held);
        p->sendInventory(false);
    });
}

/// VerticalSlabs off: vertical slabs can't be placed (Quark doesn't register them at all).
void onPlacing(ll::event::PlayerPlacingBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || config().enableVerticalSlabs) return;
    auto const& item = player.getSelectedItem();
    if (!item.isNull() && isVerticalSlabId(item.getTypeName())) ev.cancel();
}

/// A weathering (non-waxed, not yet oxidized) copper vertical slab.
bool isWeatheringSlab(std::string_view name) {
    auto stage = copperSlabStage(name);
    return stage && !stage->second && stage->first < 3;
}

int gTickDelayMax = 8191;

/// Queues the next "random tick" of a weathering copper slab: a delay uniform in [1, max] gives on
/// average one tick per 4096 game ticks = one Bedrock random tick at randomTickSpeed 1 (16^3
/// blocks per subchunk). Custom blocks get no random ticks, so the slab carries its own.
uint64_t levelTick(BlockSource& region) { return region.getLevel().getCurrentTick().tickID; }

/// `restart`: the block just changed type (placed, aged, scraped, ...), so a tick queued for the
/// old block may be dropped by the game: start a new chain (the old tick, if it still comes, is
/// then rejected as a stray by takeWeatheringTick).
void scheduleWeathering(BlockSource& region, BlockPos const& pos, bool restart = false) {
    auto const& b = region.getBlock(pos);
    if (!isWeatheringSlab(b.getTypeName())) return;
    auto  now = levelTick(region);
    auto& due = st().pending[game::globalPos(region, pos)];
    if (!restart && due > now) return; // one chain per block
    int delay = 1 + nextInt(std::max(1, gTickDelayMax));
    due       = now + static_cast<uint64_t>(delay);
    region.addToTickingQueue(pos, b, delay, 0, false);
}

/// The queued tick at `pos` is the chain's current one (not a stray duplicate). Consumes it.
bool takeWeatheringTick(BlockSource& region, BlockPos const& pos) {
    auto& pending = st().pending;
    auto  it      = pending.find(game::globalPos(region, pos));
    if (it == pending.end()) return true; // a tick queued before a reload: adopt it
    auto now = levelTick(region);
    // a tick that comes before the chain's due time is a stray (queued for an older chain); a late
    // one is the chain's own tick, delayed while the chunk was unloaded or out of simulation range
    if (it->second > now + 1) return false;
    pending.erase(it);
    return true;
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    auto const& nb = ev.newBlock();
    // a block that just became weathering copper starts its tick chain (the chain itself
    // reschedules from the tick hook; a state-only change keeps the pending tick)
    if (ev.previousBlock().getTypeName() != nb.getTypeName()) {
        if (isWeatheringSlab(nb.getTypeName())) scheduleWeathering(region, ev.pos(), true);
        else if (isWeatheringSlab(ev.previousBlock().getTypeName()))
            st().pending.erase(game::globalPos(region, ev.pos())); // broken / waxed / oxidized
    }
    // a double vertical slab holds no water (Quark: WATERLOGGED false when merged)
    if (!isVerticalSlab(nb) || typeOf(nb) != "double") return;
    auto pos = ev.pos();
    int  dim = static_cast<int>(region.getDimensionId());
    later([dim, pos] {
        auto* r = game::regionOf(dim);
        if (!r || typeOf(r->getBlock(pos)) != "double") return;
        auto const& extra = r->getExtraBlock(pos);
        if (extra.isAir()) return;
        if (auto const* air = bs::of("minecraft:air")) r->setExtraBlock(pos, *air, 3);
    });
}

void onTick(ll::event::ServerLevelTickEvent&) {
    auto& s = st();
    ++s.tick;
    if (!s.tasks.empty()) {
        std::vector<Task> due;
        for (auto it = s.tasks.begin(); it != s.tasks.end();) {
            if (it->due <= s.tick) {
                due.push_back(std::move(*it));
                it = s.tasks.erase(it);
            } else ++it;
        }
        for (auto& t : due) {
            try {
                t.fn();
            } catch (std::exception const& e) {
                log().error("vslabs: task failed: {}", e.what());
            }
        }
    }
    if (s.tick % 1200 == 0) {
        s.debounce.clear();
        s.bolts.clear();
    }
}

// ---- lightning (Java LightningBolt#clearCopperOnLightningStrike) ----------------------------------

} // namespace

void clearCopperOnLightning(BlockSource& region, BlockPos const& strike) {
    auto const& hit = region.getBlock(strike);
    BlockPos    at  = strike;
    if (hit.getTypeName().ends_with("lightning_rod")) {
        // the rod points away from the block it is attached to
        static constexpr int kDx[6] = {0, 0, 0, 0, -1, 1}, kDy[6] = {-1, 1, 0, 0, 0, 0}, kDz[6] = {0, 0, -1, 1, 0, 0};
        int f = bs::getInt(hit, "facing_direction").value_or(1) % 6;
        at    = BlockPos{strike.x - kDx[f], strike.y - kDy[f], strike.z - kDz[f]};
    }
    if (!copperAge(region.getBlock(at).getTypeName())) return;
    if (auto st = copperSlabStage(region.getBlock(at).getTypeName()); st && st->first > 0) {
        setCopper(region, at, copperSlabId(0, false));
    }
    int walks = nextInt(3) + 3;
    for (int w = 0; w < walks; ++w) {
        int      steps = nextInt(8) + 1;
        BlockPos cur   = at;
        for (int s = 0; s < steps; ++s) {
            // randomStepCleaningCopper: 10 random positions in the 3x3x3 cube around `cur`
            std::optional<BlockPos> next;
            for (int i = 0; i < 10 && !next; ++i) {
                BlockPos p{cur.x + nextInt(3) - 1, cur.y + nextInt(3) - 1, cur.z + nextInt(3) - 1};
                auto     name = region.getBlock(p).getTypeName();
                auto     age  = copperAge(name);
                if (!age) continue;
                if (auto st = copperSlabStage(name); st && st->first > 0) {
                    setCopper(region, p, copperSlabId(st->first - 1, false));
                    if (auto level = ll::service::getLevel())
                        level->broadcastLevelEvent(LevelEvt::ParticlesElectricSpark, center(p), 0, nullptr);
                }
                next = p;
            }
            if (!next) break;
            cur = *next;
        }
    }
}

namespace {

int  gCopperTicks = 0;
bool gInstalled   = false;

// ---- hooks -------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    WallConnectHook,
    ll::memory::HookPriority::Normal,
    WallBlock,
    &WallBlock::_desiredConnectionState,
    WallConnectionType,
    BlockSource&    region,
    BlockPos const& pos,
    uchar const     neighbor
) {
    auto result = origin(region, pos, neighbor);
    if (!config().enableVerticalSlabs) return result;
    auto dir = shapes::faceName(neighbor);
    if (dir.empty() || dir == "up" || dir == "down") return result;
    auto c = connectsTo(region.getBlock(shapes::neighbor(pos, dir)), shapes::opposite(dir), true);
    if (!c) return result;
    if (!*c) return WallConnectionType::None;
    if (result != WallConnectionType::None) return result;
    // tall under a solid block, or under a wall that connects the same way (vanilla / Lepton walls)
    auto const& above = region.getBlock(shapes::neighbor(pos, "up"));
    if (above.isAir()) return WallConnectionType::Short;
    if (above.mCachedComponentData->mIsSolid) return WallConnectionType::Tall;
    auto up = bs::getString(above, "wall_connection_type_" + std::string(dir));
    if (!up) up = bs::getString(above, "lepton:" + std::string(dir));
    return (up && *up != "none") ? WallConnectionType::Tall : WallConnectionType::Short;
}

LL_TYPE_INSTANCE_HOOK(
    QueuedTickHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::queuedTick,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    Random&         random
) {
    origin(region, pos, random);
    auto const& name = getTypeName();
    if (!name.starts_with("lepton:") || !name.ends_with("cut_copper_vertical_slab")) return;
    if (region.getLevel().isClientSide() || !takeWeatheringTick(region, pos)) return;
    ++gCopperTicks;
    if (config().enableVerticalSlabs) {
        int speed = randomTickSpeed(region.getLevel());
        for (int i = 0; i < speed; ++i) {
            if (weatherOnce(region, pos)) break;
        }
    }
    // next tick (also while the module is off, so the chain survives a /quark reload); an aged
    // block got its own from onBlockChanged
    if (region.getBlock(pos).getTypeName() == name) scheduleWeathering(region, pos);
}

LL_TYPE_STATIC_HOOK(
    FuelHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::getItemBurnDuration,
    float,
    Item const& item,
    float       burnInterval
) {
    auto r = origin(item, burnInterval);
    if (r > 0.f) return r;
    auto const& name = item.mFullName->getString();
    if (!name.starts_with("lepton:") || !name.ends_with("_vertical_slab") || !config().enableVerticalSlabs) return r;
    // Quark: a wooden vertical slab burns as long as its slab (150 ticks); nether woods don't burn
    std::string slab;
    if (auto parent = parentSlabOf(name)) slab = *parent;
    else if (auto const* b = bs::of(name); b && b->mDirectData->mFlameOdds != FlameOdds::Never) slab = "minecraft:oak_slab";
    if (slab.empty()) return r;
    auto stack = game::makeItem(slab);
    if (stack.isNull() || !stack.mItem.get()) return r;
    return origin(*stack.mItem.get(), burnInterval);
}

LL_TYPE_INSTANCE_HOOK(
    LightningHook,
    ll::memory::HookPriority::Normal,
    LightningBolt,
    &LightningBolt::$normalTick,
    void
) {
    origin();
    if (isClientSide() || !config().enableVerticalSlabs) return;
    auto id = getOrCreateUniqueID().rawID;
    if (!st().bolts.insert(id).second) return;
    auto     p = getPosition();
    BlockPos strike{
        static_cast<int>(std::floor(p.x)),
        static_cast<int>(std::floor(p.y - 1.0e-6f)),
        static_cast<int>(std::floor(p.z))
    };
    try {
        clearCopperOnLightning(getDimensionBlockSource(), strike);
    } catch (std::exception const& e) {
        log().error("vslabs: lightning: {}", e.what());
    }
}

// ---- module ----------------------------------------------------------------------------------------

class VerticalSlabsModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(onPlacing));
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        mRule = shapes::addConnectRule([](shapes::Kind self, Block const& other, std::string_view face) {
            return connectsTo(other, face, self != shapes::Kind::Fence).value_or(false);
        });
        WallConnectHook::hook();
        QueuedTickHook::hook();
        FuelHook::hook();
        LightningHook::hook();
        gInstalled = true;
        if (ll::service::getLevel()) reapplyRecipes(); // BDS: the world is already loaded
        static bool registered = false;
        if (!registered) {
            // the world callbacks can't be unregistered: they do nothing while uninstalled
            registered = true;
            game::db::onWorldReady([] {
                if (gInstalled) reapplyRecipes();
            });
            game::db::onWorldClosed([] {
                st() = State{};
                resetRecipes();
            });
            lp::config::onReload([] {
                if (gInstalled && ll::service::getLevel()) reapplyRecipes();
            });
        }
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        shapes::removeConnectRule(mRule);
        WallConnectHook::unhook();
        QueuedTickHook::unhook();
        FuelHook::unhook();
        LightningHook::unhook();
        gInstalled = false;
        restoreRecipes();
        st() = State{};
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    int                                 mRule = 0;
};

} // namespace

int  testing::copperTicks() { return gCopperTicks; }
void testing::setTickDelayMax(int ticks) { gTickDelayMax = ticks; }

} // namespace lp::feature::vslabs

LP_REGISTER_MODULE(lp::feature::vslabs::VerticalSlabsModule);
