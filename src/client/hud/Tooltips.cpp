// U16 hud, ImprovedTooltips (client build).
//
// Quark adds "tooltip components" (images) to item tooltips. Bedrock tooltips are text, built by
// `ClientInstanceModel::getFormattedHoverText` and drawn by `HoverTextRenderer`. Lepton inserts
// blank lines that reserve the components' space (each component's first line carries an
// invisible marker of format codes). A hook on the renderer records where the box and those lines
// are; the components are painted there after the screen (the game draws the batched box later,
// so anything drawn inside the renderer would end up below it).

#ifdef LP_CLIENT

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HoverTextRenderer.h"
#include "mc/client/gui/screens/models/ClientInstanceModel.h"
#include "mc/client/multiplayer/ClientLevel.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/locale/I18n.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/MapItem.h"
#include "mc/world/level/saveddata/maps/MapItemSavedData.h"

#include "client/Client.h"
#include "client/hud/Painter.h"
#include "client/ui/Ui.h"
#include "feature/hud/HudConfig.h"
#include "feature/hud/Logic.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace lp::client::hud {

using namespace lp::feature::hud;

namespace {

// ---- shared state --------------------------------------------------------------------------------

std::atomic<bool>  gShift{false};
std::atomic<float> gLineHeight{10.f}; ///< measured from the rendered tooltip

enum class Kind { Attributes, Food, Shulker, Map, Enchant, Fuel };

struct AttrEntry {
    Attribute             attr;
    std::optional<double> diff; ///< vs the equipped item (showUpgradeStatus)
};

struct Component {
    Kind        kind = Kind::Food;
    std::string marker; ///< format-code prefix of the first reserved line (unique in the tooltip)
    int         lines  = 1;
    float       height = 10;
    // payload
    std::vector<std::pair<SlotGroup, std::vector<AttrEntry>>> groups;
    FoodRow                                                   food;
    bool                                                      poison = false;
    std::vector<ItemStack>                                    items;
    std::optional<int>                                        tint;
    ActorUniqueID                                             mapId;
    std::string                                               fuel;
};

struct Layout {
    std::vector<Component> comps;
};

/// Final hover text -> layout. The game may build hover texts off the render thread: guarded.
std::map<std::string, std::shared_ptr<Layout const>> gLayouts;
std::mutex                                           gLayoutsMutex;

std::shared_ptr<Layout const> findLayout(std::string const& text) {
    std::lock_guard lock(gLayoutsMutex);
    auto            it = gLayouts.find(text);
    return it == gLayouts.end() ? nullptr : it->second;
}

Font* font() {
    auto* c = client::currentClient();
    if (!c) return nullptr;
    try {
        return &c->getFontHandle().getFont();
    } catch (...) {
        return nullptr;
    }
}

float width(std::string_view s) {
    auto* f = font();
    return f ? static_cast<float>(f->getLineLength(s, 1.f, false)) : static_cast<float>(s.size()) * 6.f;
}

std::string stripCodes(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 2, "\xC2\xA7") == 0) { // '§' in UTF-8
            i += 2;                             // skip the code character too
            continue;
        }
        out += s[i];
    }
    return out;
}

