// U17 placement, client mod: the VariantSelector (radial menu on a held key, HUD, tooltip, held
// item override, pick-block rule) and LongRangePickBlock (pick block on nothing ray-traces 200
// blocks).

#ifdef LP_CLIENT

#include "client/placement/ClientPlacement.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include "mc/deps/minecraft_renderer/game/ItemContextFlags.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "client/Client.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::client::placement {

namespace fp = feature::placement;
using fp::cfg;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

std::string heldBlockId(Player& p) {
    auto const* b = fp::blockOfItem(p.getSelectedItem());
    return b ? b->getTypeName() : std::string();
}

bool holdingHammer(Player& p) { return game::itemId(p.getSelectedItem()) == fp::kHammer; }

/// The block under the crosshair (client ray), "" when none.
std::string lookedAtBlock(Player& p) {
    auto& region = p.getDimensionBlockSource();
    auto  ray    = fp::clip(region, p.getEyePos(), p.getViewVector(1.0f), fp::reachOf(p), &p);
    if (!ray.hit) return {};
    return region.getBlock(ray.pos).getTypeName();
}

/// One ItemStack per block id for drawing (client thread; cleared when a world is joined).
std::map<std::string, ItemStack>& itemCache() {
    static std::map<std::string, ItemStack> cache;
    return cache;
}
std::mutex& itemCacheMutex() {
    static std::mutex m;
    return m;
}
ItemStack const& stackOf(std::string const& id) {
    std::lock_guard l(itemCacheMutex());
    auto&           cache = itemCache();
    auto  it    = cache.find(id);
    if (it != cache.end()) return it->second;
    return cache.emplace(id, game::makeItem(id, 1)).first->second;
}

std::string variantLabel(std::string const& type) {
    if (type.empty()) return client::ctr("lp.placement.variant_original", "Original");
    return client::ctr("lp.placement.variant." + type, type);
}

void playClick() {
    if (auto* c = client::currentClient()) c->playUI("random.click", 1.0f, 1.0f);
}

// ---- the radial menu (Quark's VariantSelectorScreen) -------------------------------------------

std::atomic<bool> gKeyReleased{false};
std::atomic<bool> gShiftDown{false}; ///< from the raw key events (Screen.hasShiftDown())

class VariantSelectorScreen : public ui::CustomScreen {
public:
    VariantSelectorScreen(std::string original, std::string current) : mCurrent(std::move(current)) {
        mVariants.emplace_back("", original);
        for (auto const& v : fp::visibleVariants()) mVariants.emplace_back(v, fp::variantOf(original, v));
        mStart = std::chrono::steady_clock::now();
        gKeyReleased = false;
    }

    void render(ui::DrawContext& dc, ui::Vec mouse) override {
        float timeIn =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - mStart).count() * 20.0f; // ticks
        auto  size     = dc.screenSize();
        float cx       = std::floor(size.x / 2);
        float cy       = std::floor(size.y / 2);
        int   segments = static_cast<int>(mVariants.size());
        float degPer   = kPi * 2 / static_cast<float>(segments);
        float pad      = -(kPi / static_cast<float>(segments)) + kPi / 2; // the original at the bottom
        float angle    = mouseAngle(cx, cy, mouse.x, mouse.y);
        float dist     = (cx - mouse.x) * (cx - mouse.x) + (cy - mouse.y) * (cy - mouse.y);
        int   active   = dist > 64 ? static_cast<int>((kPi * 2 + angle - pad) / degPer) % segments : -1;

        mSelected = -1;
        std::vector<float>     radius(segments);
        std::vector<ui::Color> color(segments);
        for (int seg = 0; seg < segments; ++seg) {
            bool exists = !mVariants[seg].second.empty();
            bool right  = mVariants[seg].first == mCurrent;
            bool hover  = exists && active == seg;
            float r = std::max(0.0f, std::min((timeIn - static_cast<float>(seg) * 6.0f / static_cast<float>(segments)) * 40.0f, 50.0f));
            if (hover || right) r *= 1.1f;
            if (!exists) r *= 0.9f;
            int gs = 0x39 + (seg % 2 == 0 ? 0x29 : 0);
            int rr = gs, gg = gs, bb = gs, aa = 0x44;
            if (exists) {
                gg += 0x22;
                aa = 0x99;
            } else {
                rr /= 4;
                gg /= 4;
                bb /= 4;
            }
            if (hover) {
                mSelected = seg;
                rr        = 0x00;
                gg = bb = 0xAA;
            } else if (right) {
                rr = bb = 0x00;
                gg      = 0xAA;
            }
            radius[seg] = r;
            color[seg]  = ui::Color{rr / 255.f, gg / 255.f, bb / 255.f, aa / 255.f};
        }

