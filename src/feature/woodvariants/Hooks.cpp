// Game hooks of U6 woodvariants: recipe gating by config, bookshelf enchanting power, leaf carpet
// composting, ladder fuel, lanterns and hanging signs below posts.

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/json/Value.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/managers/models/EnchantingContainerManagerModel.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/ComposterBlock.h"
#include "mc/world/level/block/FlowerBlock.h"
#include "mc/world/level/block/FoliageBlock.h"
#include "mc/world/level/block/HangingSignBlock.h"
#include "mc/world/level/block/LanternBlock.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"

#include "feature/woodvariants/WoodVariants.h"
#include "game/BlockStates.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::woodvariants {

namespace bs = game::blockstates;

namespace {

// ---- recipes -----------------------------------------------------------------------------------

std::string resultOf(Json::Value const& v) {
    auto fromResult = [](Json::Value const& r) -> std::string {
        if (r.isString()) return r.asString("");
        if (r.isObject() && r.isMember("item")) {
            auto const& i = r["item"];
            return i.isString() ? i.asString("") : std::string();
        }
        if (r.isArray() && r.size() > 0) {
            auto const& f = r[0];
            if (f.isObject() && f.isMember("item")) return f["item"].asString("");
        }
        return {};
    };
    if (!v.isObject()) return {};
    if (v.isMember("result")) return fromResult(v["result"]);
    for (auto k : {"minecraft:recipe_shaped", "minecraft:recipe_shapeless"}) {
        if (v.isMember(k) && v[k].isObject() && v[k].isMember("result")) return fromResult(v[k]["result"]);
    }
    return {};
}

} // namespace

/// Whether a recipe of this unit must be skipped (its module is disabled, docs §3.8).
bool recipeDisabled(std::string const& recipeId, std::string const& result) {
    if (!recipeId.starts_with("lepton:")) return false;
    std::string_view path(recipeId);
    path.remove_prefix(7);
    if (path.starts_with("azalea_hedge_oak_from_") || path.starts_with("flowering_azalea_hedge_oak_from_")) {
        if (azaleaWoodEnabled()) return true;
    }
    if (path == "oak_bookshelf" || path.starts_with("lectern_from_")) {
        if (!moduleEnabled("variant_bookshelves")) return true;
    }
    if (path == "oak_ladder" && !moduleEnabled("variant_ladders")) return true;
    if (path.starts_with("vertical_") && path.ends_with("_planks_revert") && !moduleEnabled("vertical_planks")) return true;
    if (result.starts_with("lepton:")) {
        if (auto const* b = bs::of(result); b && !moduleEnabled(*b)) return true;
    }
    return false;
}