std::vector<std::string> split(std::string const& s) {
    std::vector<std::string> out;
    size_t                   start = 0;
    while (true) {
        auto p = s.find('\n', start);
        out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

std::string join(std::vector<std::string> const& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

std::string gameText(std::string const& key) {
    try {
        return getI18n().get(key, nullptr);
    } catch (...) {
        return key;
    }
}

constexpr char kHex[] = "0123456789abcdef";

/// Invisible format codes that identify the stack the tooltip is built for (a hash of the item and
/// its data), so two items with the same text (two shulker boxes, two maps) never share a layout.
thread_local std::string gStackCodes;

std::string stackCodes(ItemStackBase const& stack) {
    // Only what tells apart items whose tooltips read the same: the map a map shows, the contents of
    // a shulker box (cheap, and no ItemStackBase::save, which runs item fix-ups).
    auto key = game::itemId(stack) + ":" + std::to_string(stack.getAuxValue());
    if (key.starts_with("minecraft:filled_map"))
        key += ":" + std::to_string(MapItem::getMapId(stack.mUserData.get()).rawID);
    if (isShulkerBox(stack))
        for (auto const& is : shulkerContents(stack)) key += ":" + game::itemId(is) + "x" + std::to_string(is.mCount);
    auto        h = static_cast<uint32_t>(std::hash<std::string>{}(key));
    std::string out;
    for (int i = 7; i >= 0; --i) out += std::string("\xC2\xA7") + kHex[(h >> (i * 4)) & 15];
    return out;
}

/// Reserved lines for a component of `w` x `h` UI units; the first one gets a marker that is unique
/// in the tooltip (component id) and names the stack.
std::vector<std::string> reserve(Component& c, int id, float w, float h) {
    c.height           = h;
    c.lines            = std::max(1, static_cast<int>(std::ceil(h / gLineHeight.load())));
    int         spaces = std::max(1, static_cast<int>(std::ceil(w / std::max(1.f, width(" ")))));
    std::string pad(static_cast<size_t>(spaces), ' ');
    c.marker = std::string("\xC2\xA7r\xC2\xA7") + kHex[(id >> 4) & 15] + "\xC2\xA7" + kHex[id & 15] + gStackCodes
             + "\xC2\xA7r";
    std::vector<std::string> out{c.marker + pad};
    for (int i = 1; i < c.lines; ++i) out.push_back("\xC2\xA7r" + pad);
    return out;
}

// ---- line builders -------------------------------------------------------------------------------

bool isAttributeLine(std::string const& line) {
    // Bedrock's modifier lines: "+7 Attack Damage" (blue), with the localized attribute names.
    static std::vector<std::string> names;
    static std::string              namesLocale;
    static std::mutex               namesMutex;
    std::lock_guard                 lock(namesMutex);
    if (namesLocale != client::clientLocale()) {
        namesLocale = client::clientLocale();
        names.clear();
        for (auto key :
             {"attribute.name.generic.attackDamage",
              "attribute.name.generic.armor",
              "attribute.name.generic.armorToughness",
              "attribute.name.generic.knockbackResistance"}) {
            auto t = gameText(key);
            if (!t.empty() && t != key) names.push_back(t);
        }
    }
    auto plain = stripCodes(line);
    if (plain.empty() || (plain[0] != '+' && plain[0] != '-' && !std::isdigit(static_cast<unsigned char>(plain[0]))))
        return false;
    return std::any_of(names.begin(), names.end(), [&](std::string const& n) {
        return plain.find(n) != std::string::npos;
    });
}

std::optional<Component> attributeComponent(ItemStackBase const& stack, Player const& player) {
    auto attrs = attributesOf(stack);
    if (attrs.empty()) return std::nullopt;
    Component c;
    c.kind = Kind::Attributes;
    // The item worn / held in the same slot, when it is a different item with attributes.
    std::vector<Attribute> compared;
    if (cfg().showUpgradeStatus) {
        auto const& eq = tickerSource(player, equipmentSlotFor(stack));
        if (!game::isEmpty(eq) && !(eq == stack)) compared = attributesOf(eq);
    }
    for (auto const& a : attrs) {
        auto it = std::find_if(c.groups.begin(), c.groups.end(), [&](auto const& g) { return g.first == a.group; });
        if (it == c.groups.end()) {
            c.groups.push_back({a.group, {}});
            it = std::prev(c.groups.end());
        }
        AttrEntry e{a, std::nullopt};
        for (auto const& o : compared)
            if (o.id == a.id && o.op == a.op && o.group == a.group) e.diff = a.amount - o.amount;
        it->second.push_back(std::move(e));
    }
    return c;
}

float attributeWidth(Component const& c) {
    float w = 0;
    for (auto const& [g, entries] : c.groups) {
        float gw = c.groups.size() > 1 ? 16.f : 0.f;
        for (auto const& e : entries) gw += width(formatAttribute(e.attr).first) + 20;
        w = std::max(w, gw);
    }
    return w;
}

/// enchantItems() per enchantment type, until the config is reloaded.
std::map<int, std::vector<std::string>> gEnchantCache;
std::mutex                              gEnchantMutex;

std::vector<std::string> cachedEnchantItems(int type) {
    std::lock_guard lock(gEnchantMutex);
    auto            it = gEnchantCache.find(type);
    if (it == gEnchantCache.end()) it = gEnchantCache.emplace(type, enchantItems(type)).first;
    return it->second;
}

bool inFurnaceScreen() {
    auto name = ui::topScreen();
    return name.find("furnace") != std::string::npos || name.find("smoker") != std::string::npos;
}

std::string buildTooltip(ItemStackBase const& stack, std::string text, std::shared_ptr<Layout>& layout) {
    auto const& conf   = cfg();
    auto*       player = client::currentPlayer();
    if (!conf.enableImprovedTooltips || game::isEmpty(stack) || !player) return text;
    auto lines = split(text);
    if (lines.empty()) return text;
    bool shift  = gShift.load();
    layout      = std::make_shared<Layout>();
    gStackCodes = stackCodes(stack);
    int id      = 1; // component ids: unique within this tooltip, stable across rebuilds

    std::vector<std::string> insert; // the block inserted at line 1 (top-down order)
    // Quark's order (each inserts at 1): attributes, food + saturation, shulker, map => from the top:
    // map, shulker, food, saturation, attributes.
    if (conf.mapTooltips && game::itemId(stack) == "minecraft:filled_map") {
        if (!conf.mapRequireShift || shift) {
            Component c;
            c.kind  = Kind::Map;
            c.mapId = MapItem::getMapId(stack.mUserData.get());
            auto r  = reserve(c, id++, 72, 75);
            insert.insert(insert.end(), r.begin(), r.end());
            layout->comps.push_back(std::move(c));
        } else {
            insert.push_back(
                "\xC2\xA7"
                "7"
                + ctr("quark.misc.map_shift", "Hold Shift to see the map")
            );
        }
    }
    if (conf.shulkerTooltips && isShulkerBox(stack)) {
        auto items = shulkerContents(stack);
        if (!items.empty()) {
            // Drop the vanilla contents lines (every line after the name that names an item inside, and
            // the "and N more..." line).
            // A content line is "<name>" or "<name> x<count>"; the "more" line directly follows them.
            auto isContentLine = [&](std::string const& plain) {
                for (auto const& is : items) {
                    auto name = is.getName();
                    if (name.empty() || plain.compare(0, name.size(), name) != 0) continue;
                    std::string_view rest = std::string_view(plain).substr(name.size());
                    if (rest.empty()) return true;
                    if (rest.size() > 2 && rest.substr(0, 2) == " x"
                        && std::all_of(rest.begin() + 2, rest.end(), [](char ch) {
                               return std::isdigit(static_cast<unsigned char>(ch));
                           }))
                        return true;
                }
                return false;
            };
            std::vector<std::string> kept{lines[0]};
            bool                     afterContent = false;
            for (size_t i = 1; i < lines.size(); ++i) {
                auto plain   = stripCodes(lines[i]);
                bool content = isContentLine(plain);
                if (!content && afterContent && plain.find("...") != std::string::npos) content = true;
                afterContent = content;
                if (!content) kept.push_back(lines[i]);
            }
            lines = std::move(kept);
            if (!conf.shulkerBoxRequireShift || shift) {
                Component c;
                c.kind            = Kind::Shulker;
                auto [cols, rows] = shulkerDims(static_cast<int>(items.size()));
                if (conf.shulkerBoxUseColors) c.tint = shulkerColor(stack);
                c.items = std::move(items);
                auto r  = reserve(c, id++, 9.f + cols * 18.f, 11.f + rows * 18.f);
                insert.insert(insert.end(), r.begin(), r.end());
                layout->comps.push_back(std::move(c));
            } else {
                insert.push_back(
                    "\xC2\xA7"
                    "7"
                    + ctr("quark.misc.shulker_box_shift", "Hold Shift to see contents")
                );
            }
        }
    }
    if (conf.foodTooltips || conf.showSaturation) {
        if (auto food = foodInfo(stack); food && food->nutrition > 0) {
            if (conf.foodTooltips) {
                Component c;
                c.kind   = Kind::Food;
                c.food   = foodRow(food->nutrition, conf.foodDivisor, conf.foodCompressionThreshold);
                c.poison = food->poison;
                auto r   = reserve(c, id++, static_cast<float>(c.food.width), 10);
                insert.insert(insert.end(), r.begin(), r.end());
                layout->comps.push_back(std::move(c));
            }
            if (conf.showSaturation) {
                int  tier = saturationTier(food->saturation);
                auto key  = std::string(food->poison ? "quark.misc.bad_saturation" : "quark.misc.saturation")
                          + std::to_string(tier);
                static constexpr std::array<char const*, 6>
                    kGood{"Fulfilling Meal", "Hearty Meal", "Meal", "Light Meal", "Light Snack", "Full-Course Meal"};
                static constexpr std::array<char const*, 6> kBad{
                    "Worryingly Fulfilling",
                    "Disturbingly Hearty",
                    "Ill-Advised Meal",
                    "Questionable Rations",
                    "Disgusting Snack",
                    "Dangerously Tantalizing"
                };
                insert.push_back(
                    "\xC2\xA7"
                    "7"
                    + ctr(key, food->poison ? kBad[static_cast<size_t>(tier)] : kGood[static_cast<size_t>(tier)])
                );
            }
        }
    }
    if (conf.attributeTooltips && !shift) {
        // The vanilla modifier lines are replaced only when the component shows the values instead.
        if (auto c = attributeComponent(stack, *player)) {
            // Bedrock puts an empty line before the modifiers: it goes with them.
            std::vector<std::string> kept;
            for (size_t i = 0; i < lines.size(); ++i) {
                if (i > 0 && isAttributeLine(lines[i])) {
                    if (kept.size() > 1 && stripCodes(kept.back()).empty()) kept.pop_back();
                    continue;
                }
                kept.push_back(lines[i]);
            }
            while (kept.size() > 1 && stripCodes(kept.back()).empty()) kept.pop_back();
            lines  = std::move(kept);
            auto r = reserve(*c, id++, attributeWidth(*c), static_cast<float>(c->groups.size()) * 10.f);
            insert.insert(insert.end(), r.begin(), r.end());
            layout->comps.push_back(std::move(*c));
        }
    }
    lines.insert(lines.begin() + 1, insert.begin(), insert.end());

    if (conf.enchantingTooltips) {
        size_t from = 1;
        for (auto const& e : bookEnchants(stack)) {
            auto want = stripCodes(e.line);
            for (size_t i = from; i < lines.size(); ++i) {
                if (stripCodes(lines[i]).find(want) == std::string::npos) continue;
                auto const& ids = cachedEnchantItems(e.type);
                if (ids.empty()) break;
                Component c;
                c.kind = Kind::Enchant;
                for (auto const& item : ids) c.items.push_back(game::makeItem(item));
                int  rows = (static_cast<int>(ids.size()) + 9) / 10;
                auto r    = reserve(c, id++, 3.f + std::min<int>(10, static_cast<int>(ids.size())) * 9.f, rows * 10.f);
                lines.insert(lines.begin() + static_cast<long>(i) + 1, r.begin(), r.end());
                layout->comps.push_back(std::move(c));
                from = i + 1 + r.size();
                break;
            }
        }
    }
    if (conf.fuelTimeTooltips && inFurnaceScreen()) {
        if (int ticks = fuelTicks(stack); ticks > 0) {
            Component c;
            c.kind = Kind::Fuel;
            c.fuel = fuelText(ticks, conf.fuelTimeDivisor);
            auto r = reserve(c, id++, 18 + width(c.fuel), 18);
            lines.insert(lines.end(), r.begin(), r.end());
            layout->comps.push_back(std::move(c));
        }
    }
    if (layout->comps.empty()) layout.reset();
    return join(lines);
}

// ---- drawing ---------------------------------------------------------------------------------------

std::string slotIcon(SlotGroup g) {
    switch (g) {
    case SlotGroup::MainHand:
        return "mainhand";
    case SlotGroup::OffHand:
        return "offhand";
    case SlotGroup::Head:
        return "head";
    case SlotGroup::Chest:
        return "chest";
    case SlotGroup::Legs:
        return "legs";
    case SlotGroup::Feet:
        return "feet";
    case SlotGroup::Armor:
        return "armor";
    case SlotGroup::Body:
        return "body";
    default:
        return "any";
    }
}

Color rgb(int hex) { return Color::rgb(hex); }

void drawAttributes(Painter& p, Component const& c, float x0, float y) {
    long long ticks =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count()
        / 50;
    y -= 1;
    for (auto const& [group, entries] : c.groups) {
        float x = x0;
        if (c.groups.size() > 1) {
            p.image("textures/lepton/hud/slot/" + slotIcon(group), x, y, 9, 9);
            x += 16;
        }
        for (auto const& e : entries) {
            auto [text, negative] = formatAttribute(e.attr);
            Color col             = negative ? rgb(0xFF5555) : ui::colors::white;
            p.image("textures/lepton/hud/attribute/" + e.attr.id, x, y, 9, 9);
            if (e.diff && cfg().showUpgradeStatus) {
                col = *e.diff > 0 ? rgb(0x55FF55) : *e.diff < 0 ? rgb(0xFF5555) : ui::colors::white;
                if (*e.diff != 0) {
                    float yp = y - 2;
                    if (cfg().animateUpDownArrows && ticks % 20 < 10) yp += 1;
                    p.image(
                        *e.diff < 0 ? "textures/lepton/hud/downgrade" : "textures/lepton/hud/upgrade",
                        x - 2,
                        yp,
                        13,
                        13
                    );
                }
            }
            p.text(text, x + 12, y + 1, col);
            x += p.textWidth(text) + 20;
        }
        y += 10;
    }
}

void drawFood(Painter& p, Component const& c, float x0, float y) {
    y                  -= 1;
    std::string suffix  = c.poison ? "_hunger" : "";
    for (int i = 0; i < c.food.shanks; ++i) {
        float x = x0 + i * 9 - 1;
        p.image("textures/lepton/hud/food_empty" + suffix, x, y, 9, 9);
        bool half = c.food.halfLast && i == c.food.shanks - 1;
        p.image(std::string("textures/lepton/hud/food_") + (half ? "half" : "full") + suffix, x, y, 9, 9);
    }
    if (!c.food.text.empty()) p.text(c.food.text, x0 + 10, y + 1, rgb(0x666666));
}

void drawShulker(Painter& p, Component const& c, float x, float y, float screenW) {
    constexpr float kCorner = 5, kEdge = 18, kTex = 30;
    int             size  = static_cast<int>(c.items.size());
    auto [cols, rows]     = shulkerDims(size);
    y                    -= 1;
    float texW            = kCorner * 2 + kEdge * cols;
    if (x + texW > screenW) x -= x + texW - screenW;
    Color tint = c.tint ? rgb(*c.tint) : ui::colors::white;
    auto  part = [&](float dx, float dy, float w, float h, float u, float v) {
        p.imagePart(
            "textures/lepton/hud/shulker_widget",
            x + dx,
            y + dy,
            w,
            h,
            u / kTex,
            v / kTex,
            w / kTex,
            h / kTex,
            tint
        );
    };
    float far = kCorner + 1 + kEdge + 1; // 25: the right / bottom border in the texture
    part(0, 0, kCorner, kCorner, 0, 0);
    part(kCorner + kEdge * cols, 0, kCorner, kCorner, far, 0);
    part(0, kCorner + kEdge * rows, kCorner, kCorner, 0, far);
    part(kCorner + kEdge * cols, kCorner + kEdge * rows, kCorner, kCorner, far, far);
    for (int r = 0; r < rows; ++r) {
        part(0, kCorner + kEdge * r, kCorner, kEdge, 0, kCorner + 1);
        part(kCorner + kEdge * cols, kCorner + kEdge * r, kCorner, kEdge, far, kCorner + 1);
        for (int col = 0; col < cols; ++col) {
            if (r == 0) {
                part(kCorner + kEdge * col, 0, kEdge, kCorner, kCorner + 1, 0);
                part(kCorner + kEdge * col, kCorner + kEdge * rows, kEdge, kCorner, kCorner + 1, far);
            }
            part(kCorner + kEdge * col, kCorner + kEdge * r, kEdge, kEdge, kCorner + 1, kCorner + 1);
        }
    }
    for (int i = 0; i < size; ++i) {
        float       xp = x + 6 + (i % 9) * 18.f;
        float       yp = y + 6 + (i / 9) * 18.f;
        auto const& is = c.items[static_cast<size_t>(i)];
        p.item(is, xp, yp, 1, is.mCount, true);
    }
}

// The level's map data is only touched on the client tick: drawing works on a copy of the pixels.
std::mutex                                                gMapMutex;
std::optional<ActorUniqueID>                              gMapWanted; ///< the hovered map (drawing -> client tick)
std::map<int64, std::shared_ptr<std::vector<uint> const>> gMapPixels; ///< map id -> 128x128 ABGR pixels
std::atomic<bool>                                         gMapResetAsked{false};

/// Map ids are per world: forget cached pixels and request counts when the world changes.
void resetMaps() {
    std::lock_guard lock(gMapMutex);
    gMapPixels.clear();
    gMapWanted.reset();
    gMapResetAsked = true;
}

/// Client tick: copies the pixels of the hovered map, or asks the server for the data when the
/// client has none (vanilla MapInfoRequestPacket; remote servers send map data only for held maps),
/// at most once a second and 10 times per map (a server may not know it).
void updateWantedMap() {
    std::optional<ActorUniqueID> wanted;
    {
        std::lock_guard lock(gMapMutex);
        wanted.swap(gMapWanted);
    }
    auto*              client    = client::currentClient();
    auto*              level     = client ? client->getLevel() : nullptr;
    auto*              player    = client::currentPlayer();
    static void const* lastLevel = nullptr;
    if (level != lastLevel) {
        lastLevel = level;
        resetMaps();
        return;
    }
    if (!wanted || !level) return;
    // The level finds a map's data by the map item's data: look for the item in the inventory.
    MapItemSavedData* data = nullptr;
    if (player) {
        auto& inv = player->getInventory();
        for (int i = 0; i < inv.getContainerSize() && !data; ++i) {
            auto const& s = inv.getItem(i);
            if (s.mUserData && game::itemId(s) == "minecraft:filled_map"
                && MapItem::getMapId(s.mUserData.get()) == *wanted)
                data = level->getMapSavedData(s.mUserData.get());
        }
    }
    if (!data) data = level->getMapSavedData(*wanted);
    if (data && data->mPixels.get().size() >= 128 * 128) {
        auto            pixels = std::make_shared<std::vector<uint> const>(data->mPixels.get());
        std::lock_guard lock(gMapMutex);
        if (gMapPixels.size() > 32) gMapPixels.clear();
        gMapPixels[wanted->rawID] = std::move(pixels);
        return;
    }
    struct Asked {
        std::chrono::steady_clock::time_point at;
        int                                   tries = 0;
    };
    static std::map<int64, Asked> asked;
    if (gMapResetAsked.exchange(false)) asked.clear();
    auto  now = std::chrono::steady_clock::now();
    auto& a   = asked[wanted->rawID];
    if (a.tries >= 10 || (a.tries > 0 && now - a.at < std::chrono::seconds(1))) return;
    a.at = now;
    level->requestMapInfo(*wanted, false);
    if (a.tries++ == 0) log().info("hud: map {} requested from the server", wanted->rawID);
}

void drawMap(Painter& p, Component const& c, float x, float y) {
    p.image("textures/lepton/hud/map_background", x + 3 - 3.5f, y + 3 - 3.5f, 71, 71);
    std::shared_ptr<std::vector<uint> const> shared;
    {
        std::lock_guard lock(gMapMutex);
        gMapWanted = c.mapId; // refreshed every tick while shown
        if (auto it = gMapPixels.find(c.mapId.rawID); it != gMapPixels.end()) shared = it->second;
    }
    if (!shared || shared->size() < 128 * 128) return;
    auto const& pixels = *shared;
    float const s      = 0.5f;
    for (int row = 0; row < 128; ++row) {
        int col = 0;
        while (col < 128) {
            uint v   = pixels[static_cast<size_t>(row * 128 + col)];
            int  end = col + 1;
            while (end < 128 && pixels[static_cast<size_t>(row * 128 + end)] == v) ++end;
            float a = ((v >> 24) & 0xff) / 255.f;
            if (a > 0) {
                Color col4{(v & 0xff) / 255.f, ((v >> 8) & 0xff) / 255.f, ((v >> 16) & 0xff) / 255.f, a};
                p.fill(x + 3 + col * s, y + 3 + row * s, (end - col) * s, s, col4);
            }
            col = end;
        }
    }
}

void drawEnchant(Painter& p, Component const& c, float x, float y) {
    int drawn = 0;
    for (auto const& is : c.items) {
        p.item(is, x + (6 + (drawn % 10) * 18) * 0.5f, y + (drawn / 10) * 10.f, 0.5f, -1, false);
        ++drawn;
    }
}

void drawFuel(Painter& p, Component const& c, float x, float y) {
    p.image("textures/lepton/hud/fuel", x + 1, y + 1, 13, 13);
    p.text(c.fuel, x + 16, y + 5, rgb(0xFFB600));
}

int gRenderLogs = 0;
int gPaintLogs  = 0;

struct PendingDraw {
    std::shared_ptr<Layout const> layout;
    float                         textX = 0, textY = 0, screenW = 0;
    std::vector<int>              lines; ///< line of each component (-1: not found)
};
std::mutex                 gPendingMutex;
std::optional<PendingDraw> gPending;

LL_TYPE_INSTANCE_HOOK(
    HoverRenderHook,
    ll::memory::HookPriority::Normal,
    HoverTextRenderer,
    &HoverTextRenderer::$render,
    void,
    MinecraftUIRenderContext& rc,
    IClientInstance&          client,
    UIControl&                owner,
    int                       pass
) {
    origin(rc, client, owner, pass);
    try {
        auto const& content = mContent.get();
        if (content.empty()) return;
        auto        layout    = findLayout(content);
        auto const& formatted = mFormattedContent.get();
        if (!layout) layout = findLayout(formatted);
        auto lines  = split(formatted.empty() ? content : formatted);
        auto box    = mBoxDimensions.get();
        auto cursor = mCursorPosition.get();
        auto offset = mOffset.get();
        auto base   = screenPos(owner);
        if (gRenderLogs < 3 && layout) {
            ++gRenderLogs;
            log().info(
                "hud: tooltip drawn ({} lines, box {:.1f}x{:.1f}, cursor {:.1f},{:.1f}, offset {:.1f},{:.1f}, owner "
                "{:.1f},{:.1f} "
                "{:.1f}x{:.1f}, max width {}, formatted {} raw {} bytes)",
                lines.size(),
                box.x,
                box.y,
                cursor.x,
                cursor.y,
                offset.x,
                offset.y,
                base.x,
                base.y,
                owner.mSize.get().x,
                owner.mSize.get().y,
                mMaxWidth,
                formatted.size(),
                content.size()
            );
        }
        if (!lines.empty() && box.y > 0)
            gLineHeight = std::clamp((box.y - 8) / static_cast<float>(lines.size()), 8.f, 12.f);
        if (!layout) return;
        // The cursor position is in screen UI units (not relative to the owner control).
        float boxX = cursor.x + offset.x;
        float boxY = cursor.y + offset.y;
        float sw   = 0;
        if (auto* c = client::currentClient()) sw = c->getGuiData()->mScreenSizeData.get().clientUIScreenSize.get().x;
        // The game batches the tooltip box and draws it after this renderer returns, so whatever is
        // drawn here ends up below it: remember what to draw, and paint it after the screen.
        PendingDraw pending{layout, boxX + 4, boxY + 4, sw > 0 ? sw : 1e9f, {}};
        auto        rawLines = split(content);
        for (auto const& comp : layout->comps) {
            // The formatted text may rewrite format codes: fall back to the raw lines (the tooltip
            // has no maximum width, so its lines are not wrapped).
            auto find = [&](std::vector<std::string> const& ls) {
                size_t li = 0;
                while (li < ls.size() && ls[li].find(comp.marker) == std::string::npos) ++li;
                return li < ls.size() ? std::optional<size_t>(li) : std::nullopt;
            };
            auto at = find(lines);
            if (!at) at = find(rawLines);
            pending.lines.push_back(at ? static_cast<int>(*at) : -1);
        }
        std::lock_guard lock(gPendingMutex);
        gPending = std::move(pending);
    } catch (std::exception const& e) {
        log().error("hud: tooltip drawing failed: {}", e.what());
    }
}

/// Paints the tooltip components recorded by the hover renderer this frame, on top of the screen.
void paintPending(MinecraftUIRenderContext& rc) {
    std::optional<PendingDraw> pending;
    {
        std::lock_guard lock(gPendingMutex);
        pending.swap(gPending);
    }
    if (!pending) return;
    Painter     p(rc);
    auto const& comps = pending->layout->comps;
    for (size_t i = 0; i < comps.size() && i < pending->lines.size(); ++i) {
        auto const& comp = comps[i];
        int         li   = pending->lines[i];
        if (gPaintLogs < 8) {
            ++gPaintLogs;
            log().info("hud: tooltip component {} painted at line {}", static_cast<int>(comp.kind), li);
        }
        if (li < 0) continue;
        float x = pending->textX;
        float y = pending->textY + static_cast<float>(li) * gLineHeight.load();
        switch (comp.kind) {
        case Kind::Attributes:
            drawAttributes(p, comp, x, y);
            break;
        case Kind::Food:
            drawFood(p, comp, x, y);
            break;
        case Kind::Shulker:
            drawShulker(p, comp, x, y, pending->screenW);
            break;
        case Kind::Map:
            drawMap(p, comp, x, y);
            break;
        case Kind::Enchant:
            drawEnchant(p, comp, x, y);
            break;
        case Kind::Fuel:
            drawFuel(p, comp, x, y);
            break;
        }
    }
}

int gBuildLogs = 0;

LL_TYPE_INSTANCE_HOOK(
    HoverTextHook,
    ll::memory::HookPriority::Low,
    ClientInstanceModel,
    &ClientInstanceModel::$getFormattedHoverText,
    std::string,
    ItemStackBase const& item,
    bool const           showCategory
) {
    auto text = origin(item, showCategory);
    try {
        std::shared_ptr<Layout> layout;
        auto                    out = buildTooltip(item, text, layout);
        if (gBuildLogs < 12 && !game::isEmpty(item)) {
            ++gBuildLogs;
            log().debug("hud: hover text of {}: {}", game::itemId(item), text);
        }
        if (layout) {
            std::lock_guard lock(gLayoutsMutex);
            if (gLayouts.size() > 64) gLayouts.clear();
            // The game rebuilds the hover text often: log only tooltips not seen yet.
            bool fresh    = !gLayouts.contains(out);
            auto n        = layout->comps.size();
            gLayouts[out] = std::move(layout);
            if (fresh) log().debug("hud: tooltip components for {} ({})", game::itemId(item), n);
        }
        return out;
    } catch (std::exception const& e) {
        log().error("hud: tooltip building failed: {}", e.what());
        return text;
    }
}

} // namespace

/// The tooltip text (with Lepton's reserved lines) for tests.
std::string improvedTooltip(ItemStackBase const& item, std::string vanilla, int* components) {
    std::shared_ptr<Layout> layout;
    auto                    out = buildTooltip(item, std::move(vanilla), layout);
    if (components) *components = layout ? static_cast<int>(layout->comps.size()) : 0;
    return out;
}

class ImprovedTooltipsModule {
public:
    void install() {
        mKey = ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>(
            [](ll::event::KeyInputEvent& ev) {
                if (ev.keyCode() == ui::Key::Shift) gShift = ev.isDown();
            }
        );
        mPaint = ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
            [](ll::event::AfterUIRenderEvent& ev) {
                // Tooltips belong to the top screen: not the HUD layer below an open screen.
                if (isHudView(ev.screenView()) && ui::topScreen() != "hud_screen") return;
                try {
                    paintPending(ev.uiRenderContext());
                } catch (std::exception const& e) {
                    log().error("hud: tooltip painting failed: {}", e.what());
                }
            },
            ll::event::EventPriority::Low // after the UI toolkit's overlays
        );
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>(
            [](ll::event::ClientLevelTickEvent&) {
                try {
                    updateWantedMap();
                } catch (std::exception const& e) {
                    log().error("hud: map request failed: {}", e.what());
                }
            }
        );
        static bool reloadHooked = false;
        if (!reloadHooked) {
            reloadHooked = true;
            config::onReload([] {
                {
                    std::lock_guard lock(gEnchantMutex);
                    gEnchantCache.clear();
                }
                {
                    std::lock_guard lock(gLayoutsMutex);
                    gLayouts.clear();
                }
            });
        }
        HoverTextHook::hook();
        HoverRenderHook::hook();
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mKey);
        ll::event::EventBus::getInstance().removeListener(mPaint);
        ll::event::EventBus::getInstance().removeListener(mTick);
        HoverTextHook::unhook();
        HoverRenderHook::unhook();
        {
            std::lock_guard lock(gLayoutsMutex);
            gLayouts.clear();
        }
    }

private:
    ll::event::ListenerPtr mKey;
    ll::event::ListenerPtr mPaint;
    ll::event::ListenerPtr mTick;
};

} // namespace lp::client::hud

LP_REGISTER_MODULE(lp::client::hud::ImprovedTooltipsModule);

// The hotbar slot positions of each container screen, once per screen (fractions of the screen),
// for the client e2e scenario that hovers items.
LP_REGISTER_SCREEN_OVERLAY("*", [](lp::client::ui::Overlay&, lp::client::ui::ScreenInfo const& s) {
    static std::set<std::string> seen;
    if (seen.contains(s.name) || !s.settled || s.size.x <= 0 || s.size.y <= 0) return;
    std::map<int, lp::client::ui::Rect> hotbar;
    for (auto const& slot : s.slots)
        if (slot.collection.find("hotbar") != std::string::npos && slot.index < 9)
            hotbar.emplace(slot.index, slot.rect);
    if (hotbar.size() < 9) return;
    seen.insert(s.name);
    std::string out;
    for (auto const& [i, r] : hotbar)
        out += std::format(
            "{}{:.4f},{:.4f}",
            out.empty() ? "" : ";",
            (r.x + r.w / 2) / s.size.x,
            (r.y + r.h / 2) / s.size.y
        );
    lp::log().info("hud: {} hotbar slots at {}", s.name, out);
});

#endif // LP_CLIENT
