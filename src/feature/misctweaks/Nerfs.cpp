// GameNerfs (experimental): mending, drops, selective mob griefing, ice roads, falling block dupe,
// mushroom block removal, tripwire hook dupe, village spawn nerf. (Elytra lock: Elytra.cpp.)

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/IRandom.h"
#include "mc/deps/core/math/Random.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/BreakDoorGoal.h"
#include "mc/world/actor/ai/goal/EatBlockGoal.h"
#include "mc/world/actor/ai/goal/PlaceBlockGoal.h"
#include "mc/world/actor/ai/goal/RaidGardenGoal.h"
#include "mc/world/actor/ai/goal/TakeBlockGoal.h"
#include "mc/world/actor/item/Boat.h"
#include "mc/world/actor/item/FallingBlockActor.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/inventory/simulation/AllowedAnywhereConsume.h"
#include "mc/world/inventory/simulation/ContainerValidationCommitObject.h"
#include "mc/world/inventory/simulation/ContainerValidationCraftInputs.h"
#include "mc/world/inventory/simulation/ExpectedAnywhereConsume.h"
#include "mc/world/inventory/simulation/ExpectedSlotConsume.h"
#include "mc/world/inventory/simulation/ContainerValidationCraftResult.h"
#include "mc/world/inventory/simulation/validation/AnvilContainerScreenValidator.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/BedrockSpawner.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/Explosion.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/TripWireHookBlock.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/feature/HugeMushroomFeature.h"
#include "mc/world/level/levelgen/structure/VillageFeature.h"

#include "feature/misctweaks/Internal.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"

// The game inlines this destructor (no exported symbol); returning the result by value from the
// anvil hook needs one, so this unit provides it (it only destroys the members).
ContainerValidationCraftResult::~ContainerValidationCraftResult() = default;

