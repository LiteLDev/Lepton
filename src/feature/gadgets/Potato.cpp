// Tiny potato (Quark oddities/TinyPotatoModule): a 4x6x4 potato block that believes in you. It holds
// one item per side, jumps and puts out a heart when used, changes its look with its name and has a
// few name easter eggs. Custom blocks have no block entity: the name, angry flag and held items
// live in the world DB, the look and jump frame in block states. See docs/spec/gadgets.md.

#include "feature/gadgets/Potato.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/service/Bedrock.h"

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/BuiltInActorComponents.h"
#include "mc/world/actor/state/ActorPropertyUtils.h"
#include "mc/world/actor/state/PropertyComponent.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "core/GlobalPos.h"
#include "feature/gadgets/Gadgets.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "net/Net.h"

namespace lp::feature::gadgets::potato {

using core::GlobalPos;
namespace bs = game::blockstates;

// ---- names --------------------------------------------------------------------------------------------

namespace {

std::string stripFormatting(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        // "§" is two bytes in UTF-8 (C2 A7) followed by the code character
        if (static_cast<unsigned char>(s[i]) == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA7) {
            i += 2;
            continue;
        }
        out += s[i];
    }
    return out;
}

std::string trim(std::string s) {
    auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t b = 0;
    while (b < s.size() && ws(static_cast<unsigned char>(s[b]))) ++b;
    return s.substr(b);
}

bool matches(std::string const& name, std::string const& m) { return name == m || name.starts_with(m + " "); }
std::string removeFront(std::string const& name, std::string const& m) { return trim(name.substr(m.size())); }

/// Quark's model name: ResourceLocation-safe characters only.
std::string modelName(std::string const& name) {
    std::string out = name;
    for (auto& c : out) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '_' || c == '-';
        if (!ok) c = '_';
    }
    return out;
}

enum Look : int {
    Default = 0, Angry, Halloween, Ace, Agender, Aro, Aureylian, Bi, Bob, Bosnia, Dinnerbone, Enby, Pride,
    Genderfluid, Lesbian, Gray, Double, Flat, Pan, Robo, Triple, Sus, Tomato, Trans, Invisible
};

std::map<std::string, int, std::less<>> const& lookTable() {
    static std::map<std::string, int, std::less<>> const t = [] {
        std::map<std::string, int, std::less<>> m;
        auto add = [&](int v, std::initializer_list<char const*> names) {
            for (auto const* n : names) m[n] = v;
        };
        add(Halloween, {"halloween", "spooky", "spooktater"});
        add(Ace, {"acetater", "asexualtater", "tacer", "taceter"});
        add(Agender, {"agendertater"});
        add(Aro, {"aromantictater", "arotater", "tataro"});
        add(Aureylian, {"aureylian"});
        add(Bi, {"bitater", "biter"});
        add(Bob, {"bob", "snences"});
        add(Bosnia, {"bosniantater", "bosnia_herzegovina", "bosniaherzegovina", "botania_herzegovina", "botaniaherzegovina",
                     "botaniatater", "botater"});
        add(Dinnerbone, {"dinnerbone", "grumm"});
        add(Enby, {"enbytater", "eutrotater", "nbtater", "nonbinarytater", "wiretater"});
        add(Pride, {"gay", "gaytater", "gayter", "lgbtater", "pridetater"});
        add(Genderfluid, {"genderfluidtater", "taterfluid"});
        add(Lesbian, {"lesbiantater", "girlstater", "lesbiabtater", "lesbiamtater", "lesbitater", "lessbientater"});
        add(Gray, {"graytater", "grayter", "greytater", "greyter", "kyle_hyde"});
        add(Double, {"kingdaddydmac"});
        add(Flat, {"pahimar"});
        add(Pan, {"pantater", "panter"});
        add(Robo, {"robotater"});
        add(Triple, {"snorps", "manytater", "pluraltater", "systater", "systemtater"});
        add(Sus, {"tatersus"});
        add(Tomato, {"tomater"});
        add(Trans, {"transtater", "eggtater", "tategg"});
        add(Invisible, {"mami", "soaryn"});
        return m;
    }();
    return t;
}

bool isHalloween() {
    auto t  = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    return tm.tm_mon == 9 && tm.tm_mday == 31;
}

} // namespace

