// Pathfinder Maps (Quark tools/PathfinderMapsModule + PathfindersQuillItem): the quill searches a
// biome along Quark's square spiral and becomes a filled map with an X at the biome. Trades for
// cartographers and wandering traders. See docs/spec/gadgets.md.

#include "feature/gadgets/Pathfinder.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/entity/components_json_legacy/EconomyTradeableComponent.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/item/trading/MerchantRecipeList.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/biome/source/BiomeArea.h"
#include "mc/world/level/biome/source/BiomeSource.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/WorldGenerator.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/saveddata/maps/MapDecoration.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"
#include "mc/world/level/saveddata/maps/MapItemTrackedActor.h"

#include "feature/gadgets/Gadgets.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Net.h"

namespace lp::feature::gadgets::pathfinder {

// ---- biome ids ----------------------------------------------------------------------------------------

std::string bedrockBiome(std::string_view javaId) {
    auto path = javaId.substr(javaId.find(':') == std::string_view::npos ? 0 : javaId.find(':') + 1);
    static std::map<std::string, std::string, std::less<>> const table{
        {"snowy_plains",            "ice_plains"           },
        {"windswept_hills",         "extreme_hills"        },
        {"windswept_gravelly_hills", "extreme_hills_mutated"},
        {"windswept_forest",        "extreme_hills_plus_trees"},
        {"windswept_savanna",       "savanna_mutated"      },
        {"dark_forest",             "roofed_forest"        },
        {"swamp",                   "swampland"            },
        {"old_growth_pine_taiga",   "mega_taiga"           },
        {"old_growth_spruce_taiga", "redwood_taiga_mutated"},
        {"old_growth_birch_forest", "birch_forest_mutated" },
        {"badlands",                "mesa"                 },
        {"wooded_badlands",         "mesa_plateau_stone"   },
        {"eroded_badlands",         "mesa_bryce"           },
        {"mushroom_fields",         "mushroom_island"      },
        {"ice_spikes",              "ice_plains_spikes"    },
        {"snowy_taiga",             "cold_taiga"           },
        {"snowy_beach",             "cold_beach"           },
        {"stony_shore",             "stone_beach"          },
        {"sparse_jungle",           "jungle_edge"          },
        {"savanna_plateau",         "savanna_plateau"      },
        {"nether_wastes",           "hell"                 },
        {"the_end",                 "the_end"              },
        {"frozen_ocean",            "frozen_ocean"         },
    };
    if (auto it = table.find(path); it != table.end()) return it->second;
    return std::string(path);
}

std::string javaPath(std::string_view javaId) {
    auto c = javaId.find(':');
    return std::string(c == std::string_view::npos ? javaId : javaId.substr(c + 1));
}

std::string biomeName(Player const* player, std::string_view javaId) {
    auto path = javaPath(javaId);
    // fallback: "old_growth_pine_taiga" -> "Old Growth Pine Taiga"
    std::string pretty;
    bool        up = true;
    for (char ch : path) {
        if (ch == '_') {
            pretty += ' ';
            up = true;
        } else {
            pretty += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
            up = false;
        }
    }
    auto key = "lp.gadgets.biome." + path;
    return player ? trFor(*player, key, pretty) : tr(key, pretty);
}

// ---- trades -----------------------------------------------------------------------------------------

std::optional<TradeInfo> parseCustom(std::string const& line) {
    std::vector<std::string> tokens;
    std::string              cur;
    for (char ch : line) {
        if (ch == ',') {
            tokens.push_back(cur);
            cur.clear();
        } else cur += ch;
    }
    tokens.push_back(cur);
    if (tokens.size() != 5 && tokens.size() != 6) return std::nullopt;
    try {
        TradeInfo t;
        t.biome    = tokens[0];
        t.level    = std::stoi(tokens[1]);
        t.minPrice = std::stoi(tokens[2]);
        t.maxPrice = std::stoi(tokens[3]);
        t.color    = static_cast<int>(static_cast<uint32_t>(std::stoul(tokens[4], nullptr, 16)));
        if (t.biome.empty() || t.maxPrice < t.minPrice) return std::nullopt;
        return t;
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<TradeInfo> tradeList() {
    std::vector<TradeInfo> out;
    auto const&            c = cfg();
    if (c.applyDefaultTrades) {
        for (auto const& e : c.builtinTrades) {
            TradeInfo t;
            t.biome    = e.biome;
            t.enabled  = e.enabled;
            t.level    = e.level;
            t.minPrice = e.minPrice;
            t.maxPrice = std::max(e.minPrice, e.maxPrice);
            try {
                t.color = static_cast<int>(static_cast<uint32_t>(std::stoul(e.color, nullptr, 16)));
            } catch (...) {
                t.color = -1;
            }
            out.push_back(t);
        }
    }
    for (auto const& line : c.customs) {
        if (auto t = parseCustom(line)) out.push_back(*t);
        else log().warn("[Custom Pathfinder Maps] Error while reading custom map string \"{}\"", line);
    }
    return out;
}

// ---- quill items --------------------------------------------------------------------------------------

namespace {

constexpr char const* kTarget   = "targetBiome";
constexpr char const* kColor    = "biomeColor";
constexpr char const* kSearch   = "isSearching";
constexpr char const* kSourceX  = "sourceX";
constexpr char const* kSourceZ  = "sourceZ";
constexpr char const* kPosX     = "posX";
constexpr char const* kPosZ     = "posZ";
constexpr char const* kLeg      = "posLeg";
constexpr char const* kLegIndex = "posLegIndex";

bool isQuill(ItemStackBase const& s) { return game::itemId(s) == kQuill; }

void setLore(ItemStack& s, Player const* player) {
    std::vector<std::string> lore;
    auto                     target = getString(s, kTarget);
    if (target && !target->empty()) {
        if (getBool(s, kSearch))
            lore.push_back("§9" + (player ? trFor(*player, "quark.misc.quill_searching", "Searching") : tr("quark.misc.quill_searching", "Searching")) + "...");
        lore.push_back("§7" + biomeName(player, *target));
    } else {
        lore.push_back("§7" + (player ? trFor(*player, "quark.misc.quill_blank", "Blank") : tr("quark.misc.quill_blank", "Blank")));
    }
    s.setCustomLore(lore);
}

/// Where a player's quills are: the 36 inventory slots, then the off hand, then armor (Quark's
/// getActiveQuill order).
struct SlotRef {
    enum Kind { Inv, Offhand, Armor } kind = Inv;
    int slot                               = 0;
};

ItemStack read(Player& p, SlotRef r) {
    switch (r.kind) {
    case SlotRef::Inv: return game::inv::playerInventory(p).getItem(r.slot);
    case SlotRef::Offhand: return p.getOffhandSlot();
    case SlotRef::Armor: return p.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(r.slot));
    }
    return {};
}

void write(Player& p, SlotRef r, ItemStack const& s) {
    switch (r.kind) {
    case SlotRef::Inv: game::inv::playerInventory(p).setItem(r.slot, s); break;
    case SlotRef::Offhand: p.setOffhandSlot(s); break;
    case SlotRef::Armor: p.setArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(r.slot), s); break;
    }
    game::inv::sync(p);
}

std::optional<SlotRef> activeQuill(Player& p) {
    auto& inv = game::inv::playerInventory(p);
    for (int i = 0; i < game::inv::kPlayerSlots; ++i) {
        auto const& s = inv.getItem(i);
        if (isQuill(s) && getBool(s, kSearch)) return SlotRef{SlotRef::Inv, i};
    }
    if (auto const& s = p.getOffhandSlot(); isQuill(s) && getBool(s, kSearch)) return SlotRef{SlotRef::Offhand, 0};
    for (int a = 0; a < 4; ++a) {
        auto const& s = p.getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(a));
        if (isQuill(s) && getBool(s, kSearch)) return SlotRef{SlotRef::Armor, a};
    }
    return std::nullopt;
}

void resetSearch(ItemStack& s) {
    for (auto const* k : {kSourceX, kSourceZ, kSearch, kPosX, kPosZ, kLeg, kLegIndex}) removeKey(s, k);
}

} // namespace

