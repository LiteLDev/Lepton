#pragma once

// Item tweak helpers shared with the selftests and the client tooltip.

#include <cstdint>
#include <string_view>
#include <vector>

class Item;
class ItemStack;
class ItemStackBase;
class Player;

namespace lp::feature::mobtweaks {

inline constexpr std::string_view kElytraRecipe = "lepton:elytra_duplication";
inline constexpr char const*      kCosmeticTag  = "lepton:cosmetic_anvil_items";

/// The baked level of enchantment `type` that applies to `stack` given its real level `realLevel`
/// (0 when the baked enchantment is incompatible with another enchantment on the item).
[[nodiscard]] int effectiveBakedLevel(int type, ItemStackBase const& stack, int realLevel);
/// "minecraft:<tier>_<kind>" for harvest level 0..4 (wooden .. netherite), nullptr if unknown.
[[nodiscard]] Item const* tierTool(int harvestLevel, std::string_view kind);
/// NoDurabilityOnCosmetics: an anvil craft with this material (right slot) spares the anvil.
[[nodiscard]] bool sparesAnvil(ItemStackBase const& material);
/// Marks the player's next anvil damage roll as spared (what the anvil craft hook does).
void noteSparedCraft(int64_t playerId);
/// True (and forgets it) if the player's current anvil craft spares the anvil.
bool consumeSparedCraft(int64_t playerId);
namespace testing {
/// False: the anvil / enchanting table / grindstone see baked enchantments too (selftests compare).
bool& gearScopes();
} // namespace testing

/// Inserts the stacks into the player's inventory, dropping what doesn't fit.
void giveOrDrop(Player& player, std::vector<ItemStack> items);

} // namespace lp::feature::mobtweaks
