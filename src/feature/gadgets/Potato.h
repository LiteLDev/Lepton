#pragma once

// Tiny potato internals shared with the selftests (docs/spec/gadgets.md, TinyPotato).

#include <array>
#include <optional>
#include <string>
#include <string_view>

class Block;
class BlockPos;
class ItemStack;
class Player;

namespace lp::core {
struct GlobalPos;
}

namespace lp::feature::gadgets::potato {

/// Quark's TinyPotatoInfo: the name without formatting, lower case and trimmed, with an
/// "enchanted/glowy/shiny/gay" prefix and a rune colour after it removed.
struct NameInfo {
    std::string name;
    bool        enchanted = false;
};
[[nodiscard]] NameInfo cleanName(std::string_view raw);

/// The look (0..24, see docs/spec/gadgets.md §2.2) of a potato with this custom name.
[[nodiscard]] int variantFor(std::string_view rawName, bool angry, bool halloween, bool jumping);

/// "son", "daughter", "child", "children", "tomato" for a held potato named `rawName`.
[[nodiscard]] std::string genderFor(std::string_view rawName);

/// The look of a placed potato block (`lepton:look` + 16 * `lepton:look_page`).
[[nodiscard]] int variantOf(Block const& block);

/// Jump animation frame (`lepton:jump`) for `ticksLeft` ticks of jump left (20..0).
[[nodiscard]] int jumpFrame(int ticksLeft);

/// Per-block data (world DB `lp:gadgets:tater:<dim>:<x>:<y>:<z>`).
struct Tater {
    std::string                name;
    bool                       angry = false;
    std::array<std::string, 6> items; ///< SNBT, "" = empty
    int                        jumpTicks = 0;
    int                        soundCd   = 0;
};

/// The stored data of the potato at `pos` (nullopt: not a known potato).
[[nodiscard]] std::optional<Tater> dataAt(core::GlobalPos const& pos);
/// Registers a potato (block already placed) with a name / angry flag.
void registerAt(core::GlobalPos const& pos, std::string const& name, bool angry);
/// Quark's TinyPotatoBlockEntity.interact + the heart (the block use).
void interact(Player& player, core::GlobalPos const& pos, int face);
/// Starts a jump (20 ticks) if not jumping.
void jump(core::GlobalPos const& pos);
/// A potato item as broken from a potato with this name / angry flag.
[[nodiscard]] ItemStack makePotatoItem(std::string const& name, bool angry, Player const* player);

} // namespace lp::feature::gadgets::potato
