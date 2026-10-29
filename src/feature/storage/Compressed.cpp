// CompressedBlocks (fuel, beacon base, compost), GlassShard (shard drops) and UtilityRecipes tweaks
// (dragon's breath, torches as fuel, compostable toxins, raw ore block smelting).

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/ComposterBlock.h"
#include "mc/world/level/block/ResourceDropsCause.h"
#include "mc/world/level/block/ResourceDropsContext.h"
#include "mc/world/level/block/actor/BeaconBlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaTickBlockActorComponent.h"
#include "mc/deps/core/math/IRandom.h"

#include "feature/storage/Storage.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::storage {

namespace {

constexpr int kSmeltSlowdown = 9; // raw ore blocks take 9x as long (Quark cookingtime 1800 / 900)

bool utility(bool StorageConfig::*option) { return cfg().enableUtilityRecipes && cfg().*option; }

/// Item name of an Item (empty for null).
std::string const& nameOf(Item const& item) { return item.mFullName->getString(); }

// ---- fuel -------------------------------------------------------------------------------------


/// The game's burn duration: Java ticks scaled by the furnace's burn interval (200 = a furnace,
/// 100 = blast furnace / smoker: coal gives 1600 at 200).
float burnDuration(int ticks, float burnInterval) { return ticks <= 0 ? 0.f : static_cast<float>(ticks) * burnInterval / 200.f; }

LL_TYPE_STATIC_HOOK(
    StorageItemBurnHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::getItemBurnDuration,
    float,
    Item const& item,
    float       burnInterval
) {
    if (auto ticks = fuelTicks(nameOf(item))) return burnDuration(*ticks, burnInterval);
    return origin(item, burnInterval);
}

LL_TYPE_STATIC_HOOK(
    StorageBurnHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::getBurnDuration,
    float,
    ItemStackBase const& stack,
    float                burnInterval
) {
    auto const* item = stack.isNull() ? nullptr : stack.mItem.get();
    if (item) {
        if (auto ticks = fuelTicks(nameOf(*item))) return burnDuration(*ticks, burnInterval);
    }
    return origin(stack, burnInterval);
}
LL_TYPE_STATIC_HOOK(
    StorageFuelSlotHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::isItemAllowedInFuelSlot,
    bool,
    int                  slot,
    ItemStackBase const& stack,
    int                  amount
) {
    if (slot == 1 && !stack.isNull()) {
        if (auto const* item = stack.mItem.get()) {
            if (auto t = fuelTicks(nameOf(*item))) return *t > 0;
        }
    }
    return origin(slot, stack, amount);
}

// Raw ore blocks smelt 9x slower: on 8 of 9 ticks the progress the furnace makes is taken back.
LL_TYPE_INSTANCE_HOOK(
    StorageFurnaceTickHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::$tick,
    void,
    BlockSource& region
) {
    // tick() overrides IVanillaTickBlockActorComponent::tick: the game passes that base subobject
    auto*       self  = thisFor<IVanillaTickBlockActorComponent>();
    auto const& input = self->getItem(0);
    auto const* item  = input.isNull() ? nullptr : input.mItem.get();
    if (!item || region.getLevel().isClientSide()) {
        origin(region);
        return;
    }
    auto const& n = nameOf(*item);
    if (!n.starts_with("minecraft:raw_") || !n.ends_with("_block") || !utility(&StorageConfig::smeltRawOreBlocks)) {
        origin(region);
        return;
    }
    bool slowTick = region.getLevel().getCurrentTick().tickID % kSmeltSlowdown != 0;
    if (!slowTick) {
        origin(region);
        return;
    }
    int& progress = self->mCookingProgress;
    int  before   = progress;
    progress      = before - 1; // the furnace adds 1 back while it cooks
    origin(region);
    if (progress > 0 && progress < before - 1) progress += 1; // decaying (no fuel): undo our step
    progress = std::max(progress, 0);
}

// Smelting XP: raw ore blocks (6.3 / 9), bone meal (0.05 each).
LL_TYPE_STATIC_HOOK(
    StorageFurnaceXpHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::_getXPRewardMultiplier,
    float,
    ItemStackBase const& item
) {
    if (!item.isNull()) {
        auto id = game::itemId(item);
        if (utility(&StorageConfig::smeltRawOreBlocks)) {
            if (id == "minecraft:iron_block" || id == "minecraft:copper_block") return 6.3f;
            if (id == "minecraft:gold_block") return 9.f;
        }
        if (id == "minecraft:bone_meal" && utility(&StorageConfig::boneMealUtility)) return 0.05f;
    }
    return origin(item);
}

// ---- compost ----------------------------------------------------------------------------------

LL_TYPE_STATIC_HOOK(
    StorageCompostHook,
    ll::memory::HookPriority::Normal,
    ComposterBlock,
    &ComposterBlock::getFillChance,
    schar,
    ItemStackBase const& item
) {
    if (!item.isNull()) {
        if (auto c = compostChance(game::itemId(item))) return static_cast<schar>(*c);
    }
    return origin(item);
}

// ---- beacon base ------------------------------------------------------------------------------

using BeaconKey = std::tuple<int, int, int, int>;
std::map<BeaconKey, int>& raisedLevels() {
    static std::map<BeaconKey, int> m;
    return m;
}

bool isBaseBlock(Block const& b, bool crates) {
    auto const& n = b.getTypeName();
    if (n == "minecraft:iron_block" || n == "minecraft:gold_block" || n == "minecraft:emerald_block"
        || n == "minecraft:diamond_block" || n == "minecraft:netherite_block")
        return true;
    return crates && n == "lepton:golden_apple_crate";
}

int countLevels(BlockSource& region, BlockPos const& pos, bool crates) {
    int levels = 0;
    for (int i = 1; i <= 4; ++i) {
        int y = pos.y - i;
        if (y < region.getMinHeight()) break;
        bool full = true;
        for (int x = pos.x - i; x <= pos.x + i && full; ++x)
            for (int z = pos.z - i; z <= pos.z + i && full; ++z)
                if (!isBaseBlock(region.getBlock(BlockPos{x, y, z}), crates)) full = false;
        if (!full) break;
        levels = i;
    }
    return levels;
}

void applyBeaconEffects(BeaconBlockActor& beacon, BlockSource& region, int levels) {
    int primary = beacon.mPrimaryEffectId, secondary = beacon.mSecondaryEffectId;
    if (levels <= 0 || primary <= 0) return;
    auto const& pos      = beacon.getPosition();
    double      range    = levels * 10 + 10;
    int         duration = (9 + levels * 2) * 20;
    int         amp      = (levels >= 4 && primary == secondary) ? 1 : 0;
    region.getLevel().forEachPlayer([&](Player& p) {
        if (p.getDimensionId() != region.getDimensionId()) return true;
        auto const& v = p.getPosition();
        if (std::abs(v.x - (pos.x + 0.5)) > range + 0.5 || std::abs(v.z - (pos.z + 0.5)) > range + 0.5) return true;
        if (v.y < pos.y - range) return true; // Java: the box reaches up to the build height
        MobEffectInstance e(static_cast<uint>(primary), EffectDuration{duration});
        e.mAmplifier = amp;
        e.mAmbient   = true;
        p.addEffect(e);
        if (levels >= 4 && secondary > 0 && secondary != primary) {
            MobEffectInstance s(static_cast<uint>(secondary), EffectDuration{duration});
            s.mAmbient = true;
            p.addEffect(s);
        }
        return true;
    });
}

// Golden apple crates count as beacon base blocks: after the game's own tick the pyramid is measured
// with them; when that gives more levels, the beacon gets them (and its effects, every 80 ticks).
// BeaconBlockActor::tick overrides IVanillaTickBlockActorComponent::tick, so `this` is that subobject.
LL_TYPE_INSTANCE_HOOK(
    StorageBeaconTickHook,
    ll::memory::HookPriority::Normal,
    BeaconBlockActor,
    &BeaconBlockActor::$tick,
    void,
    BlockSource& region
) {
    origin(region);
    if (!cfg().enableCompressedBlocks || !cfg().enableGoldenAppleCrate) return;
    auto& self   = static_cast<BeaconBlockActor&>(reinterpret_cast<IVanillaTickBlockActorComponent*>(this)->getBlockActor());
    bool  client = region.getLevel().isClientSide();
    auto  tick   = region.getLevel().getCurrentTick().tickID;
    auto  key    = std::make_tuple(static_cast<int>(region.getDimensionId()), self.getPosition().x, self.getPosition().y, self.getPosition().z);
    auto& known  = raisedLevels();
    if (tick % 20 == 0 || !known.contains(key)) {
        if (known.size() > 4096) known.clear();
        known[key] = countLevels(region, self.getPosition(), true);
    }
    int ours = known[key];
    if (ours <= self.mNumLevels) return;
    self.mNumLevels = ours; // the game re-measures without crates: keep ours after every tick
    if (!client && tick % 80 == 0) applyBeaconEffects(self, region, ours);
}

// ---- glass shards -----------------------------------------------------------------------------

std::uint32_t nextFromIRandom(void* r) { return static_cast<std::uint32_t>(static_cast<IRandom*>(r)->nextInt()); }

LL_TYPE_INSTANCE_HOOK(
    StorageSpawnResourcesHook,
    ll::memory::HookPriority::Normal,
    BlockType,
    &BlockType::spawnResources,
    void,
    BlockSource&                region,
    BlockPos const&             pos,
    Block const&                block,
    IRandom&                    random,
    ResourceDropsContext const& ctx,
    Actor const*                actorContext
) {
    auto const& n = block.getTypeName();
    if (n == "lepton:charcoal_block" && !region.getLevel().isClientSide()) {
        // Quark: requiresCorrectToolForDrops - a player mining it needs a pickaxe
        bool mined = false, pickaxe = false;
        try {
            mined = ctx.mCause == ResourceDropsCause::PlayerMining && actorContext && actorContext->isPlayer();
            ItemStack const& used = ctx.mUsedItem;
            auto const*      item = used.isNull() ? nullptr : used.mItem.get();
            pickaxe = item && item->hasTag(HashedString("minecraft:is_pickaxe"));
        } catch (...) {}
        if (mined && !pickaxe) return;
    }
    bool glass = n == "minecraft:glass" || n.ends_with("_stained_glass") || n == "lepton:dirty_glass";
    bool pane  = n == "lepton:dirty_glass_pane";
    if ((!glass && !pane) || region.getLevel().isClientSide()) {
        origin(region, pos, block, random, ctx, actorContext);
        return;
    }
    bool silk = false;
    try {
        silk = ctx.isUsingSilkTouch();
    } catch (...) {}
    if (silk) { // the block itself (vanilla behaviour)
        origin(region, pos, block, random, ctx, actorContext);
        return;
    }
    if (pane) return; // Quark: the dirty pane drops only with silk touch
    auto shard = shardFor(n);
    if (shard.empty()) {
        if (n != "lepton:dirty_glass") origin(region, pos, block, random, ctx, actorContext);
        return;
    }
    int   fortune = 0;
    float radius  = 0.f;
    try {
        fortune = ctx.getMiningLootBonusLevel();
        if (ctx.mCause == ResourceDropsCause::Explosion) radius = ctx.mExplosionRadius;
    } catch (...) {}
    int count = shardCount(&nextFromIRandom, &random, fortune, radius);
    if (count <= 0) return;
    auto stack = game::makeItem(shard, count);
    if (!stack.isNull()) BlockType::popResource(region, pos, stack, actorContext);
}

} // namespace

