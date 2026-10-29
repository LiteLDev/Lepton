// Item tweaks: GoldToolsHaveFortune, DiamondRepair, NoDurabilityOnCosmetics and the DragonScales
// elytra recipe. Spec: docs/spec/mobtweaks.md §3. These hooks also run in the client build, so mod
// clients (anvil preview, mining prediction) agree with the server.

#include <algorithm>
#include <array>
#include <unordered_map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/PlayerUIContainer.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/crafting/CraftHandlerBase.h"
#include "mc/world/inventory/network/crafting/CraftHandlerCrafting.h"
#include "mc/world/inventory/simulation/ContainerScreenValidation.h"
#include "mc/world/inventory/simulation/ContainerValidationCraftInputs.h"
#include "mc/world/inventory/simulation/ContainerValidationCraftResult.h"
#include "mc/world/inventory/simulation/validation/AnvilContainerScreenValidator.h"
#include "mc/world/item/components/ComponentItem.h"
#include "mc/world/item/DiggerItem.h"
#include "mc/world/item/HumanoidArmorItem.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/MaceItem.h"
#include "mc/world/item/ShieldItem.h"
#include "mc/world/item/WeaponItem.h"
#include "mc/world/item/WolfArmorItem.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/AnvilBlock.h"
#include "mc/world/level/block/Block.h"

#include "feature/mobtweaks/Items.h"
#include "feature/mobtweaks/MobTweaks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::mobtweaks {

