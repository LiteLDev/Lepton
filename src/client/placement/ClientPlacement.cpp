// U17 placement, client mod: shared client state, the reacharound indicator, the rotation lock
// (keybind + HUD indicator) and EnhancedLadders' client-side movement (sliding, inventory sneak).

#ifdef LP_CLIENT

#include "client/placement/ClientPlacement.h"

#include <atomic>
#include <cmath>
#include <mutex>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/client/ClientJoinLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/input/ClientMoveInputHandler.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/input/MoveInputState.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/phys/AABB.h"

#include "client/Client.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::client::placement {

namespace fp = feature::placement;
using fp::cfg;

// ---- state ------------------------------------------------------------------------------------

namespace {
struct State {
    std::mutex                    mutex;
    std::optional<LockProfile>    lock;
    std::string                   variant;
    std::optional<ReachTarget>    reach;
    bool                          forcedSneak = false;
};
State& st() {
    static State s;
    return s;
}
} // namespace

std::optional<LockProfile> clientLock() {
    std::lock_guard l(st().mutex);
    return st().lock;
}

std::string clientVariant() {
    std::lock_guard l(st().mutex);
    return st().variant;
}

void setClientVariant(std::string const& variant, bool sync) {
    bool changed = false;
    {
        std::lock_guard l(st().mutex);
        changed      = st().variant != variant;
        st().variant = variant;
    }
    if (sync && changed) net::sendToServer(fp::VariantMsg{variant});
    log().info("placement: client variant '{}'", variant);
}

std::optional<ReachTarget> currentReachTarget() {
    std::lock_guard l(st().mutex);
    return st().reach;
}

std::string keyName(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x6F);
    if (vk >= 0x60 && vk <= 0x69) return "NUM " + std::to_string(vk - 0x60);
    switch (vk) {
    case 0x09:
        return "TAB";
    case 0x10:
        return "SHIFT";
    case 0x11:
        return "CTRL";
    case 0x12:
        return "ALT";
    case 0x14:
        return "CAPS LOCK";
    case 0x20:
        return "SPACE";
    case 0xC0:
        return "`";
    case 0xBD:
        return "-";
    case 0xBB:
        return "=";
    case 0xDB:
        return "[";
    case 0xDD:
        return "]";
    case 0xBA:
        return ";";
    case 0xDE:
        return "'";
    case 0xBC:
        return ",";
    case 0xBE:
        return ".";
    case 0xBF:
        return "/";
    case 0xDC:
        return "\\";
    default:
        return "KEY " + std::to_string(vk);
    }
}

std::string boundKeyName(std::string const& keybind) {
    auto keys = ui::keysOf(keybind);
    if (keys.empty() || keys.front() == ui::Key::Unbound) return {};
    return keyName(keys.front());
}

void drawIcon(ui::DrawContext& dc, std::initializer_list<std::string_view> rows, float x, float y, ui::Color color, ui::Color shade) {
    float yy = y;
    for (auto row : rows) {
        float run = -1;
        char  kind = 0;
        auto  flush = [&](float end) {
            if (run < 0) return;
            dc.fill({x + run, yy, end - run, 1}, kind == '#' ? color : shade);
            run = -1;
        };
        for (size_t i = 0; i <= row.size(); ++i) {
            char c = i < row.size() ? row[i] : '.';
            if (c == '#' || c == '+') {
                if (run >= 0 && c != kind) flush(static_cast<float>(i));
                if (run < 0) {
                    run  = static_cast<float>(i);
                    kind = c;
                }
            } else flush(static_cast<float>(i));
        }
        yy += 1;
    }
}

bool hudHidden() {
    auto* c = client::currentClient();
    auto* p = client::currentPlayer();
    if (!c || !p) return true;
    if (p->isSpectator()) return true;
    try {
        return c->getOptions().getHideHud();
    } catch (...) {
        return false;
    }
}

// ---- reacharound indicator ----------------------------------------------------------------------

