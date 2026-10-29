#include "feature/enchanting/Table.h"

#include <algorithm>
#include <set>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/AABB.h"

#include "feature/enchanting/Config.h"
#include "feature/enchanting/Enchants.h"
#include "feature/enchanting/Messages.h"
#include "feature/enchanting/Util.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Forms.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Net.h"

namespace lp::feature::enchanting::table {

using core::GlobalPos;

namespace {

// ---- influences --------------------------------------------------------------------------------

struct InfluenceList {
    std::vector<int> boost, dampen;
};

struct CustomInfluence {
    std::string                                      block;
    std::vector<std::pair<std::string, std::string>> states;
    int                                              strength = 1;
    int                                              color    = 0;
    InfluenceList                                    list;
};

struct Influences {
    bool                          parsed = false;
    bool                          candlesFailed = false;
    std::vector<InfluenceList>    candles; ///< 16, wool colour order
    std::vector<CustomInfluence>  custom;
};

Influences& influences() {
    static Influences i;
    return i;
}

InfluenceList parseList(std::string const& s) {
    InfluenceList out;
    size_t        start = 0;
    while (start <= s.size()) {
        auto        end  = s.find(',', start);
        std::string part = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start            = end == std::string::npos ? s.size() + 1 : end + 1;
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (part.empty()) continue;
        bool damp = part.front() == '-';
        if (damp) part.erase(part.begin());
        if (auto const* d = defById(part)) (damp ? out.dampen : out.boost).push_back(d->type);
    }
    return out;
}

void parseInfluences() {
    auto& inf = influences();
    inf       = {};
    inf.parsed = true;
    auto const& c = cfg();
    for (auto const& entry : c.statesToInfluences) {
        std::vector<std::string> split;
        size_t                   start = 0;
        while (true) {
            auto end = entry.find(';', start);
            split.push_back(entry.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (split.size() != 4) continue;
        CustomInfluence ci;
        auto            state = split[0];
        if (auto br = state.find('['); br != std::string::npos) {
            ci.block   = state.substr(0, br);
            auto inner = state.substr(br + 1);
            if (!inner.empty() && inner.back() == ']') inner.pop_back();
            size_t s = 0;
            while (s < inner.size()) {
                auto e  = inner.find(',', s);
                auto kv = inner.substr(s, e == std::string::npos ? std::string::npos : e - s);
                s       = e == std::string::npos ? inner.size() : e + 1;
                if (auto eq = kv.find('='); eq != std::string::npos) ci.states.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
            }
        } else {
            ci.block = state;
        }
        if (ci.block.find(':') == std::string::npos) ci.block = "minecraft:" + ci.block;
        try {
            ci.strength = std::stoi(split[1]);
            auto col    = split[2];
            if (!col.empty() && col.front() == '#') col.erase(col.begin());
            ci.color = static_cast<int>(std::stoul(col, nullptr, 16));
        } catch (...) {
            continue;
        }
        ci.list = parseList(split[3]);
        inf.custom.push_back(std::move(ci));
    }
    if (c.influencesList.size() < 16) {
        log().error("Matrix Enchanting Influences must be of at least size 16, please fix this in the config.");
        inf.candlesFailed = true;
    } else {
        for (size_t i = 0; i < 16; ++i) inf.candles.push_back(parseList(c.influencesList[i]));
    }
}

Influences const& inf() {
    if (!influences().parsed) parseInfluences();
    return influences();
}

constexpr char const* kColors[16] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                     "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};

int candleColor(std::string const& id) {
    for (int i = 0; i < 16; ++i)
        if (id == std::string("minecraft:") + kColors[i] + "_candle") return i;
    return -1;
}

bool isBookshelf(std::string const& id) {
    if (id == "minecraft:bookshelf") return true;
    return id.size() > 10 && id.ends_with("_bookshelf") && id != "minecraft:chiseled_bookshelf";
}

float enchantPowerBonus(std::string const& id) { return isBookshelf(id) ? 1.0f : 0.0f; }

bool isSoulBase(std::string const& id) { return id == "minecraft:soul_sand" || id == "minecraft:soul_soil"; }

std::string valueString(game::blockstates::Value const& v) {
    if (auto const* i = std::get_if<int>(&v)) return std::to_string(*i);
    if (auto const* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    return std::get<std::string>(v);
}

/// The influencer at `pos`: stack size and the boost/dampen lists (after soul inversion).
struct Influencer {
    int           stack = 0;
    InfluenceList list;
    bool          inverted = false;
    int           color    = 0xffffff;
};

std::optional<Influencer> influencerAt(BlockSource& region, BlockPos const& pos, Block const& block) {
    auto const& id = block.getTypeName();
    for (auto const& ci : inf().custom) {
        if (ci.block != id) continue;
        bool match = true;
        for (auto const& [k, v] : ci.states) {
            auto val = game::blockstates::get(block, k);
            if (!val || valueString(*val) != v) {
                match = false;
                break;
            }
        }
        if (match) return Influencer{ci.strength, ci.list, false, ci.color};
    }
    if (inf().candlesFailed) return std::nullopt;
    int color = candleColor(id);
    if (color < 0) return std::nullopt;
    if (!game::blockstates::getBool(block, "lit").value_or(false)) return std::nullopt;
    Influencer out;
    out.stack = game::blockstates::getInt(block, "candles").value_or(0) + 1;
    out.list  = inf().candles[static_cast<size_t>(color)];
    if (cfg().soulCandlesInvert) {
        BlockPos below{pos.x, pos.y - 1, pos.z};
        auto     belowId = game::blockIdAt(region, below);
        if (isSoulBase(belowId)) out.inverted = true;
        else if (enchantPowerBonus(belowId) > 0) {
            BlockPos below2{pos.x, pos.y - 2, pos.z};
            if (isSoulBase(game::blockIdAt(region, below2))) out.inverted = true;
        }
    }
    if (out.inverted) std::swap(out.list.boost, out.list.dampen);
    return out;
}

float enchantPowerAt(BlockSource& region, BlockPos const& pos, Power& power) {
    auto const& block = region.getBlock(pos);
    if (cfg().allowInfluencing) {
        if (auto in = influencerAt(region, pos, block)) {
            if (!in->list.boost.empty() || !in->list.dampen.empty()) {
                for (int t : in->list.boost) power.influences[t] += in->stack;
                for (int t : in->list.dampen) power.influences[t] -= in->stack;
                return 1;
            }
        }
    }
    return enchantPowerBonus(block.getTypeName());
}

bool gapOk(BlockSource& region, BlockPos const& pos) {
    auto const& block = region.getBlock(pos);
    if (block.isAir()) return true;
    auto const& id = block.getTypeName();
    if (cfg().allowUnderwaterEnchanting && (id == "minecraft:water" || id == "minecraft:flowing_water")) return true;
    return cfg().allowShortBlockEnchanting && isShortBlock(block);
}

// ---- storage -----------------------------------------------------------------------------------

std::map<GlobalPos, Data>& cache() {
    static std::map<GlobalPos, Data> m;
    return m;
}
std::set<GlobalPos>& dirty() {
    static std::set<GlobalPos> s;
    return s;
}
std::string dbKey(GlobalPos const& pos) { return "lp:enchanting:table:" + pos.key(); }

void flushAll() {
    for (auto const& pos : dirty()) {
        auto it = cache().find(pos);
        if (it == cache().end()) continue;
        auto const& d = it->second;
        if (game::isEmpty(d.item) && game::isEmpty(d.lapis) && d.charge == 0) {
            game::db::erase(dbKey(pos));
            continue;
        }
        game::db::save(dbKey(pos), {
                                        {"item",   game::itemToSnbt(d.item) },
                                        {"lapis",  game::itemToSnbt(d.lapis)},
                                        {"charge", d.charge                 },
        });
    }
    dirty().clear();
}

// ---- sessions ----------------------------------------------------------------------------------

struct Session {
    GlobalPos   pos;
    bool        mod = false;
    std::string lastState;
};

std::map<std::string, Session>& sessions() {
    static std::map<std::string, Session> m;
    return m;
}

bool isLapis(ItemStack const& s) { return game::itemId(s) == "minecraft:lapis_lazuli"; }

std::string nameOf(Player const& p, EnchantDef const& d, int level) {
    auto n = trFor(p, nameKey(d), englishName(d));
    if (d.maxLevel != 1 || level != 1) n += " " + roman(level);
    return n;
}

std::string itemName(ItemStack const& s) {
    if (game::isEmpty(s)) return "-";
    auto n = s.getCustomName();
    if (n.empty()) n = s.getDescriptionName();
    if (s.mCount > 1) n += " x" + std::to_string(s.mCount);
    return n;
}

void showForm(Player& player, GlobalPos const& pos);

std::set<std::string>& pendingRefresh() {
    static std::set<std::string> s;
    return s;
}

/// Re-shows the main form next tick (once: several actions in one tick send a single form, as a
/// form sent while another is open is refused by the client and would end the session).
void refreshFormLater(Player& player, GlobalPos const& pos) {
    auto uuid = uuidOf(player);
    if (!pendingRefresh().insert(uuid).second) return;
    later(1, [uuid, pos] {
        pendingRefresh().erase(uuid);
        auto* p = playerByUuid(uuid);
        if (!p) return;
        auto s = sessions().find(uuid);
        if (s == sessions().end() || s->second.pos != pos || s->second.mod) return;
        showForm(*p, pos);
    });
}

} // namespace

// ---- public: power -----------------------------------------------------------------------------

bool isTableId(std::string const& id) {
    if (id == kBlockId) return true;
    return id == "minecraft:enchanting_table" && cfg().automaticallyConvert;
}

bool isShortBlock(Block const& block) {
    AABB        buf;
    auto const& box = block.getBlockType().getVisualShape(block, buf);
    float       h   = box.max.y - box.min.y;
    if (h <= 0 || (box.max.x - box.min.x) <= 0) return true;
    constexpr float f = 3.0f / 16.0f;
    return (box.min.y == 0 && box.max.y <= f + 1e-4f) || (box.max.y == 1.0f && box.min.y >= 1.0f - f - 1e-4f);
}

Power computePower(BlockSource& region, BlockPos const& pos) {
    Power out;
    float power = 0;
    for (int j = -1; j <= 1; ++j) {
        for (int k = -1; k <= 1; ++k) {
            if (j == 0 && k == 0) continue;
            BlockPos test{pos.x + k, pos.y, pos.z + j};
            BlockPos testUp{pos.x + k, pos.y + 1, pos.z + j};
            if (!gapOk(region, test) || !gapOk(region, testUp)) continue;
            for (int y = 0; y <= 1; ++y) {
                power += enchantPowerAt(region, {pos.x + k * 2, pos.y + y, pos.z + j * 2}, out);
                if (k != 0 && j != 0) {
                    power += enchantPowerAt(region, {pos.x + k * 2, pos.y + y, pos.z + j}, out);
                    power += enchantPowerAt(region, {pos.x + k, pos.y + y, pos.z + j * 2}, out);
                }
            }
        }
    }
    out.power = std::min(static_cast<int>(power), cfg().maxBookshelves);
    return out;
}

// ---- public: storage ---------------------------------------------------------------------------

Data& data(GlobalPos const& pos) {
    auto it = cache().find(pos);
    if (it != cache().end()) return it->second;
    Data d;
    if (auto j = game::db::load(dbKey(pos)); j && j->is_object()) {
        d.item   = game::itemFromSnbt(j->value("item", "{}"));
        d.lapis  = game::itemFromSnbt(j->value("lapis", "{}"));
        d.charge = j->value("charge", 0);
    }
    return cache().emplace(pos, std::move(d)).first->second;
}

void markDirty(GlobalPos const& pos) { dirty().insert(pos); }

void normalize(GlobalPos const& pos) {
    auto& d = data(pos);
    if (d.charge <= 0 && !game::isEmpty(d.lapis) && cfg().chargePerLapis > 0) {
        d.lapis.remove(1);
        if (d.lapis.mCount == 0) d.lapis = ItemStack{};
        d.charge += cfg().chargePerLapis;
        markDirty(pos);
    }
}

std::optional<Matrix> matrixOf(ItemStack const& item) {
    if (!isMatrixTarget(item)) return std::nullopt;
    bool book = isBook(item);
    if (auto s = stringTag(item, kMatrixTag)) {
        auto j = nlohmann::json::parse(*s, nullptr, false);
        if (!j.is_discarded()) return Matrix::fromJson(j, book);
    }
    return Matrix(book);
}

void writeMatrix(ItemStack& item, Matrix const& m) { setStringTag(item, kMatrixTag, m.toJson().dump()); }

MatrixEnv envFor(ItemStack const& item, Power const& power) {
    auto const& c = cfg();
    MatrixEnv   env;
    env.registry                = &registry();
    env.influences              = power.influences;
    env.bookshelfPower          = power.power;
    env.enchantability          = enchantValueOf(item);
    env.zeroButEnchantable      = env.enchantability == 0 && enchantSlotOf(item) != 0;
    ItemStack copy              = item;
    env.applicable              = [copy](EnchantDef const& e) { return applicable(copy, e); };
    env.maxBookshelves          = c.maxBookshelves;
    env.piecePriceScale         = c.piecePriceScale;
    env.bookEnchantability      = c.bookEnchantability;
    env.baseMaxPieceCount       = c.baseMaxPieceCount;
    env.baseMaxPieceCountBook   = c.baseMaxPieceCountBook;
    env.minLevelCutoff          = c.minLevelCutoff;
    env.influenceMax            = c.influenceMax;
    env.minLevelScaleFactor     = c.minLevelScaleFactor;
    env.minLevelScaleFactorBook = c.minLevelScaleFactorBook;
    env.dupeMultiplier          = c.dupeMultiplier;
    env.incompatibleMultiplier  = c.incompatibleMultiplier;
    env.influencePower          = c.influencePower;
    env.allowBooks              = c.allowBooks;
    env.allowTreasures          = c.allowTreasures;
    env.allowUndiscoverable     = c.allowUndiscoverableEnchantments;
    env.allowZeroEnchantability = c.allowZeroEnchantability;
    env.normalizeRarity         = c.normalizeRarity;
    env.treasureWhitelist       = normalizeIds(c.treasureWhitelist);
    env.undiscoverableWhitelist = normalizeIds(c.undiscoverableWhitelist);
    env.disallowed              = normalizeIds(c.disallowedEnchantments);
    return env;
}

ItemStack outputFor(ItemStack const& in, Power const& power) {
    auto m = matrixOf(in);
    if (!m || m->placed.empty()) return {};
    auto env = envFor(in, power);
    bool ok  = true;
    auto res = m->result(env, ok);
    if (!ok || res.empty()) return {};
    ItemStack out;
    if (isBook(in)) {
        out = game::makeItem("minecraft:enchanted_book", 1);
    } else {
        out = in;
        removeTag(out, kMatrixTag);
    }
    setEnchants(out, res);
    return out;
}

ItemStack outputOf(GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return {};
    auto const& d = data(pos);
    if (game::isEmpty(d.item)) return {};
    return outputFor(d.item, computePower(*region, game::blockPos(pos)));
}

// ---- public: sessions --------------------------------------------------------------------------

std::optional<GlobalPos> openBy(Player const& player) {
    auto it = sessions().find(uuidOf(player));
    if (it == sessions().end()) return std::nullopt;
    return it->second.pos;
}

nlohmann::json stateJson(Player const& player, GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    auto& d      = data(pos);
    Power power;
    if (region) power = computePower(*region, game::blockPos(pos));
    nlohmann::json j;
    j["pos"]            = {pos.dim, pos.x, pos.y, pos.z};
    j["item"]           = game::itemToSnbt(d.item);
    j["lapis"]          = game::itemToSnbt(d.lapis);
    j["charge"]         = d.charge;
    j["chargePerLapis"] = cfg().chargePerLapis;
    j["power"]          = power.power;
    j["influencePower"] = cfg().influencePower;
    j["creative"]       = creative(player);
    j["level"]          = playerLevel(player);
    auto m              = matrixOf(d.item);
    if (m) {
        auto env          = envFor(d.item, power);
        j["matrix"]       = m->toJson();
        j["book"]         = m->book;
        bool canGen       = m->canGeneratePiece(env);
        j["canGenerate"]  = canGen;
        j["price"]        = m->newPiecePrice(env);
        j["minLevel"]     = m->minXpLevel(power.power, env);
        bool xp           = m->validateXp(playerLevel(player), creative(player), power.power, env);
        j["validXp"]      = xp;
        j["active"]       = canGen && xp && (creative(player) || d.charge > 0);
    } else {
        j["matrix"] = nullptr;
        j["active"] = false;
    }
    j["output"] = game::itemToSnbt(region ? outputFor(d.item, power) : ItemStack{});
    return j;
}

void broadcast(GlobalPos const& pos) {
    for (auto& [uuid, s] : sessions()) {
        if (s.pos != pos) continue;
        auto* p = playerByUuid(uuid);
        if (!p) continue;
        // Forms are re-shown by the acting player's own form callbacks only: sending a form to a
        // player who has another one open is refused by the client.
        if (!s.mod) continue;
        auto state = stateJson(*p, pos).dump();
        if (state == s.lastState) continue;
        s.lastState = state;
        net::sendToClient(*p, MatrixStateMsg{true, state, false});
    }
}

bool open(Player& player, GlobalPos const& pos) {
    if (!cfg().enableMatrixEnchanting) return false;
    normalize(pos);
    bool    mod = net::hasClientMod(player);
    Session s{pos, mod, {}};
    sessions()[uuidOf(player)] = s;
    log().info("enchanting: {} opened the matrix enchanter at {} ({})", player.getRealName(), pos.key(), mod ? "screen" : "form");
    if (mod) {
        auto state                             = stateJson(player, pos).dump();
        sessions()[uuidOf(player)].lastState   = state;
        net::sendToClient(player, MatrixStateMsg{true, state, true});
    } else {
        showForm(player, pos);
    }
    return true;
}

void close(Player& player, bool notifyClient) {
    auto it = sessions().find(uuidOf(player));
    if (it == sessions().end()) return;
    bool mod = it->second.mod;
    sessions().erase(it);
    if (notifyClient && mod) net::sendToClient(player, MatrixStateMsg{false, {}});
}

bool operate(Player& player, int op, int arg0, int arg1, int arg2) {
    auto pos = openBy(player);
    if (!pos || !cfg().enableMatrixEnchanting) return false;
    normalize(*pos);
    auto& d = data(*pos);
    auto  m = matrixOf(d.item);
    if (!m) return false;
    auto* region = game::regionOf(pos->dim);
    if (!region) return false;
    auto power = computePower(*region, game::blockPos(*pos));
    auto env   = envFor(d.item, power);
    bool changed = false;
    switch (op) {
    case OperAdd: {
        if (m->canGeneratePiece(env) && m->validateXp(playerLevel(player), creative(player), power.power, env)) {
            bool cr   = creative(player);
            int  cost = m->newPiecePrice(env);
            if (d.charge > 0 || cr) {
                if (m->generatePiece(env, false)) {
                    if (!cr) {
                        addLevels(player, -cost);
                        d.charge = std::max(d.charge - 1, 0);
                    }
                    log().info("enchanting: {} generated a piece ({} pieces)", player.getRealName(), m->count);
                }
            }
        }
        changed = true; // Quark commits even when nothing was generated
        break;
    }
    case OperPlace: changed = m->place(arg0, arg1, arg2); break;
    case OperRemove: changed = m->remove(arg0); break;
    case OperRotate: changed = m->rotate(arg0); break;
    case OperMerge: changed = m->merge(arg0, arg1, env); break;
    default: return false;
    }
    if (changed && op != OperAdd) {
        static constexpr char const* kNames[] = {"generated", "placed", "removed", "rotated", "merged"};
        log().info("enchanting: {} {} a piece", player.getRealName(), kNames[op]);
    }
    if (changed) {
        writeMatrix(d.item, *m);
        markDirty(*pos);
        normalize(*pos);
    }
    broadcast(*pos);
    return changed;
}

bool slotAction(Player& player, std::string const& action, int slot) {
    auto pos = openBy(player);
    if (!pos) return false;
    auto& d    = data(*pos);
    auto& pinv = game::inv::playerInventory(player);
    bool  done = false;
    if (action == "put_item") {
        if (slot < 0 || slot >= game::inv::kPlayerSlots || !game::isEmpty(d.item)) return false;
        auto const& src = pinv.getItem(slot);
        if (game::isEmpty(src)) return false;
        ItemStack one = src;
        one.set(1);
        ItemStack rest = src;
        rest.remove(1);
        pinv.setItem(slot, rest.mCount > 0 ? rest : ItemStack{});
        d.item = one;
        done   = true;
    } else if (action == "put_lapis") {
        if (slot < 0 || slot >= game::inv::kPlayerSlots) return false;
        auto const& src = pinv.getItem(slot);
        if (!isLapis(src)) return false;
        int have = game::isEmpty(d.lapis) ? 0 : d.lapis.mCount;
        int max  = src.getMaxStackSize();
        int move = std::min<int>(src.mCount, max - have);
        if (move <= 0) return false;
        if (have == 0) {
            d.lapis = src;
            d.lapis.set(move);
        } else {
            d.lapis.add(move);
        }
        ItemStack rest = src;
        rest.remove(move);
        pinv.setItem(slot, rest.mCount > 0 ? rest : ItemStack{});
        done = true;
    } else if (action == "take_item") {
        if (game::isEmpty(d.item)) return false;
        auto item = d.item;
        d.item    = ItemStack{};
        giveOrDrop(player, item);
        done = true;
    } else if (action == "take_lapis") {
        if (game::isEmpty(d.lapis)) return false;
        auto l  = d.lapis;
        d.lapis = ItemStack{};
        giveOrDrop(player, l);
        done = true;
    } else if (action == "take_output") {
        auto out = outputOf(*pos);
        if (game::isEmpty(out)) return false;
        d.item = ItemStack{};
        giveOrDrop(player, out);
        playSound(*pos, static_cast<int>(SharedTypes::Legacy::LevelSoundEvent::EnchantingTableUse), 0.9f + (std::rand() % 10) / 100.0f);
        log().info("enchanting: {} enchanted {}", player.getRealName(), out.getTypeName());
        done = true;
    }
    if (!done) return false;
    game::inv::sync(player);
    markDirty(*pos);
    normalize(*pos);
    broadcast(*pos);
    return true;
}

void dropContents(BlockSource& region, GlobalPos const& pos) {
    auto& d = data(pos);
    dropAt(region, pos, d.item);
    dropAt(region, pos, d.lapis);
    d = Data{};
    markDirty(pos);
    std::vector<std::string> viewers;
    for (auto const& [uuid, s] : sessions())
        if (s.pos == pos) viewers.push_back(uuid);
    for (auto const& uuid : viewers)
        if (auto* p = playerByUuid(uuid)) {
            close(*p);
            game::forms::closeFor(*p);
        } else sessions().erase(uuid);
}

// ---- forms (vanilla clients) -------------------------------------------------------------------

namespace {

constexpr char const* kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr char const* kPieceColors[8] = {"§c", "§a", "§9", "§e", "§d", "§b", "§6", "§f"};

void showInsertForm(Player& player, GlobalPos const& pos, bool lapis) {
    using namespace game::forms;
    SimpleForm form(trFor(player, lapis ? "lp.enchanting.form.add_lapis" : "lp.enchanting.form.insert", lapis ? "Add Lapis" : "Insert Item"));
    auto& pinv = game::inv::playerInventory(player);
    int   n    = 0;
    for (int i = 0; i < game::inv::kPlayerSlots; ++i) {
        auto const& s = pinv.getItem(i);
        if (game::isEmpty(s)) continue;
        if (lapis != isLapis(s)) continue;
        ++n;
        form.button(itemName(s), [pos, i, lapis](Player& p) {
            slotAction(p, lapis ? "put_lapis" : "put_item", i);
            refreshFormLater(p, pos);
        });
    }
    if (n == 0) form.content(trFor(player, "lp.enchanting.form.nothing", "Nothing to insert."));
    form.button(trFor(player, "lp.enchanting.form.back", "Back"), [pos](Player& p) { refreshFormLater(p, pos); });
    form.onClose([pos](Player& p) { refreshFormLater(p, pos); });
    form.sendTo(player);
}

void showPieceForm(Player& player, GlobalPos const& pos, int id) {
    using namespace game::forms;
    auto& d = data(pos);
    auto  m = matrixOf(d.item);
    if (!m) return showForm(player, pos);
    auto const* p = m->piece(id);
    auto const* def = p ? defOf(p->ench) : nullptr;
    if (!p || !def) return showForm(player, pos);
    std::vector<std::string> actions{trFor(player, "lp.enchanting.form.place", "Place on the grid")};
    std::vector<int>         mergeTargets;
    for (int pid : m->placed) {
        auto const* t = m->piece(pid);
        if (t && t->ench == p->ench && t->level < def->maxLevel) {
            mergeTargets.push_back(pid);
            actions.push_back(trFor(player, "lp.enchanting.form.merge_into", "Merge into {}", nameOf(player, *def, t->level)));
        }
    }
    std::vector<std::string> cols{"1", "2", "3", "4", "5"};
    CustomForm form(nameOf(player, *def, p->level));
    // Shape preview (5x5 around the centre).
    std::string shape;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            bool on = false;
            for (auto const& b : p->blocks) on = on || (b[0] == x && b[1] == y);
            shape += on ? (x == 0 && y == 0 ? "§e■" : "§f■") : "§8□";
        }
        shape += "\n";
    }
    form.label(shape + "§r" + trFor(player, "lp.enchanting.form.centre", "The yellow cell goes to the chosen column and row."));
    form.dropdown("action", trFor(player, "lp.enchanting.form.action", "Action"), actions, 0);
    form.dropdown("x", trFor(player, "lp.enchanting.form.column", "Column"), cols, 2);
    form.dropdown("y", trFor(player, "lp.enchanting.form.row", "Row"), cols, 2);
    form.stepSlider("rot", trFor(player, "lp.enchanting.form.rotate", "Rotate (clockwise)"), {"0°", "90°", "180°", "270°"}, 0);
    form.onSubmit([pos, id, mergeTargets](Player& pl, Response const& r) {
        int rot = r.index("rot", 0);
        for (int i = 0; i < rot; ++i) operate(pl, OperRotate, id, 0, 0);
        int action = r.index("action", 0);
        if (action > 0 && action - 1 < static_cast<int>(mergeTargets.size())) {
            operate(pl, OperMerge, mergeTargets[static_cast<size_t>(action - 1)], id, 0);
        } else if (!operate(pl, OperPlace, id, r.index("x", 2), r.index("y", 2))) {
            pl.sendMessage(trFor(pl, "lp.enchanting.form.no_room", "§cThe piece does not fit there."));
        }
        refreshFormLater(pl, pos);
    });
    form.onClose([pos](Player& pl) { refreshFormLater(pl, pos); });
    form.sendTo(player);
}

} // namespace

namespace {
void showFormImpl(Player& player, GlobalPos const& pos) {
    using namespace game::forms;
    normalize(pos);
    auto& d      = data(pos);
    auto  j      = stateJson(player, pos);
    auto  m      = matrixOf(d.item);
    auto  output = game::itemFromSnbt(j.value("output", "{}"));

    std::string text;
    text += trFor(player, "lp.enchanting.form.item", "Item: {}", itemName(d.item)) + "\n";
    text += trFor(player, "lp.enchanting.form.lapis", "Lapis: {}  Charge: {}/{}", game::isEmpty(d.lapis) ? 0 : d.lapis.mCount, d.charge,
                  cfg().chargePerLapis) + "\n";
    text += trFor(player, "lp.enchanting.form.bookshelves", "Bookshelves: {}", j.value("power", 0)) + "\n";
    if (m) {
        if (j.value("canGenerate", false) && !creative(player)) {
            int price = j.value("price", 1), minL = j.value("minLevel", 0);
            text += trFor(player, "lp.enchanting.form.cost", "New piece: {} levels (needs level {})", price, std::max(price, minL)) + "\n";
        }
        text += "\n";
        for (int y = 0; y < Matrix::kHeight; ++y) {
            for (int x = 0; x < Matrix::kWidth; ++x) {
                int id = m->grid[x][y];
                if (id < 0) text += "§8·";
                else {
                    auto idx = std::find(m->placed.begin(), m->placed.end(), id) - m->placed.begin();
                    auto const* pc = m->piece(id);
                    text += std::string(kPieceColors[pc ? pc->type % 8 : 0]) + kLetters[idx % 26];
                }
                text += " ";
            }
            text += "§r\n";
        }
        for (size_t i = 0; i < m->placed.size(); ++i) {
            auto const* p = m->piece(m->placed[i]);
            auto const* def = p ? defOf(p->ench) : nullptr;
            if (!p || !def) continue;
            text += std::string(kPieceColors[p->type % 8]) + kLetters[i % 26] + "§r " + nameOf(player, *def, p->level);
            if (int mx = maxXpFor(*def, p->level); mx > 0)
                text += " §7(" + trFor(player, "quark.gui.enchanting.upgrade", "Upgrade: {}/{}", p->xp, mx) + ")§r";
            text += "\n";
        }
    } else if (!game::isEmpty(d.item)) {
        text += trFor(player, "lp.enchanting.form.not_enchantable", "§7This item can't be enchanted.") + "\n";
    }

    SimpleForm form(trFor(player, "lp.enchanting.title", "Enchant"), text);
    if (!game::isEmpty(output))
        form.button(trFor(player, "lp.enchanting.form.take_output", "Take {}", itemName(output)),
                    [pos](Player& p) {
                        slotAction(p, "take_output", -1);
                        refreshFormLater(p, pos);
                    });
    if (m && j.value("active", false)) {
        form.button(creative(player) ? trFor(player, "lp.enchanting.form.generate_free", "+ New piece")
                                     : trFor(player, "lp.enchanting.form.generate", "+ New piece ({} levels)", j.value("price", 1)),
                    [pos](Player& p) {
                        operate(p, OperAdd, 0, 0, 0);
                        refreshFormLater(p, pos);
                    });
    }
    if (m) {
        for (int id : m->benched) {
            auto const* p   = m->piece(id);
            auto const* def = p ? defOf(p->ench) : nullptr;
            if (!p || !def) continue;
            form.button(trFor(player, "lp.enchanting.form.piece", "Piece: {}", nameOf(player, *def, p->level)),
                        [pos, id](Player& pl) { showPieceForm(pl, pos, id); });
        }
        for (size_t i = 0; i < m->placed.size(); ++i) {
            int         id  = m->placed[i];
            auto const* p   = m->piece(id);
            auto const* def = p ? defOf(p->ench) : nullptr;
            if (!p || !def) continue;
            form.button(trFor(player, "lp.enchanting.form.remove", "Remove {} {}", std::string(1, kLetters[i % 26]), nameOf(player, *def, p->level)),
                        [pos, id](Player& pl) {
                            operate(pl, OperRemove, id, 0, 0);
                            refreshFormLater(pl, pos);
                        });
        }
    }
    if (game::isEmpty(d.item))
        form.button(trFor(player, "lp.enchanting.form.insert", "Insert Item"), [pos](Player& p) { showInsertForm(p, pos, false); });
    else
        form.button(trFor(player, "lp.enchanting.form.take_item", "Take {} back", itemName(d.item)),
                    [pos](Player& p) {
                        slotAction(p, "take_item", -1);
                        refreshFormLater(p, pos);
                    });
    form.button(trFor(player, "lp.enchanting.form.add_lapis", "Add Lapis"), [pos](Player& p) { showInsertForm(p, pos, true); });
    if (!game::isEmpty(d.lapis))
        form.button(trFor(player, "lp.enchanting.form.take_lapis", "Take Lapis back"), [pos](Player& p) {
            slotAction(p, "take_lapis", -1);
            refreshFormLater(p, pos);
        });
    form.onClose([](Player& p) { close(p, false); });
    form.sendTo(player);
}
} // namespace

namespace {
void showForm(Player& player, GlobalPos const& pos) { showFormImpl(player, pos); }
} // namespace

// ---- lifecycle helpers used by the module ------------------------------------------------------

void resetCaches() {
    cache().clear();
    dirty().clear();
    sessions().clear();
    pendingRefresh().clear();
    influences() = {};
}
void flush() { flushAll(); }
void reparseInfluences() { influences() = {}; }

/// Validates the open sessions (distance, block) and resends changed states. Every 20 ticks.
void tickSessions() {
    std::vector<std::string> gone;
    std::set<GlobalPos>      seen;
    for (auto const& [uuid, s] : sessions()) {
        auto* p      = playerByUuid(uuid);
        auto* region = game::regionOf(s.pos.dim);
        if (!p || !region || distanceSq(*p, s.pos) > 64.0 + 16.0 || !isTableId(game::blockIdAt(*region, game::blockPos(s.pos)))) {
            gone.push_back(uuid);
            continue;
        }
        seen.insert(s.pos);
    }
    for (auto const& uuid : gone) {
        if (auto* p = playerByUuid(uuid)) {
            close(*p);
            if (!net::hasClientMod(*p)) game::forms::closeFor(*p);
        } else sessions().erase(uuid);
    }
    for (auto const& pos : seen) {
        normalize(pos);
        // Only mod clients get unsolicited updates (forms are re-sent after actions only).
        for (auto& [uuid, s] : sessions()) {
            if (s.pos != pos || !s.mod) continue;
            auto* p = playerByUuid(uuid);
            if (!p) continue;
            auto state = stateJson(*p, pos).dump();
            if (state == s.lastState) continue;
            s.lastState = state;
            net::sendToClient(*p, MatrixStateMsg{true, state, false});
        }
    }
}

} // namespace lp::feature::enchanting::table