        // triangle fan, rasterised in rows of UI units: a point belongs to the sector of its angle
        // when it lies on the centre's side of that sector's chord
        float maxR = 56;
        for (float y = -maxR; y <= maxR; y += 1) {
            int   runSeg   = -1;
            float runStart = 0;
            auto  flush    = [&](float end) {
                if (runSeg >= 0 && end > runStart) dc.fill({cx + runStart, cy + y, end - runStart, 1}, color[runSeg]);
                runSeg = -1;
            };
            for (float x = -maxR; x <= maxR + 1; x += 1) {
                int   seg = -1;
                float px = x + 0.5f, py = y + 0.5f;
                if (x <= maxR) {
                    float a = std::fmod(std::atan2(py, px) + kPi * 2, kPi * 2);
                    int   s = static_cast<int>((kPi * 2 + a - pad) / degPer) % segments;
                    float mid = (static_cast<float>(s) + 0.5f) * degPer + pad;
                    float along = px * std::cos(mid) + py * std::sin(mid);
                    if (along <= radius[s] * std::cos(degPer / 2)) seg = s;
                }
                if (seg != runSeg) {
                    flush(x);
                    if (seg >= 0) {
                        runSeg   = seg;
                        runStart = x;
                    }
                }
            }
            flush(maxR + 1);
        }
        for (int seg = 0; seg < segments; ++seg) {
            if (mVariants[seg].second.empty()) continue;
            float center = (static_cast<float>(seg) + 0.5f) * degPer + pad;
            float cxp    = cx + std::cos(center) * radius[seg];
            float cyp    = cy + std::sin(center) * radius[seg];
            float xdp    = std::floor((cxp - cx) * 0.6f + cx);
            float ydp    = std::floor((cyp - cy) * 0.6f + cy);
            dc.item(stackOf(mVariants[seg].second), xdp - 8, ydp - 8, 1, false);
        }
        if (mSelected >= 0) {
            auto const& v = mVariants[mSelected];
            dc.text(variantLabel(v.first), cx, cy + 60, ui::colors::white, 1, ui::Align::Center);
        }
        if (!mLogged) {
            mLogged = true;
            log().info("placement: variant selector opened with {} segments", segments);
            for (int seg = 0; seg < segments; ++seg) {
                float center = (static_cast<float>(seg) + 0.5f) * degPer + pad;
                log().info(
                    "placement: variant segment {} '{}' at {:.3f},{:.3f}",
                    seg,
                    mVariants[seg].first,
                    (cx + std::cos(center) * 35) / size.x,
                    (cy + std::sin(center) * 35) / size.y
                );
            }
        }
        // Quark: releasing the key picks (tick())
        if (gKeyReleased.exchange(false)) release(timeIn);
    }

    void onClick(ui::Vec, int button) override {
        // a click picks too (mouse-bound keys and touch-like use)
        if (button == 0) release(100);
    }

private:
    static float mouseAngle(float x, float y, float mx, float my) {
        return std::fmod(std::atan2(my - y, mx - x) + kPi * 2, kPi * 2);
    }

    void release(float timeIn) {
        if (mDone) return;
        mDone       = true;
        int pick    = mSelected;
        if (pick == -1 && timeIn < 10) pick = 0;
        if (pick != -1) {
            setClientVariant(mVariants[pick].first, true);
            playClick();
            log().info("placement: variant selector picked '{}'", mVariants[pick].first);
        }
        close();
    }

    std::vector<std::pair<std::string, std::string>> mVariants; ///< (type, block id or "")
    std::string                                      mCurrent;
    std::chrono::steady_clock::time_point            mStart;
    int                                              mSelected = -1;
    bool                                             mDone     = false;
    bool                                             mLogged   = false;
};