namespace {

// ---- GoldToolsHaveFortune ------------------------------------------------------------------------

/// Java EnchantmentHelper.isEnchantmentCompatible: the baked enchantment applies only if it is
/// compatible with every other enchantment on the item.
bool compatibleWithItem(int type, ItemStackBase const& stack) {
    if (!stack.mUserData) return true;
    auto& all = Enchant::mEnchants();
    for (auto const& e : stack.constructItemEnchantsFromUserData().getAllEnchants()) {
        int other = static_cast<int>(e.mEnchantType);
        if (other == type) continue;
        if (other < 0 || other >= static_cast<int>(all.size()) || !all[other]) continue;
        if (!all[other]->isCompatibleWith(static_cast<Enchant::Type>(type))) return false;
    }
    return true;
}

LL_STATIC_HOOK(
    EnchantLevelHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::getEnchantLevel,
    int,
    Enchant::Type        type,
    ItemStackBase const& stack
) {
    int level = origin(type, stack);
    return std::max(level, effectiveBakedLevel(static_cast<int>(type), stack, level));
}

LL_STATIC_HOOK(
    HasEnchantHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::hasEnchant,
    bool,
    Enchant::Type        type,
    ItemStackBase const& stack
) {
    return origin(type, stack) || effectiveBakedLevel(static_cast<int>(type), stack, 0) > 0;
}

std::optional<bool> goldHarvest(Item const& self, Block const& block) {
    auto const& c = cfg();
    if (!c.enableGoldToolsHaveFortune) return std::nullopt;
    auto const& name = self.mFullName->getString();
    static constexpr std::string_view kPrefix = "minecraft:golden_";
    if (!name.starts_with(kPrefix)) return std::nullopt;
    auto kind = std::string_view(name).substr(kPrefix.size());
    if (kind != "pickaxe" && kind != "axe" && kind != "shovel" && kind != "hoe" && kind != "sword") return std::nullopt;
    auto const* other = tierTool(c.harvestLevel, kind);
    if (!other) return std::nullopt;
    return other->canDestroySpecial(block);
}

LL_TYPE_INSTANCE_HOOK(
    DiggerHarvestHook,
    ll::memory::HookPriority::Normal,
    DiggerItem,
    &DiggerItem::$canDestroySpecial,
    bool,
    Block const& block
) {
    if (auto r = goldHarvest(*this, block)) return *r;
    return origin(block);
}

LL_TYPE_INSTANCE_HOOK(
    ComponentHarvestHook,
    ll::memory::HookPriority::Normal,
    ComponentItem,
    &ComponentItem::$canDestroySpecial,
    bool,
    Block const& block
) {
    if (auto r = goldHarvest(*this, block)) return *r;
    return origin(block);
}

LL_TYPE_INSTANCE_HOOK(
    WeaponHarvestHook,
    ll::memory::HookPriority::Normal,
    WeaponItem,
    &WeaponItem::$canDestroySpecial,
    bool,
    Block const& block
) {
    if (auto r = goldHarvest(*this, block)) return *r;
    return origin(block);
}

// ---- DiamondRepair -------------------------------------------------------------------------------

bool repairDecision(Item const& self, ItemStackBase const& repair, bool vanilla) {
    if (!cfg().enableDiamondRepair) return vanilla;
    auto r = repairOverride(self.mFullName->getString(), game::itemId(repair));
    return r.value_or(vanilla);
}

#define LP_REPAIR_HOOK(Name, Cls)                                                                                     \
    LL_TYPE_INSTANCE_HOOK(                                                                                            \
        Name,                                                                                                         \
        ll::memory::HookPriority::Normal,                                                                             \
        Cls,                                                                                                          \
        &Cls::$isValidRepairItem,                                                                                     \
        bool,                                                                                                         \
        ItemStackBase const&   source,                                                                                \
        ItemStackBase const&   repair,                                                                                \
        BaseGameVersion const& version                                                                                \
    ) {                                                                                                               \
        return repairDecision(*this, repair, origin(source, repair, version));                                        \
    }

LP_REPAIR_HOOK(DiggerRepairHook, DiggerItem)
LP_REPAIR_HOOK(WeaponRepairHook, WeaponItem)
LP_REPAIR_HOOK(ArmorRepairHook, HumanoidArmorItem)
LP_REPAIR_HOOK(ComponentRepairHook, ComponentItem)
LP_REPAIR_HOOK(ShieldRepairHook, ShieldItem)
LP_REPAIR_HOOK(MaceRepairHook, MaceItem)
LP_REPAIR_HOOK(WolfArmorRepairHook, WolfArmorItem)

// ---- NoDurabilityOnCosmetics ---------------------------------------------------------------------

/// Player unique id -> tick of an anvil craft that must not damage the anvil.
std::unordered_map<int64_t, uint64_t>& sparedCrafts() {
    static std::unordered_map<int64_t, uint64_t> m;
    return m;
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
    if (auto* player = screenContext.mPlayer; player && !player->isClientSide()) {
        auto const& material = player->mPlayerUIContainer->getItem(static_cast<int>(PlayerUISlot::AnvilMaterial));
        auto        id       = player->getOrCreateUniqueID().rawID;
        if (sparesAnvil(material)) sparedCrafts()[id] = tickNow();
        else sparedCrafts().erase(id);
    }
    return origin(screenContext, screenValidation, std::move(craftInputs), numCrafts);
}

LL_STATIC_HOOK(
    AnvilDamageHook,
    ll::memory::HookPriority::Normal,
    &AnvilBlock::tryApplyCraftingDamage,
    void,
    Player const&   player,
    BlockPos const& pos,
    IRandom&        random
) {
    if (consumeSparedCraft(player.getOrCreateUniqueID().rawID)) return;
    origin(player, pos, random);
}

// ---- DragonScales: elytra duplication ------------------------------------------------------------

/// Elytras consumed by an elytra_duplication craft, per craft handler, given back on success.
std::unordered_map<CraftHandlerBase const*, std::pair<int64_t, std::vector<ItemStack>>>& pendingElytras() {
    static std::unordered_map<CraftHandlerBase const*, std::pair<int64_t, std::vector<ItemStack>>> m;
    return m;
}

LL_TYPE_INSTANCE_HOOK(
    CraftConsumeHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerCrafting,
    &CraftHandlerCrafting::$handleConsumedItem,
    ItemStackNetResult,
    FullContainerName const& container,
    uchar const              slot,
    ItemStack const&         consumed
) {
    Player& player = mPlayer;
    if (!player.isClientSide()) {
        auto const* recipe = player.getLevel().getRecipes().getRecipeByNetId(mRecipeNetId);
        if (recipe && recipe->mRecipeId.get() == kElytraRecipe) {
            if (!cfg().enableDragonScales) return ItemStackNetResult::Error;
            if (game::itemId(consumed) == "minecraft:elytra") {
                auto& entry = pendingElytras()[this];
                entry.first = player.getOrCreateUniqueID().rawID;
                ItemStack copy(consumed);
                copy.set(1);
                entry.second.push_back(std::move(copy));
            }
        }
    }
    return origin(container, slot, consumed);
}

LL_TYPE_INSTANCE_HOOK(
    CraftPostHook,
    ll::memory::HookPriority::Normal,
    CraftHandlerBase,
    &CraftHandlerBase::postRequest,
    void,
    bool wasSuccess
) {
    origin(wasSuccess);
    auto it = pendingElytras().find(this);
    if (it == pendingElytras().end()) return;
    auto [playerId, items] = std::move(it->second);
    pendingElytras().erase(it);
    if (!wasSuccess) return;
    // Give the original elytra back once the craft request is done (next tick: the request's own
    // inventory changes are sent first).
    later(1, [playerId, items = std::move(items)]() mutable {
        auto* actor = game::entities::find(playerId);
        if (!actor || !actor->isPlayer()) return;
        giveOrDrop(static_cast<Player&>(*actor), items);
        log().info("mobtweaks: elytra duplicated");
    });
}

} // namespace

