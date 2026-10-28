#pragma once

// U16 hud: the logic behind the HUD and tooltip features (both builds, so the server harness can
// selftest it; the client build draws with it in src/client/hud/).

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mc/world/item/ItemStack.h"

class Player;
class ItemStackBase;

namespace lp::feature::hud {

// ---- UsageTicker ------------------------------------------------------------------------------

/// The slots of the ticker, in Quark's element order.
enum class TickerSlot { MainHand, OffHand, Head, Chest, Legs, Feet };

[[nodiscard]] constexpr bool isArmorSlot(TickerSlot s) { return s >= TickerSlot::Head; }
/// Quark's `EquipmentSlot.getIndex()`: hands 0/1, feet 0, legs 1, chest 2, head 3.
[[nodiscard]] int slotIndex(TickerSlot s);

/// Animation state of one ticker element (Quark's MAX_TIME = 60, ANIM_TIME = 5).
struct TickerAnim {
    static constexpr int kMaxTime  = 60;
    static constexpr int kAnimTime = 5;
    int                  liveTicks = 0;

    /// One client tick. `shownEmpty`: nothing to show; `changed`: item, damage or total changed.
    void tick(bool shownEmpty, bool changed);
    /// Vertical offset above the bottom of the screen (0..20) at `partial` ticks into the frame.
    [[nodiscard]] float offset(float partial) const;
    [[nodiscard]] bool  visible() const { return liveTicks > 0; }
};

/// What the slot shows: the stack to draw (empty = nothing) and its total count.
struct TickerStack {
    ItemStack stack;
    int       count = 0;
};

/// The stack in the player's slot.
[[nodiscard]] ItemStack const& tickerSource(Player const& player, TickerSlot slot);
/// Total count of `display` in the player's inventory (36 slots, armour, off hand), at least the
/// stack's own count; 1 for non-stackable non-block items.
[[nodiscard]] int tickerCount(Player const& player, ItemStackBase const& display);
/// The projectile a bow / crossbow without Infinity would use (empty if none / not a launcher).
[[nodiscard]] std::optional<ItemStack> projectileFor(Player const& player, ItemStackBase const& weapon);
/// Quark's `getLogicalStack`: what to show for the real `stack` (whose total is `count`).
[[nodiscard]] ItemStack logicalStack(Player const& player, TickerSlot slot, ItemStack const& stack, int count);
/// Quark's `getLogicalStack` + `getStackCount` for the slot.
[[nodiscard]] TickerStack tickerStack(Player const& player, TickerSlot slot);
/// Quark's `shouldChange`.
[[nodiscard]] bool tickerChanged(ItemStackBase const& now, ItemStackBase const& before, int countNow, int countBefore);
/// X of the element (UI units) for a screen of width `width`.
[[nodiscard]] float tickerX(float width, TickerSlot slot, bool invert, bool offhandEmpty, int shiftLeft, int shiftRight);

// ---- ElytraIndicator ---------------------------------------------------------------------------

/// An elytra that can still fly (not broken).
[[nodiscard]] bool canElytraFly(ItemStackBase const& chest);

// ---- food ------------------------------------------------------------------------------------

struct FoodInfo {
    int   nutrition  = 0;
    float saturation = 0; ///< Java's absolute saturation: nutrition * modifier * 2
    bool  poison     = false;
};
[[nodiscard]] std::optional<FoodInfo> foodInfo(ItemStackBase const& stack);
/// Quark's simplified saturation tier 0..5.
[[nodiscard]] int saturationTier(float saturation);

struct FoodRow {
    int         shanks   = 0;     ///< shanks to draw
    bool        halfLast = false; ///< the last drawn shank is a half one
    std::string text;             ///< "x3.5" when compressed, else ""
    int         width    = 0;     ///< reserved width (count * 9)
};
[[nodiscard]] FoodRow foodRow(int nutrition, int divisor, int compressionThreshold);

// ---- fuel ------------------------------------------------------------------------------------

/// Burn time in ticks (0 = not a fuel).
[[nodiscard]] int fuelTicks(ItemStackBase const& stack);
/// "x8" / "x1.5".
[[nodiscard]] std::string fuelText(int ticks, int divisor);

// ---- shulker boxes ---------------------------------------------------------------------------

[[nodiscard]] bool isShulkerBox(ItemStackBase const& stack);
/// Non-empty stacks inside, in slot order.
[[nodiscard]] std::vector<ItemStack> shulkerContents(ItemStackBase const& stack);
/// Grid columns x rows for `size` stacks.
[[nodiscard]] std::pair<int, int> shulkerDims(int size);
/// Dye colour (0xRRGGBB) of a dyed shulker box, or nullopt for the undyed one.
[[nodiscard]] std::optional<int> shulkerColor(ItemStackBase const& stack);

// ---- enchanted books -------------------------------------------------------------------------

struct BookEnchant {
    int         type  = 0; ///< Enchant::Type
    int         level = 0;
    std::string line;      ///< the hover-text line of this enchantment ("Sharpness V")
};
[[nodiscard]] std::vector<BookEnchant> bookEnchants(ItemStackBase const& stack);
/// Item ids (from `enchantingStacks` + `enchantingAdditionalStacks`) the enchantment can go on.
[[nodiscard]] std::vector<std::string> enchantItems(int enchantType);

// ---- attributes ------------------------------------------------------------------------------

/// Quark's equipment slot groups (EquipmentSlotGroup order: any, mainhand, offhand, hand, feet, legs,
/// chest, head, armor, body).
enum class SlotGroup { Any, MainHand, OffHand, Hand, Feet, Legs, Chest, Head, Armor, Body };
enum class AttrOp { AddValue, AddMultipliedBase, AddMultipliedTotal };

struct Attribute {
    std::string id;         ///< icon name: attack_damage, armor, armor_toughness, knockback_resistance, ...
    SlotGroup   group = SlotGroup::MainHand;
    AttrOp      op    = AttrOp::AddValue;
    double      amount = 0; ///< modifier amount
    double      base   = 0; ///< the player's base value (added for AddValue display)
};
/// The attribute modifiers of a Bedrock item, merged per (attribute, op, group).
[[nodiscard]] std::vector<Attribute> attributesOf(ItemStackBase const& stack);
/// Display text of one entry and whether it is "negative" (red without comparison).
[[nodiscard]] std::pair<std::string, bool> formatAttribute(Attribute const& a);
/// Quark's `#.##` number format.
[[nodiscard]] std::string formatNumber(double v);
/// The ticker slot a stack is compared against (its equipment slot).
[[nodiscard]] TickerSlot equipmentSlotFor(ItemStackBase const& stack);

} // namespace lp::feature::hud
