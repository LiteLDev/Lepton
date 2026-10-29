// U15 inventory, client side (client build): the buttons of InventorySorting / EasyTransferring,
// the ChestSearching bar, the HotbarChanger HUD and keys, ExpandedItemInteractions clicks and
// hints, ItemSharing (Shift + key over a slot, item card). Everything that changes items is a
// request to the server (feature/inventory/Messages.h). docs/spec/inventory.md §4.

#ifdef LP_CLIENT

#include <algorithm>
#include <atomic>
#include <map>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/containers/models/ContainerModel.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/components/storage_item_utility/StorageItemUtility.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/inventory/Common.h"
#include "feature/inventory/Config.h"
#include "feature/inventory/Messages.h"
#include "feature/inventory/Search.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::client::inventory {

using namespace lp::client::ui;
namespace inv = lp::feature::inventory;
using feature::inventory::cfg;

namespace {

using Clock = std::chrono::steady_clock;

// ---- shared state (window thread <-> render thread) ---------------------------------------------

/// What the input handlers (window thread) know about the slot under the mouse. Only the render
/// thread reads items; the input side works from this snapshot of the last frame.
struct Hovered {
    std::optional<Slot> slot;
    std::string         action;        ///< predicted interaction click ("" = none)
    bool                filled = false; ///< the slot holds an item
    int                 bundleId = -2;  ///< dynamic container id of a bundle (-2: not a bundle, -1: unknown)
};

struct Frame {
    std::string       screen;    ///< screen of the last overlay frame ("" = none)
    std::vector<Slot> slots;     ///< its item slots
    bool              container = false; ///< accepted container screen (container buttons)
    Rect              field;     ///< search field (w = 0: not shown)
    bool              cursorEmpty = true;
    Hovered           hovered;
};

std::mutex          gMutex;
Frame               gFrame;
Vec                 gMouse;
bool                gShift = false;
std::deque<std::function<void()>> gQueue; ///< work for the render (client) thread

// Flags written by the input handlers and read by the render thread.
std::atomic<bool> gSearchEnabled = false;
std::atomic<bool> gSearchFocused = false;
std::atomic<bool> gShiftLocked   = false;
std::atomic<bool> gHotbarOpen    = false;
// render-thread state
std::string gSearch;
float       gHotbarHeight = 0; ///< 0..1
Clock::time_point gLastHud{};
Clock::time_point gLastShare{};
bool        gReported = false;
std::string gReportedFor; ///< screen whose button positions were logged

struct Card {
    ItemStack         item;
    std::string       text;
    Clock::time_point until;
};
std::optional<Card> gCard;

void post(std::function<void()> fn) {
    std::lock_guard lock(gMutex);
    gQueue.push_back(std::move(fn));
}

void runQueue() {
    std::deque<std::function<void()>> q;
    {
        std::lock_guard lock(gMutex);
        q.swap(gQueue);
    }
    for (auto& fn : q) {
        try {
            fn();
        } catch (std::exception const& e) {
            log().error("inventory: client action failed: {}", e.what());
        }
    }
}

bool isPlayerCollection(std::string const& c) {
    return c == "inventory_items" || c == "hotbar_items" || c == "combined_hotbar_and_inventory_items";
}
bool isContainerCollection(std::string const& c) {
    return !isPlayerCollection(c) && c.find("container") != std::string::npos;
}

// ---- client-side item reading ---------------------------------------------------------------------

ContainerModel* modelOf(std::string const& collection) {
    auto* p = client::currentPlayer();
    if (!p) return nullptr;
    auto const& manager = p->mContainerManager.get();
    if (!manager) return nullptr;
    auto& models = manager->mContainers.get();
    if (auto it = models.find(collection); it != models.end() && it->second) return it->second.get();
    for (auto& [name, model] : models)
        if (model && model->mContainerStringName.get() == collection) return model.get();
    return nullptr;
}

ItemStack slotItem(std::string const& collection, int index) {
    if (auto* m = modelOf(collection); m && index >= 0 && index < m->getContainerSize()) return m->getItemStack(index);
    auto* p = client::currentPlayer();
    if (!p) return {};
    auto& pinv = p->getInventory();
    if (collection == "hotbar_items" && index < 9) return pinv.getItem(index);
    if (collection == "inventory_items" && index < 27) return pinv.getItem(index + 9);
    if (collection == "combined_hotbar_and_inventory_items" && index < 36) return pinv.getItem(index);
    return {};
}

ItemStack cursorItem() { return slotItem("cursor_items", 0); }

std::optional<Slot> slotAt(Frame const& f, Vec pos) {
    for (auto const& s : f.slots)
        if (s.collection != "cursor_items" && s.rect.contains(pos.x, pos.y)) return s;
    return std::nullopt;
}

float invGuiScale() {
    auto* c = client::currentClient();
    return c ? c->getGuiData()->mInvGuiScale : 1.f;
}

void click() {
    if (!cfg().satisfyingClick) return;
    if (auto* c = client::currentClient()) c->playUI("random.click", 1.f, 1.f);
}

bool spectator() {
    auto* p = client::currentPlayer();
    return p && p->isSpectator();
}

// ---- requests ---------------------------------------------------------------------------------------

void sortPlayer() {
    if (!cfg().enableInventorySorting) return;
    click();
    net::sendToServer(inv::SortRequest{true});
}
void sortContainer() {
    if (!cfg().enableInventorySorting || !cfg().enableChests) return;
    click();
    net::sendToServer(inv::SortRequest{false});
}
void transfer(bool restock) {
    if (!cfg().enableEasyTransferring) return;
    bool smart;
    {
        std::lock_guard lock(gMutex);
        smart = gShift || gShiftLocked;
    }
    net::sendToServer(inv::TransferRequest{smart, restock});
    log().info("inventory: {} request sent", restock ? "extract" : "insert");
}

// ---- drawing helpers ---------------------------------------------------------------------------------

enum class Icon { Sort, Insert, Extract, ShiftLock, Filter };

void drawIcon(DrawContext& dc, Rect r, Icon icon, Color c) {
    float u = r.w / 10.f; // one icon pixel
    auto  px = [&](float x, float y, float w, float h) { dc.fill({r.x + x * u, r.y + y * u, w * u, h * u}, c); };
    switch (icon) {
    case Icon::Sort: // three bars, longest on top
        px(2, 2, 6, 1), px(2, 4.5f, 4, 1), px(2, 7, 2, 1);
        break;
    case Icon::Insert: // arrow down into a tray
        px(4.5f, 1, 1, 4), px(2.5f, 4, 5, 1), px(3.5f, 5, 3, 1), px(4.5f, 6, 1, 1), px(1, 8, 8, 1);
        break;
    case Icon::Extract: // arrow up out of a tray
        px(4.5f, 3, 1, 4), px(2.5f, 3, 5, 1), px(3.5f, 2, 3, 1), px(4.5f, 1, 1, 1), px(1, 8, 8, 1);
        break;
    case Icon::ShiftLock: // the shift arrow
        px(4.5f, 1, 1, 1), px(3.5f, 2, 3, 1), px(2.5f, 3, 5, 1), px(1.5f, 4, 7, 1), px(3.5f, 5, 3, 3);
        break;
    case Icon::Filter: // magnifier
        px(2, 1, 3, 1), px(1, 2, 1, 3), px(5, 2, 1, 3), px(2, 5, 3, 1), px(5.5f, 5.5f, 1, 1), px(6.5f, 6.5f, 2, 2);
        break;
    }
}

void miniButton(Overlay& o, Rect r, Icon icon, bool shifted, std::string const& tooltip, std::function<void()> fn) {
    o.button(r, "", std::move(fn), tooltip);
    drawIcon(o.draw(), r, icon, shifted ? colors::yellow : colors::white);
}

void tooltipBox(DrawContext& dc, Vec at, std::vector<std::string> const& lines) {
    float w = 0;
    for (auto const& l : lines) w = std::max(w, dc.textWidth(l));
    Rect box{at.x + 10, at.y - 12, w + 6, 10.f * lines.size() + 4};
    dc.fill(box, Color{0.06f, 0.0f, 0.12f, 0.94f});
    dc.outline(box, Color::rgb(0x5000ff, 0.6f));
    for (size_t i = 0; i < lines.size(); ++i) dc.text(lines[i], box.x + 3, box.y + 2 + 10.f * i);
}

// ---- interaction prediction (same rules as the server) ------------------------------------------------

/// The interaction the server would perform for a click on `slot` (Quark ExpandedItemInteractions).
std::string predictInteraction(Slot const& slot, bool inventoryScreen) {
    auto const& c = cfg();
    if (!c.enableExpandedItemInteractions) return {};
    auto at     = slotItem(slot.collection, slot.index);
    auto cursor = cursorItem();
    auto* p     = client::currentPlayer();
    if (!p || p->isSpectator()) return {};
    if (c.enableShulkerBoxInteraction && inv::isShulkerBox(cursor) && cursor.mCount == 1 && !game::isEmpty(at) && !inv::isShulkerBox(at)) {
        auto box = cursor, item = at;
        if (inv::insertIntoShulker(box, item, true) > 0) return "pick_into";
    }
    if (c.enableLavaInteraction && game::itemId(at) == "minecraft:lava_bucket" && !game::isEmpty(cursor) && !p->isCreative()
        && !inv::isFireResistant(cursor) && !inv::isShulkerBox(cursor))
        return "trash";
    if (c.enableArmorInteraction && game::isEmpty(cursor) && !game::isEmpty(at) && at.mCount == 1) {
        int s = inv::armorSlotFor(at);
        if (s >= 0 && !inv::hasBindingCurse(p->getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(s)))) return "equip";
    }
    if (c.enableShulkerBoxInteraction && inv::isShulkerBox(at) && at.mCount == 1) {
        if (game::isEmpty(cursor)) {
            if (c.allowOpeningShulkerBoxes && inventoryScreen && isPlayerCollection(slot.collection)) return "open";
        } else if (inv::isShulkerBox(cursor)) {
            auto into = at, from = cursor;
            if (inv::dumpShulker(into, from, true) > 0) return "dump";
        } else {
            auto box = at, item = cursor;
            if (inv::insertIntoShulker(box, item, true) > 0) return "insert";
        }
    }
    return {};
}