NameInfo cleanName(std::string_view raw) {
    std::string s = stripFormatting(raw);
    s             = trim(s);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    NameInfo info;
    for (auto const* e : {"enchanted", "glowy", "shiny", "gay"}) {
        if (matches(s, e)) {
            info.enchanted = true;
            s              = removeFront(s, e);
            break;
        }
    }
    if (info.enchanted) {
        static char const* const kRunes[] = {"white", "orange", "magenta", "light blue", "yellow", "lime", "pink", "gray",
                                             "light gray", "cyan", "purple", "blue", "brown", "green", "red", "black",
                                             "rainbow", "blank"};
        bool found = false;
        // longest first so "light blue" wins over "blue"... Quark walks the enum in order and needs
        // "name " as a prefix, so "light blue x" never matches "blue": the order does not matter.
        for (auto const* r : kRunes) {
            std::string key = r;
            if (matches(s, key)) {
                s     = removeFront(s, key);
                found = true;
                break;
            }
            if (key.find("gray") != std::string::npos) {
                auto grey = key;
                grey.replace(grey.find("gray"), 4, "grey");
                if (matches(s, grey)) {
                    s     = removeFront(s, grey);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            for (auto const* r : {"gay homosexual", "rainbow", "lgbt", "lgbtq", "lgbtq+", "gay"}) {
                if (matches(s, r)) {
                    s = removeFront(s, r);
                    break;
                }
            }
        }
    }
    info.name = s;
    return info;
}

int variantFor(std::string_view rawName, bool angry, bool halloween, bool jumping) {
    auto name = modelName(cleanName(rawName).name);
    if (name == "eloraam" && jumping) return Invisible;
    if (!name.empty()) {
        auto const& t = lookTable();
        if (auto it = t.find(name); it != t.end()) return it->second;
    }
    if (angry) return Angry;
    if (halloween) return Halloween;
    return Default;
}

std::string genderFor(std::string_view rawName) {
    static std::map<std::string, std::string, std::less<>> const g = [] {
        std::map<std::string, std::string, std::less<>> m;
        for (auto const* n : {"girlstater", "lesbiabtater", "lesbiamtater", "lesbiantater", "lesbitater", "lessbientater"})
            m[n] = "daughter";
        for (auto const* n : {"agendertater", "enbytater", "nbtater", "nonbinarytater", "robotater", "wiretater", "eutrotater",
                              "bob", "snences", "genderfluidtater", "taterfluid", "eggtater", "tategg", "transtater"})
            m[n] = "child";
        for (auto const* n : {"manytater", "pluraltater", "snorps", "systater", "systemtater"}) m[n] = "children";
        m["tomater"] = "tomato";
        return m;
    }();
    auto name = cleanName(rawName).name;
    if (auto it = g.find(name); it != g.end()) return it->second;
    return "son";
}

int variantOf(Block const& block) {
    return bs::getInt(block, "lepton:look").value_or(0) + 16 * bs::getInt(block, "lepton:look_page").value_or(0);
}

int jumpFrame(int ticksLeft) { return ticksLeft <= 0 ? 0 : std::min(10, (ticksLeft + 1) / 2); }

// ---- per-block data ---------------------------------------------------------------------------------------

namespace {

constexpr char const* kDbPrefix = "lp:gadgets:tater:";

std::map<GlobalPos, Tater> gTaters; ///< every known potato of the world
std::set<GlobalPos>        gDirty;
std::set<GlobalPos>        gCreativeBreaks; ///< broken by a creative player this tick
std::set<GlobalPos>        gPendingBreaks;  ///< removed, drops not spawned yet
std::unordered_map<int64_t, uint64_t> gLastUse;
struct Looking {
    std::string name;
    uint64_t    sentAt = 0;
};
std::unordered_map<int64_t, Looking> gLooking;
std::vector<ll::event::ListenerPtr>  gListeners;

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

nlohmann::json toJson(Tater const& t) {
    nlohmann::json j;
    j["name"]  = t.name;
    j["angry"] = t.angry;
    j["items"] = nlohmann::json::array();
    for (auto const& i : t.items) j["items"].push_back(i);
    j["displays"] = t.displays;
    return j;
}

Tater fromJson(nlohmann::json const& j) {
    Tater t;
    t.name  = j.value("name", std::string());
    t.angry = j.value("angry", false);
    if (j.contains("items") && j["items"].is_array())
        for (size_t i = 0; i < 6 && i < j["items"].size(); ++i)
            if (j["items"][i].is_string()) t.items[i] = j["items"][i].get<std::string>();
    if (j.contains("displays") && j["displays"].is_array())
        for (size_t i = 0; i < 6 && i < j["displays"].size(); ++i)
            if (j["displays"][i].is_number_integer()) t.displays[i] = j["displays"][i].get<int64_t>();
    return t;
}

void markDirty(GlobalPos const& pos) { gDirty.insert(pos); }

void flush() {
    if (!game::db::available()) return;
    for (auto const& pos : gDirty) {
        auto it = gTaters.find(pos);
        if (it == gTaters.end()) game::db::erase(kDbPrefix + pos.key());
        else game::db::save(kDbPrefix + pos.key(), toJson(it->second));
    }
    gDirty.clear();
}

bool isPotato(Block const& b) { return game::blockId(b) == kPotato; }

ItemStack itemAt(Tater const& t, int slot) {
    if (t.items[static_cast<size_t>(slot)].empty()) return {};
    return game::itemFromSnbt(t.items[static_cast<size_t>(slot)]);
}
void setItemAt(Tater& t, int slot, ItemStack const& s) {
    t.items[static_cast<size_t>(slot)] = game::isEmpty(s) ? std::string() : game::itemToSnbt(s);
}

/// The look and jump frame states of the block at `pos`.
void applyStates(BlockSource& region, GlobalPos const& pos, Tater const& t) {
    auto        bp  = game::blockPos(pos);
    auto const& cur = region.getBlock(bp);
    if (!isPotato(cur)) return;
    int variant = variantFor(t.name, t.angry, isHalloween(), t.jumpTicks > 0);
    int frame   = jumpFrame(t.jumpTicks);
    if (variantOf(cur) == variant && bs::getInt(cur, "lepton:jump") == frame) return;
    if (auto const* b = bs::with(cur, {{"lepton:look", variant % 16}, {"lepton:look_page", variant / 16}, {"lepton:jump", frame}}))
        game::setBlock(region, bp, *b);
}

std::string customNameOf(ItemStackBase const& s) {
    if (getBool(s, "lp:auto_name")) return {}; // the "Angry Potato" display name we set ourselves
    return s.getCustomName();
}

void spawnItem(BlockSource& region, GlobalPos const& pos, ItemStack const& item) {
    if (game::isEmpty(item)) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    Vec3 at{pos.x + 0.5f, pos.y + 0.3f, pos.z + 0.5f};
    level->getSpawner().spawnItem(region, item, nullptr, at, 10);
}

// ---- held item displays ----------------------------------------------------------------------------------
// Custom blocks can't render item stacks: every held item is shown by a `lepton:tiny_potato_item`
// display entity holding it (Quark's TinyPotatoRenderer#renderItems poses), posed by its properties.

constexpr char const* kItemDisplay = "lepton:tiny_potato_item";

/// Java horizontal direction of the potato's face (2 north, 3 south, 4 west, 5 east).
int facingOf(Block const& b) {
    auto d = bs::getString(b, "minecraft:cardinal_direction").value_or("south");
    return d == "north" ? 2 : d == "west" ? 4 : d == "east" ? 5 : 3;
}

float yawFor(int dir) {
    switch (dir) {
    case 2: return 180.f;
    case 4: return 90.f;
    case 5: return -90.f;
    default: return 0.f;
    }
}

void setIntProperty(Actor& actor, std::string_view name, int value) {
    if (auto p = actor.getEntityContext().tryGetComponent<PropertyComponent>())
        ActorPropertyUtils::tryQueueIntValue(*p, actor, HashedString(name).getHash(), value);
}

void orient(Actor& actor, float yaw) {
    actor.mBuiltInComponents->mActorRotationComponent->mRot     = Vec2{0.f, yaw};
    actor.mBuiltInComponents->mActorRotationComponent->mRotPrev = Vec2{0.f, yaw};
}

int kindOf(ItemStack const& s) {
    if (game::itemId(s) == kPotato) return 2;
    return s.mBlock ? 1 : 0;
}

void despawnDisplays(Tater& t) {
    for (auto& id : t.displays) {
        if (id)
            if (auto* a = game::entities::find(id)) game::entities::despawn(*a);
        id = 0;
    }
}

void syncDisplays(BlockSource& region, GlobalPos const& pos, Tater& t) {
    t.displaysDirty = false;
    auto const& block  = region.getBlock(game::blockPos(pos));
    int         facing = facingOf(block);
    Vec3        at{pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f};
    bool        changed = false;
    for (int i = 0; i < 6; ++i) {
        auto  item = itemAt(t, i);
        auto& id   = t.displays[static_cast<size_t>(i)];
        if (game::isEmpty(item)) {
            if (id) {
                if (auto* a = game::entities::find(id)) game::entities::despawn(*a);
                id      = 0;
                changed = true;
            }
            continue;
        }
        Actor* a = id ? game::entities::find(id) : nullptr;
        if (!a) {
            a = game::entities::spawn(region, kItemDisplay, at);
            if (!a) continue;
            id      = game::entities::idOf(*a);
            changed = true;
        }
        a->teleportTo(at, false, 0, 1, false);
        orient(*a, yawFor(i >= 2 ? i : facing));
        a->setCarriedItem(item);
        setIntProperty(*a, "lepton:face", i);
        setIntProperty(*a, "lepton:kind", kindOf(item));
    }
    if (changed) markDirty(pos);
}

/// Display entities nobody owns (potato broken while unloaded, edited worlds).
void sweepDisplays() {
    std::set<int64_t> owned;
    for (auto const& [pos, t] : gTaters)
        for (auto id : t.displays)
            if (id) owned.insert(id);
    for (auto* a : game::entities::ofType(kItemDisplay))
        if (!owned.contains(game::entities::idOf(*a))) game::entities::despawn(*a);
}

/// Wakes the comparators next to `pos` after the potato's items changed.
void updateComparators(BlockSource& region, GlobalPos const& pos) {
    auto bp = game::blockPos(pos);
    region.updateNeighborsAt(bp);
    // comparators re-read their input in their queued tick (as when a container changes)
    for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
        for (int dist = 1; dist <= 2; ++dist) { // directly, or through one block
            BlockPos    at{bp.x + dx * dist, bp.y, bp.z + dz * dist};
            auto const& b = region.getBlock(at);
            if (b.getTypeName().find("comparator") != std::string::npos) region.addToTickingQueue(at, b, 2);
        }
    }
}
void onBroken(int dim, GlobalPos pos) {
    auto it = gTaters.find(pos);
    bool creative = gCreativeBreaks.erase(pos) > 0;
    auto* region  = game::regionOf(dim);
    if (!region) return;
    Tater t = it != gTaters.end() ? it->second : Tater{};
    despawnDisplays(t);
    for (int i = 0; i < 6; ++i) spawnItem(*region, pos, itemAt(t, i));
    if (!creative && cfg().enableTinyPotato) spawnItem(*region, pos, makePotatoItem(t.name, t.angry, nullptr));
    if (it != gTaters.end()) gTaters.erase(it);
    markDirty(pos);
    log().info("gadgets: tiny potato at {} broken{}", pos.key(), creative ? " (creative)" : "");
}

// ---- events -------------------------------------------------------------------------------------------------

void onInteractEvent(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !cfg().enableTinyPotato) return;
    auto& region = player.getDimensionBlockSource();
    if (!isPotato(region.getBlock(ev.blockPos()))) return;
    if (player.isSneaking() && !game::isEmpty(ev.item())) return; // sneak + item: place / use the item (Java)
    ev.cancel();
    auto id = player.getOrCreateUniqueID().rawID;
    if (auto it = gLastUse.find(id); it != gLastUse.end() && now() - it->second < 5) return;
    gLastUse[id] = now();
    auto pos     = game::globalPos(region, ev.blockPos());
    int  face    = static_cast<int>(ev.face());
    later(1, [id, pos, face] {
        auto* p = playerById(id);
        if (!p) return;
        auto* r = game::regionOf(pos.dim);
        if (!r || !isPotato(r->getBlock(game::blockPos(pos)))) return;
        interact(*p, pos, face);
    });
}

struct HeldPotato {
    std::string name;
    bool        angry = false;
    uint64_t    at    = 0;
};
std::unordered_map<int64_t, HeldPotato> gPlacing; ///< the potato item in hand when placing started

void onPlacingEvent(ll::event::PlayerPlacingBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& held = player.getSelectedItem();
    if (game::itemId(held) != kPotato) return;
    gPlacing[player.getOrCreateUniqueID().rawID] = HeldPotato{customNameOf(held), getBool(held, "quark:is_angry"), now()};
}

void onPlacedEvent(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !isPotato(ev.placedBlock())) return;
    // the stack may already be used up: take the name captured when the placement started
    std::string name;
    bool        angry = false;
    auto        it    = gPlacing.find(player.getOrCreateUniqueID().rawID);
    if (it != gPlacing.end() && now() - it->second.at <= 2) {
        name  = it->second.name;
        angry = it->second.angry;
        gPlacing.erase(it);
    } else if (auto const& held = player.getSelectedItem(); game::itemId(held) == kPotato) {
        name  = customNameOf(held);
        angry = getBool(held, "quark:is_angry");
    }
    auto        pos   = game::globalPos(player.getDimensionBlockSource(), ev.pos());
    later(1, [pos, name, angry] {
        auto* r = game::regionOf(pos.dim);
        if (!r || !isPotato(r->getBlock(game::blockPos(pos)))) return;
        registerAt(pos, name, angry);
    });
}

