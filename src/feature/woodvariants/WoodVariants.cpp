#include "feature/woodvariants/WoodVariants.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "nlohmann/json.hpp"

#include "core/GlobalPos.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Shapes.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("woodvariants", lp::feature::woodvariants::WoodVariantsConfig);

namespace lp::feature::woodvariants {

namespace bs     = game::blockstates;
namespace shapes = game::shapes;

namespace {

constexpr std::string_view kSixDirs[6] = {"down", "up", "north", "south", "west", "east"};
constexpr std::string_view kFourDirs[4] = {"north", "east", "south", "west"};

std::string axisOfDir(std::string_view dir) {
    if (dir == "up" || dir == "down") return "y";
    if (dir == "north" || dir == "south") return "z";
    return "x";
}

bool isLiquid(Block const& b) {
    auto const& n = b.getTypeName();
    return n == "minecraft:water" || n == "minecraft:flowing_water" || n == "minecraft:lava" || n == "minecraft:flowing_lava";
}

bool isAirLike(Block const& b) { return b.isAir() || isLiquid(b); }

/// Flags read from other units' config files (their sections may not exist yet).
struct External {
    bool azaleaWood         = false; ///< U7 newwoods enableAzaleaWood
    bool freestandingLadder = false; ///< U17 placement enableEnhancedLadders && allowFreestanding
};
External& external() {
    static External e;
    return e;
}

std::optional<nlohmann::json> readSection(std::string_view section) {
    try {
        auto          path = config::configDir() / (std::string(section) + ".json");
        std::ifstream in(path);
        if (!in) return std::nullopt;
        return nlohmann::json::parse(in, nullptr, false, true);
    } catch (...) {
        return std::nullopt;
    }
}

void loadExternal() {
    External e;
    if (auto j = readSection("newwoods"); j && j->is_object()) e.azaleaWood = j->value("enableAzaleaWood", false);
    if (auto j = readSection("placement"); j && j->is_object()) {
        e.freestandingLadder = j->value("enableEnhancedLadders", false) && j->value("allowFreestanding", false);
    }
    external() = e;
}

// ---- update queue ------------------------------------------------------------------------------

struct State {
    std::set<core::GlobalPos>                           queue;
    std::unordered_map<int64_t, bool>                   wasTrying; ///< by player unique id
    std::unordered_map<int64_t, bool>                   crawling;  ///< players we put into a hollow log
    uint64_t                                            tick = 0;
};
State& st() {
    static State s;
    return s;
}

bool isOurs(Block const& b) {
    auto const& n = b.getTypeName();
    if (!n.starts_with("lepton:")) return false;
    return hasTag(b, "lepton:post") || hasTag(b, "lepton:hedge") || hasTag(b, "lepton:ladder")
        || hasTag(b, "lepton:leaf_carpet") || hasTag(b, "lepton:hollow_log");
}

void queue(BlockSource& region, BlockPos const& pos) {
    int   dim = static_cast<int>(region.getDimensionId());
    auto& q   = st().queue;
    q.insert(game::globalPos(dim, pos));
    for (auto d : kSixDirs) q.insert(game::globalPos(dim, shapes::neighbor(pos, d)));
}

void popBlock(BlockSource& region, BlockPos const& pos) {
    BlockChangeContext ctx{};
    region.getLevel().destroyBlock(region, pos, true, ctx);
}

bool setIfChanged(BlockSource& region, BlockPos const& pos, Block const& cur, Block const* next) {
    if (!next || *next == cur) return false;
    return game::setBlock(region, pos, *next);
}

bool isLadder(Block const& b) { return b.getTypeName() == "minecraft:ladder" || hasTag(b, "lepton:ladder"); }

std::string ladderFacing(Block const& b) {
    if (hasTag(b, "lepton:ladder")) return bs::getString(b, "minecraft:block_face").value_or("north");
    return shapes::faceName(bs::getInt(b, "facing_direction").value_or(2)).data();
}

/// Java LadderBlock#canSurvive (+ U17 EnhancedLadders' freestanding rule when that unit enables it).
bool ladderSurvives(BlockSource& region, BlockPos const& pos, Block const& b) {
    auto facing = ladderFacing(b);
    if (isAirLike(region.getBlock(shapes::neighbor(pos, shapes::opposite(facing))))) {
        if (!external().freestandingLadder) return false;
        auto const& above = region.getBlock(shapes::neighbor(pos, "up"));
        return isLadder(above) && ladderFacing(above) == facing;
    }
    return true;
}

// ---- auto crawl --------------------------------------------------------------------------------

BlockPos feetBlock(Actor const& a) {
    auto f = a.getFeetPos();
    return {
        static_cast<int>(std::floor(f.x)),
        static_cast<int>(std::floor(f.y + 0.01f)),
        static_cast<int>(std::floor(f.z))
    };
}

bool isCrawlLog(Block const& b, std::string_view dir) {
    return hasTag(b, "lepton:hollow_log") && moduleEnabled(b) && axisOf(b) == axisOfDir(dir);
}

/// Quark HollowLogsModule#tryClimb: crawl into the hollow log at `pos` along `dir`.
bool tryClimb(Player& player, std::string_view dir, BlockPos const& pos) {
    auto& region = player.getDimensionBlockSource();
    if (!isCrawlLog(region.getBlock(pos), dir)) return false;
    auto step = shapes::neighbor(BlockPos{0, 0, 0}, dir);
    player.startSwimming();
    Vec3 to{
        static_cast<float>(pos.x) + 0.5f - static_cast<float>(step.x) * 0.3f,
        static_cast<float>(pos.y) + 0.13f,
        static_cast<float>(pos.z) + 0.5f - static_cast<float>(step.z) * 0.3f
    };
    player.teleport(to, player.getDimensionId(), player.getRotation());
    st().crawling[player.getOrCreateUniqueID().rawID] = true;
    log().info("woodvariants: {} crawled into a hollow log at {} {} {}", player.getRealName(), pos.x, pos.y, pos.z);
    return true;
}

void onTick(ll::event::ServerLevelTickEvent& ev) {
    auto& s = st();
    ++s.tick;
    if (!s.queue.empty()) {
        auto q = std::move(s.queue);
        s.queue.clear();
        for (auto const& gp : q) {
            auto* region = game::regionOf(gp.dim);
            if (!region) continue;
            auto pos = game::blockPos(gp);
            if (pos.y < region->getMinHeight() || pos.y >= region->getMaxHeight()) continue;
            if (!region->hasChunksAt(pos, 0, false)) continue;
            if (!isOurs(region->getBlock(pos))) continue;
            try {
                updateBlock(*region, pos);
            } catch (std::exception const& e) {
                log().error("woodvariants: update failed: {}", e.what());
            }
        }
    }
    for (auto* a : ev.level().getRuntimeActorList()) {
        if (!a || !a->isPlayer() || a->isClientSide()) continue;
        autoCrawlTick(*static_cast<Player*>(a));
    }
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    queue(region, ev.pos());
    if (isOurs(ev.newBlock())) applyBlockProperties(ev.newBlock());
}

// Blocks of a disabled module can't be placed (docs §3.8).
void onPlacing(ll::event::PlayerPlacingBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& item = player.getSelectedItem();
    if (item.isNull()) return;
    auto const& name = item.getTypeName();
    if (!name.starts_with("lepton:")) return;
    auto const* block = bs::of(name);
    if (block && !moduleEnabled(*block)) ev.cancel();
}

} // namespace

// ---- public ------------------------------------------------------------------------------------

bool azaleaWoodEnabled() { return external().azaleaWood; }

WoodVariantsConfig const& config() { return lp::config::get<WoodVariantsConfig>(); }

bool hasTag(Block const& block, std::string_view tag) { return block.hasTag(HashedString(std::string(tag))); }

std::string moduleOf(Block const& block) {
    static constexpr std::string_view kModules[] = {
        "vertical_planks", "variant_bookshelves", "variant_ladders", "wooden_posts", "hedges", "leaf_carpet", "hollow_logs"};
    if (!block.getTypeName().starts_with("lepton:")) return {};
    for (auto m : kModules) {
        if (hasTag(block, "lepton:wv_" + std::string(m))) return std::string(m);
    }
    return {};
}

bool moduleEnabled(std::string_view m) {
    auto const& c = config();
    if (m == "vertical_planks") return c.enableVerticalPlanks;
    if (m == "variant_bookshelves") return c.enableVariantBookshelves;
    if (m == "variant_ladders") return c.enableVariantLadders;
    if (m == "wooden_posts") return c.enableWoodenPosts;
    if (m == "hedges") return c.enableHedges;
    if (m == "leaf_carpet") return c.enableLeafCarpet;
    if (m == "hollow_logs") return c.enableHollowLogs;
    return true;
}

bool moduleEnabled(Block const& block) {
    auto m = moduleOf(block);
    return m.empty() || moduleEnabled(m);
}

std::string axisOf(Block const& block) {
    auto face = bs::getString(block, "minecraft:block_face").value_or("up");
    return axisOfDir(face);
}

std::string postSideType(BlockSource& region, BlockPos const& pos, std::string_view dir) {
    auto const& side = region.getBlock(shapes::neighbor(pos, dir));
    auto const& name = side.getTypeName();
    auto        axis = axisOfDir(dir);
    bool        chain =
        (name == "minecraft:chain" || name.ends_with("_chain"))
        && bs::getString(side, "pillar_axis").value_or("y") == axis;
    if (dir == "down") {
        if (name.ends_with("lantern") && bs::getBool(side, "hanging").value_or(false)) chain = true;
        if (name.ends_with("_hanging_sign") && bs::getBool(side, "hanging").value_or(false)) chain = true;
    }
    if (chain) return "chain";
    if (hasTag(side, "lepton:post") && axisOf(side) == axis) return "other_post";
    if (dir == "up" && hasTag(side, "lepton:hedge")) return "other_post";
    return "none";
}

bool updateBlock(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    if (hasTag(b, "lepton:post")) {
        bs::Changes ch;
        for (auto d : kSixDirs) ch.emplace_back("lepton:connect_" + std::string(d), postSideType(region, pos, d));
        return setIfChanged(region, pos, b, bs::with(b, ch));
    }
    if (hasTag(b, "lepton:hedge")) {
        bs::Changes ch;
        for (auto d : kFourDirs) ch.emplace_back("lepton:" + std::string(d), hasTag(region.getBlock(shapes::neighbor(pos, d)), "lepton:hedge"));
        ch.emplace_back("lepton:extend", hasTag(region.getBlock(shapes::neighbor(pos, "down")), "lepton:hedge"));
        return setIfChanged(region, pos, b, bs::with(b, ch));
    }
    if (hasTag(b, "lepton:ladder")) {
        if (ladderSurvives(region, pos, b)) return false;
        popBlock(region, pos);
        return true;
    }
    if (hasTag(b, "lepton:leaf_carpet")) {
        if (!region.getBlock(shapes::neighbor(pos, "down")).isAir()) return false;
        popBlock(region, pos);
        return true;
    }
    return false;
}

void applyBlockProperties(Block const& block) {
    uint64_t bits = 0;
    if (hasTag(block, "lepton:ladder") || hasTag(block, "lepton:hollow_log")) bits |= static_cast<uint64_t>(BlockProperty::Climbable);
    if (hasTag(block, "lepton:post")) bits |= static_cast<uint64_t>(BlockProperty::CanSupportCenterHangingBlock);
    if (!bits) return;
    auto& type = const_cast<BlockType&>(block.getBlockType());
    auto  cur  = static_cast<uint64_t>(type.mProperties);
    if ((cur & bits) != bits) type.mProperties = static_cast<BlockProperty>(cur | bits);
}

void applyKnownBlockProperties() {
    static std::vector<std::string> const extra = [] {
        // U7's woods (Quark's AzaleaWood / BlossomTrees / AncientWood variants), when present
        std::vector<std::string> v;
        for (auto w : {"azalea", "blossom", "ancient"}) {
            v.push_back("lepton:" + std::string(w) + "_ladder");
            v.push_back("lepton:" + std::string(w) + "_post");
            v.push_back("lepton:stripped_" + std::string(w) + "_post");
            v.push_back("lepton:hollow_" + std::string(w) + "_log");
        }
        return v;
    }();
    for (auto const* list : {&allBlockIds(), &extra}) {
        for (auto const& id : *list) {
            if (auto const* b = bs::of(id)) applyBlockProperties(*b);
        }
    }
}

std::vector<BlockPos> variantBookshelfPositions(BlockSource& region, BlockPos const& table) {
    std::vector<BlockPos> out;
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dz = -2; dz <= 2; ++dz) {
            if (std::abs(dx) != 2 && std::abs(dz) != 2) continue;
            for (int dy = 0; dy <= 1; ++dy) {
                BlockPos shelf{table.x + dx, table.y + dy, table.z + dz};
                auto const& b = region.getBlock(shelf);
                if (!hasTag(b, "lepton:bookshelf") || !moduleEnabled(b)) continue;
                BlockPos mid{table.x + dx / 2, table.y + dy, table.z + dz / 2};
                if (!game::replaceable(region, mid)) continue;
                out.push_back(shelf);
            }
        }
    }
    return out;
}