namespace lp::feature::misctweaks {

// ---- helpers -------------------------------------------------------------------------------------

int mendingLevel(ItemStackBase const& item) {
    if (game::isEmpty(item)) return 0;
    return EnchantUtils::getEnchantLevel(Enchant::Type::Mending, item);
}

bool nerfAnvilResult(ItemStackBase const& left, ItemStackBase const& right, ItemStackBase& result) {
    auto const& c = cfg();
    if (!c.enableGameNerfs || !c.nerfMending || game::isEmpty(result)) return false;
    if (game::isEmpty(left) || game::isEmpty(right)) return false;
    auto has = [&](ItemStackBase const& s) {
        int l = mendingLevel(s);
        return l > 0 && (!c.noNerfForMendingTwo || l < 2);
    };
    if (!has(left) && !has(right)) return false;
    auto leftId = game::itemId(left), rightId = game::itemId(right);
    if (leftId != rightId && rightId != "minecraft:enchanted_book") return false;

    ItemEnchants enchants = result.constructItemEnchantsFromUserData();
    auto         sets     = enchants.getEnchantInstances();
    for (auto& set : sets)
        std::erase_if(set, [](EnchantmentInstance const& e) { return e.mEnchantType == Enchant::Type::Mending; });
    enchants.setEnchantInstances(sets);
    result.saveEnchantsToUserData(enchants);
    result.setRepairCost(0);
    if (result.isDamageableItem()) result.setDamageValue(0);
    return true;
}

bool isNonGriefing(std::string_view type) {
    auto const& c = cfg();
    if (!c.enableGameNerfs || !c.enableSelectiveMobGriefing) return false;
    std::string t = normalizeId(type);
    return std::ranges::any_of(c.nonGriefingEntities, [&](std::string const& e) { return normalizeId(e) == t; });
}

bool villageSuppressed(int x, int z, int spawnX, int spawnZ, int maxDist) {
    if (maxDist <= 0) return false;
    double dx = static_cast<double>(x) - spawnX, dz = static_cast<double>(z) - spawnZ;
    double p  = 1.0 - (dx * dx + dz * dz) / (static_cast<double>(maxDist) * maxDist);
    if (p <= 0) return false;
    // Deterministic per position (Java seeds its random with the position too).
    std::mt19937_64                        rng(static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32 | static_cast<uint32_t>(z));
    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (u(rng) >= p) return false;
    for (int i = 0; i < 3; ++i)
        if (u(rng) < 0.02) return false; // re-picked a zombie town centre: the village stays
    return true;
}

namespace {

bool nerf(bool MiscTweaksConfig::* flag) { return cfg().enableGameNerfs && cfg().*flag; }

// ---- mending ---------------------------------------------------------------------------------------

LL_STATIC_HOOK(
    MendingRepairHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::getRandomDamagedItemWithMending,
    ItemStack const&,
    IRandom&   random,
    Mob const& equipped
) {
    if (!nerf(&MiscTweaksConfig::nerfMending)) return origin(random, equipped);
    if (!cfg().noNerfForMendingTwo) return ItemStack::EMPTY_ITEM();
    std::vector<ItemStack const*> candidates;
    auto consider = [&](ItemStack const& s) {
        if (!game::isEmpty(s) && s.isDamageableItem() && s.getDamageValue() > 0 && mendingLevel(s) > 1)
            candidates.push_back(&s);
    };
    consider(equipped.getCarriedItem());
    consider(equipped.getOffhandSlot());
    for (int i = 0; i < 4; ++i) consider(equipped.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(i)));
    if (candidates.empty()) return ItemStack::EMPTY_ITEM();
    return *candidates[static_cast<size_t>(random.nextInt(static_cast<int>(candidates.size())))];
}

LL_TYPE_INSTANCE_HOOK(
    AnvilResultHook,
    ll::memory::HookPriority::Normal,
    AnvilContainerScreenValidator,
    &AnvilContainerScreenValidator::$getCraftResult,
    ContainerValidationCraftResult,
    ContainerScreenContext const&                     screenContext,
    ContainerScreenValidation&                        screenValidation,
    std::unique_ptr<ContainerValidationCraftInputs>   craftInputs,
    uchar const                                       numCrafts
) {
    auto result = origin(screenContext, screenValidation, std::move(craftInputs), numCrafts);
    if (!nerf(&MiscTweaksConfig::nerfMending)) return result;
    Player* player = screenContext.mPlayer;
    auto&   results = result.mResults.get();
    if (!player || results.empty()) return result;
    auto const& ui    = player->mPlayerUIContainer.get();
    auto const& left  = ui.getItem(1); // PlayerUISlot::AnvilInput
    auto const& right = ui.getItem(2); // PlayerUISlot::AnvilMaterial
    if (nerfAnvilResult(left, right, results.front()))
        log().info("misctweaks: anvil removed Mending for {}", player->getRealName());
    return result;
}

// ---- drops -----------------------------------------------------------------------------------------

bool dropBlocked(Actor const* spawner, ItemStack const& item) {
    if (!spawner || !cfg().enableGameNerfs) return false;
    if (spawner->getHealth() > 0 && !spawner->isDead()) return false;
    auto const& type = spawner->getTypeName();
    auto        id   = game::itemId(item);
    if (type == "minecraft:iron_golem" && cfg().disableIronFarms && id == "minecraft:iron_ingot") return true;
    if (type == "minecraft:sheep" && cfg().disableWoolDrops && (id == "minecraft:wool" || id.ends_with("_wool")))
        return true;
    return false;
}

LL_TYPE_INSTANCE_HOOK(
    SpawnItemHook,
    ll::memory::HookPriority::Normal,
    Spawner,
    &Spawner::$spawnItem,
    ItemActor*,
    BlockSource&     region,
    ItemStack const& inst,
    Actor*           spawner,
    Vec3 const&      pos,
    int              throwTime
) {
    if (dropBlocked(spawner, inst)) return nullptr;
    return origin(region, inst, spawner, pos, throwTime);
}

LL_TYPE_INSTANCE_HOOK(
    BedrockSpawnItemHook,
    ll::memory::HookPriority::Normal,
    BedrockSpawner,
    &BedrockSpawner::$spawnItem,
    ItemActor*,
    BlockSource&     region,
    ItemStack const& inst,
    Actor*           spawner,
    Vec3 const&      pos,
    int              throwTime
) {
    if (dropBlocked(spawner, inst)) return nullptr;
    return origin(region, inst, spawner, pos, throwTime);
}

// ---- selective mob griefing ------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(ExplodeHook, ll::memory::HookPriority::Normal, Explosion, &Explosion::explode, bool, IRandom& random) {
    if (cfg().enableGameNerfs && cfg().enableSelectiveMobGriefing && mSourceID->rawID != -1) {
        BlockSource& region = mRegion;
        Actor*       source = region.getLevel().fetchEntity(mSourceID, false);
        if (source && isNonGriefing(source->getTypeName())) {
            mBreaking = false;
            mFire     = false;
        }
    }
    return origin(random);
}