ItemStack makeQuill(std::string const& biome, int color, Player const* player) {
    auto s = game::makeItem(kQuill);
    if (game::isEmpty(s)) return s;
    putString(s, kTarget, biome);
    putInt(s, kColor, color);
    putBool(s, "isUnderground", false);
    setLore(s, player);
    return s;
}

// ---- spiral search ------------------------------------------------------------------------------

std::optional<std::pair<int, int>> nextPos(Spiral& sp, int searchRadius) {
    constexpr int step = 32;
    // EAST, SOUTH, WEST, NORTH
    static constexpr int dirs[4][2] = {
        {1,  0 },
        {0,  1 },
        {-1, 0 },
        {0,  -1}
    };
    int newX    = sp.x + dirs[(sp.leg + 4) % 4][0];
    int newZ    = sp.z + dirs[(sp.leg + 4) % 4][1];
    int legSize = sp.leg / 2 + 1;
    int maxLegs = 4 * (searchRadius / step);
    if (sp.legIndex >= legSize) {
        if (sp.leg > maxLegs) return std::nullopt;
        sp.leg++;
        sp.legIndex = 0;
    }
    sp.legIndex++;
    sp.x = newX;
    sp.z = newZ;
    return std::pair{sp.sourceX + newX * step, sp.sourceZ + newZ * step};
}

