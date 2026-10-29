// U10 redstone: the Redstone Randomizer and the Ender Watcher (both tick every game tick through
// their minecraft:tick component; the Block::queuedTick hook in Common.cpp calls in here).

#include "feature/redstone/Redstone.h"

#include <array>
#include <cmath>
#include <random>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockSupportType.h"
#include "mc/world/level/block/components/BlockRedstoneProducerComponent.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/phys/HitResultType.h"

#include "core/GlobalPos.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::redstone {

namespace bs = game::blockstates;

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float randomFloat() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

int                  gForcedSide = -1;
std::array<float, 4> gLastHit{};

std::set<core::GlobalPos>& pendingRandomizers() {
    static std::set<core::GlobalPos> s;
    return s;
}

/// Java FACING of a randomizer (the input side): the opposite of the placement look direction.
int randomizerFacing(Block const& b) { return opposite(faceFromName(bs::getString(b, "minecraft:cardinal_direction").value_or("north"))); }

/// Java RedstoneRandomizerBlock#calculateInputStrength.
int inputStrength(BlockSource& region, BlockPos const& pos, Block const& b) { return signalFrom(region, pos, randomizerFacing(b)); }

/// Java canSupportRigidBlock(below): the block below has a sturdy top face.
bool supported(BlockSource& region, BlockPos const& pos) {
    auto const& below = region.getBlock(offset(pos, 0));
    return !below.isAir() && below.canProvideSupport(1, BlockSupportType::Edge);
}

void applyRandomizer(core::GlobalPos const& gp) {
    pendingRandomizers().erase(gp);
    auto* region = game::regionOf(gp.dim);
    if (!region) return;
    BlockPos pos = game::blockPos(gp);
    if (!region->hasChunksAt(pos, 0, false)) return;
    auto const& b = region->getBlock(pos);
    if (b.getTypeName() != "lepton:redstone_randomizer") return;
    auto state = bs::getString(b, "lepton:powered").value_or("off");
    bool isOn  = state != "off";
    bool want  = inputStrength(*region, pos, b) > 0;
    if (isOn == want) return;
    std::string next = "off";
    if (want) {
        bool left = gForcedSide >= 0 ? gForcedSide == 1 : std::uniform_int_distribution<int>(0, 1)(rng()) == 1;
        next      = left ? "left" : "right";
    }
    if (auto const* nb = bs::with(b, {{"lepton:powered", next}})) {
        // a data-driven producer keeps the faces of the permutation it was created for: drop the
        // component when turning off and build a fresh one for the new side when turning on
        removeCircuitComponent(*region, pos);
        game::setBlock(*region, pos, *nb);
        if (want) BlockRedstoneProducerComponent::setupRedstoneComponent(*nb, *region, pos);
        log().info("redstone: randomizer at {} {} {} -> {}", pos.x, pos.y, pos.z, next);
    }
}

} // namespace

void randomizerTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    if (!cfg().enableRedstoneRandomizer) return;
    if (!supported(region, pos)) {
        destroyBlock(region, pos, true);
        return;
    }
    auto state = bs::getString(block, "lepton:powered").value_or("off");
    bool isOn  = state != "off";
    bool want  = inputStrength(region, pos, block) > 0;
    auto gp    = game::globalPos(region, pos);
    if (isOn != want && pendingRandomizers().insert(gp).second) later(2, [gp] { applyRandomizer(gp); });
    // Java animateTick: redstone dust above the block while on (shown about every few ticks)
    if (isOn && randomFloat() < 0.2f) {
        dustParticle(
            region,
            static_cast<float>(pos.x) + 0.5f + (randomFloat() - 0.5f) * 0.2f,
            static_cast<float>(pos.y) + 0.4f + (randomFloat() - 0.5f) * 0.2f,
            static_cast<float>(pos.z) + 0.5f + (randomFloat() - 0.5f) * 0.2f
        );
    }
}

void watcherTick(BlockSource& region, BlockPos const& pos, Block const& block) {
    if (!cfg().enableEnderWatcher) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    bool  wasLooking = bs::getBool(block, "lepton:watched").value_or(false);
    int   currWatch  = bs::getInt(block, "lepton:power").value_or(0);
    int   dim        = dimOf(region);
    Vec3  center{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    int   newWatch = 0;
    bool  looking  = false;
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim) return true;
        Vec3 eye = p.getEyePos();
        if (std::abs(eye.x - center.x) > 81 || std::abs(eye.y - center.y) > 82 || std::abs(eye.z - center.z) > 81) return true;
        auto const& helm = p.getArmor(SharedTypes::Legacy::ArmorSlot::Head);
        if (!game::isEmpty(helm) && game::itemId(helm) == "minecraft:carved_pumpkin") return true; // ender mask
        // the player's look rotation (x pitch, y yaw, degrees)
        Vec2 const& rot   = p.getRotation();
        float       pitch = rot.x * 0.017453292f, yaw = rot.y * 0.017453292f;
        Vec3        view{-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        Vec3 end{eye.x + view.x * 64.0f, eye.y + view.y * 64.0f, eye.z + view.z * 64.0f};
        HitResult hit = region.clip(eye, end, false, ShapeType::Outline, 64, false, false, nullptr,
                                    [](BlockSource const&, Block const&, bool) { return true; }, false);
        if (hit.mType != HitResultType::Tile || !(hit.mBlock == pos)) return true;
        looking   = true;
        int  face = hit.mFacing;
        gLastHit  = {hit.mPos.x - pos.x, hit.mPos.y - pos.y, hit.mPos.z - pos.z, static_cast<float>(face)};
        int  axis = face <= 1 ? 1 : face <= 3 ? 2 : 0;
        double x  = axis == 0 ? 0.0 : std::abs(hit.mPos.x - pos.x - 0.5);
        double y  = axis == 1 ? 0.0 : std::abs(hit.mPos.y - pos.y - 0.5);
        double z  = axis == 2 ? 0.0 : std::abs(hit.mPos.z - pos.z - 0.5);
        // 0.7071067811865476: the hypotenuse of an isosceles triangle with cathetus 0.5
        double fract = 1.0 - std::sqrt(x * x + y * y + z * z) / 0.7071067811865476;
        int    watch = std::clamp(static_cast<int>(std::ceil(fract * 15.0)), 0, 15);
        newWatch     = std::max(newWatch, watch);
        return true;
    });
    if (looking != wasLooking || currWatch != newWatch) {
        if (auto const* nb = bs::with(block, {{"lepton:watched", looking}, {"lepton:power", newWatch}})) {
            game::setBlock(region, pos, *nb);
            if (looking != wasLooking)
                log().info("redstone: ender watcher at {} {} {} {} (power {})", pos.x, pos.y, pos.z, looking ? "watched" : "idle", newWatch);
        }
    }
    if (looking) {
        dustParticle(
            region,
            static_cast<float>(pos.x) - 0.1f + randomFloat() * 1.2f,
            static_cast<float>(pos.y) - 0.1f + randomFloat() * 1.2f,
            static_cast<float>(pos.z) - 0.1f + randomFloat() * 1.2f
        );
    }
}

void testing::forceRandomizerSide(int side) { gForcedSide = side; }
std::array<float, 4> testing::lastWatcherHit() { return gLastHit; }

} // namespace lp::feature::redstone