#define LP_GRIEF_GOAL_HOOK(NAME, GOAL, MOB)                                                                            \
    LL_TYPE_INSTANCE_HOOK(NAME, ll::memory::HookPriority::Normal, GOAL, &GOAL::$canUse, bool) {                        \
        if (isNonGriefing(MOB.getTypeName())) return false;                                                            \
        return origin();                                                                                               \
    }

LP_GRIEF_GOAL_HOOK(TakeBlockHook, TakeBlockGoal, mMob)
LP_GRIEF_GOAL_HOOK(PlaceBlockHook, PlaceBlockGoal, mMob)
LP_GRIEF_GOAL_HOOK(EatBlockHook, EatBlockGoal, mMob)
LP_GRIEF_GOAL_HOOK(BreakDoorHook, BreakDoorGoal, BreakDoorGoal::mMob)
LP_GRIEF_GOAL_HOOK(RaidGardenHook, RaidGardenGoal, mMob)

#undef LP_GRIEF_GOAL_HOOK

// ---- ice roads ---------------------------------------------------------------------------------------

constexpr float kBoatFriction = 0.45f;

LL_TYPE_INSTANCE_HOOK(BoatTickHook, ll::memory::HookPriority::Normal, Boat, &Boat::$normalTick, void) {
    origin();
    if (!nerf(&MiscTweaksConfig::disableIceRoads) || isInWater() || !isOnGround()) return;
    // A boat driven by a remote player moves on that client; the server would only fight it.
    if (!onClientSide(*this) && getControllingPlayer().rawID != -1 && getControllingPlayer().rawID != 0) {
        auto* driver = getLevel().fetchEntity(getControllingPlayer(), false);
        if (driver && driver->isType(ActorType::Player) && !static_cast<Player*>(driver)->isSimulated()) return;
    }
    Vec3 const&  feet  = getPosition();
    BlockPos     below = game::blockPosOf(Vec3{feet.x, feet.y - 0.2f, feet.z});
    Block const& b     = getDimensionBlockSourceConst().getBlock(below);
    float        f     = b.getBlockType().mFriction;
    if (f <= 0.f || std::abs(f - kBoatFriction) < 1e-4f) return;
    auto& delta = const_cast<Vec3&>(getPosDelta());
    delta.x *= kBoatFriction / f;
    delta.z *= kBoatFriction / f;
}

// ---- falling block dupe ------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(FallingTickHook, ll::memory::HookPriority::Normal, FallingBlockActor, &FallingBlockActor::$normalTick, void) {
    if (nerf(&MiscTweaksConfig::disableFallingBlockDupe) && mRemoved) return;
    origin();
}

// ---- huge mushrooms ----------------------------------------------------------------------------------

bool isCap(Block const& b) {
    auto const& n = b.getTypeName();
    if (n != "minecraft:red_mushroom_block" && n != "minecraft:brown_mushroom_block") return false;
    int bits = game::blockstates::getInt(b, "huge_mushroom_bits").value_or(0);
    return bits != 10 && bits != 15; // stems
}

bool replaceableByCap(Block const& b) {
    return b.isAir() || b.getTypeName().find("leaves") != std::string::npos;
}

LL_TYPE_INSTANCE_HOOK(
    MushroomPlaceHook,
    ll::memory::HookPriority::Normal,
    HugeMushroomFeature,
    &HugeMushroomFeature::placeFixed,
    bool,
    BlockSource&    region,
    BlockPos const& pos,
    int             mushroomType,
    int             height
) {
    if (!nerf(&MiscTweaksConfig::disableMushroomBlockRemoval)) return origin(region, pos, mushroomType, height);
    std::vector<std::pair<BlockPos, Block const*>> before;
    int top = pos.y + std::max(height, 4) + 3;
    for (int y = pos.y; y <= top; ++y)
        for (int x = pos.x - 3; x <= pos.x + 3; ++x)
            for (int z = pos.z - 3; z <= pos.z + 3; ++z) {
                BlockPos p{x, y, z};
                Block const& b = region.getBlock(p);
                if (!replaceableByCap(b)) before.emplace_back(p, &b);
            }
    bool ok       = origin(region, pos, mushroomType, height);
    int  restored = 0;
    for (auto const& [p, old] : before) {
        Block const& now = region.getBlock(p);
        if (&now != old && isCap(now)) {
            game::setBlock(region, p, *old);
            ++restored;
        }
    }
    if (restored) log().debug("misctweaks: huge mushroom kept {} blocks", restored);
    return ok;
}