std::vector<int> searchHeights(int y, int minY, int maxY) {
    // Java Mth.outFromOrigin(y, minY, maxY, 64): y, y+64, y-64, y+128, y-128, ... inside [minY, maxY)
    std::vector<int> out;
    if (y >= minY && y < maxY) out.push_back(y);
    for (int k = 1;; ++k) {
        int  up = y + k * 64, down = y - k * 64;
        bool any = false;
        if (up >= minY && up < maxY) out.push_back(up), any = true;
        if (down >= minY && down < maxY) out.push_back(down), any = true;
        if (!any && (up >= maxY && down < minY)) break;
        if (k > 64) break;
    }
    return out;
}

std::string biomeAt(Dimension& dim, int x, int y, int z) {
    auto* gen = dim.mWorldGenerator;
    if (!gen) return {};
    BoundingBox box(BlockPos(x, y, z), BlockPos(x, y, z));
    auto        area = gen->getBiomeSource().getBiomeArea(box, 1);
    auto const& v    = *area.mBiomes;
    if (v.empty() || !v[0]) return {};
    return v[0]->mHash->getString();
}

std::optional<BlockPos> searchSteps(
    Dimension&              dim,
    std::string const&      bedrockName,
    Spiral&                 sp,
    std::vector<int> const& hs,
    int                     steps,
    bool&                   failed
) {
    failed = false;
    for (int i = 0; i < steps; ++i) {
        auto p = nextPos(sp, cfg().searchRadius);
        if (!p) {
            failed = true;
            return std::nullopt;
        }
        for (int h : hs)
            if (biomeAt(dim, p->first, h, p->second) == bedrockName) return BlockPos(p->first, h, p->second);
    }
    return std::nullopt;
}

// ---- concurrent search (multiThreaded) ---------------------------------------------------------------

namespace {

struct Key {
    int         dim, x, z;
    std::string biome;
    auto        operator<=>(Key const&) const = default;
};
struct Result {
    bool                    failed = false;
    std::optional<BlockPos> found;
};
struct Job {
    Key              key;
    Dimension*       dim = nullptr; ///< alive until the world closes (the worker is stopped first)
    std::vector<int> heights;
};

std::mutex              gMutex;
std::condition_variable gWake;
std::map<Key, Result>   gResults;
std::vector<Job>        gQueue;
std::set<Key>           gComputing;
std::thread             gWorker;
std::atomic<bool>       gStop{false};

void workerLoop() {
    while (true) {
        Job job;
        {
            std::unique_lock lk(gMutex);
            gWake.wait(lk, [] { return gStop.load() || !gQueue.empty(); });
            if (gStop) return;
            job = std::move(gQueue.front());
            gQueue.erase(gQueue.begin());
        }
        Result res;
        Spiral sp;
        sp.sourceX  = job.key.x;
        sp.sourceZ  = job.key.z;
        bool failed = false;
        auto name   = bedrockBiome(job.key.biome);
        // the whole spiral, in slices so a stop request is seen quickly
        while (!gStop) {
            res.found = searchSteps(*job.dim, name, sp, job.heights, 256, failed);
            if (res.found || failed) break;
        }
        res.failed = !res.found;
        std::lock_guard lk(gMutex);
        gComputing.erase(job.key);
        if (!gStop) gResults[job.key] = res;
    }
}

void ensureWorker() {
    if (gWorker.joinable()) return;
    gStop   = false;
    gWorker = std::thread(workerLoop);
}

void stopWorker() {
    {
        std::lock_guard lk(gMutex);
        gStop = true;
    }
    gWake.notify_all();
    if (gWorker.joinable()) gWorker.join();
    std::lock_guard lk(gMutex);
    gQueue.clear();
    gComputing.clear();
    gResults.clear();
    gStop = false;
}

/// Quark's searchConcurrent: pass while computing, then the cached result for (dim, source, biome).
std::optional<Result> concurrent(Dimension& d, int dim, int sx, int sz, std::vector<int> heights, std::string const& biome) {
    Key             key{dim, sx, sz, biome};
    std::lock_guard lk(gMutex);
    if (gComputing.contains(key)) return std::nullopt;
    if (auto it = gResults.find(key); it != gResults.end()) return it->second;
    gComputing.insert(key);
    gQueue.push_back(Job{key, &d, std::move(heights)});
    ensureWorker();
    gWake.notify_one();
    return std::nullopt;
}

// ---- per-player iterative state --------------------------------------------------------------------

struct Running {
    Spiral   sp;
    uint64_t savedAt = 0;
    bool     sentHud = false;
};
std::unordered_map<int64_t, Running> gRunning;

Spiral spiralOf(ItemStackBase const& s) {
    Spiral sp;
    sp.sourceX  = getInt(s, kSourceX).value_or(0);
    sp.sourceZ  = getInt(s, kSourceZ).value_or(0);
    sp.x        = getInt(s, kPosX).value_or(0);
    sp.z        = getInt(s, kPosZ).value_or(0);
    sp.leg      = getInt(s, kLeg).value_or(0);
    sp.legIndex = getInt(s, kLegIndex).value_or(0);
    return sp;
}

void storeSpiral(ItemStack& s, Spiral const& sp) {
    putInt(s, kPosX, sp.x);
    putInt(s, kPosZ, sp.z);
    putInt(s, kLeg, sp.leg);
    putInt(s, kLegIndex, sp.legIndex);
}

} // namespace