namespace {

void drawReacharound(ui::DrawContext& dc) {
    if (!cfg().enableReacharoundPlacing || hudHidden() || ui::topScreen() != "hud_screen") return;
    auto t = currentReachTarget();
    if (!t) return;
    auto  size = dc.screenSize();
    float x    = std::floor((size.x - 15) / 2);
    float y    = std::floor((size.y - 15) / 2);
    auto  col  = ui::Color{1, 1, 1, 0.9f};
    if (t->face == fp::Down) {
        // vertical: the block goes under the edge -> brackets below the crosshair
        drawIcon(
            dc,
            {"................",
             "................",
             "................",
             "................",
             "................",
             "................",
             "................",
             "................",
             "................",
             "................",
             "...#........#...",
             "...#........#...",
             "...##########...",
             "......#..#......",
             ".......##.......",
             "................"},
            x,
            y,
            col
        );
    } else {
        // horizontal: the block goes in front of the edge -> brackets beside the crosshair
        drawIcon(
            dc,
            {"................",
             "................",
             "................",
             "................",
             "................",
             "#.###......###.#",
             ".#..#......#..#.",
             "..#.#......#.#..",
             "..#.#......#.#..",
             ".#..#......#..#.",
             "#.###......###.#",
             "................",
             "................",
             "................",
             "................",
             "................"},
            x,
            y,
            col
        );
    }
}

// ---- rotation lock ----------------------------------------------------------------------------

void toggleLock() {
    if (!cfg().enableLockRotation) return;
    auto* p = client::currentPlayer();
    if (!p || p->isSpectator() || ui::topScreen() != "hud_screen") return;
    if (!net::serverHasMod()) {
        log().info("placement: rotation lock needs Lepton on the server");
        return;
    }
    auto&   region = p->getDimensionBlockSource();
    Vec3    look   = p->getViewVector(1.0f);
    fp::Ray ray    = fp::clip(region, p->getEyePos(), look, fp::reachOf(*p), p);
    auto    next   = fp::profileFor(ray, look);
    std::optional<LockProfile> now;
    {
        std::lock_guard l(st().mutex);
        if (st().lock && *st().lock == next) st().lock.reset();
        else st().lock = next;
        now = st().lock;
    }
    fp::LockMsg msg;
    msg.locked = now.has_value();
    msg.facing = now ? now->facing : 0;
    msg.half   = now ? now->half : -1;
    msg.key    = boundKeyName("lock_rotation");
    net::sendToServer(msg);
    if (now) log().info("placement: client locked rotation facing {} half {}", fp::dirName(now->facing), now->half);
    else log().info("placement: client cleared the rotation lock");
}

/// 12x12 direction icons (Quark's GENERAL_ICONS row 65 has one per Direction).
void drawFacingIcon(ui::DrawContext& dc, int facing, float x, float y, ui::Color c) {
    switch (facing) {
    case fp::Down:
        drawIcon(dc, {"............", ".....##.....", ".....##.....", ".....##.....", ".....##.....", ".....##.....", "..########..", "...######...", "....####....", ".....##.....", "............", "############"}, x, y, c);
        break;
    case fp::Up:
        drawIcon(dc, {"############", "............", ".....##.....", "....####....", "...######...", "..########..", ".....##.....", ".....##.....", ".....##.....", ".....##.....", ".....##.....", "............"}, x, y, c);
        break;
    case fp::North:
        drawIcon(dc, {"############", "#..........#", "#.##....##.#", "#.###...##.#", "#.####..##.#", "#.##.##.##.#", "#.##..####.#", "#.##...###.#", "#.##....##.#", "#.##....##.#", "#..........#", "############"}, x, y, c);
        break;
    case fp::South:
        drawIcon(dc, {"############", "#..........#", "#..######..#", "#.##.......#", "#.##.......#", "#..#####...#", "#......##..#", "#.......##.#", "#.......##.#", "#.#######..#", "#..........#", "############"}, x, y, c);
        break;
    case fp::West:
        drawIcon(dc, {"############", "#..........#", "#.##....##.#", "#.##....##.#", "#.##....##.#", "#.##.##.##.#", "#.########.#", "#.###..###.#", "#.##....##.#", "#.#......#.#", "#..........#", "############"}, x, y, c);
        break;
    default: // East
        drawIcon(dc, {"############", "#..........#", "#.########.#", "#.##.......#", "#.##.......#", "#.######...#", "#.##.......#", "#.##.......#", "#.##.......#", "#.########.#", "#..........#", "############"}, x, y, c);
        break;
    }
}

void drawHalfIcon(ui::DrawContext& dc, int half, float x, float y, ui::Color c) {
    if (half == 1)
        drawIcon(dc, {"############", "############", "############", "############", "############", "############", "#..........#", "#..........#", "#..........#", "#..........#", "#..........#", "############"}, x, y, c);
    else
        drawIcon(dc, {"############", "#..........#", "#..........#", "#..........#", "#..........#", "#..........#", "############", "############", "############", "############", "############", "############"}, x, y, c);
}

void drawLock(ui::DrawContext& dc) {
    if (!cfg().enableLockRotation || hudHidden() || ui::topScreen() != "hud_screen") return;
    auto lock = clientLock();
    static std::optional<LockProfile> lastShown;
    if (lock != lastShown) {
        lastShown = lock;
        if (lock) log().info("placement: lock indicator {} {}", fp::dirName(lock->facing), lock->half);
    }
    if (!lock) return;
    auto      size = dc.screenSize();
    float     x    = std::floor(size.x / 2) + 20;
    float     y    = std::floor(size.y / 2) - 8;
    ui::Color c    = cfg().lockRotationRenderLikeCrossHair ? ui::Color{1, 1, 1, 1} : ui::Color{1, 1, 1, 0.5f};
    drawFacingIcon(dc, lock->facing, x + 2, y + 2, c);
    if (cfg().showKeybind) {
        auto key = boundKeyName("lock_rotation");
        if (!key.empty()) dc.text("[" + key + "]", x + 20, y + 3, ui::colors::white);
    }
    if (lock->half > -1) drawHalfIcon(dc, lock->half, x + 18, y + 2, c);
}

// ---- EnhancedLadders (client movement) ------------------------------------------------------------

bool climbable(Block const& b) { return b.hasProperty(BlockProperty::Climbable); }

void setSneakInput(bool on) {
    auto* c = client::currentClient();
    if (!c) return;
    auto* move = ClientMoveInputHandler::getMoveInput(*c);
    if (!move) return;
    move->mRawInputState.get().mFlagValues.get().set(static_cast<size_t>(MoveInputState::Flag::SneakDown), on);
}

void laddersTick(LocalPlayer& p) {
    auto& c = cfg();
    if (!c.enableEnhancedLadders) return;
    auto*  client = client::currentClient();
    auto*  move   = client ? ClientMoveInputHandler::getMoveInput(*client) : nullptr;
    auto&  region = p.getDimensionBlockSource();
    Vec3   feet   = p.getFeetPos();
    BlockPos pos{static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y)), static_cast<int>(std::floor(feet.z))};
    bool   onClimb  = p.onClimbableBlock();
    bool   scaffold = region.getBlock(pos).getTypeName() == "minecraft:scaffolding";
    float  pitch    = p.getRotation().x;
    Vec2   mv       = move ? move->mMove.get() : Vec2{0, 0};
    bool   noMove   = mv.x == 0 && mv.y == 0;

    // inventory sneak: a screen is open while on a ladder
    bool sneak = c.allowInventorySneak && onClimb && !p.isFlying() && !scaffold && ui::topScreen() != "hud_screen"
              && !(mv.y == 0 && pitch > 70) && !p.isOnGround();
    if (sneak) {
        setSneakInput(true);
        st().forcedSneak = true;
    } else if (st().forcedSneak) {
        setSneakInput(false);
        st().forcedSneak = false;
    }

    // sliding: looking down on a ladder with no input
    if (!c.allowSliding || !onClimb) return;
    bool jumping = move && move->mInputState.get().mFlagValues.get().test(static_cast<size_t>(MoveInputState::Flag::JumpDown));
    if (p.isSneaking() != scaffold || !noMove || pitch <= 70 || jumping || p.isFlying()) return;
    BlockPos down{pos.x, pos.y - 1, pos.z};
    if (!climbable(region.getBlock(down))) return;
    float fall   = static_cast<float>(fp::fallSpeed());
    AABB  target = p.getAABB();
    target.min.y += fall;
    target.max.y += fall;
    if (!region.fetchAABBs(target, false).empty()) return;
    p.move(Vec3{0, fall, 0});
}

