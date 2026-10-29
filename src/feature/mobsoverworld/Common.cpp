#include "feature/mobsoverworld/Common.h"

#include <array>
#include <cmath>
#include <numbers>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/LevelEventPacket.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/network/packet/SpawnParticleEffectPacketPayload.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/actor/state/PropertyContainer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemTag.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/GameType.h"

#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"

namespace lp::feature::mobsoverworld {

namespace {
std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

optional_ref<PropertyComponent> props(Actor& actor) {
    return actor.getEntityContext().tryGetComponent<PropertyComponent>();
}
} // namespace

int intProperty(Actor& actor, std::string_view name) {
    auto p = props(actor);
    if (!p) return 0;
    auto v = p->mPropertyContainer->getValueVariant(HashedString(name).getHash());
    if (!v) return 0;
    if (auto const* i = std::get_if<int>(&*v)) return *i;
    if (auto const* b = std::get_if<bool>(&*v)) return *b ? 1 : 0;
    if (auto const* f = std::get_if<float>(&*v)) return static_cast<int>(std::lround(*f));
    return 0;
}

bool boolProperty(Actor& actor, std::string_view name) { return intProperty(actor, name) != 0; }

bool setIntProperty(Actor& actor, std::string_view name, int value) {
    auto p = props(actor);
    if (!p) return false;
    return ActorPropertyUtils::tryQueueIntValue(*p, actor, HashedString(name).getHash(), value);
}

bool setBoolProperty(Actor& actor, std::string_view name, bool value) {
    auto p = props(actor);
    if (!p) return false;
    return ActorPropertyUtils::tryQueueBoolValue(*p, actor, HashedString(name).getHash(), value);
}

int64_t gameTick() {
    auto level = ll::service::getLevel();
    return level ? static_cast<int64_t>(level->getCurrentTick().tickID) : 0;
}

int difficulty() {
    auto level = ll::service::getLevel();
    return level ? static_cast<int>(level->getDifficulty()) : 2;
}

int randomInt(int bound) {
    if (bound <= 1) return 0;
    return std::uniform_int_distribution<int>(0, bound - 1)(rng());
}

float randomFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

void playSound(Actor const& near, std::string const& sound, Vec3 const& pos, float volume, float pitch) {
    auto packet = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!packet) return;
    auto& p    = static_cast<PlaySoundPacket&>(*packet);
    p.mName    = sound;
    p.mPos     = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    p.mVolume  = volume;
    p.mPitch   = pitch;
    p.sendTo(near);
}

void particles(Actor const& near, std::string const& effect, Vec3 const& pos, int count, float spreadXZ, float spreadY) {
    auto dim = static_cast<uchar>(static_cast<int>(near.getDimensionId()));
    for (int i = 0; i < count; ++i) {
        Vec3 at = pos;
        if (spreadXZ > 0 || spreadY > 0) {
            at.x += (randomFloat() * 2 - 1) * spreadXZ;
            at.y += (randomFloat() * 2 - 1) * spreadY;
            at.z += (randomFloat() * 2 - 1) * spreadXZ;
        }
        SpawnParticleEffectPacket packet{SpawnParticleEffectPacketPayload(at, effect, dim, std::nullopt)};
        packet.sendTo(near);
    }
}

void blockBreakParticles(BlockSource& region, BlockPos const& pos) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Block const& block = region.getBlock(pos);
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    level->broadcastLevelEvent(
        SharedTypes::Legacy::LevelEvent::ParticlesDestroyBlock,
        at,
        static_cast<int>(block.mNetworkId),
        nullptr
    );
}

Mob* asMob(Actor& actor) {
    if (!actor.hasCategory(ActorCategory::Mob)) return nullptr;
    return static_cast<Mob*>(&actor);
}

BlockPos below(Actor const& actor) {
    BlockPos p = actor.getFeetBlockPos();
    p.y -= 1;
    return p;
}