std::string hintKey(std::string const& action) {
    bool        invert = cfg().invertClicks;
    std::string key    = action == "trash" ? "quark.misc.trash_item"
                       : action == "insert" || action == "pick_into" ? "quark.misc.insert_shulker_box"
                       : action == "dump"  ? "quark.misc.merge_shulker_box"
                       : action == "equip" ? "quark.misc.equip_armor"
                                           : "quark.misc.open_shulker";
    return invert ? key + ".invert" : key;
}

std::string hintText(std::string const& action) {
    static std::map<std::string, std::string> const fallback{
        {"quark.misc.trash_item", "Right-Click to Trash Item"},       {"quark.misc.trash_item.invert", "Left-Click to Trash Item"},
        {"quark.misc.insert_shulker_box", "Right-Click to Insert Item"}, {"quark.misc.insert_shulker_box.invert", "Left-Click to Insert Item"},
        {"quark.misc.merge_shulker_box", "Right-Click to Dump Contents"}, {"quark.misc.merge_shulker_box.invert", "Left-Click to Dump Contents"},
        {"quark.misc.equip_armor", "Right-Click to Equip"},            {"quark.misc.equip_armor.invert", "Left-Click to Equip"},
        {"quark.misc.open_shulker", "Right-Click to Open"},            {"quark.misc.open_shulker.invert", "Left-Click to Open"},
    };
    auto key = hintKey(action);
    auto it  = fallback.find(key);
    return (action == "trash" ? "§c" : "§e") + ctr(key, it == fallback.end() ? key : it->second);
}

