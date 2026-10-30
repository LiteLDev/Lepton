// U13 enchanting: the rune duplication recipes depend on the config (Quark's recipe flags
// `color_runes` and `corundum`): the disabled one is taken out of the world's recipe list when a
// world loads. Removed recipe objects are kept alive (the game may still hold raw pointers to them).

#include <memory>
#include <set>

#include "ll/api/service/Bedrock.h"

#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"

#include "feature/crystals/Crystals.h"
#include "feature/enchanting/Config.h"
#include "feature/enchanting/Tomes.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::enchanting {

std::vector<std::string> disabledRuneRecipes(bool colorRunes, bool corundumRunes) {
    std::vector<std::string> out;
    if (!colorRunes || !corundumRunes) out.emplace_back("lepton:rune_duplication");
    if (!colorRunes || corundumRunes) out.emplace_back("lepton:rune_duplication_no_corundum");
    return out;
}

bool corundumRunesEnabled() {
    auto const& c = crystals::cfg();
    return c.enableCorundum && c.crystalsCraftRunes;
}

namespace {

std::vector<std::shared_ptr<Recipe>>& graveyard() {
    static std::vector<std::shared_ptr<Recipe>> g;
    return g;
}

int removeRecipes() {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    auto                  list = disabledRuneRecipes(cfg().enableColorRunes, corundumRunesEnabled());
    std::set<std::string> ids(list.begin(), list.end());
    auto&                 recipes = level->getRecipes();
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
    log().info("enchanting: disabled {} rune recipe(s)", gone.size());
    return static_cast<int>(gone.size());
}

class RuneRecipesModule {
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

} // namespace lp::feature::enchanting

LP_REGISTER_MODULE(lp::feature::enchanting::RuneRecipesModule);
