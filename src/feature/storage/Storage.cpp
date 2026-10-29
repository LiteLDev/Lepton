#include "feature/storage/Storage.h"

#include <array>
#include <unordered_map>
#include <utility>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/CreativeItemInitializer.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"

#ifdef LP_CLIENT
#include "ll/api/event/world/ClientLevelTickEvent.h"
#endif

#include "game/BlockStates.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("storage", lp::feature::storage::StorageConfig);

namespace lp::feature::storage {

namespace {

struct GateEntry {
    std::string_view id;
    std::string_view gates;
};
struct PottedEntry {
    std::string_view block;
    std::string_view item;
};
#include "feature/storage/Content.inc"

namespace bs = game::blockstates;

using Flag = bool StorageConfig::*;
constexpr std::pair<std::string_view, Flag> kFlags[] = {
    {"enableCompressedBlocks",      &StorageConfig::enableCompressedBlocks     },
    {"burnsForever",                &StorageConfig::burnsForever               },
    {"enableCharcoalBlock",         &StorageConfig::enableCharcoalBlock        },
    {"enableSugarCaneBlock",        &StorageConfig::enableSugarCaneBlock       },
    {"enableCactusBlock",           &StorageConfig::enableCactusBlock          },
    {"enableChorusFruitBlock",      &StorageConfig::enableChorusFruitBlock     },
    {"enableStickBlock",            &StorageConfig::enableStickBlock           },
    {"enableAppleCrate",            &StorageConfig::enableAppleCrate           },
    {"enableGoldenAppleCrate",      &StorageConfig::enableGoldenAppleCrate     },
    {"enablePotatoCrate",           &StorageConfig::enablePotatoCrate          },
    {"enableCarrotCrate",           &StorageConfig::enableCarrotCrate          },
    {"enableGoldenCarrotCrate",     &StorageConfig::enableGoldenCarrotCrate    },
    {"enableBeetrootCrate",         &StorageConfig::enableBeetrootCrate        },
    {"enableCocoaBeanSack",         &StorageConfig::enableCocoaBeanSack        },
    {"enableNetherWartSack",        &StorageConfig::enableNetherWartSack       },
    {"enableGunpowderSack",         &StorageConfig::enableGunpowderSack        },
    {"enableBerrySack",             &StorageConfig::enableBerrySack            },
    {"enableGlowBerrySack",         &StorageConfig::enableGlowBerrySack        },
    {"enableBlazeLantern",          &StorageConfig::enableBlazeLantern         },
    {"enableBondedLeather",         &StorageConfig::enableBondedLeather        },
    {"enableBondedRabbitHide",      &StorageConfig::enableBondedRabbitHide     },
    {"enableStools",                &StorageConfig::enableStools               },
    {"enableRope",                  &StorageConfig::enableRope                 },
    {"forceEnableMoveTileEntities", &StorageConfig::forceEnableMoveTileEntities},
    {"enableDispenserBehavior",     &StorageConfig::enableDispenserBehavior    },
    {"enableShearVines",            &StorageConfig::enableShearVines           },
    {"enableMorePottedPlants",      &StorageConfig::enableMorePottedPlants     },
    {"enableGlassShard",            &StorageConfig::enableGlassShard           },
    {"enableUtilityRecipes",        &StorageConfig::enableUtilityRecipes       },
    {"betterStoneToolCrafting",     &StorageConfig::betterStoneToolCrafting    },
    {"enableDispenser",             &StorageConfig::enableDispenser            },
    {"enableRepeater",              &StorageConfig::enableRepeater             },
    {"enableMinecarts",             &StorageConfig::enableMinecarts            },
    {"enableChestBoats",            &StorageConfig::enableChestBoats           },
    {"logsToChests",                &StorageConfig::logsToChests               },
    {"coralToDye",                  &StorageConfig::coralToDye                 },
    {"bentRecipes",                 &StorageConfig::bentRecipes                },
    {"compostableToxins",           &StorageConfig::compostableToxins          },
    {"effectiveDragonBreath",       &StorageConfig::effectiveDragonBreath      },
    {"torchesBurn",                 &StorageConfig::torchesBurn                },
    {"boneMealUtility",             &StorageConfig::boneMealUtility            },
    {"charcoalToBlackDye",          &StorageConfig::charcoalToBlackDye         },
    {"easyHopper",                  &StorageConfig::easyHopper                 },
    {"easySticks",                  &StorageConfig::easySticks                 },
    {"smeltRawOreBlocks",           &StorageConfig::smeltRawOreBlocks          },
};

using Index = std::unordered_map<std::string_view, std::string_view>;
template <std::size_t N>
Index makeIndex(GateEntry const (&entries)[N]) {
    Index m;
    for (auto const& e : entries) m.emplace(e.id, e.gates);
    return m;
}
Index const& contentIndex() {
    static Index const m = makeIndex(kContentGates);
    return m;
}
Index const& recipeIndex() {
    static Index const m = makeIndex(kRecipeGates);
    return m;
}

std::vector<std::function<void()>>& deferred() {
    static std::vector<std::function<void()>> v;
    return v;
}

/// Recipes whose ingredients belong to another unit (U3's stones) are only loaded with that unit.
bool ingredientsPresent(std::string_view recipeId) {
    constexpr std::string_view kStoneTool = "lepton:storage_stone_";
    if (!recipeId.starts_with(kStoneTool)) return true;
    auto at = recipeId.find("_from_");
    if (at == std::string_view::npos) return true;
    auto material = recipeId.substr(at + 6);
    for (auto other : {"limestone", "jasper", "shale", "permafrost"}) {
        if (material == other) return !game::makeItem(std::string("lepton:") + other).isNull();
    }
    return true;
}

// Disabled recipes are not loaded (Quark: recipe conditions on the module flags).
LL_TYPE_INSTANCE_HOOK(
    StorageLoadRecipeHook,
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
    if (!recipeEnabled(recipeId)) return true; // as if loaded: the recipe simply does not exist
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

// Disabled content is left out of the creative inventory.
LL_TYPE_INSTANCE_HOOK(
    StorageCreativeHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addCreativeItemServer,
    void,
    CreativeGroupInfo&  group,
    ItemInstance const& item
) {
    if (!item.isNull() && !contentEnabled(game::itemId(item))) return;
    origin(group, item);
}

void setProperty(std::string_view id, BlockProperty prop, bool on) {
    auto type = BlockType::tryGetFromRegistry(HashedString(std::string(id)));
    if (!type) return;
    auto& t   = const_cast<BlockType&>(*type);
    auto  cur = static_cast<uint64_t>(t.mProperties);
    auto  bit = static_cast<uint64_t>(prop);
    auto  nxt = on ? (cur | bit) : (cur & ~bit);
    if (nxt != cur) t.mProperties = static_cast<BlockProperty>(nxt);
}

void onTick(ll::event::ServerLevelTickEvent&) {
    if (deferred().empty()) return;
    auto work = std::move(deferred());
    deferred().clear();
    for (auto& fn : work) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("storage: deferred action failed: {}", e.what());
        }
    }
}

} // namespace