void onDestroyEvent(ll::event::PlayerDestroyBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !player.isCreative()) return;
    auto& region = player.getDimensionBlockSource();
    if (!isPotato(region.getBlock(ev.pos()))) return;
    auto pos = game::globalPos(region, ev.pos());
    gCreativeBreaks.insert(pos);
    later(2, [pos] { gCreativeBreaks.erase(pos); });
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    if (ev.layer() != 0) return;
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    // cheap check first: this runs for every block change of the server
    if (ev.previousBlock().getTypeName() != kPotato || ev.newBlock().getTypeName() == kPotato) return;
    auto pos = game::globalPos(region, ev.pos());
    if (!gPendingBreaks.insert(pos).second) return; // one break can report several changes
    int dim = pos.dim;
    later(1, [dim, pos] {
        gPendingBreaks.erase(pos);
        onBroken(dim, pos);
    });
}

void sendNameTo(Player& p, std::string const& name) {
    auto id  = p.getOrCreateUniqueID().rawID;
    auto& lk = gLooking[id];
    if (net::hasClientMod(p)) {
        if (lk.name != name) {
            PotatoNameMessage m;
            m.name = name;
            auto clean = cleanName(name).name;
            if (clean == "pahimar") m.extra = trFor(p, "lp.gadgets.wip", "[WIP]");
            else if (clean == "soaryn") m.extra = trFor(p, "lp.gadgets.soon", "(soon)");
            net::sendToClient(p, m);
        }
    } else if (!name.empty() && (lk.name != name || now() - lk.sentAt >= 30)) {
        auto clean = cleanName(name).name;
        std::string text = name;
        if (clean == "pahimar") text += " " + trFor(p, "lp.gadgets.wip", "[WIP]");
        else if (clean == "soaryn") text += " " + trFor(p, "lp.gadgets.soon", "(soon)");
        actionBar(p, text);
        lk.sentAt = now();
    } else if (name.empty() && !lk.name.empty()) {
        actionBar(p, " ");
    }
    lk.name = name;
}