/// The game's per-planks bookshelf recipes (`minecraft:bookshelf_from_<wood>_planks`) have the same
/// ingredients as the variant bookshelf of that wood: while VariantBookshelves is on, the variant
/// wins (Quark), so the vanilla duplicate is not loaded. The oak one stays (it is the oak bookshelf).
bool vanillaRecipeReplaced(std::string const& recipeId) {
    static constexpr std::string_view kPrefix = "minecraft:bookshelf_from_";
    if (!recipeId.starts_with(kPrefix) || !recipeId.ends_with("_planks")) return false;
    if (!moduleEnabled("variant_bookshelves")) return false;
    auto wood = recipeId.substr(kPrefix.size(), recipeId.size() - kPrefix.size() - 7);
    if (wood == "oak") return false;
    return bs::of("lepton:" + wood + "_bookshelf") != nullptr;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    RecipeGateHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::_loadRecipe,
    bool,
    std::string const&        recipeId,
    RecipeType                type,
    Json::Value const&        objData,
    MinEngineVersion const&   minEngineVersion,
    SemVersion const&         formatVersion,
    bool                      isBaseGamePack,
    Recipes::Buffers&         buffers
) {
    try {
        if (recipeId.starts_with("lepton:") && recipeDisabled(recipeId, resultOf(objData))) return false;
        if (recipeId.starts_with("minecraft:bookshelf_from_") && vanillaRecipeReplaced(recipeId)) return false;
    } catch (...) {}
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

// ---- enchanting power --------------------------------------------------------------------------

struct EnchantCtx {
    BlockSource* region = nullptr;
    BlockPos     table{};
    bool         positionsAdded = false;
};
thread_local EnchantCtx* gEnchant = nullptr;

LL_TYPE_INSTANCE_HOOK(
    EnchantRecalcHook,
    ll::memory::HookPriority::Normal,
    EnchantingContainerManagerModel,
    &EnchantingContainerManagerModel::recalculateOptions,
    void
) {
    EnchantCtx ctx;
    try {
        ctx.region = &this->mPlayer.getDimensionBlockSource();
        ctx.table  = this->mBlockPos;
    } catch (...) {}
    auto* prev = gEnchant;
    gEnchant   = ctx.region ? &ctx : nullptr;
    origin();
    gEnchant = prev;
}

LL_STATIC_HOOK(
    BookCasePositionsHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::getBookCasePositions,
    std::vector<Vec3>,
    BlockSource& source,
    Vec3 const&  pos
) {
    auto out = origin(source, pos);
    if (!moduleEnabled("variant_bookshelves")) return out;
    BlockPos table{
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    };
    for (auto const& p : variantBookshelfPositions(source, table)) {
        out.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
    }
    if (gEnchant) gEnchant->positionsAdded = true;
    return out;
}

LL_STATIC_HOOK(
    EnchantCostsHook,
    ll::memory::HookPriority::Normal,
    &EnchantUtils::getEnchantCosts,
    std::vector<int>,
    IRandom&             random,
    ItemStackBase const& item,
    int                  bookcaseCount
) {
    // the game counted the shelves without getBookCasePositions: add the variants here
    if (gEnchant && !gEnchant->positionsAdded && moduleEnabled("variant_bookshelves")) {
        bookcaseCount = std::min(15, bookcaseCount + variantBookshelfCount(*gEnchant->region, gEnchant->table));
    }
    return origin(random, item, bookcaseCount);
}

// ---- leaf carpet composting, ladder fuel -------------------------------------------------------

Block const* blockOfItem(ItemStackBase const& item) {
    if (item.isNull()) return nullptr;
    auto const& name = item.getTypeName();
    if (!name.starts_with("lepton:")) return nullptr;
    return bs::of(name);
}

LL_STATIC_HOOK(
    FillChanceHook,
    ll::memory::HookPriority::Normal,
    &ComposterBlock::getFillChance,
    schar,
    ItemStackBase const& item
) {
    if (auto const* b = blockOfItem(item); b && hasTag(*b, "lepton:leaf_carpet") && moduleEnabled(*b)) return 20;
    return origin(item);
}

LL_STATIC_HOOK(
    BurnDurationHook,
    ll::memory::HookPriority::Normal,
    &FurnaceBlockActor::getBurnDuration,
    float,
    ItemStackBase const& item,
    float                burnInterval
) {
    if (auto const* b = blockOfItem(item); b && hasTag(*b, "lepton:ladder") && moduleEnabled(*b)) {
        // Quark: fuel.addWood (300 ticks, like planks)
        static ItemStack planks = game::makeItem("minecraft:oak_planks");
        return origin(planks, burnInterval);
    }
    return origin(item, burnInterval);
}

// ---- lanterns / hanging signs below posts ------------------------------------------------------

bool postAbove(BlockSource& region, BlockPos const& pos) {
    auto const& above = region.getBlock(BlockPos{pos.x, pos.y + 1, pos.z});
    return hasTag(above, "lepton:post") && moduleEnabled(above);
}

LL_TYPE_INSTANCE_HOOK(
    LanternSurviveHook,
    ll::memory::HookPriority::Normal,
    LanternBlock,
    &LanternBlock::$canSurvive,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    if (origin(region, pos)) return true;
    // Quark WoodenPostsModule#canHangingBlockConnect: hanging lanterns survive below a post
    auto const& self = region.getBlock(pos);
    bool        hanging = bs::getBool(self, "hanging").value_or(true);
    return hanging && postAbove(region, pos);
}

LL_TYPE_INSTANCE_HOOK(
    LanternMayPlaceHook,
    ll::memory::HookPriority::Normal,
    LanternBlock,
    &LanternBlock::$mayPlace,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    return origin(region, pos) || postAbove(region, pos);
}

LL_TYPE_INSTANCE_HOOK(
    HangingSignAttachHook,
    ll::memory::HookPriority::Normal,
    HangingSignBlock,
    &HangingSignBlock::_canAttachTo,
    bool,
    BlockSource&   region,
    BlockPos const attachPos,
    uchar          attachFace,
    uchar          ourFace
) {
    if (origin(region, attachPos, attachFace, ourFace)) return true;
    auto const& b = region.getBlock(attachPos);
    return woodvariants::hasTag(b, "lepton:post") && woodvariants::moduleEnabled(b) && (attachFace == 0 || ourFace == 1);
}

// ---- plants on hedges --------------------------------------------------------------------------

/// Quark HedgeBlock#canSustainPlant (UP): flowers can grow on a hedge. `pos` is the ground block
/// (or, depending on the caller, the plant's own position with the hedge below).
bool hedgeSustains(BlockSource& region, BlockPos const& pos) {
    auto const& ground = region.getBlock(pos);
    if (hasTag(ground, "lepton:hedge")) return moduleEnabled(ground);
    if (!ground.isAir()) return false;
    auto const& below = region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z});
    return hasTag(below, "lepton:hedge") && moduleEnabled(below);
}