// ---- config / gates ---------------------------------------------------------------------------

StorageConfig const& cfg() { return config::get<StorageConfig>(); }

bool flag(std::string_view key) {
    for (auto const& [name, member] : kFlags)
        if (name == key) return cfg().*member;
    return false;
}

bool gatesOn(std::string_view keys) {
    while (!keys.empty()) {
        auto comma = keys.find(',');
        auto key   = keys.substr(0, comma);
        if (!key.empty() && !flag(key)) return false;
        if (comma == std::string_view::npos) break;
        keys.remove_prefix(comma + 1);
    }
    return true;
}

bool contentEnabled(std::string_view id) {
    auto const& m  = contentIndex();
    auto        it = m.find(id);
    return it == m.end() || gatesOn(it->second);
}

bool recipeEnabled(std::string_view recipeId) {
    if (recipeId == "minecraft:nether_wart_block") {
        // the sack takes the 3x3 pattern; 4 nether wart make the block instead
        return !(cfg().enableCompressedBlocks && cfg().enableNetherWartSack);
    }
    auto const& m  = recipeIndex();
    auto        it = m.find(recipeId);
    if (it == m.end()) return true;
    return gatesOn(it->second) && ingredientsPresent(recipeId);
}

std::vector<std::string_view> contentIds() {
    std::vector<std::string_view> v;
    for (auto const& e : kContentGates) v.push_back(e.id);
    return v;
}

