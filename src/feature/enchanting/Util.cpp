#include "feature/enchanting/Util.h"

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"

namespace lp::feature::enchanting {

Player* playerByUuid(std::string const& uuid) {
    auto level = ll::service::getLevel();
    if (!level || uuid.empty()) return nullptr;
    return level->getPlayer(mce::UUID::fromString(uuid));
}

std::string uuidOf(Player const& player) { return player.getUuid().asString(); }

void giveOrDrop(Player& player, ItemStack stack) {
    if (game::isEmpty(stack)) return;
    game::inv::insert(game::inv::playerInventory(player), stack, game::inv::kPlayerAll);
    game::inv::sync(player);
    if (!game::isEmpty(stack)) {
        auto level = ll::service::getLevel();
        if (level) level->getSpawner().spawnItem(player.getDimensionBlockSource(), stack, nullptr, player.getPosition(), 0);
    }
}

void dropAt(BlockSource& region, core::GlobalPos const& pos, ItemStack const& stack) {
    if (game::isEmpty(stack)) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    level->getSpawner().spawnItem(region, stack, nullptr, at, 10);
}

void later(int ticks, std::function<void()> fn) {
    ll::coro::keepThis([ticks, fn = std::move(fn)]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(ticks);
        fn();
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

void playSound(core::GlobalPos const& pos, int levelSoundEvent, float pitch) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return;
    Vec3 at{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f};
    region->getLevel().playSound(static_cast<SharedTypes::Legacy::LevelSoundEvent>(levelSoundEvent), at, 1.0f, pitch);
}

double distanceSq(Player const& player, core::GlobalPos const& pos) {
    if (static_cast<int>(player.getDimensionId()) != pos.dim) return 1e18;
    auto const& p  = player.getPosition();
    double      dx = p.x - (pos.x + 0.5), dy = p.y - (pos.y + 0.5), dz = p.z - (pos.z + 0.5);
    return dx * dx + dy * dy + dz * dz;
}

} // namespace lp::feature::enchanting
