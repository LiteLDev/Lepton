// Client side of the enchanting unit (client build): the matrix enchanting screen (Quark's
// MatrixEnchantingScreen, drawn with the client UI toolkit), the anvil / smithing helper overlay
// and the tooltips (pending enchantments, ancient tomes, runes, the rune template).

#ifdef LP_CLIENT

#include <cctype>
#include <cmath>
#include <memory>

#include "nlohmann/json.hpp"

#include "mc/world/Container.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"

#include "client/Client.h"
#include "client/ui/Ui.h"
#include "feature/enchanting/Config.h"
#include "feature/enchanting/Enchants.h"
#include "feature/enchanting/Matrix.h"
#include "feature/enchanting/Messages.h"
#include "feature/enchanting/Table.h"
#include "feature/enchanting/Tomes.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::client::enchanting {

namespace fe = lp::feature::enchanting;
using namespace lp::client::ui;

namespace {

constexpr float kW = 176, kH = 166;

Color rgb(int c, float a = 1) { return Color::rgb(c & 0xffffff, a); }

std::string enchName(int type, int level) {
    auto const* d = fe::defOf(type);
    if (!d) return "?";
    auto n = ctr(fe::nameKey(*d), fe::englishName(*d));
    if (d->maxLevel != 1 || level != 1) n += " " + fe::roman(level);
    return n;
}

/// `§` formatting segments of a line: (colour, text). DrawContext::text draws the `§` symbols
/// literally, so the colours are applied per segment.
std::vector<std::pair<Color, std::string>> segments(std::string const& s) {
    static constexpr int kCodes[16] = {0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xffaa00, 0xaaaaaa,
                                       0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff};
    std::vector<std::pair<Color, std::string>> out;
    Color                                      cur = colors::white;
    std::string                                text;
    for (size_t i = 0; i < s.size(); ++i) {
        if (static_cast<unsigned char>(s[i]) == 0xc2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xa7) {
            char code = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 2])));
            int  idx  = code >= '0' && code <= '9' ? code - '0' : code >= 'a' && code <= 'f' ? code - 'a' + 10 : -1;
            if (idx >= 0 || code == 'r') {
                if (!text.empty()) out.emplace_back(cur, std::move(text));
                text.clear();
                cur = idx >= 0 ? Color::rgb(kCodes[idx]) : colors::white;
            }
            i += 2; // other codes (bold, ...) are dropped
            continue;
        }
        text += s[i];
    }
    if (!text.empty()) out.emplace_back(cur, std::move(text));
    return out;
}

float formattedWidth(DrawContext& dc, std::string const& s) {
    float w = 0;
    for (auto const& [c, t] : segments(s)) w += dc.textWidth(t);
    return w;
}

void formattedText(DrawContext& dc, std::string const& s, float x, float y) {
    for (auto const& [c, t] : segments(s)) {
        dc.text(t, x, y, c);
        x += dc.textWidth(t);
    }
}

/// Draws a tooltip box with lines at the mouse.
void tooltip(DrawContext& dc, Vec mouse, std::vector<std::string> const& lines) {
    if (lines.empty()) return;
    float w = 0;
    for (auto const& l : lines) w = std::max(w, formattedWidth(dc, l));
    float h  = static_cast<float>(lines.size()) * 10 + 4;
    auto  sz = dc.screenSize();
    float x  = std::min(mouse.x + 10, sz.x - w - 8);
    float y  = std::max(2.f, std::min(mouse.y - 12, sz.y - h - 2));
    dc.fill({x - 3, y - 3, w + 6, h + 2}, Color{0.06f, 0.0f, 0.12f, 0.94f});
    dc.outline({x - 3, y - 3, w + 6, h + 2}, Color{0.31f, 0.0f, 1.0f, 0.5f});
    float ly = y;
    for (auto const& l : lines) {
        formattedText(dc, l, x, ly);
        ly += 10;
    }
}

void slotBox(DrawContext& dc, float x, float y, float size = 18) {
    dc.fill({x, y, size, size}, rgb(0x8b8b8b));
    dc.fill({x, y, size, 1}, rgb(0x373737));
    dc.fill({x, y, 1, size}, rgb(0x373737));
    dc.fill({x, y + size - 1, size, 1}, rgb(0xffffff));
    dc.fill({x + size - 1, y, 1, size}, rgb(0xffffff));
}