// ---- public -----------------------------------------------------------------------------------

std::optional<int> fuelTicks(std::string_view id) {
    auto const& c = cfg();
    if (id == "minecraft:torch" || id == "minecraft:soul_torch") {
        if (c.enableUtilityRecipes && c.torchesBurn) return 400;
        return std::nullopt;
    }
    if (!id.starts_with("lepton:")) return std::nullopt;
    auto on = [&](std::string_view x) { return contentEnabled(x) ? 1 : 0; };
    if (id == "lepton:charcoal_block") return on(id) * std::max(0, c.charcoalBlockFuelTime);
    if (id == "lepton:blaze_lantern") return on(id) * std::max(0, c.blazeLanternFuelTime);
    if (id == "lepton:stick_block") return on(id) * std::max(0, c.stickBlockFuelTime);
    // Zeta: every Quark block with a wood sound burns for 300 ticks (bundles, crates, stools)
    if (id == "lepton:sugar_cane_block" || id == "lepton:cactus_block" || id == "lepton:chorus_fruit_block"
        || id.ends_with("_crate") || id.ends_with("_stool")) {
        auto const ids = contentIds();
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) return std::nullopt;
        return on(id) * 300;
    }
    return std::nullopt;
}

std::optional<int> compostChance(std::string_view id) {
    auto const& c = cfg();
    if (id == "minecraft:poisonous_potato" || id == "minecraft:rotten_flesh") {
        if (!c.enableUtilityRecipes || !c.compostableToxins) return std::nullopt;
        return id == "minecraft:poisonous_potato" ? 85 : 30;
    }
    static constexpr std::string_view kCompostable[] = {
        "lepton:sugar_cane_block", "lepton:cactus_block",   "lepton:apple_crate",       "lepton:potato_crate",
        "lepton:carrot_crate",     "lepton:beetroot_crate", "lepton:cocoa_beans_sack",  "lepton:nether_wart_sack",
        "lepton:berry_sack",       "lepton:glowberry_sack",
    };
    for (auto x : kCompostable)
        if (x == id) return contentEnabled(id) ? std::optional<int>(100) : std::nullopt;
    return std::nullopt;
}