void tickNames() {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([](Player& p) {
        std::string name;
        if (cfg().enableTinyPotato && !gTaters.empty()) {
            auto hit = p.traceRay(p.isCreative() ? 6.0f : 5.0f, false, true);
            if (hit.mType == HitResultType::Tile) {
                auto pos = game::globalPos(p.getDimensionBlockSource(), hit.mBlock);
                if (auto it = gTaters.find(pos); it != gTaters.end()) name = it->second.name;
            }
        }
        sendNameTo(p, name);
        return true;
    });
}

constexpr char const* kTypos[] = {"vaskii", "vazki", "voskii", "vazkkii", "vazkki", "vazzki", "vaskki",
                                  "vozkii", "vazkil", "vaskil", "vazkill", "vaskill", "vaski"};

/// Quark's TinyPotatoBlockItem.inventoryTick: a potato named with a typo of "vazkii" sings.
void tickWrongNeighbourhood() {
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([](Player& p) {
        auto& inv     = game::inv::playerInventory(p);
        bool  changed = false;
        for (int i = 0; i < game::inv::kPlayerSlots; ++i) {
            ItemStack s = inv.getItem(i);
            if (game::itemId(s) != kPotato) continue;
            auto name = stripFormatting(customNameOf(s));
            if (std::find(std::begin(kTypos), std::end(kTypos), name) == std::end(kTypos)) continue;
            int n = getInt(s, "quark:ticks").value_or(0);
            if (n >= 17) continue;
            p.sendMessage("§c" + trFor(p, "quark.misc.you_came_to_the_wrong_neighborhood." + std::to_string(n), ""));
            putInt(s, "quark:ticks", n + 1);
            inv.setItem(i, s);
            changed = true;
        }
        if (changed) game::inv::sync(p);
        return true;
    });
}

} // namespace

