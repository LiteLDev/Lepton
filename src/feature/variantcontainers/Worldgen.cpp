// Structure chests (Quark VariantChestsModule#getGenerationChestBlockState, replaceWorldgenChests):
// world generation gives a vanilla chest its loot table through
// RandomizableBlockActorContainerBase::setLootTable (often on a generation thread). The position
// and table are queued; on the server thread the chest becomes the structure's variant chest,
// keeping facing, pairing and the (not yet rolled) loot table.
//
// Bedrock does not tell a chest which structure made it, so the structure comes from the chest's
// loot table (and the biome for the village chests shared by every village kind).

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/RandomizableBlockActorContainerBase.h"

#include "feature/variantcontainers/VariantContainers.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::variantcontainers {

namespace bs = game::blockstates;

namespace {

struct Entry {
    BlockPos    pos;
    std::string table;
    int         seed  = 0;
    int         tries = 0;
};
std::mutex& queueMutex() {
    static std::mutex m;
    return m;
}
std::vector<Entry>& queue() {
    static std::vector<Entry> v;
    return v;
}

/// "loot_tables/chests/village/village_plains_house.json" -> "chests/village/village_plains_house".
std::string normalizeTable(std::string t) {
    if (t.starts_with("loot_tables/")) t = t.substr(12);
    if (t.ends_with(".json")) t.resize(t.size() - 5);
    return t;
}

bool contains(std::string_view s, std::string_view x) { return s.find(x) != std::string_view::npos; }

/// Village kind of a village chest: plains, desert, savanna, snowy or taiga ("" for other chests).
/// Chests every village kind shares (smiths, temple, ...) take the kind from the biome.
std::string villageKind(std::string const& table, BlockSource& region, BlockPos const& pos) {
    auto n = normalizeTable(table);
    if (!n.starts_with("chests/village/")) return {};
    for (char const* k : {"plains", "desert", "savanna", "snowy", "taiga"})
        if (contains(n, k)) return k;
    std::string biome = region.getBiome(pos).mHash->getString();
    if (contains(biome, "desert")) return "desert";
    if (contains(biome, "savanna")) return "savanna";
    if (contains(biome, "taiga")) return "taiga";
    if (contains(biome, "snow") || contains(biome, "ice") || contains(biome, "frozen") || contains(biome, "cold")) return "snowy";
    return "plains";
}
/// Quark's structure tags (quark:<material>_chest_structures) over Bedrock's chest loot tables.
std::string materialFor(std::string const& table, BlockSource& region, BlockPos const& pos) {
    auto t = normalizeTable(table);
    if (!t.starts_with("chests/")) return {};
    auto n = t.substr(7);
    if (auto kind = villageKind(table, region, pos); !kind.empty())
        return kind == "plains" ? "oak" : kind == "desert" ? "birch" : kind == "savanna" ? "acacia" : "spruce";
    if (n.starts_with("stronghold")) return "oak";
    if (n == "igloo_chest") return "spruce";
    if (n == "desert_pyramid") return "birch";
    if (n == "jungle_temple") return "jungle";
    if (n == "woodland_mansion" || n == "pillager_outpost") return "dark_oak";
    if (n == "ruined_portal" || n.starts_with("bastion")) return "crimson";
    if (n == "nether_bridge") return "nether_brick";
    if (n.starts_with("underwater_ruin")) return "prismarine";
    if (n == "end_city_treasure") return "purpur";
    if (n.starts_with("ancient_city")) return "ancient"; // U7's chest, when it exists
    return {};
}

/// Quark structure ids -> loot table prefixes (for `structureChests` entries naming a structure).
bool structureMatches(std::string const& structure, std::string const& table, BlockSource& region, BlockPos const& pos) {
    auto n = normalizeTable(table);
    auto s = structure.starts_with("minecraft:") ? structure.substr(10) : structure;
    if (s.starts_with("village_")) return villageKind(table, region, pos) == s.substr(8);
    static std::vector<std::pair<std::string, std::string>> const kMap{
        {"stronghold", "chests/stronghold"},          {"igloo", "chests/igloo_chest"},
        {"desert_pyramid", "chests/desert_pyramid"},  {"jungle_pyramid", "chests/jungle_temple"},
        {"mansion", "chests/woodland_mansion"},       {"pillager_outpost", "chests/pillager_outpost"},
        {"ruined_portal", "chests/ruined_portal"},    {"bastion_remnant", "chests/bastion"},
        {"fortress", "chests/nether_bridge"},         {"ocean_ruin", "chests/underwater_ruin"},
        {"end_city", "chests/end_city_treasure"},     {"ancient_city", "chests/ancient_city"},
        {"shipwreck", "chests/shipwreck"},            {"buried_treasure", "chests/buried_treasure"},
        {"mineshaft", "chests/abandoned_mineshaft"},  {"trial_chambers", "chests/trial_chambers"},
        {"ruined_portal_nether", "chests/ruined_portal"},
    };
    for (auto const& [id, prefix] : kMap)
        if (s == id) return n.starts_with(prefix);
    return false;
}

/// `structureChests` ("structure=chest", Quark's format): the chest block for this table, if any.
std::optional<std::string> manualMapping(std::string const& table, BlockSource& region, BlockPos const& pos) {
    for (auto const& e : cfg().structureChests) {
        auto eq = e.find('=');
        if (eq == std::string::npos) continue;
        auto left = e.substr(0, eq), right = e.substr(eq + 1);
        bool hit  = left.starts_with("loot_tables/") || left.starts_with("chests/") ? normalizeTable(left) == normalizeTable(table)
                                                                                     : structureMatches(left, table, region, pos);
        if (hit && bs::of(right)) return right;
    }
    return std::nullopt;
}

/// The variant chest block id for a vanilla chest (nullopt: keep it).
std::optional<std::string> variantFor(std::string const& table, bool trapped, BlockSource& region, BlockPos const& pos) {
    if (auto m = manualMapping(table, region, pos)) {
        if (!trapped) return m;
        // Quark maps trapped chests only when the manual block is a regular variant chest
        auto const* b = bs::of(*m);
        if (!b || !b->hasTag(HashedString("lepton:variant_chest"))) return std::nullopt;
        auto path = m->substr(m->find(':') + 1);
        auto id   = "lepton:trapped_" + path;
        if (bs::of(id)) return id;
        return std::nullopt;
    }
    auto mat = materialFor(table, region, pos);
    if (mat.empty()) return std::nullopt;
    std::string id = trapped ? "lepton:trapped_" + mat + "_chest" : "lepton:" + mat + "_chest";
    if (!bs::of(id)) return std::nullopt; // e.g. lepton:ancient_chest without U7
    return id;
}

bool replaceOne(BlockSource& region, Entry const& e) {
    auto const& b = region.getBlock(e.pos);
    auto const& name = b.getTypeName();
    bool trapped = name == "minecraft:trapped_chest";
    if (name != "minecraft:chest" && !trapped) return true; // gone or already replaced
    auto* actor = region.getBlockEntity(e.pos);
    if (!actor || actor->mType != BlockActorType::Chest) return true;
    auto* chest = static_cast<ChestBlockActor*>(actor);
    if (normalizeTable(chest->mLootTable) != normalizeTable(e.table)) return true; // opened, or another table
    auto id = variantFor(e.table, trapped, region, e.pos);
    if (!id) return true;
    // vanilla minecraft:cardinal_direction is the front; ours is the placer's look direction
    int front = dirIndex(bs::getString(b, "minecraft:cardinal_direction").value_or("south"));
    if (front < 0) front = 0;
    ChestType type = ChestType::Single;
    std::optional<BlockPos> partner;
    if (chest->mLargeChestPaired) partner = chest->mLargeChestPairedPosition;
    if (partner) {
        int dx = partner->x - e.pos.x, dz = partner->z - e.pos.z;
        for (int d = 0; d < 4; ++d) {
            auto [sx, sz] = game::facingOffset(d);
            if (sx != dx || sz != dz || partner->y != e.pos.y) continue;
            if (d == cw(front)) type = ChestType::Left;
            else if (d == ccw(front)) type = ChestType::Right;
        }
    }
    auto const* ours = bs::of(*id);
    if (!ours) return true;
    ours = withChest(*ours, front, type);
    if (!ours) return true;
    chest->mLootTable = std::string{}; // nothing to roll or drop from the vanilla chest
    auto gp = game::globalPos(region, e.pos);
    if (!game::setBlock(region, e.pos, *ours)) return true;
    setPendingLoot(gp, e.table, e.seed);
    log().info("variantcontainers: structure chest at {} ({}) became {}", gp.key(), normalizeTable(e.table), *id);
    return true;
}

void processQueue() {
    std::vector<Entry> list;
    {
        std::lock_guard lock(queueMutex());
        list.swap(queue());
    }
    std::vector<Entry> keep;
    for (auto& e : list) {
        bool done = false;
        bool seen = false;
        for (int dim = 0; dim < 3 && !done; ++dim) {
            auto* region = game::regionOf(dim);
            if (!region || !region->hasChunksAt(e.pos, 0, false)) continue;
            auto const& n = region->getBlock(e.pos).getTypeName();
            if (n != "minecraft:chest" && n != "minecraft:trapped_chest") continue;
            seen = true;
            auto* actor = region->getBlockEntity(e.pos);
            if (!actor || actor->mType != BlockActorType::Chest) continue;
            if (normalizeTable(static_cast<ChestBlockActor*>(actor)->mLootTable) != normalizeTable(e.table)) continue;
            done = replaceOne(*region, e);
        }
        // the chunk may still be generating / not loaded yet: retry for a while (about a minute)
        if (!done && !seen && ++e.tries < 1200) keep.push_back(std::move(e));
    }
    if (!keep.empty()) {
        std::lock_guard lock(queueMutex());
        for (auto& e : keep) queue().push_back(std::move(e));
    }
}

LL_TYPE_INSTANCE_HOOK(
    SetLootTableHook,
    ll::memory::HookPriority::Normal,
    RandomizableBlockActorContainerBase,
    &RandomizableBlockActorContainerBase::setLootTable,
    void,
    std::string const& lootTable,
    int                lootTableSeed
) {
    origin(lootTable, lootTableSeed);
    if (mType != BlockActorType::Chest || lootTable.empty()) return;
    auto const& c = cfg();
    if (!c.enableVariantChests || !c.replaceWorldgenChests) return;
    std::lock_guard lock(queueMutex());
    queue().push_back(Entry{mPosition, lootTable, lootTableSeed, 0});
}

} // namespace

class WorldgenModule {
public:
    void install() {
        SetLootTableHook::hook();
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            bool empty;
            {
                std::lock_guard lock(queueMutex());
                empty = queue().empty();
            }
            if (!empty) processQueue();
        });
        game::db::onWorldClosed([] {
            std::lock_guard lock(queueMutex());
            queue().clear();
        });
    }
    void uninstall() {
        SetLootTableHook::unhook();
        ll::event::EventBus::getInstance().removeListener(mTick);
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace lp::feature::variantcontainers

LP_REGISTER_MODULE(lp::feature::variantcontainers::WorldgenModule);