// ---- the screen overlay --------------------------------------------------------------------------------

void overlay(Overlay& o, ScreenInfo const& s) {
    runQueue();
    if (s.name == "hud_screen" || s.name.find("custom_screen") != std::string::npos) {
        gReportedFor.clear();
        return;
    }
    auto const& c = cfg();
    auto&       dc = o.draw();

    // Classify the screen.
    std::set<int>     containerIdx;
    Slot const*       containerTopRight = nullptr;
    Slot const*       containerFirst    = nullptr;
    Slot const*       playerTopRight    = nullptr;
    for (auto const& slot : s.slots) {
        if (isContainerCollection(slot.collection)) {
            containerIdx.insert(slot.index);
            if (slot.index == 8) containerTopRight = &slot;
            if (slot.index == 0) containerFirst = &slot;
        }
        if ((slot.collection == "inventory_items" && slot.index == 8)
            || (slot.collection == "combined_hotbar_and_inventory_items" && slot.index == 17 && !playerTopRight))
            playerTopRight = &slot;
    }
    bool inventoryScreen = s.name == "inventory_screen";
    bool containerScreen = !inventoryScreen && static_cast<int>(containerIdx.size()) >= c.chestButtonSlotTarget
                        && containerTopRight && containerFirst && playerTopRight;
    if (!inventoryScreen && !containerScreen) {
        gReportedFor.clear();
        std::lock_guard lock(gMutex);
        gFrame = Frame{s.name, s.slots, false, {}, true, {}};
        return;
    }

    bool shift;
    Vec  mouse;
    {
        std::lock_guard lock(gMutex);
        shift = gShift || gShiftLocked;
        mouse = gMouse;
    }
    float sc = playerTopRight ? playerTopRight->rect.w / 18.f : 1.f;
    auto  at = [&](Slot const* slot, float dx, float dy) { return Rect{slot->rect.x + dx * sc, slot->rect.y + dy * sc, 10 * sc, 10 * sc}; };

    // Buttons above the player inventory, right to left by priority (Quark InventoryButtonHandler).
    if (playerTopRight) {
        float x = 6;
        if (c.enableInventorySorting && (inventoryScreen ? c.enablePlayerInventory : c.enablePlayerInventoryInChests)) {
            auto key = inventoryScreen ? "quark.gui.button.sort" : "quark.gui.button.sort_inventory";
            miniButton(o, at(playerTopRight, x, -13), Icon::Sort, false, ctr(key, inventoryScreen ? "Sort" : "Sort Inventory"),
                       [] { sortPlayer(); });
            x -= 12;
        }
        if (containerScreen && c.enableEasyTransferring) {
            miniButton(o, at(playerTopRight, x, -13), Icon::Insert, shift,
                       shift ? ctr("quark.gui.button.insert_filtered", "Insert (Filtered)") : ctr("quark.gui.button.insert", "Insert"),
                       [] { transfer(false); });
            x -= 12;
            miniButton(o, at(playerTopRight, x, -13), Icon::Extract, shift,
                       shift ? ctr("quark.gui.button.extract_filtered", "Extract (Filtered)") : ctr("quark.gui.button.extract", "Extract"),
                       [] { transfer(true); });
            x -= 12;
            if (c.enableShiftLock) {
                miniButton(o, at(playerTopRight, x, -13), Icon::ShiftLock, gShiftLocked, ctr("quark.gui.button.shift_lock", "Shift Lock"), [] {
                    gShiftLocked = !gShiftLocked;
                    log().info("inventory: shift lock {}", gShiftLocked ? "on" : "off");
                });
                x -= 12;
            }
        }
    }

    Rect field{};
    if (containerScreen) {
        if (c.enableInventorySorting && c.enableChests)
            miniButton(o, at(containerTopRight, 6, -13), Icon::Sort, false, ctr("quark.gui.button.sort_container", "Sort Container"),
                       [] { sortContainer(); });
        if (c.enableChestSearching) {
            miniButton(o, at(containerTopRight, -6, -13), Icon::Filter, gSearchEnabled, ctr("quark.gui.button.filter", "Filter"), [] {
                gSearchEnabled = !gSearchEnabled;
                gSearchFocused = gSearchEnabled.load();
                log().info("inventory: search bar {}", gSearchEnabled ? "shown" : "hidden");
            });
            if (gSearchEnabled) {
                field = Rect{containerFirst->rect.x + 10 * sc, containerFirst->rect.y - 12 * sc, 117 * sc, 10 * sc};
                Rect bg{field.x - 11 * sc, field.y - 3 * sc, 126 * sc, 13 * sc};
                dc.fill(bg, Color{0.12f, 0.12f, 0.12f, 1});
                dc.outline(bg, gSearchFocused ? colors::white : colors::gray);
                drawIcon(dc, Rect{bg.x + 1.5f * sc, bg.y + 1.5f * sc, 10 * sc, 10 * sc}, Icon::Filter, colors::gray);
                int matched = 0;
                // Match results per slot, recomputed only when the search or the slot's item changes.
                struct Cached {
                    ItemStack item;
                    bool      match = false;
                };
                static std::map<std::pair<std::string, int>, Cached> cache;
                static std::string                                   cachedFor;
                if (cachedFor != gSearch) {
                    cache.clear();
                    cachedFor = gSearch;
                }
                auto matches = [&](Slot const& slot) {
                    auto  item = slotItem(slot.collection, slot.index);
                    auto& e    = cache[{slot.collection, slot.index}];
                    bool  same = game::isEmpty(item) ? game::isEmpty(e.item)
                                                     : !game::isEmpty(e.item) && inv::sameItem(item, e.item) && item.mCount == e.item.mCount;
                    if (!same) {
                        e.item  = item;
                        e.match = inv::namesMatch(item, gSearch);
                    }
                    return e.match;
                };
                if (!gSearch.empty()) {
                    auto col = c.overlayColor;
                    Color shade{static_cast<float>(col.r), static_cast<float>(col.g), static_cast<float>(col.b), static_cast<float>(col.a)};
                    for (auto const& slot : s.slots) {
                        if (slot.collection == "cursor_items" || slot.collection.find("armor") != std::string::npos
                            || slot.collection.find("offhand") != std::string::npos)
                            continue;
                        if (matches(slot)) ++matched;
                        else o.greyOut(slot.rect, shade);
                    }
                }
                Color textColor = (!gSearch.empty() && matched == 0) ? Color::rgb(0xff5555) : colors::white;
                std::string shown = gSearch + (gSearchFocused ? "_" : "");
                if (shown.empty()) dc.text(ctr("lp.inventory.search", "Search..."), field.x + 1, field.y + 1, colors::gray);
                else dc.text(shown, field.x + 1, field.y + 1, textColor);
            }
        }
    }

    // The hovered slot (for the input handlers) and the item interaction hint next to the mouse
    // (Quark tooltips) while the cursor holds an item.
    Frame frame{s.name, s.slots, containerScreen, field, game::isEmpty(cursorItem()), {}};
    if (auto slot = slotAt(frame, mouse)) {
        auto item              = slotItem(slot->collection, slot->index);
        frame.hovered.slot     = slot;
        frame.hovered.filled   = !game::isEmpty(item);
        frame.hovered.action   = predictInteraction(*slot, inventoryScreen);
        if (game::itemId(item).find("bundle") != std::string::npos) {
            frame.hovered.bundleId = -1;
            if (auto name = StorageItemUtility::getStorageItemID(item); name && name->mDynamicId->has_value())
                frame.hovered.bundleId = static_cast<int>(**name->mDynamicId);
        }
        if (!frame.hovered.action.empty() && !frame.cursorEmpty) tooltipBox(dc, mouse, {hintText(frame.hovered.action)});
    }

    if (!gReported) {
        gReported = true;
        log().info("inventory: overlay on {} ({} slots, container={})", s.name, s.slots.size(), containerScreen);
    }
    // One line per screen with the button centres (fractions of the screen) for e2e scenarios.
    auto&              reportedFor = gReportedFor;
    auto               fx = [&](Rect const& r) { return (r.x + r.w / 2) / s.size.x; };
    auto               fy = [&](Rect const& r) { return (r.y + r.h / 2) / s.size.y; };
    if (inventoryScreen && playerTopRight && reportedFor != s.name) {
        reportedFor = s.name;
        auto r      = at(playerTopRight, 6, -13);
        std::string shulker;
        for (auto const& slot : s.slots)
            if (isPlayerCollection(slot.collection) && inv::isShulkerBox(slotItem(slot.collection, slot.index))) {
                shulker = std::format(" shulker at {:.4f},{:.4f}", fx(slot.rect), fy(slot.rect));
                break;
            }
        log().info("inventory: player buttons on {}: sort at {:.4f},{:.4f}{}", s.name, fx(r), fy(r), shulker);
    }
    if (containerScreen && reportedFor != s.name) {
        reportedFor = s.name;
        auto r      = at(containerTopRight, 6, -13);
        auto fb     = at(containerTopRight, -6, -13);
        // Player part, right to left: sort inventory (when enabled), insert, extract.
        float x  = (c.enableInventorySorting && c.enablePlayerInventoryInChests) ? -6.f : 6.f;
        auto  ib = at(playerTopRight, x, -13);
        auto  eb = at(playerTopRight, x - 12, -13);
        log().info("inventory: container buttons on {}: sort at {:.4f},{:.4f} filter at {:.4f},{:.4f} insert at "
                   "{:.4f},{:.4f} extract at {:.4f},{:.4f}",
                   s.name, fx(r), fy(r), fx(fb), fy(fb), fx(ib), fy(ib), fx(eb), fy(eb));
    }

    std::lock_guard lock(gMutex);
    gFrame = std::move(frame);
}