// ---- the screen --------------------------------------------------------------------------------

class MatrixScreen : public CustomScreen {
public:
    void setState(nlohmann::json const& j) {
        mState  = j;
        mItem   = game::itemFromSnbt(j.value("item", "{}"));
        mLapis  = game::itemFromSnbt(j.value("lapis", "{}"));
        mOutput = game::itemFromSnbt(j.value("output", "{}"));
        if (j.contains("matrix") && j["matrix"].is_object()) mMatrix = fe::Matrix::fromJson(j["matrix"], j.value("book", false));
        else mMatrix.reset();
        if (!mMatrix || !mMatrix->piece(mSelected) ||
            std::find(mMatrix->benched.begin(), mMatrix->benched.end(), mSelected) == mMatrix->benched.end())
            mSelected = -1;
        if (!mLogged) {
            mLogged = true;
            log().info("enchanting: matrix screen state received");
        }
    }

    void render(DrawContext& dc, Vec mouse) override {
        auto sz = dc.screenSize();
        mLeft   = std::floor((sz.x - kW) / 2);
        mTop    = std::floor((sz.y - kH) / 2);
        float L = mLeft, T = mTop;
        dc.fill({0, 0, sz.x, sz.y}, Color{0, 0, 0, 0.45f});
        // Panel with the vanilla bevel.
        dc.fill({L, T, kW, kH}, rgb(0xc6c6c6));
        dc.fill({L, T, kW, 1}, rgb(0xffffff));
        dc.fill({L, T, 1, kH}, rgb(0xffffff));
        dc.fill({L, T + kH - 1, kW, 1}, rgb(0x555555));
        dc.fill({L + kW - 1, T, 1, kH}, rgb(0x555555));
        dc.outline({L - 1, T - 1, kW + 2, kH + 2}, rgb(0x000000));

        dc.text(ctr("lp.enchanting.title", "Enchant"), L + 12, T + 5, rgb(0x404040), 1, Align::Left, false);
        dc.text(ctr("lp.enchanting.inventory", "Inventory"), L + 8, T + kH - 96 + 2, rgb(0x404040), 1, Align::Left, false);

        mHoverLines.clear();
        mHoverPiece = nullptr;

        // Item, lapis, output slots.
        slotBox(dc, L + 14, T + 19);
        slotBox(dc, L + 14, T + 43);
        slotBox(dc, L + 54, T + 27, 26);
        // Arrow.
        dc.fill({L + 36, T + 35, 12, 4}, rgb(0x8b8b8b));
        for (int i = 0; i < 5; ++i) dc.fill({L + 48 + i, T + 32.f + i, 1, 10.f - 2 * i}, rgb(0x8b8b8b));
        // Charge bar.
        dc.fill({L + 7, T + 32, 4, 18}, rgb(0x373737));
        int charge = mState.value("charge", 0), perLapis = std::max(1, mState.value("chargePerLapis", 4));
        if (charge > 0) {
            float h = std::min(18.f, 18.f * static_cast<float>(charge) / static_cast<float>(perLapis));
            dc.fill({L + 7, T + 32 + 18 - h, 4, h}, rgb(0x3060e0));
        }
        if (!game::isEmpty(mItem)) dc.item(mItem, L + 15, T + 20);
        if (!game::isEmpty(mLapis)) dc.item(mLapis, L + 15, T + 44);
        if (!game::isEmpty(mOutput)) dc.item(mOutput, L + 59, T + 32);
        hoverItem(mouse, {L + 15, T + 20, 16, 16}, mItem);
        hoverItem(mouse, {L + 15, T + 44, 16, 16}, mLapis);
        hoverItem(mouse, {L + 55, T + 28, 24, 24}, mOutput);

        // Bookshelf power.
        int power = mState.value("power", 0);
        if (power > 0) {
            static ItemStack book = game::makeItem("minecraft:book");
            dc.item(book, L + 50, T + 55, 1, false);
            dc.text(std::to_string(power), L + 58, T + 61, rgb(0xc8ff8f), 1, Align::Center);
        }
        // Cost.
        bool creative = mState.value("creative", false);
        if (mMatrix && mState.value("canGenerate", false) && !creative) {
            int  price = mState.value("price", 1), minLevel = mState.value("minLevel", 0);
            bool has   = mState.value("validXp", false);
            auto text  = std::to_string(price);
            if (!has && mState.value("level", 0) < minLevel) text = ctr("quark.gui.enchanting.min", "{}+", minLevel);
            dc.fill({L + 71, T + 56, 10, 10}, rgb(0x80c050));
            dc.text(text, L + 76, T + 58, has ? rgb(0xc8ff8f) : rgb(0xff8f8f), 1, Align::Center);
        }

        renderGrid(dc, mouse);
        renderList(dc, mouse);

        // Plus button.
        Rect plus{L + 86, T + 63, 50, 12};
        bool active = mState.value("active", false);
        bool hover  = plus.contains(mouse.x, mouse.y);
        dc.fill(plus, !active ? rgb(0x4a4a4a) : hover ? rgb(0x7fb24a) : rgb(0x8a6d3b));
        dc.outline(plus, rgb(0x2b2b2b));
        dc.text("+", plus.x + plus.w / 2, plus.y + 2, active ? colors::white : colors::gray, 1, Align::Center);

        // Inventory.
        if (auto* p = client::currentPlayer()) {
            auto& inv = game::inv::playerInventory(*p);
            for (int i = 0; i < 36; ++i) {
                auto  r     = invSlot(i);
                slotBox(dc, r.x - 1, r.y - 1);
                auto const& s = inv.getItem(i);
                if (!game::isEmpty(s)) {
                    dc.item(s, r.x, r.y);
                    hoverItem(mouse, r, s);
                }
            }
        }

        // Once per opening, after a few frames: widget positions as fractions of the screen (for the
        // client e2e scenario).
        if (++mFrames == 10) {
            auto at = [&](char const* what, float x, float y) { log().info("enchanting: matrix {} at {:.4f},{:.4f}", what, x / sz.x, y / sz.y); };
            at("plus", L + 111, T + 69);
            at("bench", L + 153, T + 21);
            at("grid", L + 111, T + 36);
            at("output", L + 67, T + 40);
        }

        // Tooltips last.
        if (mHoverPiece) tooltip(dc, mouse, pieceTooltip(*mHoverPiece));
        else tooltip(dc, mouse, mHoverLines);
    }

