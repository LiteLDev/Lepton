// SimpleHarvest + HoeHarvesting (docs/spec/farming.md §1, §3).
#include "feature/farming/Farming.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/IRandom.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/HarvestFarmBlockGoal.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/ResourceDrops.h"
#include "mc/world/level/block/ResourceDropsContext.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::farming {

namespace bs = game::blockstates;

// ---- harvestableBlocks parsing ---------------------------------------------------------------------

namespace {

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

/// "id[a=1,b=2]" -> id + states. false on syntax errors.
bool parseState(std::string_view text, std::string& id, std::vector<std::pair<std::string, std::string>>& states) {
    auto t  = trim(text);
    auto lb = t.find('[');
    if (lb == std::string::npos) {
        id = t;
        return !id.empty();
    }
    if (t.back() != ']') return false;
    id         = trim(std::string_view(t).substr(0, lb));
    auto inner = std::string_view(t).substr(lb + 1, t.size() - lb - 2);
    size_t start = 0;
    while (start <= inner.size()) {
        auto comma = inner.find(',', start);
        auto part  = inner.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        auto p     = trim(part);
        if (!p.empty()) {
            auto eq = p.find('=');
            if (eq == std::string::npos) return false;
            states.emplace_back(trim(std::string_view(p).substr(0, eq)), trim(std::string_view(p).substr(eq + 1)));
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return !id.empty();
}

std::string valueText(bs::Value const& v) {
    if (auto const* i = std::get_if<int>(&v)) return std::to_string(*i);
    if (auto const* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    return std::get<std::string>(v);
}

bool valueMatches(bs::Value const& v, std::string const& want) {
    if (auto const* b = std::get_if<bool>(&v)) return (want == "true" || want == "1") == *b;
    return valueText(v) == want;
}

/// Converts the written value to the state's type (taken from the block's default state).
std::optional<bs::Value> typed(Block const& like, std::string const& state, std::string const& text) {
    auto cur = bs::get(like, state);
    if (!cur) return std::nullopt;
    if (std::holds_alternative<int>(*cur)) {
        try {
            return bs::Value(std::stoi(text));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (std::holds_alternative<bool>(*cur)) return bs::Value(text == "true" || text == "1");
    return bs::Value(text);
}

Block const* resolve(std::string const& id, std::vector<std::pair<std::string, std::string>> const& states) {
    auto const* base = bs::of(id);
    if (!base) return nullptr;
    bs::Changes changes;
    for (auto const& [k, v] : states) {
        auto tv = typed(*base, k, v);
        if (!tv) return nullptr;
        changes.emplace_back(k, *tv);
    }
    return changes.empty() ? base : bs::with(*base, changes);
}

struct ResolvedRule {
    std::vector<std::pair<std::string, std::string>> states;
    Block const*                                     after = nullptr;
};

struct Tables {
    std::unordered_map<std::string, std::vector<ResolvedRule>> crops; ///< block id -> rules
    std::set<std::string>                                      rightClick;
    std::unordered_map<std::string, std::optional<std::pair<std::string, int>>> searched; ///< growth state cache
};

Tables& tables() {
    static Tables t;
    return t;
}

bool isVanilla(std::string const& id) { return id.rfind("minecraft:", 0) == 0; }

/// Max value of an int state (probing; Bedrock exposes no range).
int maxOf(Block const& block, std::string const& state) {
    int best = bs::getInt(block, state).value_or(0);
    for (int v = best + 1; v < 64; ++v) {
        if (!bs::with(block, {{state, v}})) break;
        best = v;
    }
    return best;
}

/// doHarvestingSearch: a non-vanilla data-driven crop (tag minecraft:crop) with a growth/age state.
std::optional<std::pair<std::string, int>> searchedCrop(Block const& block) {
    auto const& id = block.getTypeName();
    auto&       c  = tables().searched;
    if (auto it = c.find(id); it != c.end()) return it->second;
    std::optional<std::pair<std::string, int>> out;
    if (!isVanilla(id) && block.hasTag(HashedString("minecraft:crop"))) {
        for (auto const& [name, value] : bs::all(block)) {
            if (!std::holds_alternative<int>(value)) continue;
            if (name.find("growth") == std::string::npos && name.find("age") == std::string::npos) continue;
            auto const* def = bs::of(id);
            if (!def) break;
            out = std::make_pair(name, maxOf(*def, name));
            break;
        }
    }
    c[id] = out;
    return out;
}

bool isTillable(Block const& block) {
    static std::set<std::string> const t{
        "minecraft:grass_block",
        "minecraft:dirt",
        "minecraft:coarse_dirt",
        "minecraft:dirt_with_roots",
        "minecraft:grass_path",
    };
    return t.contains(block.getTypeName());
}

} // namespace

std::optional<HarvestRule> parseHarvestRule(std::string_view text) {
    bool   inBracket = false;
    size_t split     = std::string_view::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (ch == '[') inBracket = true;
        else if (ch == ']') inBracket = false;
        else if (ch == ',' && !inBracket) {
            split = i;
            break;
        }
    }
    HarvestRule r;
    if (!parseState(text.substr(0, split), r.id, r.states)) return std::nullopt;
    if (split != std::string_view::npos) {
        if (!parseState(text.substr(split + 1), r.afterId, r.afterStates)) return std::nullopt;
    } else {
        r.afterId = r.id; // the block's default state
    }
    return r;
}

void rebuildHarvestTables() {
    auto& t = tables();
    t     = Tables{};
    auto const& c = cfg();
    for (auto const& text : c.harvestableBlocks) {
        auto r = parseHarvestRule(text);
        if (!r) {
            log().warn("farming: bad harvestableBlocks entry '{}'", text);
            continue;
        }
        ResolvedRule rr;
        rr.states = r->states;
        rr.after  = resolve(r->afterId, r->afterStates);
        if (!rr.after) {
            // unknown block (e.g. another unit's crop not installed) is skipped like Quark's AIR check
            continue;
        }
        t.crops[r->id].push_back(std::move(rr));
    }
    for (auto const& id : c.rightClickableBlocks) {
        if (bs::of(id)) t.rightClick.insert(trim(id));
    }
}

Block const* replantedState(Block const& block) {
    auto const& t  = tables();
    auto const& id = block.getTypeName();
    if (auto it = t.crops.find(id); it != t.crops.end()) {
        for (auto const& r : it->second) {
            bool ok = true;
            for (auto const& [k, v] : r.states) {
                auto cur = bs::get(block, k);
                if (!cur || !valueMatches(*cur, v)) {
                    ok = false;
                    break;
                }
            }
            if (ok) return r.after;
        }
    }
    if (cfg().doHarvestingSearch) {
        if (auto s = searchedCrop(block)) {
            if (bs::getInt(block, s->first).value_or(-1) == s->second) return bs::with(block, {{s->first, 0}});
        }
    }
    return nullptr;
}

HarvestAction actionFor(Block const& block, bool canRightClick) {
    if (block.hasTag(HashedString("lepton:simple_harvest_blacklisted"))) return HarvestAction::None;
    if (canRightClick && tables().rightClick.contains(block.getTypeName())) return HarvestAction::Click;
    if (replantedState(block)) return HarvestAction::Harvest;
    return HarvestAction::None;
}

int hoeRange(ItemStackBase const& stack) {
    auto const& c = cfg();
    if (!c.enableHoeHarvesting || !isHoe(stack)) return 1;
    auto id  = stack.getTypeName();
    bool big = id == "minecraft:diamond_hoe" || id == "minecraft:netherite_hoe" || hasItemTag(stack, "lepton:big_harvesting_hoes");
    return big ? c.highTierHoeRadius : c.regularHoeRadius;
}

bool isHoeHarvestable(Block const& block) {
    static std::set<std::string> const list{
        // #minecraft:crops
        "minecraft:wheat", "minecraft:carrots", "minecraft:potatoes", "minecraft:beetroot", "minecraft:melon_stem",
        "minecraft:pumpkin_stem", "minecraft:torchflower_crop", "minecraft:pitcher_crop",
        // #minecraft:flowers (1.21.1)
        "minecraft:dandelion", "minecraft:poppy", "minecraft:blue_orchid", "minecraft:allium", "minecraft:azure_bluet",
        "minecraft:red_tulip", "minecraft:orange_tulip", "minecraft:white_tulip", "minecraft:pink_tulip",
        "minecraft:oxeye_daisy", "minecraft:cornflower", "minecraft:lily_of_the_valley", "minecraft:wither_rose",
        "minecraft:torchflower", "minecraft:sunflower", "minecraft:lilac", "minecraft:peony", "minecraft:rose_bush",
        "minecraft:pitcher_plant", "minecraft:flowering_azalea", "minecraft:azalea_leaves_flowered",
        "minecraft:mangrove_propagule", "minecraft:cherry_leaves", "minecraft:pink_petals", "minecraft:chorus_flower",
        "minecraft:spore_blossom",
        // #minecraft:replaceable_by_trees (1.21.1)
        "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:birch_leaves", "minecraft:jungle_leaves",
        "minecraft:acacia_leaves", "minecraft:dark_oak_leaves", "minecraft:mangrove_leaves", "minecraft:azalea_leaves",
        "minecraft:short_grass", "minecraft:fern", "minecraft:deadbush", "minecraft:vine", "minecraft:glow_lichen",
        "minecraft:tall_grass", "minecraft:large_fern", "minecraft:hanging_roots", "minecraft:seagrass",
        "minecraft:warped_roots", "minecraft:nether_sprouts", "minecraft:crimson_roots",
        // listed in quark:hoe_harvestable
        "minecraft:cactus", "minecraft:reeds", "minecraft:cocoa", "minecraft:waterlily",
    };
    static std::set<std::string> const crops{
        "minecraft:wheat", "minecraft:carrots", "minecraft:potatoes", "minecraft:beetroot", "minecraft:torchflower_crop"
    };
    auto const& id = block.getTypeName();
    bool        harvestable = list.contains(id) || block.hasTag(HashedString("lepton:hoe_harvestable"));
    if (!harvestable && !isVanilla(id) && block.hasTag(HashedString("minecraft:crop"))) harvestable = true;
    if (!harvestable) return false;
    if (id == "minecraft:water" || id == "minecraft:flowing_water") return false;
    if (crops.contains(id)) {
        auto g = bs::getInt(block, "growth");
        if (!g) return false;
        auto const* def = bs::of(id);
        return def && *g == maxOf(*def, "growth");
    }
    if (!isVanilla(id) && block.hasTag(HashedString("minecraft:crop"))) {
        auto s = searchedCrop(block);
        return s && bs::getInt(block, s->first).value_or(-1) == s->second;
    }
    if (id == "minecraft:cocoa") return bs::getInt(block, "age").value_or(0) == 2;
    if (id.ends_with("leaves") || id == "minecraft:azalea_leaves_flowered") {
        if (bs::getBool(block, "persistent_bit").value_or(false)) return false;
        if (bs::getBool(block, "lepton:persistent").value_or(false)) return false;
    }
    return true;
}

bool harvestAndReplant(BlockSource& region, BlockPos const& pos, Actor* by, ItemStack const& tool) {
    Block const& in    = region.getBlock(pos);
    Block const* after = replantedState(in);
    if (!after) return false;
    Block const& inCopy = in; // `in` stays valid: registry blocks are immutable
    auto         level  = ll::service::getLevel();
    if (!level) return false;

    ItemStack            stick = tool.isNull() ? game::makeItem("minecraft:stick") : tool;
    ResourceDropsContext ctx{
        ResourceDropsCause::PlayerMining, 0.0f, stick, pos, region.getDimensionId(), region
    };
    auto drops = inCopy.getBlockType().getResourceDrops(inCopy, level->getIRandom(), ctx);
    auto seed  = inCopy.getBlockType().asItemInstance(inCopy, nullptr);
    auto seedId = seed.isNull() ? std::string() : seed.getTypeName();
    bool taken  = false;
    for (auto& stack : drops.mItems.get()) {
        if (!taken && !seedId.empty() && !stack.isNull() && stack.getTypeName() == seedId && stack.mCount > 0) {
            stack.remove(1);
            taken = true;
        }
        if (!stack.isNull() && stack.mCount > 0) popItem(region, pos, stack);
    }
    (void)by;
    destroyEffect(region, pos, *after);
    game::setBlock(region, pos, *after);
    return true;
}

// ---- SimpleHarvest click -------------------------------------------------------------------------

namespace {

struct Planned {
    BlockPos      pos;
    HarvestAction action;
};

void runPlan(Player& player, std::vector<Planned> const& plan, bool hoe) {
    auto& region = player.getDimensionBlockSource();
    auto  tool   = player.getSelectedItem();
    bool  done   = false;
    for (auto const& p : plan) {
        if (!region.hasChunksAt(p.pos, 0, false)) continue;
        auto const& b = region.getBlock(p.pos);
        auto        a = actionFor(b, p.action == HarvestAction::Click);
        if (a == HarvestAction::Harvest) {
            done |= harvestAndReplant(region, p.pos, &player, tool);
        } else if (a == HarvestAction::Click) {
            Vec3 hit{static_cast<float>(p.pos.x) + 0.5f, static_cast<float>(p.pos.y) + 0.5f, static_cast<float>(p.pos.z) + 0.5f};
            done |= b.use(player, p.pos, 1, HandSlot::Mainhand, hit);
        }
    }
    if (done && hoe && cfg().harvestingCostsDurability && !isCreative(player)) {
        auto& inv  = game::inv::playerInventory(player);
        int   slot = player.getSelectedItemSlot();
        auto  cur  = inv.getItem(slot);
        if (isHoe(cur)) {
            cur.hurtAndBreak(1, &player);
            inv.setItem(slot, cur);
            game::inv::sync(player);
        }
    }
    if (done) log().info("farming: {} harvested {} block(s)", player.getRealName(), plan.size());
}

} // namespace

bool simpleHarvestClick(Player& player, BlockPos const& pos, bool deferred) {
    auto const& c = cfg();
    if (!c.enableSimpleHarvest || player.isSpectator()) return false;
    auto& region = player.getDimensionBlockSource();
    auto const& at = region.getBlock(pos);
    if (isTillable(at)) return false;
    auto const& hand = player.getSelectedItem();
    bool        hoe  = isHoe(hand);
    if (!c.emptyHandHarvest && !hoe) return false;
    BlockPos above{pos.x, pos.y + 1, pos.z};
    if (hoe && actionFor(at, true) == HarvestAction::None && actionFor(region.getBlock(above), true) == HarvestAction::None)
        return false;
    int  range = hoeRange(hand);
    bool click = range > 1;
    std::vector<Planned> plan;
    for (int x = 1 - range; x < range; ++x) {
        for (int z = 1 - range; z < range; ++z) {
            BlockPos p{pos.x + x, pos.y, pos.z + z};
            if (!region.hasChunksAt(p, 0, false)) continue;
            auto a = actionFor(region.getBlock(p), click);
            if (a == HarvestAction::None) {
                p = BlockPos{p.x, p.y + 1, p.z};
                a = actionFor(region.getBlock(p), click);
            }
            if (a != HarvestAction::None) plan.push_back({p, a});
        }
    }
    if (plan.empty()) return false;
    if (!deferred) {
        runPlan(player, plan, hoe);
        return true;
    }
    auto id = player.getOrCreateUniqueID().rawID;
    later(1, [id, plan, hoe] {
        if (auto* p = playerById(id)) runPlan(*p, plan, hoe);
    });
    return true;
}

// ---- HoeHarvesting -----------------------------------------------------------------------------

void hoeHarvest(Player& player, BlockPos const& basePos, Block const& broken) {
    auto const& c = cfg();
    if (!c.enableHoeHarvesting) return;
    auto const& stack = player.getSelectedItem();
    if (!isHoe(stack) || !isHoeHarvestable(broken)) return;
    int   range  = hoeRange(stack);
    auto& region = player.getDimensionBlockSource();
    bool  nonInstant = false;
    int   count      = 0;
    for (int i = 1 - range; i < range; ++i) {
        for (int k = 1 - range; k < range; ++k) {
            if (i == 0 && k == 0) continue;
            BlockPos p{basePos.x + i, basePos.y, basePos.z + k};
            if (!region.hasChunksAt(p, 0, false)) continue;
            Block const& b = region.getBlock(p);
            if (!isHoeHarvestable(b)) continue;
            if (b.getBlockType().getDestroySpeed() != 0.0f) nonInstant = true;
            b.playerDestroy(player, p);
            destroyEffect(region, p, b);
            game::clearBlock(region, p);
            ++count;
        }
    }
    if (nonInstant && !isCreative(player)) {
        auto& inv  = game::inv::playerInventory(player);
        int   slot = player.getSelectedItemSlot();
        auto  cur  = inv.getItem(slot);
        if (isHoe(cur)) {
            cur.hurtAndBreak(1, &player);
            inv.setItem(slot, cur);
            game::inv::sync(player);
        }
    }
    if (count > 0) log().info("farming: {} hoe-harvested {} extra block(s)", player.getRealName(), count);
}

// ---- events & hooks ------------------------------------------------------------------------------

namespace {

std::vector<ll::event::ListenerPtr> gListeners;

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    bool h = simpleHarvestClick(player, ev.blockPos());
    if (h) ev.cancel();
}

void onDestroy(ll::event::PlayerDestroyBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& region = player.getDimensionBlockSource();
    auto  pos    = ev.pos();
    Block const* broken = &region.getBlock(pos);
    if (!isHoe(player.getSelectedItem()) || !isHoeHarvestable(*broken)) return;
    hoeHarvest(player, pos, *broken); // neighbours only; the clicked block is broken by the game
}

} // namespace

// Villagers (HarvestFarmlandMixin): Quark applies simple harvest only while SimpleHarvest is
// disabled (`!staticEnabled && villagersUseSimpleHarvest`); reproduced as is.
LL_TYPE_INSTANCE_HOOK(
    VillagerHarvestHook,
    ll::memory::HookPriority::Normal,
    HarvestFarmBlockGoal,
    &HarvestFarmBlockGoal::$tick,
    void
) {
    auto const& c = cfg();
    if (!c.enableSimpleHarvest && c.villagersUseSimpleHarvest && mCurrentTask == HarvestFarmBlockGoal::Task::Reap
        && mReachedTarget) {
        Mob&     mob = mMob;
        auto&    region = mob.getDimensionBlockSource();
        Vec3     t      = mBlockPos;
        BlockPos base{
            static_cast<int>(std::floor(t.x)), static_cast<int>(std::floor(t.y)), static_cast<int>(std::floor(t.z))
        };
        for (BlockPos p : {base, BlockPos{base.x, base.y + 1, base.z}}) {
            if (replantedState(region.getBlock(p)) && harvestAndReplant(region, p, &mob, game::makeItem("minecraft:stick"))) {
                mCurrentTask = HarvestFarmBlockGoal::Task::None;
                break;
            }
        }
    }
    origin();
}

void installHarvest() {
    auto& bus = ll::event::EventBus::getInstance();
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerDestroyBlockEvent>(onDestroy));
    VillagerHarvestHook::hook();
}

void uninstallHarvest() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
    VillagerHarvestHook::unhook();
}

} // namespace lp::feature::farming