// ---- input -----------------------------------------------------------------------------------------------

/// US layout characters for the search field (Quark's field takes any character).
char charFor(int key, bool shift) {
    if (key >= 'A' && key <= 'Z') return static_cast<char>(shift ? key : key - 'A' + 'a');
    if (key >= '0' && key <= '9') {
        static char const shifted[] = ")!@#$%^&*(";
        return shift ? shifted[key - '0'] : static_cast<char>(key);
    }
    switch (key) {
    case 0x20: return ' ';
    case 0xBD: return shift ? '_' : '-';
    case 0xBB: return shift ? '+' : '=';
    case 0xBE: return shift ? '>' : '.';
    case 0xBC: return shift ? '<' : ',';
    case 0xBF: return shift ? '?' : '/';
    case 0xBA: return shift ? ':' : ';';
    case 0xDE: return shift ? '"' : '\'';
    case 0xDB: return shift ? '{' : '[';
    case 0xDD: return shift ? '}' : ']';
    case 0xDC: return shift ? '|' : '\\';
    case 0xC0: return shift ? '~' : '`';
    default: return 0;
    }
}

void onKey(ll::event::KeyInputEvent& ev) {
    int  key  = ev.keyCode();
    bool down = ev.isDown();
    std::unique_lock lock(gMutex);
    if (key == Key::Shift) gShift = down;
    bool shift = gShift;
    Frame f    = gFrame;
    lock.unlock();

    std::string top = topScreen();
    // Hotbar changer: 1..3 pick a row while it is open (in game).
    if (top == "hud_screen") {
        if (down && gHotbarOpen && key >= '1' && key <= '3') {
            int bar = key - '0';
            post([bar] {
                net::sendToServer(inv::ChangeHotbarRequest{bar});
                log().info("inventory: hotbar row {} requested", bar);
            });
            gHotbarOpen = false;
            ev.cancel();
        }
        return;
    }
    if (f.screen != top) return;
    // Search field typing.
    if (gSearchFocused && gSearchEnabled && f.container) {
        if (key == Key::Escape) {
            if (down) gSearchFocused = false;
            return; // Escape still closes the screen (Quark)
        }
        if (key == Key::Shift) return;
        if (down) {
            if (key == 0x08) post([] {
                if (!gSearch.empty()) gSearch.pop_back();
            });
            else if (key == Key::Enter) gSearchFocused = false;
            else if (char ch = charFor(key, shift); ch && gSearch.size() < 50) post([ch] {
                gSearch.push_back(ch);
                log().info("inventory: search '{}'", gSearch);
            });
        }
        ev.cancel();
    }
}

