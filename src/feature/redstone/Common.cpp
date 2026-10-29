// U10 redstone: config, module, ticks, shared helpers (geometry, redstone signals, block type virtual
// tables), the Block::queuedTick / neighborChanged dispatch and config-dependent recipes.

#include "feature/redstone/Redstone.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <tuple>
#include <windows.h>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"
#include "mc/world/redstone/circuit/components/CircuitComponentType.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("redstone", lp::feature::redstone::RedstoneConfig);

namespace lp::feature::redstone {

namespace bs = game::blockstates;

RedstoneConfig const& cfg() { return config::get<RedstoneConfig>(); }

namespace {

std::atomic<bool>            gInstalled{false};
std::atomic<std::thread::id> gServerThread{};

struct Task {
    uint64_t              due;
    std::function<void()> fn;
};
struct TickState {
    uint64_t                           tick = 0;
    std::vector<Task>                  tasks;
    std::vector<std::function<void()>> listeners;
};
TickState& ts() {
    static TickState s;
    return s;
}

// ---- circuit cache ---------------------------------------------------------------------------------

struct CompInfo {
    int      strength;
    uint64_t type;
    int      direction;
};
struct PowerCache {
    uint64_t                                              tick[3]{~0ull, ~0ull, ~0ull};
    std::map<std::tuple<int, int, int>, CompInfo>         byPos[3];
};
PowerCache& powerCache() {
    static PowerCache c;
    return c;
}

std::map<std::tuple<int, int, int>, CompInfo> const* componentsOf(BlockSource& region) {
    int d = dimOf(region);
    if (d < 0 || d > 2) return nullptr;
    auto& c = powerCache();
    if (c.tick[d] != ts().tick) {
        c.tick[d] = ts().tick;
        c.byPos[d].clear();
        CircuitSystem* sys = region.getDimension().mCircuitSystem.get();
        if (sys) {
            // iterate (never hash): the game's BlockPos hash is not LeviLamina's
            for (auto const& [p, comp] : sys->mSceneGraph->mAllComponents) {
                if (!comp) continue;
                c.byPos[d][{p.x, p.y, p.z}] = CompInfo{
                    comp->getStrength(),
                    static_cast<uint64_t>(comp->getCircuitComponentType()),
                    static_cast<int>(comp->mDirection)
                };
            }
        }
    }
    return &c.byPos[d];
}

// ---- recipes ---------------------------------------------------------------------------------------

std::vector<std::shared_ptr<Recipe>>& graveyard() {
    static std::vector<std::shared_ptr<Recipe>> g;
    return g;
}

std::set<std::string> disabledRecipes() {
    auto const&           c = cfg();
    std::set<std::string> out;
    if (!c.enableIronRod || c.usePreEndRecipe) out.insert("iron_rod");
    if (!c.enableIronRod || !c.usePreEndRecipe) out.insert("iron_rod_pre_end");
    if (!c.enableGravisand) out.insert("gravisand");
    for (auto w : {"wooden", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "bamboo", "crimson",
                   "warped", "pale_oak"}) {
        if (!c.enableMetalButtons || !c.enableIron) out.insert(std::string("iron_button_from_") + w);
        if (!c.enableMetalButtons || !c.enableGold) out.insert(std::string("gold_button_from_") + w);
    }
    if (!c.enableObsidianPlate) {
        out.insert("obsidian_pressure_plate");
        out.insert("obsidian_pressure_plate_crying");
        out.insert("obsidian_pressure_plate_crying2");
    }
    if (!c.enableRedstoneRandomizer) out.insert("redstone_randomizer");
    if (!c.enableEnderWatcher) out.insert("ender_watcher");
    return out;
}

int removeRecipes() {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    auto& recipes = level->getRecipes();
    auto  ids     = disabledRecipes();
    if (ids.empty()) return 0;
    std::set<Recipe const*> gone;
    for (auto& [tag, byId] : recipes.mRecipes.get()) {
        for (auto it = byId.begin(); it != byId.end();) {
            std::string const& id = it->first;
            if (id.starts_with("lepton:") && ids.contains(id.substr(7))) {
                gone.insert(it->second.get());
                graveyard().push_back(it->second);
                it = byId.erase(it);
            } else ++it;
        }
    }
    if (gone.empty()) return 0;
    for (auto& [item, map] : recipes.mRecipesByOutput.get()) {
        for (auto it = map.begin(); it != map.end();) {
            if (gone.contains(it->second)) it = map.erase(it);
            else ++it;
        }
    }
    std::erase_if(recipes.mUnlockableRecipes.get(), [&](auto const& r) { return gone.contains(r.get()); });
    log().info("redstone: disabled {} recipes", gone.size());
    return static_cast<int>(gone.size());
}

// ---- virtual tables ----------------------------------------------------------------------------------

struct Patched {
    BlockType*                 type;
    void**                     original;
    std::unique_ptr<void*[]>   table; ///< [0] = RTTI locator, [1..] = slots
};
std::vector<Patched>& patched() {
    static std::vector<Patched> p;
    return p;
}
std::vector<std::unique_ptr<void*[]>>& oldTables() {
    static std::vector<std::unique_ptr<void*[]>> t;
    return t;
}

/// The vtable slot a vcall thunk (`mov rax,[rcx]; jmp [rax+disp]`) dispatches through; -1 if unknown.
int slotOfThunk(void* thunk) {
    auto* p = static_cast<uint8_t const*>(thunk);
    for (int i = 0; i < 4 && p[0] == 0xE9; ++i) p = p + 5 + *reinterpret_cast<int32_t const*>(p + 1);
    if (p[0] != 0x48 || p[1] != 0x8B || p[2] != 0x01 || p[3] != 0xFF) return -1;
    if (p[4] == 0x20) return 0;
    if (p[4] == 0x60) return p[5] / 8;
    if (p[4] == 0xA0) return *reinterpret_cast<int32_t const*>(p + 5) / 8;
    return -1;
}

/// Number of readable pointers starting at `p` (up to `max`).
size_t readablePointers(void* const* p, size_t max) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
    auto end   = static_cast<uint8_t const*>(mbi.BaseAddress) + mbi.RegionSize;
    auto avail = static_cast<size_t>(end - reinterpret_cast<uint8_t const*>(p)) / sizeof(void*);
    return std::min(avail, max);
}

// ---- hooks -------------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    RedstoneQueuedTickHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::queuedTick,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    Random&         random
) {
    origin(region, pos, random);
    if (!gInstalled.load(std::memory_order_relaxed)) return;
    auto const& name = getTypeName();
    if (name.size() < 16 || !name.starts_with("lepton:")) return;
    bool randomizer = name == "lepton:redstone_randomizer";
    bool watcher    = !randomizer && name == "lepton:ender_watcher";
    if (!randomizer && !watcher) return;
    if (!onServerThread() || !isServerRegion(region)) return;
    if (randomizer) randomizerTick(region, pos, *this);
    else watcherTick(region, pos, *this);
}