void openSelector() {
    if (!fp::variantsEnabled()) return;
    auto* p = client::currentPlayer();
    if (!p || ui::topScreen() != "hud_screen") return;
    std::string original;
    if (holdingHammer(*p)) {
        auto looked = lookedAtBlock(*p);
        if (!looked.empty()) original = fp::originalOf(looked);
    } else {
        original = heldBlockId(*p);
    }
    if (original.empty() || !fp::hasVariants(original)) return;
    ui::openScreen(std::make_shared<VariantSelectorScreen>(original, clientVariant()));
}

// ---- HUD ---------------------------------------------------------------------------------------

void drawArrow(ui::DrawContext& dc, float x, float y, bool small, ui::Color c) {
    if (small)
        drawIcon(dc, {"......#....", "......##...", "##########.", "##########.", "......##...", "......#...."}, x + 4, y + 5, c);
    else
        drawIcon(
            dc,
            {".........#......",
             ".........##.....",
             ".........###....",
             ".........####...",
             "###############.",
             "################",
             "###############.",
             ".........####...",
             ".........###....",
             ".........##.....",
             ".........#......"},
            x + 3,
            y + 2,
            c
        );
}

void drawVariantHud(ui::DrawContext& dc) {
    auto const& c = cfg();
    if (!fp::variantsEnabled() || !c.showHud || hudHidden()) return;
    if (ui::topScreen() != "hud_screen") return;
    auto* p = client::currentPlayer();
    if (!p) return;
    std::string left, right;
    if (holdingHammer(*p)) {
        left  = lookedAtBlock(*p);
        right = left.empty() ? std::string() : fp::variantFromAny(left, clientVariant());
    } else {
        left = heldBlockId(*p);
        if (!left.empty() && !clientVariant().empty()) right = fp::variantOf(left, clientVariant());
    }
    if (left.empty() || right.empty() || left == right) return;
    static std::string lastShown;
    if (lastShown != left + ">" + right) {
        lastShown = left + ">" + right;
        log().info("placement: variant hud {} -> {}", left, right);
    }
    auto  size = dc.screenSize();
    float x    = std::floor(size.x / 2);
    float y    = std::floor(size.y / 2) + 12;
    if (c.alignHudToHotbar) {
        x += 125; // right-handed
        y = size.y - 19;
    }
    float offset = 8;
    float width  = c.smallerArrow ? 13 : 16;
    float posX   = x - offset - width + static_cast<float>(c.hudOffsetX);
    float posY   = y + static_cast<float>(c.hudOffsetY);
    if (!c.showSimpleHud) {
        dc.item(stackOf(left), posX, posY, 1, false);
        ui::Color arrow = c.variantSelectorRenderLikeCrossHair ? ui::Color{1, 1, 1, 1} : ui::Color{0.8f, 0.8f, 0.8f, 0.7f};
        drawArrow(dc, posX + 8 + 8 - (c.smallerArrow ? 1 : 0), posY, c.smallerArrow, arrow);
        posX += width * 2;
    } else if (c.alignHudToHotbar) {
        ui::Color frame = c.enableGreenTint ? ui::Color{0.5f, 1.0f, 0.5f, 1} : ui::colors::white;
        dc.outline({posX - 3, posY - 3, 22, 22}, frame, 1);
    } else {
        posX += width;
    }
    dc.item(stackOf(right), posX, posY, 1, false);
}

// ---- pick block ------------------------------------------------------------------------------

bool inInventory(Player& p, std::string const& itemId) {
    auto& inv = p.getInventory();
    for (int i = 0; i < 36; ++i)
        if (game::itemId(inv.getItem(i)) == itemId) return true;
    return false;
}

/// Quark's VariantSelectorModule.Client#onPickBlock. True when it handled the pick.
bool variantPick(LocalPlayer& p, BlockPos const& pos) {
    if (!fp::variantsEnabled()) return false;
    auto&       region = p.getDimensionBlockSource();
    std::string picked = region.getBlock(pos).getTypeName();
    if (picked.empty() || picked == "minecraft:air") return false;
    std::string base;
    if (holdingHammer(p)) base = fp::originalOf(picked);
    else base = heldBlockId(p);
    if (!base.empty() && base != picked) {
        auto key = fp::variantNameOf(base, picked);
        if (!key.empty()) {
            setClientVariant(key, true);
            return true; // the pick is cancelled: the variant was selected
        }
    }
    base = fp::originalOf(picked);
    if (!(p.isCreative() || inInventory(p, base))) return false;
    if (base != picked) {
        auto key = fp::variantNameOf(base, picked);
        if (!key.empty()) setClientVariant(key, true);
        if (!net::serverHasMod()) return false;
        net::sendToServer(fp::PickMsg{pos.x, pos.y, pos.z, base});
        return true;
    }
    setClientVariant("", true);
    return false;
}

