#pragma once
// U25 farming: SimpleHarvest, HoeHarvesting, PigLitters, PoisonPotatoUsage, CoralOnCactus,
// RenewableSporeBlossoms, PetalsOnWater, GrabChickens, HorsesSwim, VillagersFollowEmeralds.
// Spec: docs/spec/farming.md.

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"

class Actor;
class Block;
class BlockPos;
class BlockSource;
class ItemStack;
class ItemStackBase;
class Player;

namespace lp::feature::farming {

/// `config/farming.json` (keys and defaults: docs/spec/farming.md §5).
struct FarmingConfig {
    int version = 1;

    bool enableSimpleHarvest       = true;
    bool emptyHandHarvest          = true;
    bool harvestingCostsDurability = false;
    bool doHarvestingSearch        = true;
    bool villagersUseSimpleHarvest = true;
    std::vector<std::string> harvestableBlocks = {
        "minecraft:wheat[growth=7]",
        "minecraft:carrots[growth=7]",
        "minecraft:potatoes[growth=7]",
        "minecraft:beetroot[growth=7]",
        "minecraft:nether_wart[age=3]",
        "minecraft:cocoa[age=2,direction=0],minecraft:cocoa[age=0,direction=0]",
        "minecraft:cocoa[age=2,direction=1],minecraft:cocoa[age=0,direction=1]",
        "minecraft:cocoa[age=2,direction=2],minecraft:cocoa[age=0,direction=2]",
        "minecraft:cocoa[age=2,direction=3],minecraft:cocoa[age=0,direction=3]",
    };
    std::vector<std::string> rightClickableBlocks = {
        "minecraft:sweet_berry_bush",
        "minecraft:cave_vines_body_with_berries",
        "minecraft:cave_vines_head_with_berries",
    };

    bool enableHoeHarvesting = true;
    int  regularHoeRadius    = 2;
    int  highTierHoeRadius   = 3;

    bool enablePigLitters      = true;
    int  minPigLitterSize      = 2;
    int  maxPigLitterSize      = 3;
    bool pigsEatGoldenCarrots  = true;
    int  minGoldenCarrotBoost  = 0;
    int  maxGoldenCarrotBoost  = 2;

    bool   enablePoisonPotatoUsage = true;
    double chance                  = 0.1;
    bool   poisonEffect            = true;

    bool enableCoralOnCactus = true;

    bool   enableRenewableSporeBlossoms = true;
    double boneMealChance               = 0.2;

    bool enablePetalsOnWater = true;

    bool   enableGrabChickens = true;
    bool   needsNoHelmet      = true;
    int    slownessLevel      = 1;
    double chickenOffset      = 0.0;

    bool enableHorsesSwim = true;