void onMouse(ll::event::MouseInputEvent& ev) {
    float inv = invGuiScale();
    Vec   pos{ev.x() * inv, ev.y() * inv};
    int   action = ev.actionButtonId();
    bool  down   = ev.buttonData() != 0;
    std::unique_lock lock(gMutex);
    gMouse  = pos;
    Frame f = gFrame;
    lock.unlock();
    if (action < 1 || action > 4 || ev.isCancelled() || f.screen.empty() || f.screen != topScreen()) return;
    auto const& c = cfg();

    // The search field: focus on click, right click clears it.
    if (f.field.w > 0 && (action == 1 || action == 2) && down) {
        bool inside = f.field.contains(pos.x, pos.y);
        if (inside && action == 2) post([] { gSearch.clear(); });
        gSearchFocused = inside;
        if (inside) {
            ev.cancel();
            return;
        }
    }

    // Work only on the slot the last frame saw under the mouse (items are read on the render thread).
    auto slot = slotAt(f, pos);
    if (!slot || !f.hovered.slot || f.hovered.slot->collection != slot->collection || f.hovered.slot->index != slot->index)
        return;
    auto const& hovered = f.hovered;

    // Bundles: the mouse wheel rotates their contents.
    if (action == 4) {
        if (!c.enableExpandedItemInteractions || !c.allowRotatingBundles || hovered.bundleId == -2) return;
        int    id    = hovered.bundleId;
        double delta = static_cast<schar>(ev.buttonData());
        post([s = *slot, delta, id] { net::sendToServer(inv::ScrollBundleRequest{s.collection, s.index, delta, id}); });
        ev.cancel();
        return;
    }

    int interactButton = c.invertClicks ? 1 : 2;
    if (action == interactButton) {
        auto what = hovered.action;
        if (!what.empty()) {
            if (down) post([s = *slot, what] {
                net::sendToServer(inv::SlotInteractRequest{s.collection, s.index});
                log().info("inventory: interaction '{}' requested", what);
            });
            ev.cancel();
            return;
        }
    }
    // Shift lock: plain clicks act as shift-clicks (quick move on the server).
    if (action == 1 && gShiftLocked && f.container && c.enableShiftLock && f.cursorEmpty) {
        if (down && hovered.filled)
            post([s = *slot] { net::sendToServer(inv::QuickMoveRequest{s.collection, s.index}); });
        ev.cancel();
    }
}

