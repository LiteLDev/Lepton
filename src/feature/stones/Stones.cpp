#include "feature/stones/Stones.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/world/item/CreativeItemInitializer.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/block/actor/component/IVanillaTickBlockActorComponent.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("stones", lp::feature::stones::StonesConfig);

namespace lp::feature::stones {

namespace detail {
#include "feature/stones/Content.inc"

GateEntry const* blocksBegin() { return std::begin(kBlockGates); }
GateEntry const* blocksEnd() { return std::end(kBlockGates); }
} // namespace detail

namespace {

namespace bs = game::blockstates;

constexpr std::string_view kLightState = "lepton:light";

StonesConfig const& cfg() { return config::get<StonesConfig>(); }

using GateIndex = std::unordered_map<std::string_view, unsigned>;
template <std::size_t N>
GateIndex makeIndex(detail::GateEntry const (&entries)[N]) {
    GateIndex m;
    for (auto const& e : entries) m.emplace(e.id, e.gate);
    return m;
}
GateIndex const& blockIndex() {
    static GateIndex const m = makeIndex(detail::kBlockGates);
    return m;
}
GateIndex const& recipeIndex() {
    static GateIndex const m = makeIndex(detail::kRecipeGates);
    return m;
}

/// Reads a bool from another unit's config file (`fallback` when the file or key is missing).
bool readOtherBool(nlohmann::json const& j, char const* key, bool fallback) {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}
nlohmann::json readOther(char const* section) {
    std::ifstream in(config::configDir() / (std::string(section) + ".json"));
    if (!in) return {};
    return nlohmann::json::parse(in, nullptr, false, true);
}

// ---- lamps ------------------------------------------------------------------------------------

struct PendingLamp {
    int      dim;
    BlockPos pos;
};
std::vector<PendingLamp>& pendingLamps() {
    static std::vector<PendingLamp> v;
    return v;
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    auto const& b = ev.newBlock();
    if (!isLamp(b.getTypeName())) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    if (bs::getInt(b, kLightState).value_or(lampLight()) == lampLight()) return;
    // the change may come from inside a cancellable event: finish it on the next tick
    pendingLamps().push_back({game::globalPos(region, ev.pos()).dim, ev.pos()});
}

void onTick(ll::event::ServerLevelTickEvent&) {
    if (pendingLamps().empty()) return;
    auto pending = std::move(pendingLamps());
    pendingLamps().clear();
    for (auto const& p : pending) {
        auto* region = game::regionOf(p.dim);
        if (!region) continue;
        auto const& b = region->getBlock(p.pos);
        if (!isLamp(b.getTypeName())) continue;
        if (auto const* n = bs::with(b, {{std::string(kLightState), lampLight()}}); n && n != &b)
            game::setBlock(*region, p.pos, *n);
    }
}

} // namespace

// ---- gates ------------------------------------------------------------------------------------

CrossUnit& crossUnit() {
    static CrossUnit c;
    return c;
}

void refreshCrossUnit() {
    auto vs                     = readOther("vslabs");
    auto st                     = readOther("storage");
    crossUnit().verticalSlabs   = readOtherBool(vs, "enableVerticalSlabs", true);
    crossUnit().smeltOreBlocks  = readOtherBool(st, "enableUtilityRecipes", true) && readOtherBool(st, "smeltRawOreBlocks", true);
}

bool gateEnabled(unsigned g) {
    auto const& c = cfg();
    auto const& x = crossUnit();
    if ((g & G_msv) && !c.enableMoreStoneVariants) return false;
    if ((g & G_bricks) && !c.enableBricks) return false;
    if ((g & G_chiseled) && !c.enableChiseledBricks) return false;
    if ((g & G_pillar) && !c.enablePillar) return false;
    if ((g & G_vslab) && !x.verticalSlabs) return false;
    if ((g & G_rawmetal) && !c.enableRawMetalBricks) return false;
    if ((g & G_smelt) && !x.smeltOreBlocks) return false;
    if ((g & G_sturdy) && !c.enableSturdyStone) return false;
    if ((g & G_lamps) && !c.enableCelebratoryLamps) return false;
    return true;
}

unsigned blockGate(std::string_view id) {
    auto const& m  = blockIndex();
    auto        it = m.find(id);
    return it == m.end() ? 0u : it->second;
}
unsigned recipeGate(std::string_view id) {
    auto const& m  = recipeIndex();
    auto        it = m.find(id);
    return it == m.end() ? 0u : it->second;
}
bool blockEnabled(std::string_view id) {
    auto g = blockGate(id);
    return g == 0 || gateEnabled(g);
}
bool recipeEnabled(std::string_view id) {
    auto g = recipeGate(id);
    return g == 0 || gateEnabled(g);
}
int blockCount() { return static_cast<int>(std::size(detail::kBlockGates)); }
int recipeCount() { return static_cast<int>(std::size(detail::kRecipeGates)); }

int  lampLight() { return std::clamp(cfg().lightLevel, 0, 15); }
bool isLamp(std::string_view id) { return id == "lepton:stone_lamp" || id == "lepton:stone_brick_lamp"; }

float rawBricksXp(std::string_view id) {
    if (id == "lepton:raw_iron_bricks" || id == "lepton:raw_copper_bricks") return 6.3f;
    if (id == "lepton:raw_gold_bricks") return 9.0f;
    return 0.f;
}

float smeltResultXp(std::string_view id) {
    // the game asks for the multiplier of the furnace's result; these blocks only come out of a
    // furnace from raw metal bricks (or, with U8, raw ore blocks: same values in Quark)
    if (id == "minecraft:iron_block" || id == "minecraft:copper_block") return 6.3f;
    if (id == "minecraft:gold_block") return 9.0f;
    return rawBricksXp(id);
}

// ---- hooks ------------------------------------------------------------------------------------

namespace {

// Disabled recipes are not loaded (Quark: recipe conditions on the module flags).
LL_TYPE_INSTANCE_HOOK(
    StonesLoadRecipeHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::_loadRecipe,
    bool,
    std::string const&        recipeId,
    ::RecipeType              type,
    Json::Value const&        objData,
    MinEngineVersion const&   minEngineVersion,
    SemVersion const&         formatVersion,
    bool                      isBaseGamePack,
    Recipes::Buffers&         buffers
) {
    if (!recipeEnabled(recipeId)) return true;
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

// Disabled blocks are left out of the creative inventory.
LL_TYPE_INSTANCE_HOOK(
    StonesCreativeHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addCreativeItemServer,
    void,
    CreativeGroupInfo& group,
    ItemInstance const& item
) {
    if (!item.isNull() && !blockEnabled(game::itemId(item))) return;
    origin(group, item);
}

// Raw metal bricks cook 9x slower (Quark cookingtime 1800 / 900): on 8 of 9 ticks the progress the
// furnace makes is taken back.
LL_TYPE_INSTANCE_HOOK(
    StonesFurnaceTickHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::$tick,
    void,
    BlockSource& region
) {
    // tick() overrides IVanillaTickBlockActorComponent::tick: the game passes that base subobject
    auto*       self  = thisFor<IVanillaTickBlockActorComponent>();
    auto const& input = self->getItem(0);
    auto const* item  = input.isNull() ? nullptr : input.mItem.get();
    // cheap test first (no allocation): every furnace runs this each tick
    if (!item || !item->mFullName->getString().starts_with("lepton:raw_") || rawBricksXp(item->mFullName->getString()) == 0.f
        || region.getLevel().isClientSide()) {
        origin(region);
        return;
    }
    bool slowTick = region.getLevel().getCurrentTick().tickID % kSmeltSlowdown != 0;
    if (!slowTick) {
        origin(region);
        return;
    }
    int& progress = self->mCookingProgress;
    int  before   = progress;
    progress      = before - 1; // the furnace adds 1 back while it cooks
    origin(region);
    if (progress > 0 && progress < before - 1) progress += 1; // decaying (no fuel): undo our step; a reset to 0 stays
    progress = std::max(progress, 0);
}

// Smelting XP of raw metal bricks (Quark: 6.3, gold 9).
LL_TYPE_STATIC_HOOK(
    StonesFurnaceXpHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::_getXPRewardMultiplier,
    float,
    ItemStackBase const& item
) {
    if (!item.isNull()) {
        if (float xp = smeltResultXp(game::itemId(item)); xp > 0.f) return xp;
    }
    return origin(item);
}

class StonesModule {
public:
    // Recipes and the creative inventory are built when a world loads; BDS loads the world before
    // install(), so these hooks go in at mod load.
    void load() {
        refreshCrossUnit();
        config::onReload([] { refreshCrossUnit(); });
        hookAll();
        auto const& c = cfg();
        auto        on = [](bool b) { return b ? "on" : "off"; };
        log().info(
            "stones: {} blocks, {} recipes; moreStoneVariants={} bricks={} chiseled={} pillar={} rawMetalBricks={} "
            "sturdyStone={} lamps={} (light {}) verticalSlabs={} smeltOreBlocks={}",
            blockCount(), recipeCount(), on(c.enableMoreStoneVariants), on(c.enableBricks), on(c.enableChiseledBricks),
            on(c.enablePillar), on(c.enableRawMetalBricks), on(c.enableSturdyStone), on(c.enableCelebratoryLamps),
            lampLight(), on(crossUnit().verticalSlabs), on(crossUnit().smeltOreBlocks)
        );
    }
    void install() {
        hookAll(); // again after a disable/enable cycle
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        pendingLamps().clear();
        if (mHooked) {
            StonesLoadRecipeHook::unhook();
            StonesCreativeHook::unhook();
            StonesFurnaceTickHook::unhook();
            StonesFurnaceXpHook::unhook();
            mHooked = false;
        }
    }

private:
    void hookAll() {
        if (mHooked) return;
        StonesLoadRecipeHook::hook();
        StonesCreativeHook::hook();
        StonesFurnaceTickHook::hook();
        StonesFurnaceXpHook::hook();
        mHooked = true;
    }

    bool                                mHooked = false;
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace lp::feature::stones

LP_REGISTER_MODULE(lp::feature::stones::StonesModule);