    bool enableVillagersFollowEmeralds = true;
};

/// The config with Quark's @Config.Min/Max clamps applied.
[[nodiscard]] FarmingConfig const& cfg();

// ---- shared helpers (Common.cpp) ---------------------------------------------------------------

/// Runs `fn` on the server thread after `ticks` ticks (world edits after a cancelled interaction).
void later(int ticks, std::function<void()> fn);
/// Server tick counter of this module (reset per world).
[[nodiscard]] long long now();
/// Uniform [0, 1).
[[nodiscard]] double random01();
/// Uniform integer in [min(a,b), max(a,b)].
[[nodiscard]] int randomBetween(int a, int b);

[[nodiscard]] Player* playerById(long long uniqueId);
[[nodiscard]] bool    isCreative(Player const& player);
/// Main hand item of the player (mutable).
[[nodiscard]] ItemStack& mainHand(Player& player);
/// Uses up one item of `stack` in the player's selected slot (no-op in creative) and syncs.
void consumeOne(Player& player, ItemStack const& stack);
/// Plays a named sound (resource-pack name, e.g. "random.eat") at a position for nearby players.
void playSoundAt(BlockSource& region, float x, float y, float z, std::string const& name, float volume, float pitch);
/// Spawns an item entity at the centre of a block.
void popItem(BlockSource& region, BlockPos const& pos, ItemStack const& stack);
/// A level event (particles) sent to the players of `region`'s dimension near `at`.
void levelEvent(BlockSource& region, Vec3 const& at, SharedTypes::Legacy::LevelEvent event, int data);
/// Block-break particles of `block` at `pos`.
void destroyEffect(BlockSource& region, BlockPos const& pos, Block const& block);
[[nodiscard]] bool hasItemTag(ItemStackBase const& stack, std::string_view tag);
[[nodiscard]] bool isHoe(ItemStackBase const& stack);

// ---- SimpleHarvest / HoeHarvesting (Harvest.cpp) -------------------------------------------------

/// One parsed `harvestableBlocks` entry.
struct HarvestRule {
    std::string                                       id;
    std::vector<std::pair<std::string, std::string>>  states; ///< state -> value (as written)
    std::string                                       afterId;
    std::vector<std::pair<std::string, std::string>>  afterStates;
};
/// Parses "id[a=1,b=2][,after[...]]" (the comma inside brackets does not split). nullopt if bad.
[[nodiscard]] std::optional<HarvestRule> parseHarvestRule(std::string_view text);

enum class HarvestAction { None, Harvest, Click };
[[nodiscard]] HarvestAction actionFor(Block const& block, bool canRightClick);
/// The replanted state for a harvestable block (nullptr if not harvestable).
[[nodiscard]] Block const* replantedState(Block const& block);
/// HoeHarvesting radius for the item (1 = not a hoe / module off).
[[nodiscard]] int hoeRange(ItemStackBase const& stack);
[[nodiscard]] bool isHoeHarvestable(Block const& block);
/// Harvests and replants the crop at `pos` (drops minus one seed). `by` may be null (villagers).
bool harvestAndReplant(BlockSource& region, BlockPos const& pos, Actor* by, ItemStack const& tool);
/// SimpleHarvest's click logic. Plans the work and runs it on the next tick; returns true if the
/// click was a harvest (the interaction must be cancelled).
bool simpleHarvestClick(Player& player, BlockPos const& pos, bool deferred = true);
/// HoeHarvesting after the player broke `pos` (state `broken`) with a hoe.
void hoeHarvest(Player& player, BlockPos const& pos, Block const& broken);
void rebuildHarvestTables();

void installHarvest();
void uninstallHarvest();

// ---- plants (Plants.cpp) -------------------------------------------------------------------------

void installPlants();
void uninstallPlants();
/// PetalsOnWater: place / add a petal at `pos` (water source or water petals). Returns true if done.
bool placePetal(Player& player, BlockSource& region, BlockPos pos, int face, bool fromBlockClick);
/// Places a block without the water layer the game adds to waterloggable blocks (coral fans).
void placeDry(BlockSource& region, BlockPos const& pos, Block const& block);
/// RenewableSporeBlossoms: returns true if handled (bone meal consumed by the caller's rules).
bool boneMealSporeBlossom(BlockSource& region, BlockPos const& pos);

// ---- animals (Animals.cpp) -----------------------------------------------------------------------

void installAnimals();
void uninstallAnimals();
void tickAnimals();
/// Forgets the tempt state (world closed).
void resetTempts();
/// GameMode::interact handling; returns true if the interaction was consumed.
bool onInteractEntity(Player& player, Actor& target);
/// PoisonPotatoUsage on `target`; returns true if the potato was used.
bool usePoisonPotato(Player& player, Actor& target, bool forceSuccess = false);
[[nodiscard]] bool isPoisoned(Actor const& actor);
/// In love mode (the flag or a running love timer, which is set before the flag syncs).
[[nodiscard]] bool inLove(Actor& actor);
/// GrabChickens: true if the player may carry a chicken.
[[nodiscard]] bool canHostChicken(Player const& player);
/// Chickens riding `player`.
[[nodiscard]] std::vector<Actor*> carriedChickens(Player& player);
/// PigLitters after a pig was bred; `parentA`/`parentB` are the parents. Returns extra piglets.
int spawnLitter(Actor& parentA, Actor& parentB);
/// Sets the pig in love from a golden carrot (true if it accepted it).
bool feedGoldenCarrot(Player& player, Actor& pig);
/// HorsesSwim: one tick for one ridden horse; true if pushed.
bool horseSwimTick(Actor& horse);

} // namespace lp::feature::farming