// ---- keybinds ------------------------------------------------------------------------------------------

bool inInventoryScreen() {
    std::lock_guard lock(gMutex);
    return !gFrame.screen.empty() && gFrame.screen == topScreen() && (gFrame.screen == "inventory_screen" || gFrame.container);
}
bool inContainerScreen() {
    std::lock_guard lock(gMutex);
    return gFrame.container && gFrame.screen == topScreen();
}

void shareHovered() {
    auto const& c = cfg();
    if (!c.enableItemSharing || gSearchFocused) return;
    Frame f;
    Vec   mouse;
    bool  shift;
    {
        std::lock_guard lock(gMutex);
        f     = gFrame;
        mouse = gMouse;
        shift = gShift;
    }
    if (!shift || f.screen.empty() || f.screen != topScreen()) return;
    auto slot = slotAt(f, mouse);
    if (!slot || !f.hovered.slot || f.hovered.slot->collection != slot->collection || f.hovered.slot->index != slot->index
        || !f.hovered.filled)
        return;
    auto now = Clock::now();
    if (now - gLastShare < std::chrono::milliseconds(50LL * std::max(0, c.cooldown))) return;
    gLastShare = now;
    post([s = *slot] {
        net::sendToServer(inv::ShareItemRequest{s.collection, s.index});
        log().info("inventory: share requested");
    });
}