LL_TYPE_INSTANCE_HOOK(
    RedstoneNeighborHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::neighborChanged,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    BlockPos const& neighborPos
) {
    origin(region, pos, neighborPos);
    if (!gInstalled.load(std::memory_order_relaxed)) return;
    auto const& name = getTypeName();
    if (name != "lepton:gravisand") return;
    if (!onServerThread() || !isServerRegion(region)) return;
    gravisandNeighborChanged(region, pos);
}

void onLevelTick(ll::event::ServerLevelTickEvent&) {
    gServerThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
    auto& s = ts();
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
                log().error("redstone: task failed: {}", e.what());
            }
        }
    }
    for (auto& fn : s.listeners) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("redstone: tick listener failed: {}", e.what());
        }
    }
}

class RedstoneModule {
public:
    void install() {
        gServerThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldReady([] {
                if (!gInstalled) return;
                removeRecipes();
                onWorldReadyGravisand();
            });
            game::db::onWorldClosed([] {
                ts().tasks.clear();
                // the client destroys the world's block types: forget them without touching them
                for (auto& p : patched()) oldTables().push_back(std::move(p.table));
                patched().clear();
            });
        }
        RedstoneQueuedTickHook::hook();
        RedstoneNeighborHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(onLevelTick);
        gInstalled = true;
        installPistons();
        installGravisand();
        installDispensers();
        if (game::db::available()) {
            removeRecipes();
            onWorldReadyGravisand();
        }
        log().info("redstone: installed");
    }
    void uninstall() {
        gInstalled = false;
        uninstallDispensers();
        uninstallGravisand();
        uninstallPistons();
        unpatchAll();
        if (mTick) ll::event::EventBus::getInstance().removeListener(mTick);
        mTick.reset();
        RedstoneNeighborHook::unhook();
        RedstoneQueuedTickHook::unhook();
        ts().tasks.clear();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace

bool installed() { return gInstalled.load(std::memory_order_relaxed); }

uint64_t now() { return ts().tick; }
void     later(uint64_t delay, std::function<void()> fn) {
    ts().tasks.push_back({ts().tick + std::max<uint64_t>(delay, 1), std::move(fn)});
}
void onTick(std::function<void()> fn) { ts().listeners.push_back(std::move(fn)); }
bool onServerThread() { return std::this_thread::get_id() == gServerThread.load(std::memory_order_relaxed); }

// ---- geometry --------------------------------------------------------------------------------------------

BlockPos offset(BlockPos const& p, int face, int n) {
    static constexpr int d[6][3] = {
        {0,  -1, 0 },
        {0,  1,  0 },
        {0,  0,  -1},
        {0,  0,  1 },
        {-1, 0,  0 },
        {1,  0,  0 }
    };
    if (face < 0 || face > 5) return p;
    return BlockPos{p.x + d[face][0] * n, p.y + d[face][1] * n, p.z + d[face][2] * n};
}
int faceFromName(std::string_view name) {
    static constexpr std::string_view names[6] = {"down", "up", "north", "south", "west", "east"};
    for (int i = 0; i < 6; ++i)
        if (names[i] == name) return i;
    return -1;
}
std::string_view faceToName(int face) {
    static constexpr std::string_view names[6] = {"down", "up", "north", "south", "west", "east"};
    return face >= 0 && face < 6 ? names[face] : std::string_view{};
}
int clockwise(int face) {
    switch (face) {
    case 2: return 5; // north -> east
    case 5: return 3; // east -> south
    case 3: return 4; // south -> west
    case 4: return 2; // west -> north
    default: return face;
    }
}
int counterClockwise(int face) {
    switch (face) {
    case 2: return 4;
    case 4: return 3;
    case 3: return 5;
    case 5: return 2;
    default: return face;
    }
}

// ---- blocks ----------------------------------------------------------------------------------------------

bool isServerRegion(BlockSource const& region) { return !const_cast<BlockSource&>(region).getLevel().isClientSide(); }
int  dimOf(BlockSource const& region) { return static_cast<int>(region.getDimensionId()); }

bool hasCollision(BlockSource& region, BlockPos const& pos, Block const& b) {
    if (b.isAir()) return false;
    auto box = b.getBlockType().getCollisionShape(b, region, pos, nullptr);
    return box.max.x > box.min.x && box.max.y > box.min.y && box.max.z > box.min.z;
}

bool canFallThrough(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (b.isAir()) return true;
    auto const& n = b.getTypeName();
    if (n == "minecraft:fire" || n == "minecraft:soul_fire" || n == "minecraft:water" || n == "minecraft:flowing_water"
        || n == "minecraft:lava" || n == "minecraft:flowing_lava")
        return true;
    // replaceable plants, snow layers, ... (Java canBeReplaced)
    return game::replaceable(region, pos);
}

bool destroyBlock(BlockSource& region, BlockPos const& pos, bool drop) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    BlockChangeContext ctx{};
    return level->destroyBlock(region, pos, drop, ctx);
}

