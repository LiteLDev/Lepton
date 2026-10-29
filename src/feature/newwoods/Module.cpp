// U7 newwoods: the module (events, world lifecycle) and the game hooks for recipes, loot tables,
// fuel, composting and the Enchanted Fruit.

#include <atomic>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/json/Value.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/ComposterBlock.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/storage/loot/LootTable.h"
#include "mc/world/level/storage/loot/LootTables.h"

#include "feature/newwoods/NewWoods.h"
#include "feature/newwoods/Palette.h"
#include "feature/newwoods/Runtime.h"
#include "game/BlockStates.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::newwoods {

namespace bs = game::blockstates;

// ---- shared runtime ------------------------------------------------------------------------------

namespace {
std::atomic<std::thread::id> gServerThread{};

struct PaletteSlot {
    std::mutex                     mutex;
    std::shared_ptr<Palette const> current;
};
PaletteSlot& paletteSlot() {
    static PaletteSlot s;
    return s;
}
} // namespace

JavaRandom& runtimeRandom() {
    static JavaRandom r(static_cast<int64_t>(std::random_device{}()) << 20 ^ std::random_device{}());
    return r;
}

bool onServerThread() { return gServerThread.load() == std::this_thread::get_id(); }

namespace {
std::shared_ptr<Palette const> buildPalette();
}

std::shared_ptr<Palette const> palette() {
    auto&           s = paletteSlot();
    std::lock_guard lock(s.mutex);
    // chunks can generate before the world-ready callback (the client's spawn area): build on first use
    if (!s.current) {
        auto p = buildPalette();
        if (p && !p->logs.empty()) s.current = std::move(p);
    }
    return s.current;
}

namespace {
std::shared_ptr<Palette const> buildPalette() {
    auto p   = std::make_shared<Palette>();
    p->air   = bs::of("minecraft:air");
    p->dirt  = bs::of("minecraft:dirt");
    // pillar axis from the clicked face: x <- east, y <- up, z <- south
    static constexpr char const* kFaces[3] = {"east", "up", "south"};
    for (auto w : {"azalea", "blossom", "ancient"}) {
        std::string id = "lepton:" + std::string(w) + "_log";
        std::array<Block const*, 3> axes{};
        for (int a = 0; a < 3; ++a) axes[a] = bs::of(id, {{"minecraft:block_face", std::string(kFaces[a])}});
        if (axes[1]) p->logs[id] = axes;
    }
    for (auto const& t : treeTypes()) {
        if (auto const* b = bs::of(t.leaves, {{"lepton:persistent", false}})) {
            p->leaves[std::string(t.leaves)] = b;
            if (!t.ancient) p->blossomLeafTypes[&b->getBlockType()] = "lepton:" + std::string(t.name) + "_leaf";
        }
    }
    if (auto it = p->logs.find("lepton:azalea_log"); it != p->logs.end()) p->azaleaLog = it->second;
    return p;
}
} // namespace

void rebuildPalette() {
    auto p = buildPalette();
    if (!p->air || p->logs.empty()) log().warn("newwoods: the behaviour pack blocks are missing; trees can't grow");
    auto&           s = paletteSlot();
    std::lock_guard lock(s.mutex);
    s.current = std::move(p);
}

// ---- config snapshot for the generation threads -----------------------------------------------------

namespace {
struct GenConfigSlot {
    std::mutex                            mutex;
    std::shared_ptr<NewWoodsConfig const> current;
};
GenConfigSlot& genConfigSlot() {
    static GenConfigSlot s;
    return s;
}
} // namespace

std::shared_ptr<NewWoodsConfig const> genConfig() {
    auto&           s = genConfigSlot();
    std::lock_guard lock(s.mutex);
    if (!s.current) s.current = std::make_shared<NewWoodsConfig const>(config());
    return s.current;
}

void publishGenConfig() {
    auto            next = std::make_shared<NewWoodsConfig const>(config());
    auto&           s    = genConfigSlot();
    std::lock_guard lock(s.mutex);
    s.current = std::move(next);
}