LL_TYPE_INSTANCE_HOOK(
    PlacementPickBlockHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::pickBlock,
    void,
    HitResult const& hitResult,
    bool             withData
) {
    auto& region = getDimensionBlockSource();
    bool  block  = hitResult.mType == HitResultType::Tile && !region.getBlock(hitResult.mBlock).isAir();
    if (block && variantPick(*this, hitResult.mBlock)) return;
    origin(hitResult, withData);
}

std::atomic<bool> gPickPressed{false};

/// LongRangePickBlock: the game does nothing for a pick on nothing, so the middle click is caught
/// here and, when the crosshair is on nothing (or air), a 200-block ray picks the block.
void longRangePick() {
    auto* p = client::currentPlayer();
    if (!p || !cfg().enableLongRangePickBlock || ui::topScreen() != "hud_screen") return;
    auto& region = p->getDimensionBlockSource();
    Vec3  eye    = p->getEyePos();
    Vec3  look   = p->getViewVector(1.0f);
    auto  near   = fp::clip(region, eye, look, fp::reachOf(*p), p);
    if (near.hit && !region.getBlock(near.pos).isAir()) return; // the game picks it
    Vec3 to{eye.x + look.x * 200, eye.y + look.y * 200, eye.z + look.z * 200};
    auto far = region.clip(
        eye,
        to,
        false,
        ShapeType::Outline,
        201,
        false,
        false,
        p,
        [](BlockSource const&, Block const&, bool) { return true; },
        false
    );
    if (far.mType != HitResultType::Tile) return;
    log().info("placement: long range pick at {} {} {}", far.mBlock.x, far.mBlock.y, far.mBlock.z);
    if (variantPick(*p, far.mBlock)) return;
    if (net::serverHasMod()) {
        net::sendToServer(fp::PickMsg{far.mBlock.x, far.mBlock.y, far.mBlock.z, ""});
        return;
    }
    p->pickBlock(far, false); // a server without Lepton: the vanilla pick request
}

// ---- held item -----------------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    PlacementHeldItemHook,
    ll::memory::HookPriority::Normal,
    ItemInHandRenderer,
    &ItemInHandRenderer::renderItem,
    void,
    BaseActorRenderContext& renderContext,
    Actor&                  entity,
    ItemStack const&        item,
    bool                    posAndRotSetByJSON,
    ItemContextFlags        itemFlags,
    bool                    useMatrixAsIs,
    bool                    renderingMainHand
) {
    auto* local = client::currentPlayer();
    if (renderingMainHand && fp::variantsEnabled() && cfg().overrideHeldItemRender && local
        && static_cast<Actor*>(local) == &entity && !clientVariant().empty()) {
        if (auto const* b = fp::blockOfItem(item)) {
            auto v = fp::variantOf(b->getTypeName(), clientVariant());
            if (!v.empty() && v != b->getTypeName()) {
                auto const& alt = stackOf(v);
                if (!game::isEmpty(alt)) return origin(renderContext, entity, alt, posAndRotSetByJSON, itemFlags, useMatrixAsIs, renderingMainHand);
            }
        }
    }
    origin(renderContext, entity, item, posAndRotSetByJSON, itemFlags, useMatrixAsIs, renderingMainHand);
}

// ---- tooltip -----------------------------------------------------------------------------------

void variantTooltip(ItemStackBase const& item, std::vector<std::string>& lines) {
    if (!fp::variantsEnabled() || !cfg().showTooltip) return;
    auto const* b = fp::blockOfItem(item);
    if (!b) return;
    auto all = fp::allVariants(b->getTypeName());
    if (all.empty()) return;
    if (!gShiftDown.load()) {
        lines.push_back("§7" + client::ctr("quark.misc.variant_tooltip_hold_shift", "(SHIFT for Variants)"));
        return;
    }
    lines.push_back("§7" + client::ctr("quark.misc.variant_tooltip_header", "Variants:"));
    for (auto const& [type, id] : all) lines.push_back("§7 " + variantLabel(type));
}

