// U17 placement, VariantSelector (experimental): Quark's VariantsConfig (variant map built from the
// block registry), the per-player saved variant, its companion messages, the vanilla-client
// form, dropped-item conversion and the Hammer.

#include "feature/placement/Placement.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BedrockSpawner.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Forms.h"
#include "game/Items.h"
#include "game/Shapes.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::feature::placement {

LP_REGISTER_MESSAGE(VariantMsg);
LP_REGISTER_MESSAGE(VariantRestoreMsg);

namespace bs = game::blockstates;

// ---- the variant map (Quark's VariantsConfig) ------------------------------------------------

namespace {

struct VariantMap {
    bool                                                    built = false;
    std::vector<std::string>                                visible;   ///< types without '!'
    std::vector<std::string>                                sorted;    ///< by '_' count, descending
    std::multimap<std::string, std::string>                 aliasMap;  ///< original -> alias
    std::multimap<std::string, std::pair<std::string, std::string>> manual; ///< block -> (type, out)
    std::map<std::string, std::map<std::string, std::string>> variants; ///< block -> type -> block
    std::map<std::string, std::string>                      originals; ///< variant -> original
};

std::recursive_mutex& mapMutex() {
    static std::recursive_mutex m;
    return m;
}
VariantMap& vmap() {
    static VariantMap m;
    return m;
}

bool contains(std::vector<std::string> const& v, std::string const& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

bool blockExists(std::string const& id) {
    if (id.empty()) return false;
    auto const* t = BlockTypeRegistry::get().lookupByName(HashedString(id));
    if (!t) return false;
    return t->getTypeName() != "minecraft:air";
}

bool blacklisted(std::string const& block, std::string const& result) {
    auto const& bl = cfg().variants.blacklist;
    if (bl.empty()) return false;
    std::string search = block;
    if (!result.empty()) search += "=" + result;
    return !search.empty() && contains(bl, search);
}

std::string suffixed(VariantMap& m, std::string const& ns, std::string const& name, std::string const& suffix, bool ecFormat, std::string const& ogNs, int depth = 0) {
    if (depth > 8) return {};
    for (auto const& strip : cfg().variants.stripCandidates) {
        if (strip.empty() || name.size() <= strip.size() || !name.ends_with(strip)) continue;
        auto attempt = suffixed(m, ns, name.substr(0, name.size() - strip.size()), suffix, ecFormat, ogNs, depth + 1);
        if (!attempt.empty()) return attempt;
    }
    std::string target = ecFormat ? ns + ":" + ogNs + "/" + name + "_" + suffix : ns + ":" + name + "_" + suffix;
    if (blockExists(target)) return target;
    auto range = m.aliasMap.equal_range(suffix);
    for (auto it = range.first; it != range.second; ++it) {
        auto attempt = suffixed(m, ns, name, it->second, ecFormat, ogNs, depth + 1);
        if (!attempt.empty()) return attempt;
    }
    return {};
}

std::string suffixedBlock(VariantMap& m, std::string const& block, std::string const& suffix) {
    auto colon = block.find(':');
    if (colon == std::string::npos) return {};
    std::string ns = block.substr(0, colon), name = block.substr(colon + 1);
    auto        ret = suffixed(m, ns, name, suffix, false, ns);
    if (!ret.empty()) return ret;
    for (auto const& mod : cfg().variants.testedMods) {
        ret = suffixed(m, mod, name, suffix, false, ns);
        if (!ret.empty()) return ret;
        ret = suffixed(m, mod, name, suffix, true, ns);
        if (!ret.empty()) return ret;
    }
    return {};
}

std::map<std::string, std::string> const& variantsOfLocked(VariantMap& m, std::string const& block) {
    auto it = m.variants.find(block);
    if (it != m.variants.end()) return it->second;
    std::map<std::string, std::string> found;
    if (!blacklisted(block, {})) {
        for (auto const& s : m.sorted) {
            if (!contains(cfg().variants.variantTypes, s)) continue; // "!type": shown, not scanned
            auto out = suffixedBlock(m, block, s);
            if (!out.empty() && !blacklisted({}, out) && !blacklisted(block, out)) {
                found[s]           = out;
                m.originals[out]   = block;
            }
        }
    }
    auto range = m.manual.equal_range(block);
    for (auto it2 = range.first; it2 != range.second; ++it2) {
        found[it2->second.first]       = it2->second.second;
        m.originals[it2->second.second] = block;
    }
    return m.variants.emplace(block, std::move(found)).first->second;
}

VariantMap& built() {
    auto& m = vmap();
    if (m.built) return m;
    m          = VariantMap{};
    m.built    = true;
    if (!cfg().enableVariantSelector) return m;
    for (auto const& s : cfg().variants.variantTypes) {
        std::string v = s;
        std::erase(v, '!');
        m.visible.push_back(v);
    }
    m.sorted = m.visible;
    std::stable_sort(m.sorted.begin(), m.sorted.end(), [](std::string const& a, std::string const& b) {
        return std::count(a.begin(), a.end(), '_') > std::count(b.begin(), b.end(), '_');
    });
    for (auto const& s : cfg().variants.aliases) {
        auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        m.aliasMap.emplace(s.substr(eq + 1), s.substr(0, eq));
    }
    for (auto const& s : cfg().variants.manualVariants) {
        auto a = s.find(',');
        auto b = a == std::string::npos ? a : s.find(',', a + 1);
        if (b == std::string::npos) continue;
        m.manual.emplace(s.substr(a + 1, b - a - 1), std::make_pair(s.substr(0, a), s.substr(b + 1)));
    }
    std::vector<std::string> all;
    BlockTypeRegistry::get().forEachBlockType([&](BlockType const& t) {
        all.push_back(t.getTypeName());
        return true;
    });
    for (auto const& id : all) variantsOfLocked(m, id);
    if (cfg().variants.printVariantMapToLog)
        for (auto const& [v, o] : m.originals) log().info("placement: {} is variant of {}", v, o);
    log().info("placement: variant map built ({} variants of {} blocks)", m.originals.size(), all.size());
    return m;
}

} // namespace

void rebuildVariants() {
    std::lock_guard lock(mapMutex());
    vmap().built = false;
}

bool variantsEnabled() { return cfg().enableVariantSelector; }

std::vector<std::string> visibleVariants() {
    std::lock_guard lock(mapMutex());
    return built().visible;
}

bool isKnownVariant(std::string const& variant) {
    std::lock_guard lock(mapMutex());
    return contains(built().visible, variant);
}

std::string variantOf(std::string const& original, std::string const& variant) {
    std::lock_guard lock(mapMutex());
    auto&           m = built();
    if (!contains(m.sorted, variant)) return {};
    auto const& v  = variantsOfLocked(m, original);
    auto        it = v.find(variant);
    return it == v.end() ? std::string() : it->second;
}

std::string originalOf(std::string const& blockId) {
    std::lock_guard lock(mapMutex());
    auto&           m  = built();
    auto            it = m.originals.find(blockId);
    return it == m.originals.end() ? blockId : it->second;
}

std::string variantNameOf(std::string const& base, std::string const& possibleVariant) {
    std::lock_guard lock(mapMutex());
    for (auto const& [type, id] : variantsOfLocked(built(), base))
        if (id == possibleVariant) return type;
    return {};
}

bool hasVariants(std::string const& blockId) {
    std::lock_guard lock(mapMutex());
    return !variantsOfLocked(built(), blockId).empty();
}

std::vector<std::pair<std::string, std::string>> allVariants(std::string const& blockId) {
    std::lock_guard                                  lock(mapMutex());
    auto&                                            m = built();
    auto const&                                      v = variantsOfLocked(m, blockId);
    std::vector<std::pair<std::string, std::string>> out;
    for (auto s : cfg().variants.variantTypes) {
        std::erase(s, '!');
        auto it = v.find(s);
        if (it != v.end()) out.emplace_back(s, it->second);
    }
    return out;
}

std::string variantFromAny(std::string const& blockId, std::string const& variant) {
    std::string original = originalOf(blockId);
    std::string target   = variant.empty() ? original : variantOf(original, variant);
    if (target.empty() || target == blockId) return {};
    return target;
}

// ---- saved variants ----------------------------------------------------------------------------

namespace {
std::map<std::string, std::string>& savedCache() {
    static std::map<std::string, std::string> m;
    return m;
}
std::string variantKey(Player const& p) { return "lp:placement:variant:" + p.getUuid().asString(); }
} // namespace

std::string savedVariant(Player const& player) {
    auto key = variantKey(player);
    auto it  = savedCache().find(key);
    if (it != savedCache().end()) return it->second;
    std::string v;
    if (game::db::available()) {
        auto j = game::db::load(key);
        if (j && j->is_string()) v = j->get<std::string>();
    }
    savedCache()[key] = v;
    return v;
}

void setSavedVariant(Player& player, std::string const& variant) {
    if (!variant.empty() && !isKnownVariant(variant)) return;
    auto key          = variantKey(player);
    savedCache()[key] = variant;
    if (game::db::available()) game::db::save(key, variant);
    log().info("placement: {} selected variant '{}'", player.getRealName(), variant);
}

// ---- placement states for code that places blocks itself ----------------------------------------

Block const* placementState(BlockSource& region, BlockPos const& pos, Player& player, std::string const& blockId, int face, Vec3 const& click) {
    auto const* type = BlockTypeRegistry::get().lookupByName(HashedString(blockId));
    if (!type) return nullptr;
    if (blockId.starts_with("minecraft:")) {
        try {
            return &type->getPlacementBlock(player, pos, static_cast<uchar>(face), click, 0);
        } catch (...) {
            return nullptr;
        }
    }
    auto const* b = bs::of(blockId);
    if (!b) return nullptr;
    bs::Changes ch;
    auto        has  = [&](std::string_view s) { return bs::get(*b, s).has_value(); };
    std::string look(game::shapes::lookDirection(player.getRotation().y));
    bool        side = isHorizontal(face);
    auto        kind = game::shapes::kindOf(*b);
    if (kind == game::shapes::Kind::VerticalSlab) ch.emplace_back("lepton:type", game::shapes::verticalSlabType(face, click));
    else if (kind == game::shapes::Kind::Trapdoor && side) ch.emplace_back("minecraft:cardinal_direction", std::string(dirName(face)));
    else if (has("minecraft:cardinal_direction")) ch.emplace_back("minecraft:cardinal_direction", look);
    if (has("minecraft:block_face")) ch.emplace_back("minecraft:block_face", std::string(dirName(face)));
    if (has("minecraft:vertical_half"))
        ch.emplace_back("minecraft:vertical_half", std::string((face == Down || (side && click.y > 0.5f)) ? "top" : "bottom"));
    auto const* out = ch.empty() ? b : bs::with(*b, ch);
    return out ? out : b;
}

// ---- the Hammer --------------------------------------------------------------------------------

bool useHammer(Player& player, BlockPos const& pos, int face, Vec3 const& click) {
    if (!variantsEnabled() || !cfg().enableHammer) return false;
    auto&       region = player.getDimensionBlockSource();
    auto const& cur    = region.getBlock(pos);
    auto        target = variantFromAny(cur.getTypeName(), savedVariant(player));
    if (target.empty()) return false;
    auto const* place = placementState(region, pos, player, target, face, click);
    if (!place) return true;
    auto const& locked = lockedState(region, pos, player, *place, 0);
    if (&locked != &cur) {
        game::setBlock(region, pos, locked);
        game::shapes::updateShape(region, pos);
        game::shapes::queueUpdate(region, pos);
        playPlaceSound(region, pos, locked, player);
        log().info("placement: hammer turned {} into {}", cur.getTypeName(), locked.getTypeName());
    }
    return true;
}

// ---- dropped variant items -----------------------------------------------------------------------

namespace {

/// The stack a dropped `inst` becomes (Quark's convertVariantItems), or nullopt.
std::optional<ItemStack> converted(ItemStack const& inst) {
    if (!variantsEnabled() || !cfg().convertVariantItems) return std::nullopt;
    auto const* b = blockOfItem(inst);
    if (!b) return std::nullopt;
    auto original = originalOf(b->getTypeName());
    if (original == b->getTypeName()) return std::nullopt;
    ItemStack out = game::makeItem(original, 1);
    if (game::isEmpty(out)) return std::nullopt;
    return out; // Quark: new ItemStack(original) - one item
}

LL_TYPE_INSTANCE_HOOK(
    VariantSpawnItemHook,
    ll::memory::HookPriority::Normal,
    Spawner,
    &Spawner::$spawnItem,
    ItemActor*,
    BlockSource&     region,
    ItemStack const& inst,
    Actor*           spawner,
    Vec3 const&      pos,
    int              throwTime
) {
    if (auto c = converted(inst)) return origin(region, *c, spawner, pos, throwTime);
    return origin(region, inst, spawner, pos, throwTime);
}

LL_TYPE_INSTANCE_HOOK(
    VariantBedrockSpawnItemHook,
    ll::memory::HookPriority::Normal,
    BedrockSpawner,
    &BedrockSpawner::$spawnItem,
    ItemActor*,
    BlockSource&     region,
    ItemStack const& inst,
    Actor*           spawner,
    Vec3 const&      pos,
    int              throwTime
) {
    if (auto c = converted(inst)) return origin(region, *c, spawner, pos, throwTime);
    return origin(region, inst, spawner, pos, throwTime);
}

} // namespace

// ---- vanilla-client form --------------------------------------------------------------------------

std::string variantLabel(Player const& player, std::string const& type) {
    if (type.empty()) return trFor(player, "lp.placement.variant_original", "Original");
    return trFor(player, "lp.placement.variant." + type, type);
}

void openVariantForm(Player& player) {
    if (!variantsEnabled()) {
        player.sendMessage(trFor(player, "lp.placement.variant_disabled", "The Variant Selector is disabled."));
        return;
    }
    auto const* b = blockOfItem(player.getSelectedItem());
    if (!b || !hasVariants(b->getTypeName())) {
        player.sendMessage(trFor(player, "lp.placement.variant_none", "Hold a block that has variants."));
        return;
    }
    std::string base    = b->getTypeName();
    std::string current = savedVariant(player);
    game::forms::SimpleForm form(
        trFor(player, "quark.keybind.variant_selector", "Variant Selector"),
        trFor(player, "lp.placement.variant_form_content", "Choose what to place instead of {}:", base)
    );
    std::vector<std::string> types{""};
    for (auto const& t : visibleVariants()) types.push_back(t);
    for (auto const& t : types) {
        std::string target = t.empty() ? base : variantOf(base, t);
        if (!t.empty() && target.empty()) continue;
        std::string label = variantLabel(player, t);
        if (t == current) label = "§a" + label + "§r";
        form.button(label, [t](Player& p) {
            setSavedVariant(p, t);
            p.sendMessage(trFor(p, "lp.placement.variant_set", "Variant: {}", variantLabel(p, t)));
        });
    }
    form.sendTo(player);
}

struct VariantCommandParams {
    std::string name;
};

class VariantSelectorModule {
public:
    void install() {
        mVariant = net::onServer<VariantMsg>([](Player& player, VariantMsg const& m) {
            if (!variantsEnabled()) return;
            if (m.variant.size() > 64) return;
            setSavedVariant(player, m.variant);
        });
        static bool once = false;
        if (!once) {
            once = true;
            net::onClientModJoined([](Player& player) {
                if (!variantsEnabled()) return;
                net::sendToClient(player, VariantRestoreMsg{savedVariant(player)});
            });
            game::db::onWorldReady([] {
                savedCache().clear();
                rebuildVariants();
            });
            game::db::onWorldClosed([] { savedCache().clear(); });
            config::onReload([] { rebuildVariants(); });
        }
        rebuildVariants();
        VariantSpawnItemHook::hook();
        VariantBedrockSpawnItemHook::hook();
    }
    void uninstall() {
        net::removeHandler(mVariant);
        VariantSpawnItemHook::unhook();
        VariantBedrockSpawnItemHook::unhook();
    }

private:
    net::HandlerId mVariant = 0;
};

} // namespace lp::feature::placement

LP_REGISTER_MODULE(lp::feature::placement::VariantSelectorModule);

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace lp::feature::placement;
    cmd.overload().text("variant").execute([](CommandOrigin const& origin, CommandOutput& out) {
        auto* player = lp::commands::playerOf(origin);
        if (!player) return out.error("Players only");
        openVariantForm(*player);
    });
    cmd.overload<VariantCommandParams>().text("variant").required("name").execute(
        [](CommandOrigin const& origin, CommandOutput& out, VariantCommandParams const& params) {
            auto* player = lp::commands::playerOf(origin);
            if (!player) return out.error("Players only");
            if (!variantsEnabled())
                return out.error(lp::trFor(*player, "lp.placement.variant_disabled", "The Variant Selector is disabled."));
            std::string v = params.name == "original" ? std::string() : params.name;
            if (!v.empty() && !isKnownVariant(v))
                return out.error(lp::trFor(*player, "lp.placement.variant_unknown", "Unknown variant: {}", params.name));
            setSavedVariant(*player, v);
            out.success(lp::trFor(*player, "lp.placement.variant_set", "Variant: {}", variantLabel(*player, v)));
        }
    );
});