int variantBookshelfCount(BlockSource& region, BlockPos const& table) {
    return static_cast<int>(variantBookshelfPositions(region, table).size());
}

void autoCrawlTick(Player& player) {
    auto const& c   = config();
    auto        uid = player.getOrCreateUniqueID().rawID;
    auto&       s   = st();
    auto&       region = player.getDimensionBlockSource();
    BlockPos    feet   = feetBlock(player);
    if (!c.enableHollowLogs || !c.enableAutoCrawl) {
        s.wasTrying.erase(uid);
        s.crawling.erase(uid);
        return;
    }
    auto const& here = region.getBlock(feet);
    bool inLog = hasTag(here, "lepton:hollow_log") && axisOf(here) != "y";
    // keep the crawl pose while inside a horizontal hollow log (Java keeps the swimming pose
    // because there's no room to stand up)
    auto crawlIt = s.crawling.find(uid);
    bool ours    = crawlIt != s.crawling.end();
    if (ours) {
        if (inLog) {
            if (!player.isSwimming()) player.startSwimming();
        } else {
            s.crawling.erase(crawlIt);
            ours = false;
            if (player.isSwimming() && !isLiquid(region.getBlock(feet))) player.stopSwimming();
        }
    }
    bool crawling = ours && player.isSwimming();
    bool trying   = crawling || (player.isSneaking() && !hasTag(here, "lepton:hollow_log"));
    bool was      = s.wasTrying[uid];
    if (!crawling && trying && !was) {
        std::string dir(shapes::lookDirection(player.getRotation().y));
        std::string opp(shapes::opposite(dir));
        auto        front = shapes::neighbor(feet, dir);
        if (!tryClimb(player, opp, feet))                                  // crawl out
            if (!tryClimb(player, opp, shapes::neighbor(feet, "up")))      // crawl out
                if (!tryClimb(player, dir, front))                         // crawl into
                    tryClimb(player, dir, shapes::neighbor(front, "up"));  // crawl into
    }
    s.wasTrying[uid] = trying;
}

