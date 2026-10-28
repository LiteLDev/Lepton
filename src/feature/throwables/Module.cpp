// U22 throwables: event wiring, shared hooks, recipe/creative gating and the per-tick driver.

#include "feature/throwables/Throwables.h"

#include <fstream>
#include <map>
#include <set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/CreativeItemInitializer.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/Level.h"

#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::throwables {

namespace {

// ---- gates ------------------------------------------------------------------------------------

/// A boolean of another unit's config file (default when the file or key is missing).
bool otherUnitBool(char const* section, char const* key, bool fallback) {
    try {
        std::ifstream in(config::configDir() / (std::string(section) + ".json"));
        if (!in) return fallback;
        auto j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) return fallback;
        return j[key].get<bool>();
    } catch (...) {
        return fallback;
    }
}

bool heartRecipe() {
    auto const& c = cfg();
    if (!c.enablePickarang || c.neverUseHeartOfDiamond) return false;
    if (!otherUnitBool("mobsdeep", "enableStonelings", true)) return false;
    return !game::isEmpty(game::makeItem("lepton:diamond_heart"));
}

bool recipeEnabled(std::string_view id) {
    auto const& c = cfg();
    if (id == "lepton:pickarang_heart") return heartRecipe();
    if (id == "lepton:pickarang_no_heart") return c.enablePickarang && !heartRecipe();
    if (id == "lepton:flamerang_smithing") return c.enablePickarang && c.enableFlamerang;
    if (id == "lepton:torch_arrow") return c.enableTorchArrow;
    return true;
}

/// False for items of a disabled module.
bool itemEnabled(std::string_view id) {
    auto const& c = cfg();
    if (id == kPickarang) return c.enablePickarang;
    if (id == kFlamerang) return c.enablePickarang && c.enableFlamerang;
    if (id == kTorchArrow) return c.enableTorchArrow;
    if (id.starts_with("lepton:egg_parrot_")) return c.enableParrotEggs;
    if (id == kBottledCloud || id == kCloud) return c.enableBottledCloud;
    if (id == kSlimeBucket || id == kSlimeBucketHot) return c.enableSlimeInABucket;
    return true;
}

LL_TYPE_INSTANCE_HOOK(
    ThrowablesLoadRecipeHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::_loadRecipe,
    bool,
    std::string const&      recipeId,
    ::RecipeType            type,
    Json::Value const&      objData,
    MinEngineVersion const& minEngineVersion,
    SemVersion const&       formatVersion,
    bool                    isBaseGamePack,
    Recipes::Buffers&       buffers
) {
    if (!recipeEnabled(recipeId)) return true;
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

LL_TYPE_INSTANCE_HOOK(
    ThrowablesCreativeHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addCreativeItemServer,
    void,
    CreativeGroupInfo&  group,
    ItemInstance const& item
) {
    if (!item.isNull() && !itemEnabled(game::itemId(item))) return;
    origin(group, item);
}

// ---- entity interaction (parrot feeding, slime buckets) ---------------------------------------

std::map<int64_t, uint64_t>& lastInteract() {
    static std::map<int64_t, uint64_t> m;
    return m;
}
uint64_t& ticks() {
    static uint64_t t = 0;
    return t;
}

LL_TYPE_INSTANCE_HOOK(
    ThrowablesInteractHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$interact,
    bool,
    Actor&        entity,
    Vec3 const&   location,
    ::HandSlot    handSlot
) {
    Player& player = this->mPlayer;
    if (!player.isClientSide()) {
        auto pid = player.getOrCreateUniqueID().rawID;
        auto it  = lastInteract().find(pid);
        bool recent = it != lastInteract().end() && ticks() - it->second < 2;
        if (!recent) {
            if (parroteggs::onInteract(player, entity) || slime::onInteract(player, entity)) {
                lastInteract()[pid] = ticks();
                return true;
            }
        } else if (entity.getTypeName() == "minecraft:parrot" || entity.getTypeName() == "minecraft:slime") {
            return true;
        }
    }
    return origin(entity, location, handSlot);
}

// ---- events -------------------------------------------------------------------------------------

std::map<int64_t, uint64_t>& lastThrow() {
    static std::map<int64_t, uint64_t> m;
    return m;
}

bool throwRang(Player& player, ItemStack& item) {
    if (!pickarang::isRang(game::itemId(item))) return false;
    auto pid = player.getOrCreateUniqueID().rawID;
    if (auto it = lastThrow().find(pid); it != lastThrow().end() && ticks() - it->second < 3) return true;
    lastThrow()[pid] = ticks();
    return pickarang::onUse(player, item);
}

void onUseItem(ll::event::PlayerUseItemEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& item = ev.item();
    if (throwRang(player, item) || cloud::onUse(player, item)) ev.cancel();
}

void onInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& item = ev.item();
    int   face = static_cast<int>(ev.face());
    if (throwRang(player, item) || cloud::onUseOnBlock(player, item, ev.blockPos(), face)
        || slime::onUseOnBlock(player, item, ev.blockPos(), face)) {
        ev.cancel();
    }
}

