#include "feature/farming/Farming.h"

#include <algorithm>
#include <random>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"

#include "game/Inventory.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("farming", lp::feature::farming::FarmingConfig);

namespace lp::feature::farming {

namespace {

struct Task {
    long long             due;
    std::function<void()> fn;
};

struct State {
    long long         tick = 0;
    std::vector<Task> tasks;
};

State& st() {
    static State s;
    return s;
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
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
                log().error("farming: task failed: {}", e.what());
            }
        }
    }
    try {
        tickAnimals();
    } catch (std::exception const& e) {
        log().error("farming: tick failed: {}", e.what());
    }
}

} // namespace

FarmingConfig const& cfg() {
    auto& c              = config::mut<FarmingConfig>();
    c.regularHoeRadius   = std::clamp(c.regularHoeRadius, 1, 5);
    c.highTierHoeRadius  = std::clamp(c.highTierHoeRadius, 1, 5);
    c.minPigLitterSize   = std::max(c.minPigLitterSize, 1);
    c.maxPigLitterSize   = std::max(c.maxPigLitterSize, 1);
    c.minGoldenCarrotBoost = std::max(c.minGoldenCarrotBoost, 0);
    c.maxGoldenCarrotBoost = std::max(c.maxGoldenCarrotBoost, 0);
    c.chickenOffset      = std::clamp(c.chickenOffset, -1.0, 1.0);
    return c;
}

void later(int ticks, std::function<void()> fn) { st().tasks.push_back({st().tick + std::max(ticks, 1), std::move(fn)}); }

long long now() { return st().tick; }

double random01() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }

int randomBetween(int a, int b) {
    int lo = std::min(a, b), hi = std::max(a, b);
    return std::uniform_int_distribution<int>(lo, hi)(rng());
}

Player* playerById(long long uniqueId) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID id{};
    id.rawID = uniqueId;
    auto* a  = level->fetchEntity(id, false);
    return a && a->isPlayer() ? static_cast<Player*>(a) : nullptr;
}

bool isCreative(Player const& player) { return player.getPlayerGameType() == GameType::Creative; }

ItemStack& mainHand(Player& player) { return const_cast<ItemStack&>(player.getSelectedItem()); }

void consumeOne(Player& player, ItemStack const& stack) {
    if (isCreative(player)) return;
    auto& inv  = game::inv::playerInventory(player);
    int   slot = player.getSelectedItemSlot();
    auto  cur  = inv.getItem(slot);
    if (cur.isNull() || cur.getTypeName() != stack.getTypeName()) return;
    cur.remove(1);
    inv.setItem(slot, cur);
    game::inv::sync(player);
}

void playSoundAt(BlockSource& region, float x, float y, float z, std::string const& name, float volume, float pitch) {
    PlaySoundPacket pkt;
    pkt.mName    = name;
    pkt.mPos     = BlockPos{static_cast<int>(x * 8), static_cast<int>(y * 8), static_cast<int>(z * 8)};
    pkt.mVolume  = volume;
    pkt.mPitch   = pitch;
    pkt.mLoopCount = 0;
    pkt.sendTo(BlockPos{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)}, region.getDimensionId());
}

void popItem(BlockSource& region, BlockPos const& pos, ItemStack const& stack) {
    auto level = ll::service::getLevel();
    if (!level || stack.isNull() || stack.mCount <= 0) return;
    Vec3 at{
        static_cast<float>(pos.x) + 0.5f + static_cast<float>(random01() * 0.5 - 0.25),
        static_cast<float>(pos.y) + 0.5f + static_cast<float>(random01() * 0.5 - 0.25),
        static_cast<float>(pos.z) + 0.5f + static_cast<float>(random01() * 0.5 - 0.25),
    };
    level->getSpawner().spawnItem(region, stack, nullptr, at, 10);
}

void destroyEffect(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    (void)region;
    level->broadcastLevelEvent(SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock, at, static_cast<int>(block.mNetworkId), nullptr);
}

bool hasItemTag(ItemStackBase const& stack, std::string_view tag) {
    if (stack.isNull()) return false;
    auto const* item = stack.mItem.get();
    return item && item->hasTag(HashedString(std::string(tag)));
}

bool isHoe(ItemStackBase const& stack) { return !stack.isNull() && stack.mCount > 0 && hasItemTag(stack, "minecraft:is_hoe"); }

// ---- module ----------------------------------------------------------------------------------

class FarmingModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldClosed([] { st() = State{}; });
            config::onReload([] { rebuildHarvestTables(); });
        }
        rebuildHarvestTables();
        installHarvest();
        installPlants();
        installAnimals();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        uninstallAnimals();
        uninstallPlants();
        uninstallHarvest();
        st() = State{};
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::feature::farming

LP_REGISTER_MODULE(lp::feature::farming::FarmingModule);