std::vector<std::string> const& allBlockIds() {
    static std::vector<std::string> const ids = [] {
        std::vector<std::string> v;
        static constexpr std::string_view all[] = {
            "oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "bamboo", "crimson", "warped"};
        for (auto w : all) {
            std::string s(w);
            v.push_back("lepton:vertical_" + s + "_planks");
            v.push_back("lepton:" + s + "_post");
            v.push_back("lepton:stripped_" + s + "_post");
            if (s != "oak") {
                v.push_back("lepton:" + s + "_bookshelf");
                v.push_back("lepton:" + s + "_ladder");
            }
            if (s == "crimson" || s == "warped") v.push_back("lepton:hollow_" + s + "_stem");
            else if (s != "bamboo") v.push_back("lepton:hollow_" + s + "_log");
            if (s != "bamboo" && s != "crimson" && s != "warped") {
                v.push_back("lepton:" + s + "_hedge");
                v.push_back("lepton:" + s + "_leaf_carpet");
            }
        }
        for (auto a : {"azalea", "flowering_azalea"}) {
            v.push_back("lepton:" + std::string(a) + "_hedge");
            v.push_back("lepton:" + std::string(a) + "_leaf_carpet");
        }
        return v;
    }();
    return ids;
}

// ---- module ------------------------------------------------------------------------------------

namespace detail {
void installHooks();
void uninstallHooks();
void loadHooks();
} // namespace detail

class WoodVariantsModule {
public:
    void load() {
        loadExternal();
        detail::loadHooks();
    }
    void install() {
        loadExternal();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(onPlacing));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        detail::installHooks();
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldReady([] { applyKnownBlockProperties(); });
            game::db::onWorldClosed([] { st() = State{}; });
            config::onReload([] { loadExternal(); });
        }
        if (ll::service::getLevel()) applyKnownBlockProperties();
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        detail::uninstallHooks();
        st() = State{};
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::feature::woodvariants

LP_REGISTER_MODULE(lp::feature::woodvariants::WoodVariantsModule);