/// Quark's JEI hints, as tooltip lines (trowel, ladders, scaffolding).
void hintTooltip(ItemStackBase const& item, std::vector<std::string>& lines) {
    auto        id = game::itemId(item);
    auto const& c  = cfg();
    if (id == fp::kTrowel && c.enableTrowel) {
        lines.push_back("§8" + client::ctr("quark.jei.hint.trowel", "Randomly places a block from your hotbar when used. You can have the same block in multiple slots to increase its odds."));
        return;
    }
    if (id == "minecraft:scaffolding" && c.enableReplaceScaffolding) {
        lines.push_back("§8" + client::ctr("quark.jei.hint.minecraft.scaffolding", "Right clicking the Scaffolding with any block will replace the furthermost piece of Scaffolding with the block. Sneak to use the normal placing."));
        return;
    }
    auto const* b = fp::blockOfItem(item);
    if (!b || !fp::isLadder(*b) || !c.enableEnhancedLadders) return;
    std::string hint;
    auto        add = [&](std::string_view key, std::string_view fallback) {
        if (!hint.empty()) hint += " ";
        hint += client::ctr(key, fallback);
    };
    if (c.allowDroppingDown) add("quark.jei.hint.ladder_dropping", "Can be right clicked onto another ladder to drop it down.");
    if (c.allowFreestanding) add("quark.jei.hint.ladder_freestanding", "As long as there's a structurally sound one behind it, can stand without any blocks behind.");
    if (c.allowSliding) add("quark.jei.hint.ladder_sliding", "Looking down while on a ladder will make you descend faster.");
    if (c.allowInventorySneak) add("quark.jei.hint.ladder_sneak", "Opening your inventory while on a ladder will automatically make you sneak.");
    if (!hint.empty()) lines.push_back("§8" + hint);
}

} // namespace

void clearItemCache() {
    std::lock_guard l(itemCacheMutex());
    itemCache().clear();
}

class VariantSelectorClientModule {
public:
    void install() {
        PlacementPickBlockHook::hook();
        PlacementHeldItemHook::hook();
        // window thread: only note the release; the screen picks on its next frame
        mKey = ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>([](ll::event::KeyInputEvent& ev) {
            int code = ev.keyCode();
            if (code == 0x10 || code == 0xA0 || code == 0xA1) gShiftDown = ev.isDown();
            if (ev.isDown()) return;
            for (int k : ui::keysOf("variant_selector"))
                if (k == ev.keyCode()) gKeyReleased = true;
        });
        // window thread: the middle button (the game's pick); handled on the next client tick
        mMouse = ll::event::EventBus::getInstance().emplaceListener<ll::event::MouseInputEvent>([](ll::event::MouseInputEvent& ev) {
            if (ev.actionButtonId() == 3 && ev.buttonData() != 0) gPickPressed = true;
        });
        mTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ClientLevelTickEvent>([](ll::event::ClientLevelTickEvent&) {
            if (gPickPressed.exchange(false)) longRangePick();
        });
    }
    void uninstall() {
        PlacementPickBlockHook::unhook();
        PlacementHeldItemHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mKey);
        bus.removeListener(mMouse);
        bus.removeListener(mTick);
    }

private:
    ll::event::ListenerPtr mKey;
    ll::event::ListenerPtr mMouse;
    ll::event::ListenerPtr mTick;
};

} // namespace lp::client::placement

LP_REGISTER_MODULE(lp::client::placement::VariantSelectorClientModule);
LP_REGISTER_KEYBIND("variant_selector", lp::client::ui::Key::R, [] { lp::client::placement::openSelector(); });
LP_REGISTER_HUD([](lp::client::ui::DrawContext& dc) { lp::client::placement::drawVariantHud(dc); });
LP_REGISTER_TOOLTIP([](ItemStackBase const& item, std::vector<std::string>& lines) {
    lp::client::placement::variantTooltip(item, lines);
    lp::client::placement::hintTooltip(item, lines);
});

#endif // LP_CLIENT