void clearPalette() {
    auto&           s = paletteSlot();
    std::lock_guard lock(s.mutex);
    s.current.reset();
}

// ---- game hooks ------------------------------------------------------------------------------------

namespace {

std::string resultOf(Json::Value const& v) {
    auto fromResult = [](Json::Value const& r) -> std::string {
        if (r.isString()) return r.asString("");
        if (r.isObject() && r.isMember("item")) return r["item"].isString() ? r["item"].asString("") : std::string();
        if (r.isArray() && r.size() > 0 && r[0].isObject() && r[0].isMember("item")) return r[0]["item"].asString("");
        return {};
    };
    if (!v.isObject()) return {};
    if (v.isMember("result")) return fromResult(v["result"]);
    for (auto k : {"minecraft:recipe_shaped", "minecraft:recipe_shapeless"})
        if (v.isMember(k) && v[k].isObject() && v[k].isMember("result")) return fromResult(v[k]["result"]);
    return {};
}

LL_TYPE_INSTANCE_HOOK(
    RecipeGateHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::_loadRecipe,
    bool,
    std::string const&      recipeId,
    RecipeType              type,
    Json::Value const&      objData,
    MinEngineVersion const& minEngineVersion,
    SemVersion const&       formatVersion,
    bool                    isBaseGamePack,
    Recipes::Buffers&       buffers
) {
    try {
        if (recipeId.starts_with("lepton:") && recipeDisabled(recipeId, resultOf(objData))) return false;
    } catch (...) {}
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

thread_local std::string gLootDir;

LL_TYPE_INSTANCE_HOOK(
    LootLookupHook,
    ll::memory::HookPriority::Normal,
    LootTables,
    &LootTables::lookupByName,
    LootTable*,
    std::string const&   dir,
    ResourcePackManager& resourceLoader
) {
    auto saved = gLootDir;
    gLootDir   = dir;
    auto* t    = origin(dir, resourceLoader);
    gLootDir   = saved;
    return t;
}

LL_TYPE_INSTANCE_HOOK(
    LootDeserializeHook,
    ll::memory::HookPriority::Normal,
    LootTable,
    &LootTable::deserialize,
    void,
    Json::Value const& table,
    bool               upcoming,
    SemVersion const&  jsonVersion
) {
    std::string dir = mDir->empty() ? gLootDir : *mDir;
    std::optional<LootAddition> add;
    try {
        add = lootAddition(dir);
    } catch (...) {}
    if (!add || !table.isObject() || !table.isMember("pools") || !table["pools"].isArray() || table["pools"].size() == 0) {
        origin(table, upcoming, jsonVersion);
        return;
    }
    Json::Value copy(table);
    Json::Value entry(Json::ValueType::ObjectValue);
    entry["type"]    = Json::Value("item");
    entry["name"]    = Json::Value(add->item);
    entry["weight"]  = Json::Value(add->weight);
    entry["quality"] = Json::Value(add->quality);
    copy["pools"][0u]["entries"].append(std::move(entry));
    log().info("newwoods: added {} (weight {}) to {}", add->item, add->weight, dir);
    origin(copy, upcoming, jsonVersion);
}

LL_STATIC_HOOK(
    BurnDurationHook,
    ll::memory::HookPriority::Normal,
    &FurnaceBlockActor::getBurnDuration,
    float,
    ItemStackBase const& item,
    float                burnInterval
) {
    if (!item.isNull()) {
        int ticks = fuelTicks(item.getTypeName());
        if (ticks > 0) {
            // burn like the vanilla wooden item with the same Java burn time
            static ItemStack const planks = game::makeItem("minecraft:oak_planks");
            static ItemStack const slab   = game::makeItem("minecraft:oak_slab");
            static ItemStack const door   = game::makeItem("minecraft:wooden_door");
            static ItemStack const button = game::makeItem("minecraft:wooden_button");
            ItemStack const& proxy = ticks >= 300 ? planks : ticks >= 200 ? door : ticks >= 150 ? slab : button;
            if (!proxy.isNull()) return origin(proxy, burnInterval);
        }
    }
    return origin(item, burnInterval);
}

LL_STATIC_HOOK(
    FillChanceHook,
    ll::memory::HookPriority::Normal,
    &ComposterBlock::getFillChance,
    schar,
    ItemStackBase const& item
) {
    if (!item.isNull()) {
        int c = compostChance(item.getTypeName());
        if (c >= 0) return static_cast<schar>(c);
    }
    return origin(item);
}

void orbSound(Player& player) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& pkt   = static_cast<PlaySoundPacket&>(*packet);
    auto  pos   = player.getPosition();
    pkt.mName   = "random.orb";
    pkt.mPos    = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    pkt.mVolume = 1.0f;
    pkt.mPitch  = 1.0f;
    player.getDimension().sendPacketForPosition(player.getFeetBlockPos(), pkt, nullptr);
}

LL_TYPE_INSTANCE_HOOK(
    EatHook,
    ll::memory::HookPriority::Normal,
    Player,
    static_cast<void (Player::*)(ItemStack const&)>(&Player::eat),
    void,
    ItemStack const& instance
) {
    bool fruit = !isClientSide() && !instance.isNull() && instance.getTypeName() == "lepton:ancient_fruit";
    origin(instance);
    if (!fruit) return;
    if (int xp = fruitExperience(); xp > 0) {
        addExperience(xp);
        orbSound(*this);
        log().info("newwoods: {} ate an Enchanted Fruit (+{} xp)", getRealName(), xp);
    }
}

} // namespace