std::vector<std::string_view> recipeIds() {
    std::vector<std::string_view> v;
    for (auto const& e : kRecipeGates) v.push_back(e.id);
    return v;
}

bool hasTag(Block const& block, std::string_view tag) {
    return block.getBlockType().hasTag(HashedString(std::string(tag)));
}

void defer(std::function<void()> fn) { deferred().push_back(std::move(fn)); }

void giveOrDrop(Player& player, ItemStack const& item) {
    if (item.isNull()) return;
    ItemStack rest = item;
    game::inv::insert(game::inv::playerInventory(player), rest, game::inv::kPlayerAll);
    game::inv::sync(player);
    if (rest.mCount > 0) {
        player.getLevel().getSpawner().spawnItem(player.getDimensionBlockSource(), rest, nullptr, player.getPosition(), 0);
    }
}

void applyBlockProperties() {
    auto const& c = cfg();
    setProperty("lepton:rope", BlockProperty::Climbable, true);
    setProperty("lepton:cut_vine", BlockProperty::Climbable, true);
    setProperty("lepton:charcoal_block", BlockProperty::InfiniBurn, c.burnsForever && contentEnabled("lepton:charcoal_block"));
    setProperty("lepton:blaze_lantern", BlockProperty::InfiniBurn, c.burnsForever && contentEnabled("lepton:blaze_lantern"));
    // Quark: requiresCorrectToolForDrops (a pickaxe)
    if (auto type = BlockType::tryGetFromRegistry(HashedString("lepton:charcoal_block")))
        const_cast<BlockType&>(*type).mRequiresCorrectToolForDrops = true;
    stools::applyTypes();
}

// ---- module -----------------------------------------------------------------------------------

namespace {

class StorageModule {
public:
    // Recipes and the creative inventory are built when a world loads; BDS loads the world before
    // install(), so these hooks go in at mod load.
    void load() {
        hookAll();
        static bool once = false;
        if (!once) {
            once = true;
            config::onReload([] {
                applyBlockProperties();
                applyDragonBreath();
            });
            game::db::onWorldReady([] {
                applyBlockProperties();
                applyDragonBreath();
                auto const& c  = cfg();
                auto        on = [](bool b) { return b ? "on" : "off"; };
                log().info(
                    "storage: ready ({} blocks/items, {} recipes); compressed={} stools={} rope={} shearVines={} "
                    "pottedPlants={} glassShard={} utilityRecipes={}",
                    std::size(kContentGates),
                    std::size(kRecipeGates),
                    on(c.enableCompressedBlocks),
                    on(c.enableStools),
                    on(c.enableRope),
                    on(c.enableShearVines),
                    on(c.enableMorePottedPlants),
                    on(c.enableGlassShard),
                    on(c.enableUtilityRecipes)
                );
            });
            game::db::onWorldClosed([] { deferred().clear(); });
        }
    }
    void install() {
        hookAll();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
#ifdef LP_CLIENT
        // a client joining a remote server gets the server's blocks at join: apply the block
        // properties (climbing) once they exist
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](ll::event::ClientLevelTickEvent&) {
            static unsigned n = 0;
            if (n++ % 100 == 0) applyBlockProperties();
        }));
#endif
        installCompressed();
        stools::install();
        rope::install();
        plants::install();
        if (ll::service::getLevel()) {
            applyBlockProperties();
            applyDragonBreath();
        }
    }
    void uninstall() {
        plants::uninstall();
        rope::uninstall();
        stools::uninstall();
        uninstallCompressed();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        deferred().clear();
        if (mHooked) {
            StorageLoadRecipeHook::unhook();
            StorageCreativeHook::unhook();
            mHooked = false;
        }
    }

private:
    void hookAll() {
        if (mHooked) return;
        StorageLoadRecipeHook::hook();
        StorageCreativeHook::hook();
        mHooked = true;
    }

    bool                                mHooked = false;
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

std::string plants::plantOf(std::string_view blockId) {
    for (auto const& e : kPotted)
        if (e.block == blockId) return std::string(e.item);
    return {};
}

std::string plants::pottedFor(std::string_view itemId) {
    for (auto const& e : kPotted)
        if (e.item == itemId) return std::string(e.block);
    return {};
}

} // namespace lp::feature::storage

LP_REGISTER_MODULE(lp::feature::storage::StorageModule);
