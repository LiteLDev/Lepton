// A disabled module leaves no recipes and no creative entries behind (SPEC §2.3): recipes are
// skipped while the world's recipe table loads, creative items while the creative inventory is
// built (both happen when a world loads, before install() on BDS: the hooks go in at mod load).
//
// Quark flags:
//   variant_chests       chests, trapped chests (enableVariantChests)
//   chest_reversion      <wood>_chest -> chest, trapped -> trapped chest (enableRevertingWoodenChests)
//   wood_to_chest_recipes  logs -> 4 chests (U8 storage: enableUtilityRecipes && logsToChests)
//   variant_furnaces     furnaces and the blast furnace / smoker recipes from them

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "ll/api/memory/Hook.h"

#include "mc/world/item/CreativeItemInitializer.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/crafting/Recipes.h"

#include "feature/variantcontainers/VariantContainers.h"
#include "game/BlockStates.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::variantcontainers {

namespace {

/// A boolean of another unit's config file (default when missing).
bool externalFlag(char const* file, char const* key, bool fallback) {
    try {
        std::ifstream in(config::configDir() / file);
        if (!in) return fallback;
        auto j = nlohmann::json::parse(in, nullptr, false, true);
        if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) return fallback;
        return j[key].get<bool>();
    } catch (...) {
        return fallback;
    }
}

bool logsToChests() {
    return externalFlag("storage.json", "enableUtilityRecipes", true) && externalFlag("storage.json", "logsToChests", true);
}

int gSkippedRecipes = 0, gSkippedItems = 0;

bool endsWith(std::string_view s, std::string_view x) { return s.size() >= x.size() && s.substr(s.size() - x.size()) == x; }

} // namespace

/// Recipe `lepton:<name>` is allowed by the current config (true for recipes that are not ours).
bool recipeAllowed(std::string_view name) {
    auto const& c = cfg();
    bool furnace  = name.starts_with("deepslate_") || name.starts_with("blackstone_");
    if (furnace && (endsWith(name, "_furnace") || endsWith(name, "_blast_furnace") || endsWith(name, "_smoker")))
        return c.enableVariantFurnaces;
    bool chest = false;
    for (auto const& m : chestMaterials()) {
        if (name.starts_with(m + "_chest") || name.starts_with("trapped_" + m + "_chest")) chest = true;
    }
    if (!chest) return true;
    if (!c.enableVariantChests) return false;
    if (endsWith(name, "_revert")) return c.enableRevertingWoodenChests;
    if (name.find("_chest_wood_") != std::string_view::npos) return logsToChests();
    return true;
}

/// Vanilla recipes that make a vanilla chest / furnace from one kind of planks / stone: Quark makes the
/// variant from those (a vanilla chest / furnace only from mixed materials, through the vanilla tag
/// recipes), so they go while the module is on.
bool replacedVanilla(std::string_view id) {
    if (id == "minecraft:chest_from_crimson_planks" || id == "minecraft:chest_from_mangrove_planks" || id == "minecraft:chest_from_warped_planks")
        return cfg().enableVariantChests;
    if (id == "minecraft:furnace_from_blackstone" || id == "minecraft:furnace_from_cobbled_deepslate") return cfg().enableVariantFurnaces;
    return false;
}

/// Item `id` may be in the creative inventory.
bool itemAllowed(std::string_view id) {
    if (!id.starts_with("lepton:")) return true;
    for (auto const& c : chestIds())
        if (c == id) return cfg().enableVariantChests;
    for (auto const& f : furnaceIds())
        if (f == id) return cfg().enableVariantFurnaces;
    // chests other units add (Quark makeChestBlocksExternal: only while VariantChests is on)
    if (auto const* b = game::blockstates::of(id); b && isVariantChest(*b)) return cfg().enableVariantChests;
    return true;
}

namespace {

LL_TYPE_INSTANCE_HOOK(
    VcRecipeGateHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::_loadRecipe,
    bool,
    std::string const&      recipeId,
    RecipeType              type,
    Json::Value const&      objData,
    MinEngineVersion const& minEngineVersion,
    SemVersion const&       formatVersion,
    bool                    isBaseGamePack,
    Recipes::Buffers&       buffers
) {
    if ((recipeId.starts_with("lepton:") && !recipeAllowed(std::string_view(recipeId).substr(7))) || replacedVanilla(recipeId)) {
        ++gSkippedRecipes;
        return true; // as if loaded: the recipe does not exist
    }
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

LL_TYPE_INSTANCE_HOOK(
    VcCreativeGroupHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addCreativeItemServer,
    void,
    CreativeGroupInfo&  group,
    ItemInstance const& item
) {
    if (!itemAllowed(game::itemId(item))) {
        ++gSkippedItems;
        return;
    }
    origin(group, item);
}

LL_TYPE_INSTANCE_HOOK(
    VcCreativeLooseHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addLooseCreativeItem,
    void,
    ItemInstance const& item
) {
    if (!itemAllowed(game::itemId(item))) {
        ++gSkippedItems;
        return;
    }
    origin(item);
}

} // namespace

class GatingModule {
public:
    void load() {
        hook();
        game::db::onWorldClosed([] { gSkippedRecipes = gSkippedItems = 0; });
        game::db::onWorldReady([] {
            if (gSkippedRecipes || gSkippedItems)
                log().info("variantcontainers: {} recipes and {} creative entries disabled by config", gSkippedRecipes, gSkippedItems);
        });
    }
    void install() { hook(); }
    void uninstall() {
        if (!mHooked) return;
        VcRecipeGateHook::unhook();
        VcCreativeGroupHook::unhook();
        VcCreativeLooseHook::unhook();
        mHooked = false;
    }

private:
    void hook() {
        if (mHooked) return;
        VcRecipeGateHook::hook();
        VcCreativeGroupHook::hook();
        VcCreativeLooseHook::hook();
        mHooked = true;
    }
    bool mHooked = false;
};

} // namespace lp::feature::variantcontainers

LP_REGISTER_MODULE(lp::feature::variantcontainers::GatingModule);