void dropItem(BlockSource& region, BlockPos const& pos, std::string_view id, int count) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto item = game::makeItem(id, count);
    if (game::isEmpty(item)) return;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    level->getSpawner().spawnItem(region, item, nullptr, at, 10);
}

void dustParticle(BlockSource& region, float x, float y, float z) {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->spawnParticleEffect("minecraft:redstone_ore_dust_particle", Vec3{x, y, z}, &region.getDimension());
}

void levelEvent(BlockSource& region, int event, BlockPos const& pos, int data) {
    auto level = ll::service::getLevel();
    if (!level) return;
    (void)region;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    level->broadcastLevelEvent(static_cast<SharedTypes::Legacy::LevelEvent>(event), at, data, nullptr);
}

// ---- redstone ------------------------------------------------------------------------------------------

std::optional<int> componentStrength(BlockSource& region, BlockPos const& pos) {
    auto const* m = componentsOf(region);
    if (!m) return std::nullopt;
    auto it = m->find({pos.x, pos.y, pos.z});
    if (it == m->end()) return std::nullopt;
    return std::clamp(it->second.strength, 0, 15);
}

int signalFrom(BlockSource& region, BlockPos const& receiver, int face) {
    BlockPos    n    = offset(receiver, face);
    auto const& b    = region.getBlock(n);
    auto const& name = b.getTypeName();
    int         toward = opposite(face); // direction from the neighbour to the receiver
    if (name == "lepton:redstone_randomizer") {
        auto state  = bs::getString(b, "lepton:powered").value_or("off");
        auto trait  = bs::getString(b, "minecraft:cardinal_direction").value_or("north");
        int  facing = opposite(faceFromName(trait)); // Java FACING
        if (state == "left") return clockwise(facing) == toward ? 15 : 0;
        if (state == "right") return counterClockwise(facing) == toward ? 15 : 0;
        return 0;
    }
    if (name == "lepton:ender_watcher") return std::clamp(bs::getInt(b, "lepton:power").value_or(0), 0, 15);
    if (name == "minecraft:redstone_wire") return std::clamp(bs::getInt(b, "redstone_signal").value_or(0), 0, 15);
    auto const* m = componentsOf(region);
    if (!m) return 0;
    auto it = m->find({n.x, n.y, n.z});
    if (it == m->end()) return 0;
    auto const& c = it->second;
    using T       = CircuitComponentType;
    auto type     = c.type;
    if (type == static_cast<uint64_t>(T::RepeaterCapacitor) || type == static_cast<uint64_t>(T::ComparatorCapacitor)) {
        // diodes only power the block they point at
        return c.direction == toward ? std::clamp(c.strength, 0, 15) : 0;
    }
    auto group = type & static_cast<uint64_t>(T::GroupMask);
    if (group == static_cast<uint64_t>(T::ConsumerComponent) || group == static_cast<uint64_t>(T::BaseRailTransporter))
        return 0;
    return std::clamp(c.strength, 0, 15);
}