// ---- HUD: hotbar changer + shared item card --------------------------------------------------------------

void hud(DrawContext& dc) {
    runQueue();
    auto now = Clock::now();
    float dt = gLastHud.time_since_epoch().count() ? std::chrono::duration<float>(now - gLastHud).count() : 0.f;
    gLastHud = now;
    auto const& c = cfg();
    if (gHotbarOpen && spectator()) gHotbarOpen = false;
    float step = static_cast<float>(dt * 20.0 / std::max(0.1, c.animationTime));
    gHotbarHeight = std::clamp(gHotbarHeight + (gHotbarOpen ? step : -step), 0.f, 1.f);

    auto size = dc.screenSize();
    if (gHotbarHeight > 0 && c.enableHotbarChanger) {
        auto* p = client::currentPlayer();
        if (p) {
            float real   = gHotbarHeight * 90.f;
            float xStart = size.x / 2 - 91;
            float yStart = size.y - real;
            for (int row = 0; row < 3; ++row) {
                Rect bar{xStart, yStart + row * 21.f, 182, 22};
                dc.fill(bar, Color{0, 0, 0, 0.55f});
                dc.outline(bar, Color{0.2f, 0.2f, 0.2f, 0.75f});
                for (int i = 0; i < 9; ++i) dc.outline({bar.x + 1 + i * 20.f, bar.y + 1, 20, 20}, Color{0.55f, 0.55f, 0.55f, 0.75f});
                auto label = "§l" + std::to_string(row + 1);
                dc.text(label, xStart - dc.textWidth(label) - 2, bar.y + 7);
            }
            auto& pinv = p->getInventory();
            for (int i = 0; i < 27; ++i) {
                auto const& stack = pinv.getItem(i + 9);
                if (!game::isEmpty(stack)) dc.item(stack, xStart + (i % 9) * 20.f + 3, yStart + (i / 9) * 21.f + 3);
            }
        }
    }

    if (gCard && now < gCard->until && c.renderItemsInChat) {
        float w = std::max(dc.textWidth(gCard->text) + 30.f, 120.f);
        Rect  box{4, size.y - 110, w, 24};
        dc.fill(box, colors::panel);
        dc.outline(box, Color::rgb(0xdeb483));
        dc.item(gCard->item, box.x + 4, box.y + 4);
        dc.text(gCard->text, box.x + 24, box.y + 8);
    } else if (gCard && now >= gCard->until) {
        gCard.reset();
    }
}

} // namespace