    void onClick(Vec pos, int button) override {
        float L = mLeft, T = mTop;
        if (button == 0) {
            if (Rect{L + 15, T + 20, 16, 16}.contains(pos.x, pos.y)) return slot("take_item", -1);
            if (Rect{L + 15, T + 44, 16, 16}.contains(pos.x, pos.y)) return slot("take_lapis", -1);
            if (Rect{L + 55, T + 28, 24, 24}.contains(pos.x, pos.y)) return slot("take_output", -1);
            if (Rect{L + 86, T + 63, 50, 12}.contains(pos.x, pos.y)) {
                if (mState.value("active", false)) op(fe::OperAdd, 0, 0, 0);
                return;
            }
            if (Rect{L + 168, T + 11, 7, 8}.contains(pos.x, pos.y)) return void(mScroll = std::max(0, mScroll - 1));
            if (Rect{L + 168, T + 67, 7, 8}.contains(pos.x, pos.y)) return void(++mScroll);
            if (auto* p = client::currentPlayer()) {
                auto& inv = game::inv::playerInventory(*p);
                for (int i = 0; i < 36; ++i) {
                    if (!invSlot(i).contains(pos.x, pos.y)) continue;
                    auto const& s = inv.getItem(i);
                    if (game::isEmpty(s)) return;
                    return slot(game::itemId(s) == "minecraft:lapis_lazuli" ? "put_lapis" : "put_item", i);
                }
            }
            // Bench list.
            if (mMatrix) {
                for (size_t k = 0; k < mListRects.size(); ++k)
                    if (mListRects[k].first.contains(pos.x, pos.y)) return void(mSelected = mListRects[k].second);
            }
        }
        if (!mMatrix) return;
        int gx, gy;
        if (!gridCell(pos, gx, gy)) {
            if (button == 1 && mSelected != -1) op(fe::OperRotate, mSelected, 0, 0);
            return;
        }
        int hover = mMatrix->grid[gx][gy];
        if (button == 0) {
            if (mSelected != -1) {
                if (hover == -1) {
                    op(fe::OperPlace, mSelected, gx, gy);
                    mSelected = -1;
                } else {
                    auto const* hp = mMatrix->piece(hover);
                    auto const* sp = mMatrix->piece(mSelected);
                    auto const* d  = hp ? fe::defOf(hp->ench) : nullptr;
                    if (hp && sp && d && hp->ench == sp->ench && hp->level < d->maxLevel) {
                        op(fe::OperMerge, hover, mSelected, 0);
                        mSelected = -1;
                    }
                }
            } else if (hover != -1) {
                op(fe::OperRemove, hover, 0, 0);
                mSelected = hover;
            }
        } else if (button == 1 && mSelected != -1) {
            op(fe::OperRotate, mSelected, 0, 0);
        }
    }

