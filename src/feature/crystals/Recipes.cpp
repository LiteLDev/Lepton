// U12 crystals: recipes that depend on the config (disabled modules, Quark's recipe flags
// `rainbow_lamp_corundum` and `magnet_pre_end`) are taken out of the world's recipe list when a world
// loads. Removed recipe objects are kept alive (the game may still hold raw pointers to them).

#include <algorithm>
#include <memory>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"

#include "feature/crystals/Common.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::crystals {

/// The recipe ids (without "lepton:") the current config disables.
std::set<std::string> disabledRecipes() {
    auto const&           c = cfg();
    std::set<std::string> out;
    bool                  corundumLamps = c.enableCorundum && c.useCorundum;
    for (auto const& col : kColors) {
        std::string n(col.name);
        if (!c.enableCorundum) {
            out.insert(n + "_corundum_pane");
            out.insert(n + "_waxed_corundum");
        }
        if (!c.enableRainbowLamps || !corundumLamps) out.insert(n + "_corundum_lamp");
        if (!c.enableRainbowLamps || corundumLamps) out.insert(n + "_crystal_lamp");
    }
    if (!c.enableMagnets || c.usePreEndRecipe) out.insert("magnet");
    if (!c.enableMagnets || !c.usePreEndRecipe) out.insert("magnet_pre_end");
    return out;
}

namespace {

std::vector<std::shared_ptr<Recipe>>& graveyard() {
    static std::vector<std::shared_ptr<Recipe>> g;
    return g;
}

int removeRecipes() {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    auto& recipes = level->getRecipes();
    auto  ids     = disabledRecipes();
    std::set<Recipe const*> gone;
    for (auto& [tag, byId] : recipes.mRecipes.get()) {
        for (auto it = byId.begin(); it != byId.end();) {
            std::string const& id = it->first;
            if (id.starts_with("lepton:") && ids.contains(id.substr(7))) {
                gone.insert(it->second.get());
                graveyard().push_back(it->second);
                it = byId.erase(it);
            } else ++it;
        }
    }
    if (gone.empty()) return 0;
    auto& byNet = recipes.mRecipesByNetId.get();
    for (auto it = byNet.begin(); it != byNet.end();) {
        if (gone.contains(it->second)) it = byNet.erase(it);
        else ++it;
    }
    auto& byOut = recipes.mRecipesByOutput.get();
    for (auto& [item, map] : byOut) {
        for (auto it = map.begin(); it != map.end();) {
            if (gone.contains(it->second)) it = map.erase(it);
            else ++it;
        }
    }
    auto& unlockable = recipes.mUnlockableRecipes.get();
    std::erase_if(unlockable, [&](auto const& r) { return gone.contains(r.get()); });
    log().info("crystals: disabled {} recipes", gone.size());
    return static_cast<int>(gone.size());
}

class RecipesModule {
public:
    void install() {
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldReady([] { removeRecipes(); });
        }
        if (game::db::available()) removeRecipes();
    }
    void uninstall() {}
};

} // namespace

} // namespace lp::feature::crystals

LP_REGISTER_MODULE(lp::feature::crystals::RecipesModule);