int comparatorSignal(Tater const& t) {
    int n = 0;
    for (auto const& i : t.items)
        if (!i.empty()) ++n;
    return n == 0 ? 0 : static_cast<int>(std::floor(static_cast<float>(n) / 6.0f * 14.0f)) + 1;
}

int64_t displayAt(GlobalPos const& pos, int slot) {
    auto it = gTaters.find(pos);
    return it == gTaters.end() || slot < 0 || slot > 5 ? 0 : it->second.displays[static_cast<size_t>(slot)];
}

std::optional<Tater> dataAt(GlobalPos const& pos) {
    auto it = gTaters.find(pos);
    if (it == gTaters.end()) return std::nullopt;
    return it->second;
}

void registerAt(GlobalPos const& pos, std::string const& name, bool angry) {
    auto& t = gTaters[pos];
    t.name  = name;
    t.angry = angry;
    markDirty(pos);
    if (auto* r = game::regionOf(pos.dim)) applyStates(*r, pos, t);
    log().info("gadgets: tiny potato placed at {} (variant {})", pos.key(), variantFor(name, angry, isHalloween(), false));
}

void jump(GlobalPos const& pos) {
    auto it = gTaters.find(pos);
    if (it == gTaters.end() || it->second.jumpTicks > 0) return;
    it->second.jumpTicks = 20;
    if (auto* r = game::regionOf(pos.dim)) applyStates(*r, pos, it->second);
}