    void onClose() override {
        net::sendToServer(fe::MatrixCloseMsg{});
        log().info("enchanting: matrix screen closed");
    }

private:
    nlohmann::json             mState;
    ItemStack                  mItem, mLapis, mOutput;
    std::optional<fe::Matrix>  mMatrix;
    int                        mSelected = -1, mScroll = 0;
    float                      mLeft = 0, mTop = 0;
    std::vector<std::string>   mHoverLines;
    fe::Piece const*           mHoverPiece = nullptr;
    bool                       mHoverInGrid = false;
    bool                       mLogged      = false;
    int                        mFrames      = 0;
    std::vector<std::pair<Rect, int>> mListRects;

    [[nodiscard]] Rect invSlot(int i) const {
        if (i < 9) return {mLeft + 8 + i * 18.f, mTop + 142, 16, 16};
        int r = (i - 9) / 9, c = (i - 9) % 9;
        return {mLeft + 8 + c * 18.f, mTop + 84 + r * 18.f, 16, 16};
    }

    void hoverItem(Vec mouse, Rect r, ItemStack const& s) {
        if (game::isEmpty(s) || !r.contains(mouse.x, mouse.y)) return;
        mHoverLines.clear();
        auto text = tooltipFor(s);
        size_t start = 0;
        while (start <= text.size()) {
            auto end = text.find('\n', start);
            mHoverLines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    bool gridCell(Vec pos, int& gx, int& gy) const {
        float x = pos.x - mLeft - 86, y = pos.y - mTop - 11;
        if (x < 0 || y < 0) return false;
        gx = static_cast<int>(x / 10);
        gy = static_cast<int>(y / 10);
        return gx >= 0 && gx < 5 && gy >= 0 && gy < 5;
    }

    void renderBlock(DrawContext& dc, float x, float y, float s, fe::Piece const& p, float alpha, bool hovered) {
        Color c   = rgb(p.color, alpha);
        Color dk  = rgb(((p.color >> 1) & 0x7f7f7f), alpha);
        dc.fill({x, y, s, s}, dk);
        dc.fill({x + s * 0.1f, y + s * 0.1f, s * 0.8f, s * 0.8f}, c);
        // Eight texture variants (Quark's piece types): a small mark per type.
        float u = s / 10;
        switch (p.type % 8) {
        case 1: dc.fill({x + 4 * u, y + 4 * u, 2 * u, 2 * u}, dk); break;
        case 2: dc.fill({x + 2 * u, y + 2 * u, 6 * u, u}, dk); break;
        case 3: dc.fill({x + 2 * u, y + 2 * u, u, 6 * u}, dk); break;
        case 4: dc.outline({x + 3 * u, y + 3 * u, 4 * u, 4 * u}, dk); break;
        case 5: dc.fill({x + 2 * u, y + 2 * u, 2 * u, 2 * u}, dk), dc.fill({x + 6 * u, y + 6 * u, 2 * u, 2 * u}, dk); break;
        case 6: dc.fill({x + 2 * u, y + 7 * u, 6 * u, u}, dk); break;
        case 7: dc.fill({x + 7 * u, y + 2 * u, u, 6 * u}, dk); break;
        default: break;
        }
        if (hovered) dc.fill({x, y, s, s}, Color{1, 1, 1, 0.4f});
    }

    void renderPiece(DrawContext& dc, float ox, float oy, float s, fe::Piece const& p, float alpha, bool hovered) {
        for (auto const& b : p.blocks) renderBlock(dc, ox + b[0] * s, oy + b[1] * s, s, p, alpha, hovered);
    }

    void renderGrid(DrawContext& dc, Vec mouse) {
        float gx0 = mLeft + 86, gy0 = mTop + 11;
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 5; ++y) dc.fill({gx0 + x * 10.f, gy0 + y * 10.f, 10, 10}, rgb((x + y) % 2 ? 0x8b8b8b : 0x9e9e9e));
        dc.outline({gx0 - 1, gy0 - 1, 52, 52}, rgb(0x373737));
        if (!mMatrix) return;
        int  hx = -1, hy = -1;
        bool inGrid = gridCell(mouse, hx, hy);
        int  hover  = inGrid ? mMatrix->grid[hx][hy] : -1;
        auto const* hoverPiece = hover >= 0 ? mMatrix->piece(hover) : nullptr;
        if (hoverPiece) {
            mHoverPiece  = hoverPiece;
            mHoverInGrid = true;
        }
        for (int id : mMatrix->placed)
            if (auto const* p = mMatrix->piece(id)) renderPiece(dc, gx0 + p->x * 10.f, gy0 + p->y * 10.f, 10, *p, 1, p == hoverPiece);
        if (mSelected != -1 && inGrid) {
            auto const* sp = mMatrix->piece(mSelected);
            auto const* hd = hoverPiece ? fe::defOf(hoverPiece->ench) : nullptr;
            bool mergeable = sp && hoverPiece && hd && hoverPiece->ench == sp->ench && hoverPiece->level < hd->maxLevel;
            if (sp && !mergeable) {
                float a = 0.2f;
                if (mMatrix->canPlace(*sp, hx, hy)) {
                    auto t = static_cast<double>(std::chrono::steady_clock::now().time_since_epoch().count()) / 1e9 * 20;
                    a      = static_cast<float>((std::sin(t * 0.2) + 1) * 0.4 + 0.4);
                }
                renderPiece(dc, gx0 + hx * 10.f, gy0 + hy * 10.f, 10, *sp, std::min(a, 1.f), false);
            }
        }
        if (!hoverPiece && inGrid) dc.fill({gx0 + hx * 10.f, gy0 + hy * 10.f, 10, 10}, Color{1, 1, 1, 0.4f});
    }

    void renderList(DrawContext& dc, Vec mouse) {
        float lx = mLeft + 139, ly = mTop + 11;
        dc.fill({lx, ly, 29, 64}, rgb(0x2b2b2b));
        mListRects.clear();
        if (!mMatrix) return;
        int n        = static_cast<int>(mMatrix->benched.size());
        int visible  = 3;
        mScroll      = std::clamp(mScroll, 0, std::max(0, n - visible));
        for (int k = 0; k < visible && mScroll + k < n; ++k) {
            int         id = mMatrix->benched[static_cast<size_t>(mScroll + k)];
            auto const* p  = mMatrix->piece(id);
            if (!p) continue;
            Rect r{lx, ly + k * 21.f, 28, 21};
            mListRects.emplace_back(r, id);
            bool hover = r.contains(mouse.x, mouse.y);
            if (id == mSelected) dc.outline({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, colors::white);
            renderPiece(dc, r.x + 12, r.y + 8, 5, *p, 1, false);
            if (hover) {
                mHoverPiece  = p;
                mHoverInGrid = false;
            }
        }
        if (n > visible) {
            dc.fill({mLeft + 168, mTop + 11, 7, 8}, rgb(0x818181));
            dc.text("^", mLeft + 171.5f, mTop + 11, colors::white, 1, Align::Center);
            dc.fill({mLeft + 168, mTop + 67, 7, 8}, rgb(0x818181));
            dc.text("v", mLeft + 171.5f, mTop + 66, colors::white, 1, Align::Center);
        }
    }

    std::vector<std::string> pieceTooltip(fe::Piece const& p) {
        std::vector<std::string> lines;
        lines.push_back("§6" + enchName(p.ench, p.level));
        double power = mState.value("influencePower", 0.125);
        if (p.influence > 0)
            lines.push_back("§7" + ctr("quark.gui.enchanting.influence", "Candle Influence: +{}%", static_cast<int>(p.influence * power * 100)));
        else if (p.influence < 0)
            lines.push_back("§7" + ctr("quark.gui.enchanting.dampen", "Candle Influence: {}%", static_cast<int>(p.influence * power * 100)));
        auto const* d = fe::defOf(p.ench);
        int         max = d ? fe::maxXpFor(*d, p.level) : 0;
        if (max > 0) lines.push_back("§7" + ctr("quark.gui.enchanting.upgrade", "Upgrade: {}/{}", p.xp, max));
        if (!mHoverInGrid) {
            lines.emplace_back("");
            lines.push_back("§7" + ctr("quark.gui.enchanting.left_click", "Left Click to Select"));
            lines.push_back("§7" + ctr("quark.gui.enchanting.right_click", "Right Click to Rotate"));
        } else if (mSelected != -1 && mMatrix) {
            auto const* sp = mMatrix->piece(mSelected);
            if (sp && d && sp->ench == p.ench && p.level < d->maxLevel) {
                lines.emplace_back("");
                lines.push_back("§7" + ctr("quark.gui.enchanting.merge", "Click to Upgrade"));
            }
        }
        return lines;
    }

    static void op(int o, int a0, int a1, int a2) { net::sendToServer(fe::MatrixOpMsg{o, a0, a1, a2}); }
    static void slot(std::string const& action, int s) { net::sendToServer(fe::MatrixSlotMsg{action, s}); }
};

std::shared_ptr<MatrixScreen> gScreen;
fe::CraftPreviewMsg           gPreview;

void onState(fe::MatrixStateMsg const& m) {
    if (!m.open) {
        if (gScreen && currentScreen() == gScreen) gScreen->close();
        gScreen.reset();
        return;
    }
    auto j = nlohmann::json::parse(m.state, nullptr, false);
    if (j.is_discarded()) return;
    if (!gScreen || currentScreen() != gScreen) {
        // An update that crossed our close message: the server is closing the session too.
        if (!m.fresh) return;
        gScreen = std::make_shared<MatrixScreen>();
        gScreen->setState(j);
        if (!openScreen(gScreen)) {
            gScreen.reset();
            net::sendToServer(fe::MatrixCloseMsg{});
            return;
        }
        log().info("enchanting: matrix screen opened");
        return;
    }
    gScreen->setState(j);
}

void craftOverlay(Overlay& o, ScreenInfo const& s) {
    if (gPreview.kind.empty()) return;
    auto result = game::itemFromSnbt(gPreview.result);
    if (game::isEmpty(result)) return;
    float x = s.size.x / 2 + 92, y = s.size.y / 2 - 80;
    auto& dc = o.draw();
    dc.fill({x, y, 80, 52}, colors::panel);
    dc.outline({x, y, 80, 52}, colors::yellow);
    dc.item(result, x + 4, y + 4);
    if (gPreview.cost > 0)
        dc.text(ctr("lp.enchanting.craft.cost", "Cost: {}", gPreview.cost), x + 24, y + 8,
                gPreview.affordable ? Color::rgb(0x80ff20) : Color::rgb(0xff6060));
    auto label = gPreview.kind == "rune" ? ctr("lp.enchanting.craft.apply_rune", "Apply Rune") : ctr("lp.enchanting.craft.apply", "Apply");
    o.button({x + 4, y + 30, 72, 16}, label, [] { net::sendToServer(fe::CraftMsg{}); }, tooltipFor(result));
    static std::string logged;
    if (s.settled && logged != gPreview.result) {
        logged = gPreview.result;
        log().info("enchanting: craft overlay drawn on {}, apply at {:.4f},{:.4f}", s.name, (x + 40) / s.size.x, (y + 38) / s.size.y);
    }
}

std::string rainbow(std::string const& s) {
    static char const* k[] = {"§c", "§6", "§e", "§a", "§b", "§9", "§d"};
    std::string        out;
    size_t             i = 0;
    for (size_t pos = 0; pos < s.size();) {
        unsigned char c   = static_cast<unsigned char>(s[pos]);
        size_t        len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        out += k[i++ % 7] + s.substr(pos, len);
        pos += len;
    }
    return out;
}

constexpr char const* kRuneTextColor[16] = {"§f", "§6", "§d", "§b", "§e", "§a", "§d", "§8", "§7", "§3", "§5", "§9", "§6", "§2", "§c", "§8"};

void tooltipLines(ItemStackBase const& item, std::vector<std::string>& lines) {
    auto const& c  = fe::cfg();
    auto        id = game::itemId(item);
    if (id.empty()) return;
    if (c.enableMatrixEnchanting && c.showTooltip && fe::stringTag(item, fe::table::kMatrixTag))
        lines.push_back("§b" + ctr("quark.gui.enchanting.pending", "Has Pending Enchantments"));
    if (id == fe::kTomeId) {
        auto te = fe::tomeEnchant(item);
        auto const* d = te ? fe::defOf(*te) : nullptr;
        if (d)
            lines.push_back("§7" + ctr("quark.misc.ancient_tome_tooltip", "+I {} (Max. {})", ctr(fe::nameKey(*d), fe::englishName(*d)),
                                       fe::roman(d->maxLevel + c.maxLimitBreakLevels)));
        else lines.push_back("§7" + ctr("quark.misc.ancient_tome_tooltip_any", "(Any Enchantment)"));
        if (c.curseGear) lines.push_back("§c" + ctr("quark.misc.ancient_tome_tooltip_curse", "Applies a random curse"));
    }
    if (id == fe::kRuneId) {
        lines.push_back("§7" + ctr("upgrade.quark.rune_upgrade", "Runic Etching"));
        lines.emplace_back("");
        lines.push_back("§7" + ctr("lp.enchanting.applies_to", "Applies to:"));
        lines.push_back(" §9" + ctr("item.quark.smithing_template.rune.applies_to", "Enchanted Items"));
        lines.push_back("§7" + ctr("lp.enchanting.ingredients", "Ingredients:"));
        lines.push_back(" §9" + ctr("item.quark.smithing_template.rune.ingredients", "Blaze Powder, Dye, or Nothing"));
    }
    if (id != "minecraft:enchanted_book") {
        if (auto rune = fe::stringTag(item, fe::kRuneTag)) {
            lines.push_back("§7" + ctr("lp.enchanting.upgrade", "Upgrade:"));
            auto name = ctr("rune.quark." + *rune, *rune + " Runic Etching");
            if (*rune == "rainbow") lines.push_back(" " + rainbow(name));
            else {
                std::string col = "§7";
                auto const& all = fe::runeColors();
                for (size_t i = 0; i < 16; ++i)
                    if (all[i] == *rune) col = kRuneTextColor[i];
                lines.push_back(" " + col + name);
            }
        }
    }
}

class EnchantingClientModule {
public:
    void install() {
        mHandlers.push_back(net::onClient<fe::MatrixStateMsg>(onState));
        mHandlers.push_back(net::onClient<fe::CraftPreviewMsg>([](fe::CraftPreviewMsg const& m) {
            gPreview = m;
            if (!m.kind.empty()) log().info("enchanting: craft preview {} (cost {})", m.kind, m.cost);
        }));
    }
    void uninstall() {
        for (auto h : mHandlers) net::removeHandler(h);
        mHandlers.clear();
        gScreen.reset();
    }

private:
    std::vector<net::HandlerId> mHandlers;
};

} // namespace

LP_REGISTER_SCREEN_OVERLAY("*anvil*", craftOverlay);
LP_REGISTER_SCREEN_OVERLAY("*smithing*", craftOverlay);
LP_REGISTER_TOOLTIP(tooltipLines);

} // namespace lp::client::enchanting

LP_REGISTER_MODULE(lp::client::enchanting::EnchantingClientModule);

#endif // LP_CLIENT