int beaconLevels(BlockSource& region, BlockPos const& beacon) {
    bool crates = cfg().enableCompressedBlocks && cfg().enableGoldenAppleCrate;
    return countLevels(region, beacon, crates);
}

std::string shardFor(std::string_view blockId) {
    if (!cfg().enableGlassShard) return {};
    if (blockId == "minecraft:glass") return "lepton:clear_shard";
    if (blockId == "lepton:dirty_glass") return "lepton:dirty_shard";
    constexpr std::string_view kPrefix = "minecraft:", kSuffix = "_stained_glass";
    if (blockId.starts_with(kPrefix) && blockId.ends_with(kSuffix)) {
        auto color = blockId.substr(kPrefix.size(), blockId.size() - kPrefix.size() - kSuffix.size());
        return "lepton:" + std::string(color) + "_shard";
    }
    return {};
}

int shardCount(std::uint32_t (*next)(void*), void* rng, int fortune, float explosionRadius) {
    auto below = [&](std::uint32_t n) { return static_cast<int>(next(rng) % n); };
    int  count = 2 + below(3);                                // uniform 2..4
    if (fortune > 0) count += below(static_cast<std::uint32_t>(fortune) + 1); // uniform_bonus_count, multiplier 1
    count = std::clamp(count, 1, 4);                          // limit_count 1..4
    if (explosionRadius > 0.f) {                              // explosion_decay
        int kept = 0;
        for (int i = 0; i < count; ++i)
            if ((next(rng) % 1000000) / 1000000.f <= 1.f / explosionRadius) ++kept;
        count = kept;
    }
    return count;
}