ItemStack makePotatoItem(std::string const& name, bool angry, Player const* player) {
    auto s = game::makeItem(kPotato);
    if (game::isEmpty(s)) return s;
    if (!name.empty()) {
        s.setCustomName(Bedrock::Safety::RedactableString(name));
    } else if (angry) {
        auto label = player ? trFor(*player, "block.quark.tiny_potato.angry", "Angry Potato")
                            : tr("block.quark.tiny_potato.angry", "Angry Potato");
        s.setCustomName(Bedrock::Safety::RedactableString("§r" + label));
        putBool(s, "lp:auto_name", true);
    }
    if (angry) putBool(s, "quark:is_angry", true);
    return s;
}

void interact(Player& player, GlobalPos const& pos, int face) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return;
    auto& t = gTaters[pos]; // unknown potatoes (placed by commands) become known here
    if (face >= 0 && face < 6) {
        ItemStack held = player.getSelectedItem();
        ItemStack at   = itemAt(t, face);
        if (!game::isEmpty(at) && game::isEmpty(held)) {
            player.setSelectedItem(at);
            setItemAt(t, face, {});
        } else if (!game::isEmpty(held)) {
            ItemStack copy = held;
            copy.set(1);
            held.remove(1);
            if (game::isEmpty(held)) {
                player.setSelectedItem(at);
            } else {
                player.setSelectedItem(held);
                if (!game::isEmpty(at)) {
                    ItemStack back = at;
                    if (!player.add(back) || !game::isEmpty(back)) player.drop(back, false);
                }
            }
            setItemAt(t, face, copy);
        }
        game::inv::sync(player);
        t.displaysDirty = true;
        syncDisplays(*region, pos, t);
        updateComparators(*region, pos);
    }
    markDirty(pos);

    jump(pos);
    auto clean = cleanName(t.name).name;
    if (!t.name.empty() && t.soundCd == 0) {
        char const* sound = clean == "shia labeouf"    ? "lepton.block.potato.do_it"
                          : clean == "joe biden"       ? "lepton.block.potato.soda"
                          : clean == "yungnickyoung"   ? "lepton.block.potato.yung"
                                                       : nullptr;
        if (sound) {
            t.soundCd = 20;
            playSoundNear(pos.dim, sound, Vec3{pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, 1.0f, 1.0f, 16);
        }
    }
    // the potato's children
    ItemStack tater;
    bool      many = false;
    for (int i = 0; i < 6; ++i) {
        auto s = itemAt(t, i);
        if (game::itemId(s) != kPotato) continue;
        if (game::isEmpty(tater)) tater = s;
        else {
            many = true;
            break;
        }
    }
    if (!game::isEmpty(tater)) {
        std::string gender = many ? "children" : "son";
        if (!many && !customNameOf(tater).empty()) gender = genderFor(customNameOf(tater));
        std::string fallback = "Don't talk to me or my " + gender + " ever again.";
        actionBar(player, trFor(player, "quark.misc.my_" + gender, fallback));
    }
    // the heart: a random spot on top of the 4x6x4 shape
    std::uniform_real_distribution<float> u(0.f, 1.f);
    particle(pos.dim, "minecraft:heart_particle", Vec3{pos.x + 0.375f + u(rng()) * 0.25f, pos.y + 0.375f + 0.3f, pos.z + 0.375f + u(rng()) * 0.25f});
    log().info("gadgets: tiny potato at {} used by {} (face {})", pos.key(), player.getRealName(), face);
}