// ---- shared --------------------------------------------------------------------------------------

int effectiveBakedLevel(int type, ItemStackBase const& stack, int realLevel) {
    int baked = bakedLevel(type, stack);
    if (baked <= realLevel) return baked;
    return compatibleWithItem(type, stack) ? baked : 0;
}

Item const* tierTool(int harvestLevel, std::string_view kind) {
    static constexpr std::array<std::string_view, 5> kTiers{"wooden", "stone", "iron", "diamond", "netherite"};
    auto        tier = kTiers[static_cast<size_t>(std::clamp(harvestLevel, 0, 4))];
    std::string id   = "minecraft:" + std::string(tier) + "_" + std::string(kind);
    auto        reg  = ItemRegistryManager::getItemRegistry();
    return reg.lookupByName(HashedString(id)).get();
}

bool sparesAnvil(ItemStackBase const& material) {
    auto const& c = cfg();
    if (!c.enableNoDurabilityOnCosmetics) return false;
    if (game::isEmpty(material)) return true;
    return c.allowCosmeticItems && material.mItem && material.mItem->hasTag(HashedString(kCosmeticTag));
}

void noteSparedCraft(int64_t playerId) { sparedCrafts()[playerId] = tickNow(); }

bool consumeSparedCraft(int64_t playerId) {
    auto it = sparedCrafts().find(playerId);
    if (it == sparedCrafts().end()) return false;
    bool fresh = tickNow() - it->second <= 2; // the damage follows the craft in the same request
    sparedCrafts().erase(it);
    return fresh && cfg().enableNoDurabilityOnCosmetics;
}

void giveOrDrop(Player& player, std::vector<ItemStack> items) {
    auto& pinv = game::inv::playerInventory(player);
    for (auto& stack : items) {
        game::inv::insert(pinv, stack, game::inv::kPlayerAll);
        if (!game::isEmpty(stack)) player.drop(stack, false);
    }
    game::inv::sync(player);
}

class ItemsModule {
public:
    void install() {
        EnchantLevelHook::hook();
        HasEnchantHook::hook();
        DiggerHarvestHook::hook();
        ComponentHarvestHook::hook();
        WeaponHarvestHook::hook();
        DiggerRepairHook::hook();
        WeaponRepairHook::hook();
        ArmorRepairHook::hook();
        ComponentRepairHook::hook();
        ShieldRepairHook::hook();
        MaceRepairHook::hook();
        WolfArmorRepairHook::hook();
        AnvilResultHook::hook();
        AnvilDamageHook::hook();
        CraftConsumeHook::hook();
        CraftPostHook::hook();
    }
    void uninstall() {
        EnchantLevelHook::unhook();
        HasEnchantHook::unhook();
        DiggerHarvestHook::unhook();
        ComponentHarvestHook::unhook();
        WeaponHarvestHook::unhook();
        DiggerRepairHook::unhook();
        WeaponRepairHook::unhook();
        ArmorRepairHook::unhook();
        ComponentRepairHook::unhook();
        ShieldRepairHook::unhook();
        MaceRepairHook::unhook();
        WolfArmorRepairHook::unhook();
        AnvilResultHook::unhook();
        AnvilDamageHook::unhook();
        CraftConsumeHook::unhook();
        CraftPostHook::unhook();
        sparedCrafts().clear();
        pendingElytras().clear();
    }
};

} // namespace lp::feature::mobtweaks

LP_REGISTER_MODULE(lp::feature::mobtweaks::ItemsModule);