// ---- map creation -------------------------------------------------------------------------------------

ItemStack createMap(Player& player, BlockPos const& target, std::string const& javaBiome, int color) {
    auto  level = ll::service::getLevel();
    auto  map   = game::makeItem("minecraft:filled_map");
    if (!level || game::isEmpty(map)) return map;
    auto dimType = player.getDimensionId();
    auto d       = level->getDimension(dimType).lock();
    if (!d) return map;
    ActorUniqueID uid  = level->getNewUniqueID();
    auto&         data = level->createMapSavedData(uid, target, dimType, 2);
    data.setOrigin(
        Vec3{static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.z)},
        2,
        dimType,
        false,
        false,
        target
    );
    MapItem::setItemInstanceInfo(map, data);
    MapItem::renderBiomePreviewMap(*d, data);
    MapItemTrackedActor::UniqueId id;
    id.type        = MapItemTrackedActor::Type::Other;
    id.keyEntityId = ActorUniqueID{};
    id.keyBlockPos = target;
    data._addDecoration(
        MapDecoration::Type::XWhite,
        id,
        Vec3{static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.z)},
        "+",
        mce::Color(0.85f, 0.1f, 0.1f, 1.0f)
    );
    auto name = trFor(player, "item.quark.biome_map", "{} Pathfinder Map", biomeName(&player, javaBiome));
    map.setCustomName(Bedrock::Safety::RedactableString(name));
    putBool(map, "quark:is_pathfinder", true);
    putInt(map, "quark:map_color", color);
    return map;
}

// ---- use and tick -------------------------------------------------------------------------------------

namespace {

ll::event::ListenerPtr gUse;

void onUse(ll::event::PlayerUseItemEvent& ev) {
    auto& p = ev.self();
    if (p.isClientSide()) return;
    if (!isQuill(ev.item())) return;
    if (!cfg().enablePathfinderMaps) return;
    onUseItem(p, ev.item());
}

} // namespace

void onUseItem(Player& player, ItemStack const& item) {
    auto target = getString(item, kTarget);
    if (!target || target->empty() || getBool(item, kSearch)) return;
    if (activeQuill(player)) {
        actionBar(player, trFor(player, "quark.misc.only_one_quill", "A Pathfinder's Quill is already searching..."));
        return;
    }
    ItemStack s = player.getSelectedItem();
    bool      offhand = false;
    if (!isQuill(s) || getString(s, kTarget) != target) {
        s       = player.getOffhandSlot();
        offhand = true;
        if (!isQuill(s)) return;
    }
    auto pos = player.getPosition();
    playSoundNear(static_cast<int>(player.getDimensionId()), "item.book.page_turn", pos, 0.5f, 1.0f, 16);
    putBool(s, kSearch, true);
    auto bp = game::blockPosOf(pos);
    putInt(s, kSourceX, bp.x);
    putInt(s, kSourceZ, bp.z);
    setLore(s, &player);
    if (offhand) player.setOffhandSlot(s);
    else player.setSelectedItem(s);
    game::inv::sync(player);
    log().info("gadgets: quill searching {} for {}", *target, player.getRealName());
}