// ---- tripwire hook dupe ------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    TripwireCalcHook,
    ll::memory::HookPriority::Normal,
    TripWireHookBlock,
    &TripWireHookBlock::calculateState,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    bool            isBeingDestroyed,
    int             wireSourceIdx
) {
    if (!nerf(&MiscTweaksConfig::disableTripwireHookDupe)) return origin(region, pos, isBeingDestroyed, wireSourceIdx);
    constexpr int                                  kMaxWire = 42;
    constexpr std::array<std::pair<int, int>, 4>   kDirs{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    std::vector<std::pair<BlockPos, Block const*>> notHooks;
    auto remember = [&](BlockPos const& p) {
        Block const& b = region.getBlock(p);
        if (b.getTypeName() != "minecraft:tripwire_hook") notHooks.emplace_back(p, &b);
    };
    remember(pos);
    for (auto [dx, dz] : kDirs)
        for (int i = 1; i <= kMaxWire; ++i) remember(BlockPos{pos.x + dx * i, pos.y, pos.z + dz * i});
    origin(region, pos, isBeingDestroyed, wireSourceIdx);
    for (auto const& [p, old] : notHooks) {
        if (region.getBlock(p).getTypeName() == "minecraft:tripwire_hook") {
            game::setBlock(region, p, *old);
            log().info("misctweaks: blocked a tripwire hook dupe at {} {} {}", p.x, p.y, p.z);
        }
    }
}

// ---- villages ----------------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    VillageChunkHook,
    ll::memory::HookPriority::Normal,
    VillageFeature,
    &VillageFeature::$isFeatureChunk,
    bool,
    BiomeSource const&                 biomeSource,
    Random&                            random,
    ChunkPos const&                    chunkPos,
    uint                               levelSeed,
    IPreliminarySurfaceProvider const& preliminarySurfaceLevel,
    Dimension const&                   dimension
) {
    bool is = origin(biomeSource, random, chunkPos, levelSeed, preliminarySurfaceLevel, dimension);
    auto const& c = cfg();
    if (!is || !c.enableGameNerfs || !c.villageSpawnNerf) return is;
    auto level = ll::service::getLevel();
    if (!level) return is;
    BlockPos const spawn = level->getSharedSpawnPos();
    return !villageSuppressed(chunkPos.x * 16 + 8, chunkPos.z * 16 + 8, spawn.x, spawn.z, c.villageSpawnNerfDistance);
}

} // namespace

void installNerfs() {
    MendingRepairHook::hook();
    AnvilResultHook::hook();
    SpawnItemHook::hook();
    BedrockSpawnItemHook::hook();
    ExplodeHook::hook();
    TakeBlockHook::hook();
    PlaceBlockHook::hook();
    EatBlockHook::hook();
    BreakDoorHook::hook();
    RaidGardenHook::hook();
    BoatTickHook::hook();
    FallingTickHook::hook();
    MushroomPlaceHook::hook();
    TripwireCalcHook::hook();
    VillageChunkHook::hook();
}

void uninstallNerfs() {
    VillageChunkHook::unhook();
    TripwireCalcHook::unhook();
    MushroomPlaceHook::unhook();
    FallingTickHook::unhook();
    BoatTickHook::unhook();
    RaidGardenHook::unhook();
    BreakDoorHook::unhook();
    EatBlockHook::unhook();
    PlaceBlockHook::unhook();
    TakeBlockHook::unhook();
    ExplodeHook::unhook();
    BedrockSpawnItemHook::unhook();
    SpawnItemHook::unhook();
    AnvilResultHook::unhook();
    MendingRepairHook::unhook();
}

} // namespace lp::feature::misctweaks
