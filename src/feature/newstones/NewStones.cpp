// U3 newstones: config, content gates, myalite tint, mason trades, commands (docs/spec/newstones.md).
// World generation lives in WorldGen.cpp.

#include "feature/newstones/NewStones.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <numbers>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/entity/components_json_legacy/EconomyTradeableComponent.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/CreativeItemInitializer.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/item/trading/MerchantRecipeList.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "feature/newstones/Noise.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("newstones", lp::feature::newstones::NewStonesConfig);

namespace lp::feature::newstones {

namespace detail {
struct GateEntry {
    std::string_view id;
    unsigned         gate;
};
#include "feature/newstones/Content.inc"
} // namespace detail

// ---- config defaults ---------------------------------------------------------------------------------

BigStoneClusterConfig bigStoneDefaults(std::vector<std::string> tags) {
    BigStoneClusterConfig c;
    c.biomes.tags.isBlacklist   = false;
    c.biomes.tags.tags          = std::move(tags);
    c.biomes.biomes.isBlacklist = true;
    return c;
}

BigStoneClusterConfig myaliteClusterDefaults() {
    BigStoneClusterConfig c;
    c.generateInAir             = true;
    c.dimensions.dimensions     = {"minecraft:the_end"};
    c.biomes.tags.isBlacklist   = true; // no tags: every biome passes this half
    c.biomes.biomes.isBlacklist = false;
    c.biomes.biomes.biomes      = {"minecraft:end_highlands"};
    c.horizontalSize            = 20;
    c.verticalSize              = 40;
    c.horizontalVariation       = 6;
    c.verticalVariation         = 10;
    c.rarity                    = 100;
    c.minYLevel                 = 58;
    c.maxYLevel                 = 62;
    return c;
}

StoneTypeConfig endStoneTypeDefaults() {
    StoneTypeConfig c;
    c.dimensions.dimensions = {"minecraft:the_end"};
    return c;
}

UndergroundStyleConfig permafrostDefaults() {
    UndergroundStyleConfig c;
    c.biomes.tags.isBlacklist   = true;
    c.biomes.biomes.isBlacklist = false;
    c.biomes.biomes.biomes      = {"minecraft:frozen_peaks"};
    return c;
}

NewStonesConfig const& cfg() { return config::get<NewStonesConfig>(); }

// ---- gates -------------------------------------------------------------------------------------------

namespace {

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
std::unordered_set<std::string_view> const& tintedSet() {
    static std::unordered_set<std::string_view> const s(std::begin(detail::kTintedBlocks), std::end(detail::kTintedBlocks));
    return s;
}

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

} // namespace

CrossUnit& crossUnit() {
    static CrossUnit c;
    return c;
}

void refreshCrossUnit() {
    auto st                       = readOther("stones");
    auto vs                       = readOther("vslabs");
    crossUnit().moreStoneVariants = readOtherBool(st, "enableMoreStoneVariants", true);
    crossUnit().bricks            = readOtherBool(st, "enableBricks", true);
    crossUnit().chiseledBricks    = readOtherBool(st, "enableChiseledBricks", true);
    crossUnit().pillar            = readOtherBool(st, "enablePillar", true);
    crossUnit().verticalSlabs     = readOtherBool(vs, "enableVerticalSlabs", true);
}

bool stoneEnabled(std::string_view stone) {
    auto const& c = cfg();
    if (!c.enableNewStoneTypes) return false;
    if (stone == "limestone") return c.enableLimestone;
    if (stone == "jasper") return c.enableJasper;
    if (stone == "shale") return c.enableShale;
    if (stone == "myalite") return c.enableMyalite;
    return false;
}

bool gateEnabled(unsigned g) {
    auto const& c = cfg();
    auto const& x = crossUnit();
    if ((g & G_limestone) && !stoneEnabled("limestone")) return false;
    if ((g & G_jasper) && !stoneEnabled("jasper")) return false;
    if ((g & G_shale) && !stoneEnabled("shale")) return false;
    if ((g & G_myalite) && !stoneEnabled("myalite")) return false;
    if ((g & G_permafrost) && !c.enablePermafrost) return false;
    if ((g & G_msv) && !x.moreStoneVariants) return false;
    if ((g & G_bricks) && !x.bricks) return false;
    if ((g & G_chiseled) && !x.chiseledBricks) return false;
    if ((g & G_pillar) && !x.pillar) return false;
    if ((g & G_vslab) && !x.verticalSlabs) return false;
    return true;
}

unsigned blockGate(std::string_view id) {
    auto it = blockIndex().find(id);
    return it == blockIndex().end() ? 0u : it->second;
}
unsigned recipeGate(std::string_view id) {
    auto it = recipeIndex().find(id);
    return it == recipeIndex().end() ? 0u : it->second;
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
std::vector<std::string_view> blockIds() {
    std::vector<std::string_view> v;
    for (auto const& e : detail::kBlockGates) v.push_back(e.id);
    return v;
}

// ---- myalite tint -----------------------------------------------------------------------------------

int tintSteps() { return detail::kTintSteps; }
int defaultTint() { return detail::kDefaultTint; }

double myaliteHue(int x, int y, int z) {
    // MyaliteColorLogic: float arithmetic as Java, then double noise
    static PerlinSimplexNoise const noise(4543543, -4, 4);
    auto const&     m   = MthTable::get();
    constexpr float sp  = 0.15f;
    constexpr float kPi = 3.1415927f; // Mth.PI
    float           fx  = static_cast<float>(x) * sp;
    float           fy  = static_cast<float>(y) * sp;
    float           fz  = static_cast<float>(z) * sp;
    float           xvf = fx + m.sin(fz) * 2.0f;
    float           zvf = fz + m.cos(fx) * 2.0f;
    float           yvf = fy + m.sin(fy + kPi / 4.0f) * 2.0f;
    double          xv = xvf, zv = zvf, yv = yvf;
    double          n     = noise.getValue(xv + yv, zv + (yv * 2), false);
    double const    range = 0.3, shift = 0.05;
    return n * (range / 2) - range + shift;
}

uint32_t myaliteColor(int x, int y, int z) { return hsbToRgb(static_cast<float>(myaliteHue(x, y, z)), 0.7f, 0.8f); }

int tintStep(int x, int y, int z) {
    double h = myaliteHue(x, y, z);
    int    s = static_cast<int>(std::floor((h + 0.4) / 0.3 * detail::kTintSteps));
    return std::clamp(s, 0, detail::kTintSteps - 1);
}

bool isTinted(std::string_view id) { return tintedSet().contains(id); }

Block const* retinted(Block const& block, BlockPos const& pos) {
    if (!isTinted(block.getTypeName())) return nullptr;
    int want = tintStep(pos.x, pos.y, pos.z);
    if (game::blockstates::getInt(block, kTintState).value_or(-1) == want) return nullptr;
    auto const* b = game::blockstates::with(block, {{std::string(kTintState), want}});
    return b == &block ? nullptr : b;
}

namespace {

// Block changes of tinted blocks on the server thread are fixed on the next tick (the change may
// come from a cancellable event); world generation threads fix them at once.
struct PendingTint {
    int      dim;
    BlockPos pos;
};
std::mutex                   gPendingMutex;
std::vector<PendingTint>     gPending;
std::atomic<std::thread::id> gServerThread{};
thread_local bool            tlRetinting = false;

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (tlRetinting) return;
    auto const& b = ev.newBlock();
    if (!isTinted(b.getTypeName())) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    // the server thread (or, before its first tick, any thread: the changes of the level load are
    // fixed on the first tick)
    auto server = gServerThread.load();
    if (server == std::thread::id{} || std::this_thread::get_id() == server) {
        if (!retinted(b, ev.pos())) return;
        std::lock_guard lock(gPendingMutex);
        gPending.push_back({game::globalPos(region, ev.pos()).dim, ev.pos()});
        return;
    }
    // another thread: world generation (e.g. another unit placing myalite); fix it in place
    if (auto const* n = retinted(b, ev.pos())) {
        tlRetinting = true;
        region.setBlock(ev.pos(), *n, 2, nullptr, BlockChangeContext{});
        tlRetinting = false;
    }
}

void onTick(ll::event::ServerLevelTickEvent&) {
    gServerThread.store(std::this_thread::get_id());
    std::vector<PendingTint> pending;
    {
        std::lock_guard lock(gPendingMutex);
        if (gPending.empty()) return;
        pending.swap(gPending);
    }
    for (auto const& p : pending) {
        auto* region = game::regionOf(p.dim);
        if (!region) continue;
        auto const& b = region->getBlock(p.pos);
        if (auto const* n = retinted(b, p.pos)) {
            tlRetinting = true;
            game::setBlock(*region, p.pos, *n);
            tlRetinting = false;
        }
    }
}

} // namespace

GenerationScope::GenerationScope() : mPrev(tlRetinting) { tlRetinting = true; }
GenerationScope::~GenerationScope() { tlRetinting = mPrev; }

// ---- mason trades ------------------------------------------------------------------------------------

namespace {

struct MasonOffer {
    std::string buy;
    int         buyCount;
    std::string sell;
    int         sellCount;
    uint        xp;
};

std::vector<MasonOffer> masonOffers() {
    std::vector<MasonOffer> v;
    auto const&             c = cfg();
    if (!c.addNewStonesToMasonTrades || !c.enableNewStoneTypes) return v;
    for (auto s : {"limestone", "jasper", "shale"}) {
        if (!stoneEnabled(s)) continue;
        // VillagerTrades.ItemsForEmeralds(block, 1, 4, 16, 10) and EmeraldForItems(polished, 16, 16, 20)
        v.push_back({"minecraft:emerald", 1, std::string("lepton:") + s, 4, 10});
        v.push_back({std::string("lepton:polished_") + s, 16, "minecraft:emerald", 1, 20});
    }
    return v;
}

MerchantRecipe makeRecipe(MasonOffer const& o) {
    ItemInstance   buy(game::makeItem(o.buy, o.buyCount));
    ItemInstance   sell(game::makeItem(o.sell, o.sellCount));
    MerchantRecipe r(buy, sell);
    r.mTier             = 2; // journeyman (Java level 3)
    r.mUses             = 0;
    r.mMaxUses          = 16;
    r.mTraderExp        = o.xp;
    r.mRewardExp        = true;
    r.mBuyCountA        = o.buyCount;
    r.mBuyCountB        = 0;
    r.mPriceMultiplierA = 0.05f;
    r.mPriceMultiplierB = 0.05f;
    r.mDemand           = 0;
    return r;
}

constexpr int kVanillaJourneymanMasonOffers = 7; // Java: 3 stones bought, 4 blocks sold

} // namespace

int masonOfferCount() { return static_cast<int>(masonOffers().size()); }

int applyMasonTrades(std::vector<MerchantRecipe>& recipes, size_t firstNew, std::function<double()> const& rand01) {
    auto offers = masonOffers();
    if (offers.empty()) return 0;
    // already applied (a list generated again keeps its offers)
    for (auto const& r : recipes)
        for (auto const& o : offers)
            if (game::itemId(*r.mSell) == o.sell && game::itemId(*r.mBuyA) == o.buy) return 0;
    double const        chance = static_cast<double>(offers.size()) / (kVanillaJourneymanMasonOffers + offers.size());
    std::vector<size_t> replace;
    std::vector<size_t> pick;
    std::vector<size_t> unused(offers.size());
    for (size_t i = 0; i < unused.size(); ++i) unused[i] = i;
    for (size_t i = firstNew; i < recipes.size(); ++i) {
        if (recipes[i].mTier != 2 || unused.empty()) continue;
        if (rand01() >= chance) continue;
        size_t k = std::min(unused.size() - 1, static_cast<size_t>(rand01() * static_cast<double>(unused.size())));
        replace.push_back(i);
        pick.push_back(unused[k]);
        unused.erase(unused.begin() + static_cast<std::ptrdiff_t>(k));
    }
    if (replace.empty()) return 0;
    // MerchantRecipe has no exported assignment: rebuild the list
    std::vector<MerchantRecipe> out;
    out.reserve(recipes.size());
    size_t next = 0;
    for (size_t i = 0; i < recipes.size(); ++i) {
        if (next < replace.size() && replace[next] == i) out.push_back(makeRecipe(offers[pick[next++]]));
        else out.push_back(recipes[i]);
    }
    recipes.swap(out);
    return static_cast<int>(replace.size());
}

// ---- hooks -------------------------------------------------------------------------------------------

namespace {

LL_TYPE_INSTANCE_HOOK(
    NewStonesLoadRecipeHook,
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
    NewStonesCreativeHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addCreativeItemServer,
    void,
    CreativeGroupInfo&  group,
    ItemInstance const& item
) {
    if (!item.isNull() && !blockEnabled(game::itemId(item))) return;
    origin(group, item);
}

std::mt19937& tradeRng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

LL_TYPE_INSTANCE_HOOK(
    NewStonesTradesHook,
    ll::memory::HookPriority::Normal,
    EconomyTradeableComponent,
    &EconomyTradeableComponent::_generateTrades,
    bool
) {
    auto*  before = mOffers.get();
    size_t first  = before ? before->mRecipeList->size() : 0;
    bool   r      = origin();
    try {
        auto* offers = mOffers.get();
        if (offers && offers != before) first = 0; // a new list
        if (offers && mGeneratedTablePath->find("stone_mason") != std::string::npos) {
            std::uniform_real_distribution<double> d(0.0, 1.0);
            int n = applyMasonTrades(*offers->mRecipeList, first, [&] { return d(tradeRng()); });
            if (n) log().info("newstones: added {} new stone trade(s) to a mason", n);
        }
    } catch (...) {}
    return r;
}

std::string text(Player* p, std::string_view key, std::string_view fallback) {
    return p ? trFor(*p, key, fallback) : tr(key, fallback);
}

void printInfo(Player* p, CommandOutput& out) {
    auto const& c       = cfg();
    auto        section = [&](std::string_view name, bool on, std::vector<std::string> lines) {
        out.success(p ? trFor(*p, "lp.newstones.info.header", "New stones ({}):", name)
                             : tr("lp.newstones.info.header", "New stones ({}):", name));
        if (!on) lines = {text(p, "lp.newstones.info.disabled", "This module is disabled.")};
        for (auto const& l : lines) out.success("  " + l);
    };
    std::vector<std::string> stones;
    if (c.enableLimestone)
        stones.push_back(text(p, "quark.jei.hint.limestone", "Can be found in big clusters in swamps and oceans."));
    if (c.enableJasper)
        stones.push_back(text(p, "quark.jei.hint.jasper", "Can be found in big clusters in mesas and deserts."));
    if (c.enableShale) stones.push_back(text(p, "quark.jei.hint.shale", "Can be found in big clusters in cold biomes."));
    if (c.enableMyalite)
        stones.push_back(text(
            p,
            "quark.jei.hint.myalite",
            "Can be found in big clusters in the outer end highlands. It reacts to an invisible field in the world, "
            "changing color depending on where it's placed with the field's intensity."
        ));
    section("NewStoneTypes", c.enableNewStoneTypes, stones);
    section(
        "Permafrost",
        c.enablePermafrost,
        {text(p, "quark.jei.hint.permafrost", "Can be found towards the top of Frozen Peaks.")}
    );
}

void printSources(Player& p, int radius, CommandOutput& out) {
    auto& region = p.getDimensionBlockSource();
    auto  pos    = p.getFeetBlockPos();
    int   dim    = game::globalPos(region, pos).dim;
    auto  list   = sourcesNear(dim, pos.x >> 4, pos.z >> 4, radius);
    if (list.empty()) {
        out.success(trFor(p, "lp.newstones.sources.none", "No cluster sources within {} chunks.", radius));
        return;
    }
    std::sort(list.begin(), list.end(), [&](SourceInfo const& a, SourceInfo const& b) {
        auto d = [&](SourceInfo const& s) {
            return (int64_t)(s.x - pos.x) * (s.x - pos.x) + (int64_t)(s.z - pos.z) * (s.z - pos.z);
        };
        return d(a) < d(b);
    });
    for (auto const& s : list)
        out.success(trFor(p, "lp.newstones.sources.line", "{} at {} {} {} ({})", s.name, s.x, s.y, s.z, s.biome));
}

class NewStonesModule {
public:
    // Recipes, the creative inventory and the first chunks are made while a world loads; BDS loads
    // the world before install(), so the hooks go in at mod load.
    void load() {
        refreshCrossUnit();
        config::onReload([] {
            refreshCrossUnit();
            rebuildGenPlan();
        });
        game::db::onWorldReady([] { rebuildGenPlan(); });
        game::db::onWorldClosed([] {
            invalidateGenPlan();
            std::lock_guard lock(gPendingMutex);
            gPending.clear();
        });
        hookAll();
        auto const& c  = cfg();
        auto        on = [](bool b) { return b ? "on" : "off"; };
        log().info(
            "newstones: {} blocks, {} recipes; newStoneTypes={} (limestone={} jasper={} shale={} myalite={}) "
            "permafrost={} bigStoneClusters={} vanillaStoneClusters={} masonTrades={}",
            blockCount(),
            recipeCount(),
            on(c.enableNewStoneTypes),
            on(c.enableLimestone),
            on(c.enableJasper),
            on(c.enableShale),
            on(c.enableMyalite),
            on(c.enablePermafrost),
            on(c.enableBigStoneClusters),
            on(c.enableVanillaStoneClusters),
            on(c.addNewStonesToMasonTrades)
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
        {
            std::lock_guard lock(gPendingMutex);
            gPending.clear();
        }
        if (mHooked) {
            NewStonesLoadRecipeHook::unhook();
            NewStonesCreativeHook::unhook();
            NewStonesTradesHook::unhook();
            uninstallWorldGen();
            mHooked = false;
        }
    }

private:
    void hookAll() {
        if (mHooked) return;
        NewStonesLoadRecipeHook::hook();
        NewStonesCreativeHook::hook();
        NewStonesTradesHook::hook();
        installWorldGen();
        mHooked = true;
    }

    bool                                mHooked = false;
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace

} // namespace lp::feature::newstones

LP_REGISTER_MODULE(lp::feature::newstones::NewStonesModule);

struct NewStonesSourcesParam {
    int radius = 8;
};

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace lp::feature::newstones;
    cmd.overload().text("newstones").text("info").execute([](CommandOrigin const& origin, CommandOutput& out) {
        printInfo(lp::commands::playerOf(origin), out);
    });
    auto sources = [](CommandOrigin const& origin, CommandOutput& out, int radius) {
        auto* p = lp::commands::playerOf(origin);
        if (!p) return out.error(lp::tr("lp.newstones.players_only", "Only players can use this command."));
        if (!lp::commands::isAdmin(origin))
            return out.error(lp::trFor(*p, "lp.newstones.admin_only", "You need operator permission for this command."));
        printSources(*p, std::clamp(radius, 1, 16), out);
    };
    cmd.overload().text("newstones").text("sources").execute([sources](CommandOrigin const& o, CommandOutput& out) {
        sources(o, out, 8);
    });
    cmd.overload<NewStonesSourcesParam>()
        .text("newstones")
        .text("sources")
        .required("radius")
        .execute([sources](CommandOrigin const& o, CommandOutput& out, NewStonesSourcesParam const& p) {
            sources(o, out, p.radius);
        });
});