void install() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteractEvent));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(onPlacingEvent));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlacedEvent));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(onDestroyEvent));
    gListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
}

void uninstall() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
    uninstallComparator();
    flush();
}

void tick() {
    if (gTaters.empty() && now() % 30 != 0) return;
    if (cfg().enableTinyPotato) {
        std::vector<GlobalPos> gone;
        std::uniform_int_distribution<int> d100(0, 99);
        for (auto& [pos, t] : gTaters) {
            auto* r = game::regionOf(pos.dim);
            if (!r) continue;
            auto bp = game::blockPos(pos);
            if (!r->hasChunksAt(bp, 0, false)) continue;
            if (!isPotato(r->getBlock(bp))) {
                gone.push_back(pos); // removed while we did not see it (e.g. unloaded)
                continue;
            }
            if (t.soundCd > 0) --t.soundCd;
            bool changed = false;
            if (t.jumpTicks > 0) {
                int before = jumpFrame(t.jumpTicks);
                --t.jumpTicks;
                changed = jumpFrame(t.jumpTicks) != before;
            } else if (d100(rng()) == 0) {
                t.jumpTicks = 20;
                changed     = true;
            }
            if (changed) applyStates(*r, pos, t);
            if (!t.displaysDirty && now() % 100 == 0)
                for (int i = 0; i < 6 && !t.displaysDirty; ++i)
                    if (!t.items[static_cast<size_t>(i)].empty() && !game::entities::find(t.displays[static_cast<size_t>(i)])) t.displaysDirty = true;
            if (t.displaysDirty) syncDisplays(*r, pos, t);
        }
        for (auto const& pos : gone) {
            despawnDisplays(gTaters[pos]);
            gTaters.erase(pos);
            markDirty(pos);
        }
    }
    if (now() % 200 == 0 && cfg().enableTinyPotato) sweepDisplays();
    if (now() % 5 == 0) tickNames();
    if (now() % 30 == 0 && cfg().enableTinyPotato) tickWrongNeighbourhood();
}

void onWorldReady() {
    installComparator();
    gTaters.clear();
    gDirty.clear();
    std::vector<std::string> keys;
    game::db::forEachKey(kDbPrefix, [&](std::string_view suffix) { keys.emplace_back(suffix); });
    for (auto const& k : keys) {
        GlobalPos p;
        if (sscanf_s(k.c_str(), "%d:%d:%d:%d", &p.dim, &p.x, &p.y, &p.z) != 4) continue;
        if (auto j = game::db::load(kDbPrefix + k)) gTaters[p] = fromJson(*j);
    }
    if (!gTaters.empty()) log().info("gadgets: {} tiny potato(es) known", gTaters.size());
}

void onWorldSave() { flush(); }

void onWorldClosed() {
    gTaters.clear();
    gDirty.clear();
    gCreativeBreaks.clear();
    gPendingBreaks.clear();
    gPlacing.clear();
    gLastUse.clear();
    gLooking.clear();
}

} // namespace lp::feature::gadgets::potato