void finish(Player& p, SlotRef ref, ItemStack stack, std::optional<BlockPos> found) {
    std::string key, fallback;
    ItemStack   result;
    auto        biome = getString(stack, kTarget).value_or("");
    if (found) {
        result   = createMap(p, *found, biome, getInt(stack, kColor).value_or(-1));
        key      = "quark.misc.quill_finished";
        fallback = "A Pathfinder Map has been completed.";
        log().info("gadgets: quill found {} at {} {} {} for {}", biome, found->x, found->y, found->z, p.getRealName());
    } else if (cfg().allowRetrying) {
        resetSearch(stack);
        setLore(stack, &p);
        result   = stack;
        key      = "quark.misc.quill_retry";
        fallback = "A Pathfinder's Quill could not find any biome nearby. Try somewhere else.";
        log().info("gadgets: quill failed ({}), reset for {}", biome, p.getRealName());
    } else {
        key      = "quark.misc.quill_failed";
        fallback = "A Pathfinder's Quill could not find any biome nearby.";
        log().info("gadgets: quill failed ({}), consumed for {}", biome, p.getRealName());
    }
    actionBar(p, trFor(p, key, fallback));
    playSoundNear(static_cast<int>(p.getDimensionId()), "note.chime", p.getPosition(), 0.5f, 1.0f, 16);
    write(p, ref, result);
    gRunning.erase(p.getOrCreateUniqueID().rawID);
}

void tick() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto const& c = cfg();
    std::vector<int64_t> ids;
    level->forEachPlayer([&](Player& p) {
        ids.push_back(p.getOrCreateUniqueID().rawID);
        return true;
    });
    for (auto id : ids) {
        auto* p = playerById(id);
        if (!p) continue;
        auto ref     = activeQuill(*p);
        auto running = gRunning.find(id);
        // HUD state for mod clients
        if (net::hasClientMod(*p)) {
            bool searching = ref.has_value() && c.enablePathfinderMaps;
            bool was       = running != gRunning.end() && running->second.sentHud;
            if (searching != was) {
                QuillMessage m;
                m.searching = searching;
                if (ref) {
                    auto s  = read(*p, *ref);
                    m.biome = getString(s, kTarget).value_or("");
                    m.color = getInt(s, kColor).value_or(-1);
                }
                net::sendToClient(*p, m);
                if (searching) gRunning[id].sentHud = true;
                else if (running != gRunning.end()) running->second.sentHud = false;
            }
        }
        if (!ref || !c.enablePathfinderMaps) {
            if (!ref) gRunning.erase(id);
            continue;
        }
        ItemStack stack  = read(*p, *ref);
        auto      target = getString(stack, kTarget);
        if (!target || target->empty()) continue;
        int  dim = static_cast<int>(p->getDimensionId());
        auto d   = level->getDimension(DimensionType(dim)).lock();
        if (!d) continue;
        int   y       = game::blockPosOf(p->getPosition()).y;
        auto& region  = p->getDimensionBlockSource();
        auto  heights = searchHeights(y, region.getMinHeight() + 1, region.getMaxHeight());
        if (c.multiThreaded) {
            auto res = concurrent(
                *d,
                dim,
                getInt(stack, kSourceX).value_or(0),
                getInt(stack, kSourceZ).value_or(0),
                heights,
                *target
            );
            if (!res) continue;
            finish(*p, *ref, stack, res->found);
            continue;
        }
        auto& run = gRunning[id];
        if (run.savedAt == 0) {
            run.sp      = spiralOf(stack);
            run.savedAt = now();
        }
        bool failed = false;
        auto found  = searchSteps(*d, bedrockBiome(*target), run.sp, heights, std::max(1, c.pathfindersQuillSpeed), failed);
        if (found || failed) {
            finish(*p, *ref, stack, found);
            continue;
        }
        if (now() - run.savedAt >= 100) { // keep the progress on the stack, like Quark
            storeSpiral(stack, run.sp);
            write(*p, *ref, stack);
            run.savedAt = now();
        }
    }
}

// ---- trades -------------------------------------------------------------------------------------------

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

