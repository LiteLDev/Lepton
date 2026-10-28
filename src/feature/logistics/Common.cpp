#include "feature/logistics/Logistics.h"

#include <charconv>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/dimension/Dimension.h"

#include "game/Blocks.h"
#include "game/Shapes.h"
#include "mod/Config.h"

namespace lp::feature::logistics {

LP_REGISTER_CONFIG("logistics", LogisticsConfig);

LogisticsConfig const& cfg() { return config::get<LogisticsConfig>(); }

core::GlobalPos offset(core::GlobalPos const& p, int face) {
    auto const& s = kFaceStep[static_cast<size_t>(face)];
    return {p.dim, p.x + s[0], p.y + s[1], p.z + s[2]};
}

BlockPos offset(BlockPos const& p, int face) {
    auto const& s = kFaceStep[static_cast<size_t>(face)];
    return {p.x + s[0], p.y + s[1], p.z + s[2]};
}

core::GlobalPos parseKey(std::string_view key) {
    int  v[4] = {0, 0, 0, 0};
    auto p    = key.data();
    auto end  = key.data() + key.size();
    for (int i = 0; i < 4 && p < end; ++i) {
        auto r = std::from_chars(p, end, v[i]);
        p      = r.ptr + 1;
    }
    return {v[0], v[1], v[2], v[3]};
}

std::string idAt(core::GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return {};
    auto bp = game::blockPos(pos);
    if (!region->hasChunksAt(bp, 0, false)) return {};
    return game::blockIdAt(*region, bp);
}

bool powered(BlockSource& region, BlockPos const& pos) { return game::shapes::isPowered(region, pos); }

void spawnItemAt(BlockSource& region, ItemStack const& stack, Vec3 const& pos, Vec3 const& motion, bool pickupDelay) {
    auto level = ll::service::getLevel();
    if (!level || stack.isNull() || stack.mCount == 0) return;
    ItemActor* actor = level->getSpawner().spawnItem(region, stack, nullptr, pos, 0);
    if (!actor) return;
    actor->teleportTo(pos, false, 0, 1, false);
    actor->mBuiltInComponents->mStateVectorComponent->mPosDelta = motion;
    actor->mPickupDelay = pickupDelay ? 10 : 0;
}

void playSoundAt(BlockSource& region, Vec3 const& pos, std::string const& sound, float volume, float pitch) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::PlaySound);
    if (!pkt) return;
    auto& p   = static_cast<PlaySoundPacket&>(*pkt);
    p.mName   = sound;
    p.mPos    = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    p.mVolume = volume;
    p.mPitch  = pitch;
    region.getDimension().sendPacketForPosition(game::blockPosOf(pos), p, nullptr);
}

} // namespace lp::feature::logistics
