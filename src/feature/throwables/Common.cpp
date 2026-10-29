#include "feature/throwables/Throwables.h"

#include <cmath>
#include <random>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/SynchedActorData.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/entity/components/SynchedActorDataComponent.h"
#include "mc/deps/vanilla_components/ActorDataDirtyFlagsComponent.h"
#include "mc/deps/vanilla_components/ActorDataFlagComponent.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/HitResult.h"

#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Config.h"

namespace lp::feature::throwables {

LP_REGISTER_CONFIG("throwables", ThrowablesConfig);

ThrowablesConfig const& cfg() { return config::get<ThrowablesConfig>(); }

namespace {
std::mt19937_64& rng() {
    static std::mt19937_64 r{std::random_device{}()};
    return r;
}
} // namespace

double randomDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng()); }
int    randomInt(int bound) { return bound <= 1 ? 0 : std::uniform_int_distribution<int>(0, bound - 1)(rng()); }
double randomGaussian() { return std::normal_distribution<double>(0.0, 1.0)(rng()); }

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level || id == 0) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

bool isCreative(Player const& player) { return player.isCreative(); }

void playSound(std::string const& name, Vec3 const& pos, float volume, float pitch) {
    auto level = ll::service::getLevel();
    if (!level) return;
    // Every player within 16 blocks × volume hears it (the client attenuates).
    PlaySoundPacket pkt;
    pkt.mName   = name;
    pkt.mPos    = BlockPos{static_cast<int>(pos.x * 8), static_cast<int>(pos.y * 8), static_cast<int>(pos.z * 8)};
    pkt.mVolume = volume;
    pkt.mPitch  = pitch;
    float range = std::max(16.0f, 16.0f * volume);
    level->forEachPlayer([&](Player& p) {
        if (p.distanceToSqr(pos) <= range * range) pkt.sendTo(p);
        return true;
    });
}

void particle(std::string const& effect, Vec3 const& pos, int dim) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto d = level->getDimension(DimensionType{dim}).lock();
    if (!d) return;
    level->spawnParticleEffect(effect, pos, d.get());
}

void giveOrDrop(Player& player, ItemStack stack) {
    if (game::isEmpty(stack)) return;
    if (!player.add(stack) || !game::isEmpty(stack)) {
        if (!game::isEmpty(stack)) player.drop(stack, false);
    }
    player.refreshInventory();
}

void setSelected(Player& player, ItemStack const& stack) {
    player.setSelectedItem(stack);
    player.refreshInventory();
}

namespace {
template <class T>
bool setData(Actor& actor, int id, T value) {
    auto& wrap  = actor.mEntityData;
    auto* data  = wrap->mData.get();
    auto& items = data->mData->mItemsArray.get();
    if (id < 0 || static_cast<size_t>(id) >= items.size() || !items[id]) return false;
    auto ref = items[id]->template getData<T>();
    if (!ref) return false;
    if (*ref == value) return true;
    items[id]->setData(T(value));
    data->mData->mDirtyFlags->set(static_cast<size_t>(id));
    wrap->mDirtyFlags.get()->mDirtyFlags->set(static_cast<size_t>(id));
    return true;
}
} // namespace

bool setActorInt(Actor& actor, int id, int value) { return setData<int>(actor, id, value); }
bool setActorFloat(Actor& actor, int id, float value) { return setData<float>(actor, id, value); }

bool setActorFlag(Actor& actor, int flag, bool value) {
    auto& wrap  = actor.mEntityData;
    auto* flags = wrap->mFlagData.get();
    if (flag < 0 || flag >= 131) return false;
    if (flags->mValue.test(flag) == value) return true;
    flags->mValue.set(flag, value);
    // Flags travel in the Reserved0 (flags) and Reserved092 (flags 2) data items.
    int item = flag < 64 ? 0 : 92;
    auto* data  = wrap->mData.get();
    auto& items = data->mData->mItemsArray.get();
    if (static_cast<size_t>(item) < items.size() && items[item]) {
        auto ref = items[item]->template getData<int64>();
        if (ref) {
            int64 bits = *ref;
            int   bit  = flag % 64;
            bits       = value ? (bits | (int64{1} << bit)) : (bits & ~(int64{1} << bit));
            items[item]->setData(int64(bits));
        }
        data->mData->mDirtyFlags->set(static_cast<size_t>(item));
        wrap->mDirtyFlags.get()->mDirtyFlags->set(static_cast<size_t>(item));
    }
    return true;
}

bool isSlimeChunk(int chunkX, int chunkZ) {
    // Bedrock: a Mersenne Twister seeded with (x * 0x1f1f1f1f) ^ z, first output mod 10 == 0.
    auto         seed = static_cast<uint32_t>(static_cast<uint32_t>(chunkX) * 0x1f1f1f1fu) ^ static_cast<uint32_t>(chunkZ);
    std::mt19937 mt(seed);
    return mt() % 10 == 0;
}

RayHit rayFromEyes(Player& player, float reach, bool liquids) {
    RayHit out;
    Vec3   eye = player.getEyePos();
    Vec3   dir = player.getViewVector(1.0f);
    Vec3   end{eye.x + dir.x * reach, eye.y + dir.y * reach, eye.z + dir.z * reach};
    auto&  region = player.getDimensionBlockSource();
    auto   hit    = region.clip(
        eye,
        end,
        liquids,
        ShapeType::Outline,
        static_cast<int>(std::ceil(reach)) + 1,
        false,
        false,
        &player,
        [](BlockSource const&, Block const&, bool) { return true; },
        false
    );
    if (hit.mType == HitResultType::Tile) {
        out.hit  = true;
        out.x    = hit.mBlock.x;
        out.y    = hit.mBlock.y;
        out.z    = hit.mBlock.z;
        out.face = hit.mFacing;
        if (liquids && hit.mIsHitLiquid) {
            out.x    = hit.mLiquid.x;
            out.y    = hit.mLiquid.y;
            out.z    = hit.mLiquid.z;
            out.face = hit.mLiquidFacing;
        }
    } else {
        auto bp = game::blockPosOf(end);
        out.x   = bp.x;
        out.y   = bp.y;
        out.z   = bp.z;
    }
    return out;
}

bool isFenceBlock(std::string_view id, BlockSource& region, BlockPos const& pos) {
    if (id.size() > 6 && id.substr(id.size() - 6) == "_fence") return true;
    auto const& b = region.getBlock(pos);
    return b.hasTag(HashedString("lepton:fence"));
}

} // namespace lp::feature::throwables