LP_REGISTER_SCREEN_OVERLAY("*", overlay);
LP_REGISTER_HUD(hud);

// Keybinds may fire on the window thread (raw key events in screens): requests go through the
// render-thread queue.
LP_REGISTER_KEYBIND("sort_player", Key::Unbound, [] {
    if (inInventoryScreen() && cfg().enablePlayerInventory) post([] { sortPlayer(); });
}, {}, true);
LP_REGISTER_KEYBIND("sort_container", Key::Unbound, [] {
    if (inContainerScreen()) post([] { sortContainer(); });
}, {}, true);
LP_REGISTER_KEYBIND("transfer_insert", Key::Unbound, [] {
    if (inContainerScreen()) post([] { transfer(false); });
}, {}, true);
LP_REGISTER_KEYBIND("transfer_extract", Key::Unbound, [] {
    if (inContainerScreen()) post([] { transfer(true); });
}, {}, true);
LP_REGISTER_KEYBIND("shift_lock", Key::Unbound, [] {
    if (inContainerScreen() && cfg().enableShiftLock) gShiftLocked = !gShiftLocked;
}, {}, true);
LP_REGISTER_KEYBIND("change_hotbar", Key::Z, [] {
    if (!cfg().enableHotbarChanger || topScreen() != "hud_screen") return; // spectators: closed by the HUD
    gHotbarOpen = !gHotbarOpen;
    log().info("inventory: hotbar changer {}", gHotbarOpen ? "opened" : "closed");
});
LP_REGISTER_KEYBIND("share_item", 'T', [] { shareHovered(); }, {}, true);

LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
    // Quark adds "Right-Click to Equip / Open" to the hovered slot's tooltip when the cursor is empty.
    Frame f;
    Vec   mouse;
    {
        std::lock_guard lock(gMutex);
        f     = gFrame;
        mouse = gMouse;
    }
    if (f.screen.empty() || !game::isEmpty(cursorItem())) return;
    auto slot = slotAt(f, mouse);
    if (!slot) return;
    auto at = slotItem(slot->collection, slot->index);
    if (game::isEmpty(at) || !inv::sameItem(at, item)) return;
    auto action = predictInteraction(*slot, f.screen == "inventory_screen");
    if (action == "equip" || action == "open") lines.push_back(hintText(action));
});

class ClientInventoryModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::KeyInputEvent>(onKey, ll::event::EventPriority::High));
        mListeners.push_back(bus.emplaceListener<ll::event::MouseInputEvent>(onMouse, ll::event::EventPriority::Low));
        mHandler = net::onClient<inv::SharedItemNotice>([](inv::SharedItemNotice const& m) {
            auto item = game::itemFromSnbt(m.item);
            if (game::isEmpty(item)) return;
            gCard = Card{item, inv::displayName(item), Clock::now() + std::chrono::seconds(10)};
            log().info("inventory: shared item received from {}", m.sender);
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        net::removeHandler(mHandler);
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    net::HandlerId                      mHandler = 0;
};

} // namespace lp::client::inventory

LP_REGISTER_MODULE(lp::client::inventory::ClientInventoryModule);

#endif // LP_CLIENT
