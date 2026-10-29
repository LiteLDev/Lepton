// Disabled modules leave no working recipes behind (SPEC §2.3): when a world loads, the recipes of
// disabled modules are taken out of the level's recipe table (clients joining later never receive
// them, so the recipe book and crafting grid never offer them). Toggling a module takes effect on the
// next world load.

#include <memory>
#include <set>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"

#include "feature/logistics/Logistics.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::logistics {
namespace {

std::set<std::string> disabledRecipes() {
    auto const&           c = cfg();
    std::set<std::string> out;
    if (!c.enableChute) out.insert("lepton:chute");
    if (!c.enableFeedingTrough) out.insert("lepton:feeding_trough");
    if (!c.enableCrate) out.insert("lepton:crate");
    if (!c.enablePipes) out.insert({"lepton:pipe", "lepton:encased_pipe", "lepton:encased_pipe_revert"});
    if (!c.enableEncasedPipes) out.insert({"lepton:encased_pipe", "lepton:encased_pipe_revert"});
    return out;
}

/// Removed recipes stay alive here: the game keeps raw pointers (net id table).
std::vector<std::shared_ptr<Recipe>>& graveyard() {
    static std::vector<std::shared_ptr<Recipe>> v;
    return v;
}

void removeDisabledRecipes() {
    auto ids = disabledRecipes();
    if (ids.empty()) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    auto&                recipes = level->getRecipes();
    int                  removed = 0;
    std::set<Recipe const*> gone;
    for (auto& [tag, byId] : recipes.mRecipes.get()) {
        for (auto it = byId.begin(); it != byId.end();) {
            if (ids.contains(it->first)) {
                gone.insert(it->second.get());
                graveyard().push_back(it->second);
                it = byId.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
    }
    // lookups by output and recipe unlocks must not find them either (the net id table keeps its
    // entries: the recipes stay alive in the graveyard, and clients never receive their net ids)
    for (auto& [item, byId] : recipes.mRecipesByOutput.get()) std::erase_if(byId, [&](auto const& kv) { return gone.contains(kv.second); });
    std::erase_if(recipes.mUnlockableRecipes.get(), [&](auto const& r) { return gone.contains(r.get()); });
    if (removed) log().info("logistics: removed {} recipes of disabled modules", removed);
}

class GatingModule {
public:
    void install() {
        game::db::onWorldReady([] { removeDisabledRecipes(); });
        if (game::db::available()) removeDisabledRecipes();
    }
    void uninstall() {}
};

} // namespace
} // namespace lp::feature::logistics

LP_REGISTER_MODULE(lp::feature::logistics::GatingModule);
