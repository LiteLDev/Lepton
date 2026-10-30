#pragma once

// Game adapters for the enchanting unit: Bedrock's enchantment registry as `EnchantDef`s, the
// enchantments and custom tags of item stacks, XP levels. Usable on the server and the client.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "feature/enchanting/Matrix.h"

class ItemStack;
class ItemStackBase;
class Player;

namespace lp::feature::enchanting {

/// Every Bedrock enchantment (types 0..NumEnchantments-1 that exist), built on first use.
[[nodiscard]] std::vector<EnchantDef> const& registry();
[[nodiscard]] EnchantDef const*              defOf(int type);
/// "minecraft:sharpness", "sharpness", Java ids ("minecraft:binding_curse") -> the def (nullptr if unknown).
[[nodiscard]] EnchantDef const* defById(std::string_view id);
/// Normalises an enchantment id to the Bedrock form used in `EnchantDef::id` ("" if unknown).
[[nodiscard]] std::string normalizeId(std::string_view id);
/// Normalises a list of ids (unknown ones are dropped).
[[nodiscard]] std::vector<std::string> normalizeIds(std::vector<std::string> const& ids);

/// "lp.enchanting.enchant.<path>" name of an enchantment (English fallback).
[[nodiscard]] std::string nameKey(EnchantDef const& e);
[[nodiscard]] std::string englishName(EnchantDef const& e);
[[nodiscard]] std::string roman(int n);

// ---- items -------------------------------------------------------------------------------------

/// Enchantments of the stack (type -> level), from its "ench" list (books included).
[[nodiscard]] std::map<int, int> enchantsOf(ItemStackBase const& stack);
/// Replaces the stack's "ench" list (levels may exceed the maximum).
void setEnchants(ItemStackBase& stack, std::map<int, int> const& enchants);
/// Custom string tag in the stack's user data.
[[nodiscard]] std::optional<std::string> stringTag(ItemStackBase const& stack, std::string const& key);
void                                     setStringTag(ItemStackBase& stack, std::string const& key, std::string const& value);
void                                     removeTag(ItemStackBase& stack, std::string const& key);

/// Can `e` go on the item (primary or secondary slots, Java `supportsEnchantment`)?
[[nodiscard]] bool applicable(ItemStackBase const& stack, EnchantDef const& e);
[[nodiscard]] int  enchantSlotOf(ItemStackBase const& stack);
[[nodiscard]] int  enchantValueOf(ItemStackBase const& stack);
/// A matrix target: a single book, or an unenchanted single-stack item with an enchant slot.
[[nodiscard]] bool isMatrixTarget(ItemStackBase const& stack);
[[nodiscard]] bool isBook(ItemStackBase const& stack);
/// Any level above the enchantment's maximum.
[[nodiscard]] bool isOverlevel(ItemStackBase const& stack);

// ---- players -----------------------------------------------------------------------------------

[[nodiscard]] int playerLevel(Player const& player);
void              addLevels(Player& player, int levels);
[[nodiscard]] bool creative(Player const& player);

} // namespace lp::feature::enchanting
