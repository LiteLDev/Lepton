// U1 recipes: SlabsToBlocks (2 slabs -> 1 block, crawled from every "3 blocks -> slabs" recipe like
// Quark's ZRecipeCrawl) and the removal of every vertical slab recipe while VerticalSlabs is off.
// Recipes live in the level's Recipes object; changes are sent to the players with a fresh
// CraftingDataPacket.

#include "feature/vslabs/VSlabs.h"

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/sem_ver/SemVersion.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/json/Reader.h"
#include "mc/deps/json/Value.h"
#include "mc/network/packet/CraftingDataPacket.h"
#include "mc/network/packet/CraftingDataPacketPayload.h"
#include "mc/network/packet/MaterialReducerEntryOutput.h"
#include "mc/network/packet/MultiRecipePayload.h"
#include "mc/network/packet/ShapedRecipePayload.h"
#include "mc/network/packet/ShapelessRecipePayload.h"
#include "mc/network/packet/SmithingTransformRecipePayload.h"
#include "mc/network/packet/SmithingTrimRecipePayload.h"
#include "mc/resources/MinEngineVersion.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemDescriptor.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/crafting/Recipe.h"
#include "mc/world/item/crafting/RecipeIngredient.h"
#include "mc/world/item/crafting/RecipeType.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "game/BlockStates.h"
#include "game/Shapes.h"
#include "mod/Log.h"

