#include "feature/bricks/Bricks.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <optional>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ll/api/memory/Hook.h"

#include "mc/util/Random.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/monster/EnderDragon.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/SurvivalMode.h"
#include "mc/world/item/ItemTag.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/AttributeModificationContext.h"
#include "mc/world/attribute/AttributeModifier.h"
#include "mc/world/attribute/AttributeModifierOperation.h"
#include "mc/world/attribute/AttributeOperands.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/item/CreativeItemInitializer.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/registry/ItemRegistryManager.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/FireBlock.h"
#include "mc/world/level/block/SoulFireBlock.h"
#include "mc/world/phys/AABB.h"

#include "game/BlockStates.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("bricks", lp::feature::bricks::Config);

namespace lp::feature::bricks {

namespace {

#include "feature/bricks/Content.gen.inc"

struct Entry {
    std::string_view key;
    std::string_view value;
};
#define X(a, b) Entry{a, b},
constexpr std::array kItems{LP_BRICKS_ITEMS};
constexpr std::array kRecipes{LP_BRICKS_RECIPES};
#undef X

template <std::size_t N>
std::string_view find(std::array<Entry, N> const& table, std::string_view key) {
    for (auto const& e : table)
        if (e.key == key) return e.value;
    return {};
}

std::string_view stripNamespace(std::string_view id) {
    if (id.starts_with("lepton:")) return id.substr(7);
    return {};
}

Config const& cfg() { return lp::config::get<Config>(); }

GateStats gStats;

/// A boolean key of another unit's config file (default when the file or key is missing).
bool externalFlag(char const* file, char const* key, bool fallback) {
    try {
        std::ifstream in(lp::config::configDir() / file);
        if (!in) return fallback;
        auto j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_object() && j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
    } catch (...) {}
    return fallback;
}

bool itemRegistered(char const* id) {
    auto reg = ItemRegistryManager::getItemRegistry();
    return static_cast<bool>(reg.lookupByNameNoAlias(std::string_view{id}));
}

/// Tag names, hashed once (function-local: HashedString must not be built during static init).
struct Tags {
    HashedString soulFire{"lepton:soul_fire_base"};
    HashedString soulSpeed{"lepton:soul_speed"};
    HashedString infiniburn{"lepton:infiniburn"};
    HashedString dragonImmune{"lepton:dragon_immune"};
    HashedString requiresTool{"lepton:requires_tool"};
    HashedString shovelBlock{"minecraft:is_shovel_item_destructible"};
    HashedString axeBlock{"minecraft:is_axe_item_destructible"};
    HashedString hoeBlock{"minecraft:is_hoe_item_destructible"};
    ItemTag      pickaxe{HashedString("minecraft:is_pickaxe")};
    ItemTag      shovel{HashedString("minecraft:is_shovel")};
    ItemTag      axe{HashedString("minecraft:is_axe")};
    ItemTag      hoe{HashedString("minecraft:is_hoe")};
};
Tags const& tags() {
    static Tags t;
    return t;
}

bool blockHasTag(Block const& b, HashedString const& tag) { return b.getBlockType().hasTag(tag); }

/// The other units' settings, read once per world load (the gate asks for every recipe and item).
std::optional<Externals>& externalsCache() {
    static std::optional<Externals> e;
    return e;
}
Externals const& externals() {
    auto& e = externalsCache();
    if (!e) e = currentExternals();
    return *e;
}

// ---- content gate: recipes ----------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    RecipeGateHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::_loadRecipe,
    bool,
    std::string const&        recipeId,
    RecipeType                type,
    Json::Value const&        objData,
    MinEngineVersion const&   minEngineVersion,
    SemVersion const&         formatVersion,
    bool                      isBaseGamePack,
    Recipes::Buffers&         buffers
) {
    auto name = stripNamespace(recipeId);
    if (!name.empty() && isOurRecipe(name) && !recipeEnabled(cfg(), externals(), name)) {
        ++gStats.recipesSkipped;
        return true; // as if loaded: the recipe simply does not exist
    }
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

// ---- content gate: creative inventory -----------------------------------------------------------

bool creativeAllowed(ItemInstance const& item) {
    auto path = stripNamespace(game::itemId(item));
    if (path.empty() || groupOf(path).empty()) return true;
    if (itemEnabled(cfg(), externals(), path)) return true;
    ++gStats.creativeSkipped;
    return false;
}

LL_TYPE_INSTANCE_HOOK(
    CreativeGroupGateHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addCreativeItemServer,
    void,
    CreativeGroupInfo&  group,
    ItemInstance const& item
) {
    if (creativeAllowed(item)) origin(group, item);
}

LL_TYPE_INSTANCE_HOOK(
    CreativeLooseGateHook,
    ll::memory::HookPriority::Normal,
    CreativeItemInitializer,
    &CreativeItemInitializer::_addLooseCreativeItem,
    void,
    ItemInstance const& item
) {
    if (creativeAllowed(item)) origin(item);
}

// ---- soul_fire_base_blocks: fire on soul sandstone burns as soul fire ---------------------------

bool soulBase(BlockSource& region, BlockPos const& pos) {
    if (!cfg().enableSoulSandstone) return false;
    return blockHasTag(region.getBlock(BlockPos{pos.x, pos.y - 1, pos.z}), tags().soulFire);
}

LL_STATIC_HOOK(
    SoulFireSpawnHook,
    ll::memory::HookPriority::Normal,
    &FireBlock::_trySpawnSoulFire,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    if (origin(region, pos)) return true;
    if (!soulBase(region, pos)) return false;
    auto const* soul = game::blockstates::of("minecraft:soul_fire");
    if (!soul) return false;
    BlockChangeContext ctx{};
    return region.setBlock(pos, *soul, 3, nullptr, ctx);
}

LL_TYPE_INSTANCE_HOOK(
    SoulFireSurviveHook,
    ll::memory::HookPriority::Normal,
    SoulFireBlock,
    &SoulFireBlock::$canSurvive,
    bool,
    BlockSource&    region,
    BlockPos const& pos
) {
    return origin(region, pos) || soulBase(region, pos);
}

// ---- soul_speed_blocks: Soul Speed boots work on soul sandstone ---------------------------------

/// Mobs that currently carry the modifier (so it is removed when their boots come off).
std::unordered_set<int64>& soulSpeedMobs() {
    static std::unordered_set<int64> s;
    return s;
}

constexpr mce::UUID kSoulSpeedId{0x4c6570746f6e5534ull, 0x536f756c53706431ull}; // "LeptonU4SoulSpd1"

/// Java: Entity#getBlockPosBelowThatAffectsMyMovement (feet - 0.5).
BlockPos below(Mob const& mob) {
    auto feet = mob.getFeetPos();
    return BlockPos{
        static_cast<int>(std::floor(feet.x)),
        static_cast<int>(std::floor(feet.y - 0.5000001f)),
        static_cast<int>(std::floor(feet.z))
    };
}

/// Java damages the boots by 1 with a 4% chance per tick while the boost is active and the mob moves
/// (never in creative).
void damageBoots(Mob& mob, ItemStack const& boots) {
    auto const& d = mob.getPosDelta();
    if (d.x * d.x + d.z * d.z <= 1e-6f || mob.isCreative() || mob.getRandom().nextFloat() >= 0.04f) return;
    ItemStack copy = boots;
    copy.hurtAndBreak(1, &mob);
    mob.setArmor(SharedTypes::Legacy::ArmorSlot::Feet, copy);
}

// Bedrock's own soul speed only looks at soul sand / soil, so the boost is applied every mob tick here.
LL_TYPE_INSTANCE_HOOK(SoulSpeedHook, ll::memory::HookPriority::Normal, Mob, &Mob::$normalTick, void) {
    origin();
    if (isClientSide()) return;
    auto const& boots = getArmor(SharedTypes::Legacy::ArmorSlot::Feet);
    int  level        = boots.isNull() ? 0 : EnchantUtils::getEnchantLevel(Enchant::Type::SoulSpeed, boots);
    auto& tracked     = soulSpeedMobs();
    if (level <= 0 && tracked.empty()) return; // the common case: nothing to do
    auto uid = getOrCreateUniqueID().rawID;
    if (level <= 0 && !tracked.contains(uid)) return;
    auto attr = getMutableAttribute(SharedAttributes::MOVEMENT_SPEED());
    auto* inst = attr.mInstance->mPtr;
    if (!inst) return;
    bool on = cfg().enableSoulSandstone && level > 0 && isOnGround()
           && blockHasTag(getDimensionBlockSource().getBlock(below(*this)), tags().soulSpeed);
    float bonus = on ? 0.03f * (1.0f + static_cast<float>(level) * 0.35f) : 0.f; // Java getSoulSpeedBonus
    std::optional<float> current;
    for (auto const& m : inst->mModifierList.get())
        if (m.mId->a == kSoulSpeedId.a && m.mId->b == kSoulSpeedId.b) current = m.mAmount;
    if (!on) {
        tracked.erase(uid);
        if (current) inst->removeModifier(kSoulSpeedId, attr.mContext);
        return;
    }
    tracked.insert(uid);
    if (current && *current == bonus) {
        damageBoots(*this, boots);
        return; // unchanged: no attribute update
    }
    if (current) inst->removeModifier(kSoulSpeedId, attr.mContext);
    // not serializable: never saved with the entity, so a restart cannot leave it behind
    inst->addModifier(
        AttributeModifier(
            kSoulSpeedId,
            kSoulSpeedModifier,
            bonus,
            AttributeModifierOperation::OperationAddition,
            AttributeOperands::OperandCurrent,
            false
        ),
        attr.mContext
    );
    damageBoots(*this, boots);
}

// ---- requiresCorrectToolForDrops: no drops (and no playerDestroy) without the block's tool ----------

/// The item tag a `lepton:requires_tool` block needs ("" when the block has no requirement).
ItemTag const* requiredToolTag(Block const& b) {
    auto const& t = tags();
    if (!blockHasTag(b, t.requiresTool)) return nullptr;
    if (blockHasTag(b, t.shovelBlock)) return &t.shovel;
    if (blockHasTag(b, t.axeBlock)) return &t.axe;
    if (blockHasTag(b, t.hoeBlock)) return &t.hoe;
    return &t.pickaxe;
}

/// Whether the player that is breaking a block right now may harvest it, decided before the break
/// damages the tool (a tool that breaks on its last durability point still harvests, as in Java).
thread_local std::optional<bool> gHarvestVerdict;

bool canHarvest(Player& player, ItemTag const& tag) {
    auto const& held = player.getSelectedItem();
    return !held.isNull() && held.hasTag(tag);
}

LL_TYPE_INSTANCE_HOOK(
    SurvivalBreakHook,
    ll::memory::HookPriority::Normal,
    SurvivalMode,
    &SurvivalMode::$destroyBlock,
    bool,
    BlockPos const& pos,
    uchar           face
) {
    Player& player = mPlayer;
    auto const* tag = requiredToolTag(player.getDimensionBlockSource().getBlock(pos));
    auto saved = gHarvestVerdict;
    gHarvestVerdict = tag ? std::optional<bool>{canHarvest(player, *tag)} : std::nullopt;
    bool r = origin(pos, face);
    gHarvestVerdict = saved;
    return r;
}

LL_TYPE_INSTANCE_HOOK(
    ToolDropsHook,
    ll::memory::HookPriority::Normal,
    Block,
    &Block::playerDestroy,
    void,
    Player&         player,
    BlockPos const& pos
) {
    if (auto const* tag = requiredToolTag(*this)) {
        // Java: ServerPlayerGameMode only calls playerDestroy when the player can harvest the block
        bool ok = gHarvestVerdict ? *gHarvestVerdict : canHarvest(player, *tag);
        if (!ok) return;
    }
    origin(player, pos);
}

// ---- dragon_immune: the ender dragon does not break duskbound blocks ----------------------------

LL_TYPE_INSTANCE_HOOK(DragonWallsHook, ll::memory::HookPriority::Normal, EnderDragon, &EnderDragon::_checkWalls, bool, AABB bb) {
    // Swap immune blocks for bedrock (vanilla-immune) while the dragon checks the box, then put
    // them back: no drops, no neighbour updates, and the dragon is "blocked" as in Java.
    auto& region  = getDimensionBlockSource();
    auto const* bedrock = game::blockstates::of("minecraft:bedrock");
    std::vector<std::pair<BlockPos, Block const*>> saved;
    if (bedrock && cfg().enableDuskboundBlocks) {
        for (int x = (int)std::floor(bb.min.x); x <= (int)std::floor(bb.max.x); ++x)
            for (int y = (int)std::floor(bb.min.y); y <= (int)std::floor(bb.max.y); ++y)
                for (int z = (int)std::floor(bb.min.z); z <= (int)std::floor(bb.max.z); ++z) {
                    BlockPos p{x, y, z};
                    auto const& b = region.getBlock(p);
                    if (blockHasTag(b, tags().dragonImmune)) saved.emplace_back(p, &b);
                }
    }
    BlockChangeContext ctx{};
    for (auto& [p, b] : saved) region.setBlock(p, *bedrock, 0, nullptr, ctx);
    bool r = origin(bb);
    for (auto& [p, b] : saved) region.setBlock(p, *b, 0, nullptr, ctx);
    return r || !saved.empty();
}

// ---- infiniburn: fire on netherrack bricks never burns out --------------------------------------

void applyInfiniburn() {
    bool on = groupEnabled(cfg(), "netherrack_bricks");
    int  n  = 0;
    for (auto const& e : kItems) {
        if (e.value != "netherrack_bricks") continue;
        auto const* b = game::blockstates::of(std::string("lepton:") + std::string(e.key));
        if (!b || !blockHasTag(*b, tags().infiniburn)) continue;
        auto& type = const_cast<BlockType&>(b->getBlockType());
        auto bits = static_cast<uint64>(type.mProperties) & ~static_cast<uint64>(BlockProperty::InfiniBurn);
        if (on) bits |= static_cast<uint64>(BlockProperty::InfiniBurn);
        type.mProperties = static_cast<BlockProperty>(bits);
        n += on ? 1 : 0;
    }
    lp::log().debug("bricks: infiniburn on {} block types", n);
}

} // namespace

// ---- content table logic ------------------------------------------------------------------------

bool groupEnabled(Config const& c, std::string_view g) {
    bool mbt    = c.enableMoreBrickTypes;
    bool sand   = mbt && c.enableSandstoneBricks;
    bool cobble = mbt && c.enableCobblestoneBricks;
    if (g == "blue_nether_bricks") return mbt && c.enableBlueNetherBricks;
    if (g == "sandstone_bricks" || g == "red_sandstone_bricks") return sand;
    if (g == "soul_sandstone_bricks") return sand && c.enableSoulSandstone;
    if (g == "cobblestone_bricks" || g == "mossy_cobblestone_bricks") return cobble;
    if (g == "blackstone_bricks") return cobble && c.enableBlackstoneBricks;
    if (g == "dirt_bricks") return cobble && c.enableDirtBricks;
    if (g == "netherrack_bricks") return cobble && c.enableNetherrackBricks;
    if (g == "soul_sandstone") return c.enableSoulSandstone;
    if (g == "mud") return c.enableMoreMudBlocks;
    if (g == "midori") return c.enableMidori;
    if (g == "duskbound") return c.enableDuskboundBlocks;
    return false;
}

std::string_view groupOf(std::string_view path) { return find(kItems, path); }

bool itemEnabled(Config const& c, Externals const& e, std::string_view path) {
    auto g = groupOf(path);
    if (g.empty()) return true;
    if (!groupEnabled(c, g)) return false;
    if (path.find("_vertical_slab") != std::string_view::npos && !e.verticalSlabs) return false;
    return true;
}

bool isOurRecipe(std::string_view name) { return !find(kRecipes, name).empty(); }

bool recipeEnabled(Config const& c, Externals const& e, std::string_view name) {
    auto result = find(kRecipes, name);
    if (result.empty()) return true;
    if (!itemEnabled(c, e, result)) return false;
    // the vertical slab -> slab recipes need the vertical slab (U1's VerticalSlabs) as well
    if (name.find("_vertical_slab") != std::string_view::npos && !e.verticalSlabs) return false;
    // Quark's recipe conditions that go beyond "the result exists":
    bool sandstoneBricksFlag = c.enableMoreBrickTypes && c.enableSandstoneBricks;
    if (name == "soul_sandstone_wall" || name == "soul_sandstone_stairs_from_soul_sandstone_stonecutting")
        return sandstoneBricksFlag; // both carry the "sandstone_bricks" flag in Quark 4.1-488
    bool myalite = e.spiralSpires && e.duskyMyalite;
    if (name == "duskbound_block") return myalite;
    if (name == "duskbound_block_without_myalite") return !myalite;
    return true;
}

Externals currentExternals() {
    Externals e;
    e.verticalSlabs = externalFlag("vslabs.json", "enableVerticalSlabs", true);
    e.spiralSpires  = externalFlag("worldbeyond.json", "enableSpiralSpires", true);
    e.duskyMyalite  = itemRegistered("lepton:dusky_myalite");
    return e;
}

GateStats const& gateStats() { return gStats; }

Pairs contentItems() {
    Pairs v;
    for (auto const& e : kItems) v.emplace_back(e.key, e.value);
    return v;
}

Pairs contentRecipes() {
    Pairs v;
    for (auto const& e : kRecipes) v.emplace_back(e.key, e.value);
    return v;
}

namespace {

struct BricksModule {
    void load() {
        // Recipes and creative items are built when a world loads: before install() on BDS.
        RecipeGateHook::hook();
        CreativeGroupGateHook::hook();
        CreativeLooseGateHook::hook();
        // before the world DB module installs (it fires the BDS world-ready right away)
        game::db::onWorldClosed([] {
            gStats = {};
            externalsCache().reset();
            soulSpeedMobs().clear();
        });
        game::db::onWorldReady([] {
            applyInfiniburn();
            lp::log().info(
                "{}",
                lp::tr(
                    "lp.bricks.gate_summary",
                    "bricks: {} recipes and {} creative entries disabled by config",
                    gStats.recipesSkipped,
                    gStats.creativeSkipped
                )
            );
        });
    }
    void install() {
        SoulFireSpawnHook::hook();
        SoulFireSurviveHook::hook();
        SoulSpeedHook::hook();
        DragonWallsHook::hook();
        ToolDropsHook::hook();
        SurvivalBreakHook::hook();
    }
    void uninstall() {
        RecipeGateHook::unhook();
        CreativeGroupGateHook::unhook();
        CreativeLooseGateHook::unhook();
        SoulFireSpawnHook::unhook();
        SoulFireSurviveHook::unhook();
        SoulSpeedHook::unhook();
        DragonWallsHook::unhook();
        ToolDropsHook::unhook();
        SurvivalBreakHook::unhook();
    }
};

} // namespace

} // namespace lp::feature::bricks

LP_REGISTER_MODULE(lp::feature::bricks::BricksModule);