void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    skullpikes::onPlaced(player, ev.pos());
}

void onTick(ll::event::ServerLevelTickEvent&) {
    ++ticks();
    pickarang::tick();
    torcharrow::tickTrails();
    parroteggs::tick();
    skullpikes::tick();
    cloud::tick();
    slime::tick();
    if (ticks() % 20 == 0) purgeDisabled();
    if (ticks() % 200 == 0) {
        std::erase_if(lastInteract(), [](auto const& kv) { return ticks() - kv.second > 100; });
        std::erase_if(lastThrow(), [](auto const& kv) { return ticks() - kv.second > 100; });
    }
}

class ThrowablesModule {
public:
    // Recipes and the creative inventory are built when a world loads (BDS: before install()).
    void load() { hookEarly(); }

    void install() {
        hookEarly();
        ThrowablesInteractHook::hook();
        pickarang::install();
        torcharrow::install();
        parroteggs::install();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>(onUseItem));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteractBlock));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] {
                parroteggs::onWorldReady();
                skullpikes::onWorldReady();
                cloud::onWorldReady();
            });
            game::db::onWorldSave([] {
                parroteggs::onWorldSave();
                skullpikes::onWorldSave();
                cloud::onWorldSave();
            });
            game::db::onWorldClosed([] {
                pickarang::onWorldClosed();
                parroteggs::onWorldClosed();
                skullpikes::onWorldClosed();
                cloud::onWorldClosed();
            });
        }
        if (game::db::available()) {
            parroteggs::onWorldReady();
            skullpikes::onWorldReady();
            cloud::onWorldReady();
        }
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        if (game::db::available()) {
            parroteggs::onWorldSave();
            skullpikes::onWorldSave();
            cloud::onWorldSave();
        }
        pickarang::uninstall();
        torcharrow::uninstall();
        parroteggs::uninstall();
        ThrowablesInteractHook::unhook();
        if (mEarly) {
            ThrowablesLoadRecipeHook::unhook();
            ThrowablesCreativeHook::unhook();
            mEarly = false;
        }
    }

private:
    void hookEarly() {
        if (mEarly) return;
        ThrowablesLoadRecipeHook::hook();
        ThrowablesCreativeHook::hook();
        mEarly = true;
    }

    bool                                mEarly = false;
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

void purgeDisabled() {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([](Player& p) {
        auto& pinv    = game::inv::playerInventory(p);
        bool  changed = false;
        for (int i = 0; i < pinv.getContainerSize(); ++i) {
            auto const& s = pinv.getItem(i);
            if (!game::isEmpty(s) && !itemEnabled(game::itemId(s))) {
                pinv.setItem(i, ItemStack{});
                changed = true;
            }
        }
        if (auto const& off = p.getOffhandSlot(); !game::isEmpty(off) && !itemEnabled(game::itemId(off))) {
            p.setOffhandSlot(ItemStack{});
            changed = true;
        }
        if (changed) p.refreshInventory();
        return true;
    });
}

namespace testing {
bool recipeOn(std::string_view id) { return recipeEnabled(id); }
bool itemOn(std::string_view id) { return itemEnabled(id); }
} // namespace testing

} // namespace lp::feature::throwables

LP_REGISTER_MODULE(lp::feature::throwables::ThrowablesModule);
