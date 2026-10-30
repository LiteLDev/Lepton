// U17 placement: recipes of disabled content are taken out of the world's recipe list when a world
// loads (Trowel when `enableTrowel` is off; the Hammer unless `enableVariantSelector` and
// `enableHammer` are on, Quark's `hammer` recipe flag). Removed recipe objects are kept alive (the
// game may still hold raw pointers to them).

#include <memory>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"

#include "feature/placement/Placement.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::placement {

std::set<std::string> disabledRecipes() {
    std::set<std::string> out;
    if (!cfg().enableTrowel) {
        out.insert("lepton:trowel");
        out.insert("lepton:trowel_to_iron_nugget");
    }
    if (!cfg().enableVariantSelector || !cfg().enableHammer) out.insert("lepton:hammer");
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
    if (ids.empty()) return 0;
    std::set<Recipe const*> gone;
    for (auto& [tag, byId] : recipes.mRecipes.get()) {
        for (auto it = byId.begin(); it != byId.end();) {
            if (ids.contains(it->first)) {
                gone.insert(it->second.get());
                graveyard().push_back(it->second);
                it = byId.erase(it);
            } else ++it;
        }
    }
    if (gone.empty()) return 0;
    auto& byOut = recipes.mRecipesByOutput.get();
    for (auto& [item, map] : byOut) {
        for (auto it = map.begin(); it != map.end();) {
            if (gone.contains(it->second)) it = map.erase(it);
            else ++it;
        }
    }
    auto& unlockable = recipes.mUnlockableRecipes.get();
    std::erase_if(unlockable, [&](auto const& r) { return gone.contains(r.get()); });
    log().info("placement: disabled {} recipes", gone.size());
    return static_cast<int>(gone.size());
}

class PlacementRecipesModule {
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

} // namespace lp::feature::placement

LP_REGISTER_MODULE(lp::feature::placement::PlacementRecipesModule);