void onClientTick() {
    auto* p = client::currentPlayer();
    if (!p) return;
    std::optional<ReachTarget> reach;
    try {
        reach = fp::reacharoundTarget(*p);
    } catch (...) {
        reach.reset();
    }
    {
        std::lock_guard l(st().mutex);
        st().reach = reach;
    }
    // one log line per change of the indicator (e2e scenarios wait for it)
    static int last = -1;
    int        now  = !reach ? 0 : reach->face == fp::Down ? 1 : 2;
    if (now != last) {
        last = now;
        log().info("placement: reacharound indicator {}", now == 0 ? "none" : now == 1 ? "vertical" : "horizontal");
    }
    laddersTick(*p);
}

} // namespace

class PlacementClientModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mTick     = bus.emplaceListener<ll::event::ClientLevelTickEvent>([](ll::event::ClientLevelTickEvent&) { onClientTick(); });
        mJoin     = bus.emplaceListener<ll::event::ClientJoinLevelEvent>([](ll::event::ClientJoinLevelEvent&) {
            {
                std::lock_guard l(st().mutex);
                st().lock.reset();
                st().reach.reset();
                st().variant.clear();
            }
            st().forcedSneak = false;
            clearItemCache();
            fp::rebuildVariants(true);
        });
        mRestore = net::onClient<fp::VariantRestoreMsg>([](fp::VariantRestoreMsg const& m) {
            if (m.variant.empty() || fp::isKnownVariant(m.variant)) setClientVariant(m.variant, false);
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mTick);
        bus.removeListener(mJoin);
        net::removeHandler(mRestore);
        if (st().forcedSneak) setSneakInput(false);
    }

private:
    ll::event::ListenerPtr mTick;
    ll::event::ListenerPtr mJoin;
    net::HandlerId         mRestore = 0;
};

} // namespace lp::client::placement

LP_REGISTER_MODULE(lp::client::placement::PlacementClientModule);
LP_REGISTER_HUD([](lp::client::ui::DrawContext& dc) {
    lp::client::placement::drawReacharound(dc);
    lp::client::placement::drawLock(dc);
});
LP_REGISTER_KEYBIND("lock_rotation", lp::client::ui::Key::K, [] { lp::client::placement::toggleLock(); });

#endif // LP_CLIENT