Player* playerById(int64_t uniqueId) {
    auto level = ll::service::getLevel();
    if (!level || uniqueId == 0) return nullptr;
    ActorUniqueID uid;
    uid.rawID = uniqueId;
    return level->getPlayer(uid);
}

bool isCreative(Player const& player) { return player.getPlayerGameType() == GameType::Creative; }

void setSelected(Player& player, ItemStack const& stack) {
    player.setSelectedItem(stack);
    game::inv::sync(player);
}

void consumeSelected(Player& player) {
    if (isCreative(player)) return;
    ItemStack held = player.getSelectedItem();
    if (held.isNull()) return;
    held.remove(1);
    if (held.mCount == 0) held = ItemStack::EMPTY_ITEM();
    setSelected(player, held);
}

void giveOrDrop(Player& player, ItemStack const& stack) {
    ItemStack copy = stack;
    if (!player.add(copy) || copy.mCount > 0) {
        if (!copy.isNull() && copy.mCount > 0) player.drop(copy, false);
    }
    game::inv::sync(player);
}

void dropItem(BlockSource& region, Vec3 const& pos, ItemStack const& stack) {
    auto level = ll::service::getLevel();
    if (!level || stack.isNull()) return;
    level->getSpawner().spawnItem(region, stack, nullptr, pos, 10);
}

bool navigateTo(Actor& actor, Vec3 const& pos, float speed) {
    Mob* mob = asMob(actor);
    if (!mob) return false;
    auto nav = actor.getEntityContext().tryGetComponent<NavigationComponent>();
    if (!nav || !nav->mNavigation) return false;
    return nav->mNavigation->moveTo(*nav, *mob, pos, speed);
}

void stopNavigation(Actor& actor) {
    Mob* mob = asMob(actor);
    if (!mob) return;
    auto nav = actor.getEntityContext().tryGetComponent<NavigationComponent>();
    if (!nav || !nav->mNavigation) return;
    nav->mNavigation->stop(*nav, *mob);
}

void lookAt(Actor& actor, Vec3 const& pos) {
    Vec3  eye = actor.getHeadPos();
    float dx  = pos.x - eye.x;
    float dy  = pos.y - eye.y;
    float dz  = pos.z - eye.z;
    float yaw = std::atan2(dz, dx) * 180.0f / std::numbers::pi_v<float> - 90.0f;
    float h   = std::sqrt(dx * dx + dz * dz);
    float pitch = -std::atan2(dy, h) * 180.0f / std::numbers::pi_v<float>;
    const_cast<Vec2&>(actor.getRotation()) = Vec2{pitch, yaw};
}

void pushUp(Actor& actor, float dy) {
    auto& delta = const_cast<Vec3&>(actor.getPosDelta());
    delta.y += dy;
}

bool itemHasTag(ItemStack const& stack, std::string_view tag) {
    if (stack.isNull()) return false;
    return stack.hasTag(ItemTag{HashedString(tag)});
}

bool isWaterBucket(ItemStack const& stack) { return game::itemId(stack) == "minecraft:water_bucket"; }

int dyeColorOf(ItemStack const& stack) {
    static constexpr std::array<std::string_view, 16> kDyes{
        "minecraft:white_dye",  "minecraft:orange_dye", "minecraft:magenta_dye", "minecraft:light_blue_dye",
        "minecraft:yellow_dye", "minecraft:lime_dye",   "minecraft:pink_dye",    "minecraft:gray_dye",
        "minecraft:light_gray_dye", "minecraft:cyan_dye", "minecraft:purple_dye", "minecraft:blue_dye",
        "minecraft:brown_dye",  "minecraft:green_dye",  "minecraft:red_dye",     "minecraft:black_dye"};
    std::string id = game::itemId(stack);
    for (size_t i = 0; i < kDyes.size(); ++i) {
        if (id == kDyes[i]) return static_cast<int>(i);
    }
    return -1;
}

} // namespace lp::feature::mobsoverworld
