#pragma once

// Virtual containers (server thread): inventories that belong to a block position (or any
// other key) and live in the world DB, opened in the vanilla container screen.
//
// Custom blocks cannot have block entities, so the items are stored under
// `lp:vc:<dim>:<x>:<y>:<z>` (JSON, one SNBT string per slot) and cached in memory (written on
// every world save). Opening one spawns a hidden helper entity `lepton:virtual_container`
// (behaviour pack, `minecraft:inventory`) inside the block, fills it, and opens its container for
// the player: every client (mod or vanilla, local or remote) gets the vanilla chest / hopper /
// dispenser screen, with server-authoritative item moves. Several players may view one container
// at once (they share the helper entity). When the last viewer closes it, the items go back to
// the DB and the helper is removed; stray helpers (crash, chunk saved while open) are emptied and
// removed on sight, so items are never duplicated.
//
// Bind a block once (in `install()`): using the block opens it, breaking it drops the contents.
//
//     lp::game::vc::Spec spec;
//     spec.size = 27; spec.screen = vc::Screen::Chest;
//     spec.titleKey = "block.lepton.crate"; spec.titleFallback = "Crate";
//     lp::game::vc::bindBlock("lepton:crate", spec);
//
// Or drive it yourself: `vc::open(player, pos, spec)`, `vc::items(pos)`, `vc::setItems(pos, ...)`,
// `vc::dropContents(region, pos)`. Mod clients can draw extra UI over the screen with the client
// UI overlay hooks (src/client/ui).

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/GlobalPos.h"

class BlockSource;
class Container;
class ItemStack;
class Player;

namespace lp::game::vc {

enum class Screen {
    Chest,     ///< chest screen; size 1..27 (single) or 28..54 (shown as a double chest)
    Hopper,    ///< 5 slots
    Dispenser, ///< 9 slots (3x3)
};

struct Spec {
    int         size   = 27;
    Screen      screen = Screen::Chest;
    /// Screen title: translated per player with `trFor(player, titleKey, titleFallback)`.
    std::string titleKey;
    std::string titleFallback = "Container";
    /// Optional filter: items it rejects are pushed back to the viewer's inventory (or dropped).
    std::function<bool(ItemStack const&)> accepts;
    /// Called (server thread) after the contents changed while open.
    std::function<void(core::GlobalPos const&)> onChanged;
    /// Called when the block bound with `bindBlock` is used: return false to not open it.
    std::function<bool(Player&, core::GlobalPos const&)> canOpen;
};

// ---- contents (open or not) ----------------------------------------------------------------------

/// The stored items at `pos` (`size` entries, empty stacks for empty slots).
[[nodiscard]] std::vector<ItemStack> items(core::GlobalPos const& pos, int size);
/// Replaces the stored items (also updates an open screen).
void setItems(core::GlobalPos const& pos, std::vector<ItemStack> const& items);
/// True when something is stored at `pos`.
[[nodiscard]] bool hasItems(core::GlobalPos const& pos);
/// Removes and returns everything stored at `pos` (closes open screens first).
std::vector<ItemStack> takeAll(core::GlobalPos const& pos);
/// Writes changed contents to the world DB now (also done on every world save).
void flush();
/// `takeAll` and spawn the items at the block (like a broken chest).
void dropContents(BlockSource& region, core::GlobalPos const& pos);

// ---- screens -------------------------------------------------------------------------------------

/// Opens the container at `pos` for `player`. False if the area isn't loaded or the player is
/// busy (another screen open).
bool open(Player& player, core::GlobalPos const& pos, Spec const& spec);
/// Closes every open screen of `pos` (contents are kept).
void closeAll(core::GlobalPos const& pos);
/// Number of players viewing `pos`.
[[nodiscard]] int viewers(core::GlobalPos const& pos);
/// The live container while `pos` is open (nullptr otherwise). Changes go to the viewers at once.
[[nodiscard]] Container* liveContainer(core::GlobalPos const& pos);
/// The position `player` currently has open (nullopt if none).
[[nodiscard]] std::optional<core::GlobalPos> openBy(Player const& player);

// ---- block binding -------------------------------------------------------------------------------

/// Using `blockId` opens its container; when the block disappears (broken, exploded, replaced)
/// its contents are dropped. Call in `install()`; `unbindBlock` in `uninstall()`.
void bindBlock(std::string const& blockId, Spec spec);
void unbindBlock(std::string const& blockId);

/// Entity type of the helper.
inline constexpr char const* kHelperEntity = "lepton:virtual_container";

} // namespace lp::game::vc