namespace lp::feature::vslabs {

namespace bs = game::blockstates;

namespace {

constexpr std::string_view kStbPrefix = "lepton:slabs_to_blocks_";

/// A recipe taken out of the Recipes maps, kept alive so it can be put back.
struct Removed {
    HashedString              tag;
    std::string               id;
    std::shared_ptr<Recipe>   recipe;
    std::vector<ItemInstance> outputKeys; ///< mRecipesByOutput keys it was listed under
    bool                      unlockable = false;
};

struct StbEntry {
    std::string slab;  ///< slab item id
    std::string block; ///< full block item id
    int         aux = 0;
    std::string recipeId;
};

struct RecipeState {
    Recipes const*        owner = nullptr;
    bool                  crawled = false;
    std::vector<StbEntry> stb;
    std::vector<Removed>  removed;
};
RecipeState& rs() {
    static RecipeState s;
    return s;
}

std::string nameOf(ItemDescriptor const& d) {
    auto const* item = d.getItem();
    return item ? item->mFullName->getString() : std::string{};
}

/// Removes `id` (under `tag`) from every index of `recipes`. Returns what was removed.
std::optional<Removed> take(Recipes& recipes, HashedString const& tag, std::string const& id) {
    auto& all = recipes.mRecipes.get();
    auto  t   = all.find(tag);
    if (t == all.end()) return std::nullopt;
    auto it = t->second.find(id);
    if (it == t->second.end()) return std::nullopt;
    Removed r{tag, id, it->second, {}, false};
    t->second.erase(it);
    Recipe* ptr = r.recipe.get();
    for (auto& [key, byId] : recipes.mRecipesByOutput.get()) {
        auto f = byId.find(id);
        if (f != byId.end() && f->second == ptr) {
            byId.erase(f);
            r.outputKeys.push_back(key);
        }
    }
    // mRecipesByNetId is left alone: the recipe stays alive in our stash, and that map's hash is the game's
    auto& unl = recipes.mUnlockableRecipes.get();
    auto  sz  = unl.size();
    std::erase_if(unl, [&](auto const& p) { return p.get() == ptr; });
    r.unlockable = unl.size() != sz;
    return r;
}

void putBack(Recipes& recipes, Removed const& r) {
    recipes.mRecipes.get()[r.tag][r.id] = r.recipe;
    Recipe* ptr                         = r.recipe.get();
    for (auto const& key : r.outputKeys) {
        for (auto& [k, byId] : recipes.mRecipesByOutput.get()) {
            if (&k == &key || (k.getTypeName() == key.getTypeName() && k.getAuxValue() == key.getAuxValue())) {
                byId[r.id] = ptr;
                break;
            }
        }
    }
    if (r.unlockable) recipes.mUnlockableRecipes.get().emplace_back(ptr);
}

bool usesVerticalSlab(Recipe const& recipe) {
    for (auto const& ing : recipe.mMyIngredients.get()) {
        if (isVerticalSlabId(nameOf(ing))) return true;
    }
    for (auto const& out : recipe.getResultItems()) {
        if (!out.isNull() && isVerticalSlabId(out.getTypeName())) return true;
    }
    return false;
}

bool isSlabItem(std::string const& id) {
    if (id.empty() || isVerticalSlabId(id)) return false;
    auto const* b = bs::of(id);
    if (!b) return false;
    return game::shapes::kindOf(*b) == game::shapes::Kind::Slab || b->getBlockType().isSlabBlock();
}

size_t commonPrefix(std::string_view a, std::string_view b) {
    size_t n = 0;
    while (n < a.size() && n < b.size() && a[n] == b[n]) ++n;
    return n;
}

/// Quark's SlabsToBlocksModule#onVisitShaped over every shaped crafting recipe.
std::vector<StbEntry> crawl(Recipes const& recipes) {
    std::map<std::string, StbEntry> found;
    for (auto const& [tag, byId] : recipes.mRecipes.get()) {
        for (auto const& [id, recipe] : byId) {
            if (!recipe || recipe->isShapeless() || recipe->isMultiRecipe()) continue;
            if (recipe->mTag->getString() != "crafting_table") continue;
            if (recipe->mWidth != 3 || recipe->mHeight != 1) continue;
            auto const& ings = recipe->mMyIngredients.get();
            if (ings.size() != 3) continue;
            auto const& results = recipe->getResultItems();
            if (results.empty() || results[0].isNull()) continue;
            auto slab = results[0].getTypeName();
            if (!isSlabItem(slab)) continue;
            auto a = nameOf(ings[0]);
            if (a.empty() || a == "minecraft:air" || nameOf(ings[1]) != a || nameOf(ings[2]) != a) continue;
            if (ings[1].getAuxValue() != ings[0].getAuxValue() || ings[2].getAuxValue() != ings[0].getAuxValue())
                continue;
            int  aux = std::max<int>(0, ings[0].getAuxValue());
            auto pre = slab.substr(slab.find(':') + 1);
            auto it  = found.find(slab);
            if (it != found.end()) {
                // several recipes make this slab: keep the block whose name is closest to the slab's,
                // then the shorter name (purpur_block, not purpur_pillar; Quark takes the first
                // item of Java's [purpur_block, purpur_pillar] ingredient)
                auto   cur = it->second.block.substr(it->second.block.find(':') + 1);
                auto   cand = a.substr(a.find(':') + 1);
                size_t pc = commonPrefix(cur, pre), pn = commonPrefix(cand, pre);
                if (pc > pn || (pc == pn && cur.size() <= cand.size())) continue;
            }
            auto path    = slab.substr(slab.find(':') + 1);
            found[slab] = StbEntry{slab, a, aux, std::string(kStbPrefix) + (slab.starts_with("minecraft:") ? "" : slab.substr(0, slab.find(':')) + "_") + path};
        }
    }
    std::vector<StbEntry> out;
    for (auto& [k, v] : found) out.push_back(std::move(v));
    return out;
}

bool loadShapeless(Recipes& recipes, StbEntry const& e) {
    auto doc = std::format(
        R"({{"format_version":"1.20.10","minecraft:recipe_shapeless":{{"description":{{"identifier":"{0}"}},)"
        R"("tags":["crafting_table"],"ingredients":[{{"item":"{1}"}},{{"item":"{1}"}}],"unlock":[{{"item":"{1}"}}],)"
        R"("result":{{"item":"{2}","data":{3},"count":1}},"priority":50}}}})",
        e.recipeId,
        e.slab,
        e.block,
        e.aux
    );
    Json::Reader reader;
    Json::Value  root;
    if (!reader.parse(doc, root, false)) return false;
    auto              mev = MinEngineVersion::fromString("1.20.10");
    SemVersion const& ver = mev.mSemVersion.get();
    Recipes::Buffers  buffers;
    Json::Value const& inner = root["minecraft:recipe_shapeless"];
    recipes._loadRecipe(e.recipeId, RecipeType::Shapeless, inner, mev, ver, false, buffers);
    if (!hasRecipe(recipes, e.recipeId)) {
        recipes._loadRecipe(e.recipeId, RecipeType::Shapeless, root, mev, ver, false, buffers);
    }
    if (!hasRecipe(recipes, e.recipeId)) return false;
    // clients craft by network id: the loader must have given one
    for (auto& [tag, byId] : recipes.mRecipes.get()) {
        auto it = byId.find(e.recipeId);
        if (it != byId.end() && it->second->mRecipeNetId->mRawId == 0)
            log().warn("vslabs: recipe {} has no network id", e.recipeId);
    }
    return true;
}

bool banned(std::string const& slab) {
    auto const& list = config().bannedSlabs;
    return std::find(list.begin(), list.end(), slab) != list.end();
}

} // namespace

bool hasRecipe(Recipes const& recipes, std::string_view id) {
    for (auto const& [tag, byId] : recipes.mRecipes.get()) {
        if (byId.find(std::string(id)) != byId.end()) return true;
    }
    return false;
}

void applyRecipes(Recipes& recipes) {
    auto& s = rs();
    if (s.owner != &recipes) {
        s       = RecipeState{};
        s.owner = &recipes;
    }
    // back to the full set first, so the result only depends on the current config
    for (auto const& r : s.removed) putBack(recipes, r);
    s.removed.clear();

    if (!s.crawled) {
        s.crawled = true;
        int added = 0;
        for (auto& e : crawl(recipes)) {
            if (hasRecipe(recipes, e.recipeId) || loadShapeless(recipes, e)) {
                s.stb.push_back(e);
                ++added;
            } else {
                log().warn("vslabs: could not add the slabs-to-blocks recipe {}", e.recipeId);
            }
        }
        log().info("vslabs: {} slabs-to-blocks recipes", added);
    }

    std::vector<std::pair<HashedString, std::string>> drop;
    for (auto const& e : s.stb) {
        if (!config().enableSlabsToBlocks || banned(e.slab)) {
            for (auto const& [tag, byId] : recipes.mRecipes.get()) {
                if (byId.contains(e.recipeId)) drop.emplace_back(tag, e.recipeId);
            }
        }
    }
    if (!config().enableVerticalSlabs) {
        for (auto const& [tag, byId] : recipes.mRecipes.get()) {
            for (auto const& [id, recipe] : byId) {
                if (recipe && usesVerticalSlab(*recipe)) drop.emplace_back(tag, id);
            }
        }
    }
    for (auto const& [tag, id] : drop) {
        if (auto r = take(recipes, tag, id)) s.removed.push_back(std::move(*r));
    }
    if (!drop.empty()) log().info("vslabs: {} recipes disabled", drop.size());
}

void reapplyRecipes() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& recipes = level->getRecipes();
    applyRecipes(recipes);
    // the payload goes straight into the packet (a copy made by our compiler would not match the
    // game's layout of the recipe payload types)
    CraftingDataPacket pkt(CraftingDataPacketPayload::fromRecipes(recipes, false));
    pkt.mClearRecipes = true;
    level->forEachPlayer([&](Player& p) {
        if (!p.isSimulated()) pkt.sendTo(p);
        return true;
    });
}

std::vector<std::string> slabsToBlocksRecipes() {
    std::vector<std::string> out;
    auto                     level = ll::service::getLevel();
    if (!level) return out;
    for (auto const& e : rs().stb) {
        if (hasRecipe(level->getRecipes(), e.recipeId)) out.push_back(e.recipeId);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> slabsToBlocksTable() {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto const& e : rs().stb) out.emplace_back(e.slab, e.block);
    return out;
}

void resetRecipes() { rs() = RecipeState{}; }

} // namespace lp::feature::vslabs