LL_TYPE_INSTANCE_HOOK(
    FlowerMayPlaceOnHook,
    ll::memory::HookPriority::Normal,
    FlowerBlock,
    &FlowerBlock::$mayPlaceOn,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    return origin(region, pos) || hedgeSustains(region, pos);
}

// Saplings, short grass and ferns (FoliageBlock). FoliageBlock::$mayPlaceOn is MCFOLD (its code may be
// shared with other block types), so the hedge rule only applies to these plants, by name.
bool foliageOnHedge(BlockType const& plant) {
    auto const& n = plant.getTypeName();
    return n.ends_with("_sapling") || n == "minecraft:short_grass" || n == "minecraft:fern";
}

LL_TYPE_INSTANCE_HOOK(
    FoliageMayPlaceOnHook,
    ll::memory::HookPriority::Normal,
    FoliageBlock,
    &FoliageBlock::$mayPlaceOn,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    if (origin(region, pos)) return true;
    return foliageOnHedge(*this) && hedgeSustains(region, pos);
}

bool gLoaded    = false;
bool gInstalled = false;

} // namespace

namespace detail {

void loadHooks() {
    if (gLoaded) return;
    gLoaded = true;
    RecipeGateHook::hook();
}

void installHooks() {
    loadHooks();
    if (gInstalled) return;
    gInstalled = true;
    EnchantRecalcHook::hook();
    BookCasePositionsHook::hook();
    EnchantCostsHook::hook();
    FillChanceHook::hook();
    BurnDurationHook::hook();
    LanternSurviveHook::hook();
    LanternMayPlaceHook::hook();
    HangingSignAttachHook::hook();
    FlowerMayPlaceOnHook::hook();
    FoliageMayPlaceOnHook::hook();
}

void uninstallHooks() {
    if (gInstalled) {
        gInstalled = false;
        EnchantRecalcHook::unhook();
        BookCasePositionsHook::unhook();
        EnchantCostsHook::unhook();
        FillChanceHook::unhook();
        BurnDurationHook::unhook();
        LanternSurviveHook::unhook();
        LanternMayPlaceHook::unhook();
        HangingSignAttachHook::unhook();
        FlowerMayPlaceOnHook::unhook();
        FoliageMayPlaceOnHook::unhook();
    }
    if (gLoaded) {
        gLoaded = false;
        RecipeGateHook::unhook();
    }
}

} // namespace detail

} // namespace lp::feature::woodvariants