void applyDragonBreath() {
    auto breath = game::makeItem("minecraft:dragon_breath");
    auto bottle = game::makeItem("minecraft:glass_bottle");
    auto* item  = breath.isNull() ? nullptr : breath.mItem.get();
    if (!item) return;
    // Quark: effectiveDragonBreath -> no remainder; otherwise a glass bottle
    bool effective                = cfg().enableUtilityRecipes && cfg().effectiveDragonBreath;
    item->mCraftingRemainingItem  = effective ? nullptr : (bottle.isNull() ? nullptr : bottle.mItem.get());
}

void installCompressed() {
    StorageItemBurnHook::hook();
    StorageBurnHook::hook();
    StorageFuelSlotHook::hook();
    StorageFurnaceTickHook::hook();
    StorageFurnaceXpHook::hook();
    StorageCompostHook::hook();
    StorageBeaconTickHook::hook();
    StorageSpawnResourcesHook::hook();
}

void uninstallCompressed() {
    StorageItemBurnHook::unhook();
    StorageBurnHook::unhook();
    StorageFuelSlotHook::unhook();
    StorageFurnaceTickHook::unhook();
    StorageFurnaceXpHook::unhook();
    StorageCompostHook::unhook();
    StorageBeaconTickHook::unhook();
    StorageSpawnResourcesHook::unhook();
}

} // namespace lp::feature::storage