void installLoadHooks() {
    RecipeGateHook::hook();
    LootLookupHook::hook();
    LootDeserializeHook::hook();
}

void installGameHooks() {
    BurnDurationHook::hook();
    FillChanceHook::hook();
    EatHook::hook();
}
void uninstallGameHooks() {
    BurnDurationHook::unhook();
    FillChanceHook::unhook();
    EatHook::unhook();
}

// ---- module --------------------------------------------------------------------------------------

namespace {

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    auto const& prev = ev.previousBlock();
    auto const& now  = ev.newBlock();
    // a log or leaf went away: the leaves around may have lost their support
    if ((isLogLike(prev) && !isLogLike(now)) || (isAnyLeaves(prev) && !isAnyLeaves(now))) queueLeafCheck(region, ev.pos());
    if (isOurSapling(now) && !isOurSapling(prev) && onServerThread()) scheduleTick(region, ev.pos(), now);
}

// Blocks of a disabled module can't be placed (docs §3.10).
void onPlacing(ll::event::PlayerPlacingBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& item = player.getSelectedItem();
    if (item.isNull()) return;
    auto m = moduleOfId(item.getTypeName());
    if (m != Module::None && !moduleEnabled(m)) ev.cancel();
}

void onTick(ll::event::ServerLevelTickEvent& ev) {
    gServerThread.store(std::this_thread::get_id());
    try {
        runDeferredTicks();
        runLeafChecks();
        leafParticlesTick(ev.level());
    } catch (std::exception const& e) {
        log().error("newwoods: tick failed: {}", e.what());
    }
}

class NewWoodsModule {
public:
    void load() { installLoadHooks(); }
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(onPlacing));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        installLeafHooks();
        installSaplingHooks();
        installWorldGenHooks();
        installGameHooks();
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] {
                rebuildPalette();
                publishGenConfig();
            });
            game::db::onWorldClosed([] { clearPalette(); });
            config::onReload([] { publishGenConfig(); });
        }
        publishGenConfig();
        if (ll::service::getLevel()) rebuildPalette();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        uninstallLeafHooks();
        uninstallSaplingHooks();
        uninstallWorldGenHooks();
        uninstallGameHooks();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace lp::feature::newwoods

LP_REGISTER_MODULE(lp::feature::newwoods::NewWoodsModule);