bool hasNeighborSignal(BlockSource& region, BlockPos const& pos) {
    for (int f = 0; f < 6; ++f)
        if (signalFrom(region, pos, f) > 0) return true;
    return false;
}

// ---- virtual tables ----------------------------------------------------------------------------------------

bool patchVirtual(BlockType& type, void* memberThunk, void* fn) {
    int slot = slotOfThunk(memberThunk);
    if (slot < 0) {
        log().error("redstone: can't read a virtual slot from thunk {}", memberThunk);
        return false;
    }
    auto*& vptr = *reinterpret_cast<void***>(&type);
    auto   it   = std::find_if(patched().begin(), patched().end(), [&](Patched const& p) { return p.type == &type; });
    if (it != patched().end() && vptr != it->table.get() + 1) {
        // the block type got a new table (re-registered): start over
        oldTables().push_back(std::move(it->table));
        patched().erase(it);
        it = patched().end();
    }
    if (it == patched().end()) {
        size_t n = readablePointers(vptr - 1, 600);
        if (n < static_cast<size_t>(slot) + 2) return false;
        Patched p{&type, vptr, std::make_unique<void*[]>(n)};
        std::memcpy(p.table.get(), vptr - 1, n * sizeof(void*));
        patched().push_back(std::move(p));
        it = std::prev(patched().end());
        vptr = it->table.get() + 1;
    }
    it->table[static_cast<size_t>(slot) + 1] = fn;
    return true;
}

void unpatchAll() {
    for (auto& p : patched()) {
        auto*& vptr = *reinterpret_cast<void***>(p.type);
        if (vptr == p.table.get() + 1) vptr = p.original;
        oldTables().push_back(std::move(p.table));
    }
    patched().clear();
}

} // namespace lp::feature::redstone

LP_REGISTER_MODULE(lp::feature::redstone::RedstoneModule);
