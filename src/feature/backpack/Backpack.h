#pragma once

// U14 backpack: Quark's Backpack, TotemOfHolding and SeedPouch modules (docs/spec/backpack.md).
// Server side (both builds); the client mod part is src/client/backpack/.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Actor;
class ActorDamageSource;
class BlockPos;
class Container;
class ItemStack;
class ItemStackBase;
class Player;

namespace lp::feature::backpack {

/// `config/backpack.json` (Quark's options and defaults; SPEC §5.1).
struct BackpackConfig {
    int version = 1;

    bool   enableBackpack           = true;
    /// Set this to true to allow the backpacks to be unequipped even with items in them
    bool   superOpMode              = false;
    bool   enableRavagerHide        = true;
    bool   itemsInBackpackTick      = true;
    int    baseRavagerHideDrop      = 1;
    double extraChancePerLooting    = 0.5;
    bool   allowArmorStandUnloading = true;

    bool enableTotemOfHolding = true;
    /// Set this to false to remove the behaviour where totems destroy themselves if the player dies again.
    bool darkSoulsMode        = true;
    /// Spawn Totem on PVP Kill. Totem will always spawn if the player killer is themselves.
    bool enableOnPK           = false;
    /// If a totem is destroyed, the items it holds are destroyed alongside it rather than dropped
    bool destroyLostItems     = false;
    /// Set this to false to only allow the owner of a totem to collect its items rather than any player
    bool allowAnyoneToCollect = true;
    /// Set this to true to make the totem drop everything when punched once
    bool dropAllItemsOnHit    = false;

    bool enableSeedPouch           = true;
    int  maxItems                  = 640;
    bool showAllVariantsInCreative = true;
    int  shiftRange                = 3;
    /// Allow putting bone meal into the Seed Pouch (or anything else in the tag 'quark:seed_pouch_fertilizers')
    bool allowFertilizer           = true;
    int  fertilizerShiftRange      = 3;
};

[[nodiscard]] BackpackConfig const& cfg();

inline constexpr std::string_view kBackpack      = "lepton:backpack";
inline constexpr std::string_view kRavagerHide   = "lepton:ravager_hide";
inline constexpr std::string_view kBondedHide    = "lepton:bonded_ravager_hide";
inline constexpr std::string_view kSeedPouch     = "lepton:seed_pouch";
inline constexpr std::string_view kTotem         = "lepton:totem";
inline constexpr std::string_view kStorageEntity = "lepton:backpack_storage";
inline constexpr int              kBackpackSlots = 27;

[[nodiscard]] bool isItem(ItemStackBase const& stack, std::string_view id);
/// Item tag check on the stack's item ("lepton:backpack_blocked", ...).
[[nodiscard]] bool hasItemTag(ItemStackBase const& stack, std::string_view tag);

// ---- backpack (Backpack.cpp) -----------------------------------------------------------------------

/// The 27 stacks stored in a backpack item (empty stacks for empty slots).
[[nodiscard]] std::vector<ItemStack> backpackItems(ItemStackBase const& backpack);
/// Writes the stacks into the backpack item (all empty: removes the data).
void setBackpackItems(ItemStack& backpack, std::vector<ItemStack> const& items);
[[nodiscard]] bool backpackHasItems(ItemStackBase const& backpack);
/// Wears a backpack in the chest slot.
[[nodiscard]] bool wearsBackpack(Player const& player);
/// Opens the backpack screen (false when not wearing one, busy, ...). A request while a backpack
/// session exists closes it (and returns true: handled).
bool openBackpack(Player& player);
/// Closes the backpack screen (if open), writing the contents back.
void closeBackpack(Player& player);
[[nodiscard]] bool backpackOpen(Player const& player);
/// The live helper container while the player's backpack screen is open.
[[nodiscard]] Container* backpackContainer(Player const& player);
/// Ravager hide amount for a looting level and a random value in [0, 1).
[[nodiscard]] int ravagerHideDrop(int lootingLevel, double random);
/// Writes an open backpack screen back into the worn item now (before deaths); same as closeBackpack.
void flushBackpack(Player& player);

// ---- totem (Totem.cpp) --------------------------------------------------------------------------

/// Moves the dying player's items into a new totem (Quark's onPlayerDrops). Returns the totem id or 0.
int64_t spawnTotemFor(Player& player, ActorDamageSource const& source);
/// Same without the PVP check (the decision was taken when the player died).
int64_t spawnTotemNow(Player& player);
/// The keepInventory game rule is on (no drops, no totems).
[[nodiscard]] bool keepInventoryOn();
/// The player's last totem id (0 = none).
[[nodiscard]] int64_t lastTotemOf(Player const& player);
/// A player hits a totem: Quark's skipAttackInteraction. Returns the number of stacks given.
int collectFromTotem(Player& player, Actor& totem);
/// Stacks stored in a totem.
[[nodiscard]] std::vector<ItemStack> totemItems(Actor& totem);
[[nodiscard]] std::string totemOwner(int64_t totemId);

// ---- seed pouch (SeedPouch.cpp) -------------------------------------------------------------------

struct PouchContents {
    std::string snbt;  ///< the item (count 1), "" when empty
    int         count = 0;
    [[nodiscard]] bool empty() const { return snbt.empty() || count <= 0; }
};
[[nodiscard]] PouchContents pouchContents(ItemStackBase const& pouch);
void setPouchContents(ItemStack& pouch, PouchContents const& contents);
[[nodiscard]] bool pouchHoldable(ItemStackBase const& stack);
[[nodiscard]] bool pouchFertilizer(ItemStackBase const& stack);
[[nodiscard]] bool pouchCanFit(PouchContents const& c, ItemStackBase const& other);
/// Moves items from `other` into the pouch contents (mutating both); true if any moved.
bool pouchAbsorb(PouchContents& c, ItemStack& other);
/// Takes up to one max stack out of the contents.
[[nodiscard]] ItemStack pouchSplitStack(PouchContents& c);
/// Uses the pouch held in the main hand on a block (planting, 3x3 when sneaking, chest insert).
/// Returns the number of items used.
int pouchUseOn(Player& player, BlockPos const& pos, int face);
/// Use in the air: takes a stack out (or, sneaking, absorbs from the inventory).
bool pouchUseInAir(Player& player);
/// Absorbs fitting stacks from the inventory into the selected pouch.
bool pouchInsert(Player& player);
/// Takes one stack out of the selected pouch into the inventory.
bool pouchTake(Player& player);

} // namespace lp::feature::backpack