MerchantRecipe makeOffer(TradeInfo const& t, bool withCompass) {
    int  price = std::uniform_int_distribution<int>(t.minPrice, std::max(t.minPrice, t.maxPrice))(rng());
    auto em    = game::makeItem("minecraft:emerald", std::clamp(price, 1, 64));
    auto quill = makeQuill(t.biome, t.color, nullptr);
    ItemInstance buyA(em), sell(quill);
    ItemInstance buyB = withCompass ? ItemInstance(game::makeItem("minecraft:compass")) : ItemInstance(ItemStack{});
    MerchantRecipe r(buyA, buyB, sell);
    r.mTier              = std::clamp(t.level - 1, 0, 4);
    r.mUses              = 0;
    r.mMaxUses           = 12;
    r.mTraderExp         = static_cast<uint>(cfg().xpFromTrade * std::max(1, t.level - 1));
    r.mRewardExp         = true;
    r.mBuyCountA         = price;
    r.mBuyCountB         = withCompass ? 1 : 0;
    r.mPriceMultiplierA  = 0.2f;
    r.mPriceMultiplierB  = 0.2f;
    r.mDemand            = 0;
    return r;
}

bool hasQuillFor(MerchantRecipeList const& list, std::string const& biome) {
    for (auto const& r : *list.mRecipeList)
        if (isQuill(*r.mSell) && getString(*r.mSell, kTarget) == biome) return true;
    return false;
}

void addTrades(EconomyTradeableComponent& comp) {
    auto const& c = cfg();
    if (!c.enablePathfinderMaps) return;
    auto* offers = comp.mOffers.get();
    if (!offers) return;
    std::string const& table = *comp.mGeneratedTablePath;
    auto               list  = tradeList();
    std::erase_if(list, [](TradeInfo const& t) { return !t.enabled; });
    if (list.empty()) return;
    auto& recipes = *offers->mRecipeList;
    if (table.find("cartographer") != std::string::npos) {
        if (!c.addToCartographer) return;
        int added = 0;
        for (auto const& t : list) {
            if (hasQuillFor(*offers, t.biome)) continue;
            recipes.push_back(makeOffer(t, true));
            ++added;
        }
        if (added) log().info("gadgets: added {} pathfinder trade(s) to a cartographer", added);
    } else if (table.find("wandering_trader") != std::string::npos) {
        for (auto const& r : recipes)
            if (isQuill(*r.mSell)) return;
        auto const& t = list[std::uniform_int_distribution<size_t>(0, list.size() - 1)(rng())];
        if (c.addToWanderingTraderForced) {
            auto offer  = makeOffer(t, false);
            offer.mTier = 0;
            // at the front (ItemInstance has no exported assignment: rebuild instead of insert)
            std::vector<MerchantRecipe> list;
            list.reserve(recipes.size() + 1);
            list.push_back(offer);
            for (auto const& r : recipes) list.push_back(r);
            recipes.swap(list);
            log().info("gadgets: added a pathfinder trade to a wandering trader");
            return;
        }
        // Java adds one multi-trade to the generic (5 picked of ~30) / rare (1 of ~6) pools.
        bool add = (c.addToWanderingTraderGeneric && std::uniform_int_distribution<int>(0, 30)(rng()) < 5)
                || (c.addToWanderingTraderRare && std::uniform_int_distribution<int>(0, 6)(rng()) < 1);
        if (add) {
            auto offer  = makeOffer(t, false);
            offer.mTier = 0;
            recipes.push_back(offer);
        }
    }
}

LL_TYPE_INSTANCE_HOOK(GenerateTradesHook, ll::memory::HookPriority::Normal, EconomyTradeableComponent, &EconomyTradeableComponent::_generateTrades, bool) {
    bool r = origin();
    try {
        addTrades(*this);
    } catch (...) {}
    return r;
}

} // namespace

std::vector<int> spiralForTest(int steps, int searchRadius) {
    Spiral           sp;
    std::vector<int> out;
    for (int i = 0; i < steps; ++i) {
        auto p = nextPos(sp, searchRadius);
        if (!p) break;
        out.push_back(p->first);
        out.push_back(p->second);
    }
    return out;
}

void addTradesForTest(EconomyTradeableComponent& comp) { addTrades(comp); }

void install() {
    gUse = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerUseItemEvent>(onUse);
    GenerateTradesHook::hook();
}

void uninstall() {
    if (gUse) ll::event::EventBus::getInstance().removeListener(gUse);
    gUse.reset();
    GenerateTradesHook::unhook();
    stopWorker();
    gRunning.clear();
}

void onWorldClosed() {
    stopWorker();
    gRunning.clear();
}

} // namespace lp::feature::gadgets::pathfinder
