// U17 placement, LockRotation: per-player rotation lock profiles, the companion message, the
// `/quark lockrotation` command for vanilla clients and the re-orientation of placed blocks.

#include "feature/placement/Placement.h"

#include <map>
#include <string>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Shapes.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::feature::placement {

LP_REGISTER_MESSAGE(LockMsg);

namespace bs = game::blockstates;

namespace {

std::map<std::string, LockProfile>& profiles() {
    static std::map<std::string, LockProfile> m;
    return m;
}

std::string uuidOf(Player const& p) { return p.getUuid().asString(); }

std::string lockedOnceKey(Player const& p) { return "lp:placement:locked_once:" + uuidOf(p); }

/// Bedrock yaw looking along a horizontal direction.
float yawOf(int d) {
    switch (d) {
    case South:
        return 0.0f;
    case West:
        return 90.0f;
    case North:
        return 180.0f;
    case East:
        return -90.0f;
    default:
        return 0.0f;
    }
}

/// Half-like states copied from the natural placement when the lock has no half.
constexpr std::string_view kHalfStates[] = {"minecraft:vertical_half", "top_slot_bit", "upside_down_bit"};

} // namespace

// ---- profiles -----------------------------------------------------------------------------------

LockProfile profileFor(Ray const& ray, Vec3 const& look) {
    if (ray.hit) {
        int   face = ray.face;
        float y    = ray.location.y;
        int   half = std::abs(static_cast<int>((y - static_cast<float>(static_cast<int>(y))) * 2.0f));
        if (!isHorizontal(face)) half = -1;
        else if (y < 0) half = 1 - half;
        return {oppositeDir(face), half};
    }
    return {nearestDir(look), -1};
}

std::optional<LockProfile> lockProfileOf(Player const& player) {
    if (!cfg().enableLockRotation) return std::nullopt;
    auto it = profiles().find(uuidOf(player));
    if (it == profiles().end()) return std::nullopt;
    return it->second;
}

void setLockProfile(Player& player, std::optional<LockProfile> profile, std::string const& keyName) {
    auto uuid = uuidOf(player);
    if (!profile) {
        profiles().erase(uuid);
        log().info("placement: {} cleared the rotation lock", player.getRealName());
        return;
    }
    bool once = false;
    if (game::db::available()) {
        auto v = game::db::load(lockedOnceKey(player));
        once   = v && v->is_boolean() && v->get<bool>();
    }
    if (!once) {
        std::string msg = keyName.empty()
                            ? trFor(player, "lp.placement.rotation_lock_command",
                                    "You have enabled Rotation Lock. Any blocks you place will be oriented in the direction you were looking. Run {} to change or reset it. This message won't appear again.",
                                    "§b/quark lockrotation§r")
                            : trFor(player, "quark.misc.rotation_lock",
                                    "You have enabled Rotation Lock. Any blocks you place will be oriented in the direction you were looking. Press [{}] to change or reset it. This message won't appear again.",
                                    "§b" + keyName + "§r");
        player.sendMessage(msg);
        if (game::db::available()) game::db::save(lockedOnceKey(player), true);
    }
    profiles()[uuid] = *profile;
    log().info("placement: {} locked rotation facing {} half {}", player.getRealName(), dirName(profile->facing), profile->half);
}

void clearLockProfiles() { profiles().clear(); }

// ---- re-orientation -----------------------------------------------------------------------------

namespace {

/// The block's own placement rule evaluated for a click on face `F` while looking along
/// `facing` (vanilla blocks).
Block const* vanillaLocked(
    BlockSource&       region,
    BlockPos const&    pos,
    Player&            player,
    Block const&       cur,
    LockProfile const& profile,
    int                aux
) {
    int  f = oppositeDir(profile.facing);
    Vec3 click{0.5f, 0.5f, 0.5f};
    switch (f) {
    case Down:
        click.y = 0.0f;
        break;
    case Up:
        click.y = 1.0f;
        break;
    case North:
        click.z = 0.0f;
        break;
    case South:
        click.z = 1.0f;
        break;
    case West:
        click.x = 0.0f;
        break;
    case East:
        click.x = 1.0f;
        break;
    }
    if (isHorizontal(f) && profile.half >= 0) click.y = profile.half == 1 ? 0.75f : 0.25f;

    auto& rot   = const_cast<Vec2&>(player.getRotation());
    Vec2  saved = rot;
    if (isHorizontal(profile.facing)) {
        rot.x = 0.0f;
        rot.y = yawOf(profile.facing);
    } else {
        rot.x = profile.facing == Down ? 90.0f : -90.0f;
    }
    Block const* out = nullptr;
    try {
        out = &cur.getBlockType().getPlacementBlock(player, pos, static_cast<uchar>(f), click, aux);
    } catch (...) {
        out = nullptr;
    }
    rot = saved;
    if (!out || out == &cur || out->getTypeName() != cur.getTypeName()) return nullptr;
    // No half in the profile and a side face: keep the natural half (Quark only sets the half
    // when it has one, or for up/down faces).
    if (isHorizontal(f) && profile.half < 0) {
        for (auto s : kHalfStates) {
            auto v = bs::get(cur, s);
            if (!v) continue;
            if (auto const* b = bs::with(*out, {{std::string(s), *v}})) out = b;
        }
    }
    return out == &cur ? nullptr : out;
}

/// Lepton (data-driven) blocks: the states Quark's getRotatedState sets.
Block const* customLocked(Block const& cur, LockProfile const& profile) {
    int         f    = oppositeDir(profile.facing);
    bool        side = isHorizontal(f);
    auto        kind = game::shapes::kindOf(cur);
    bs::Changes ch;
    auto        has = [&](std::string_view s) { return bs::get(cur, s).has_value(); };

    switch (kind) {
    case game::shapes::Kind::Stairs:
        if (side) ch.emplace_back("minecraft:cardinal_direction", std::string(dirName(profile.facing)));
        break;
    case game::shapes::Kind::FenceGate:
    case game::shapes::Kind::Door:
    case game::shapes::Kind::Trapdoor:
        if (side) ch.emplace_back("minecraft:cardinal_direction", std::string(dirName(f)));
        break;
    case game::shapes::Kind::VerticalSlab:
        if (side && bs::getString(cur, "lepton:type").value_or("double") != "double")
            ch.emplace_back("lepton:type", std::string(dirName(f)));
        break;
    case game::shapes::Kind::Button:
    case game::shapes::Kind::PressurePlate:
        return nullptr; // attached blocks: the clicked face is their support
    default:
        if (has("minecraft:facing_direction")) ch.emplace_back("minecraft:facing_direction", std::string(dirName(f)));
        else if (has("minecraft:block_face")) ch.emplace_back("minecraft:block_face", std::string(dirName(f)));
        else if (side && has("minecraft:cardinal_direction"))
            ch.emplace_back("minecraft:cardinal_direction", std::string(dirName(profile.facing)));
        break;
    }

    // half (slabs, stairs, trapdoors; doors have no top/bottom half in Quark's sense)
    bool doubleSlab = kind == game::shapes::Kind::Slab && bs::getBool(cur, "lepton:double").value_or(false);
    if (has("minecraft:vertical_half") && kind != game::shapes::Kind::Door && !doubleSlab) {
        if (profile.half >= 0) ch.emplace_back("minecraft:vertical_half", std::string(profile.half == 1 ? "top" : "bottom"));
        else if (!side) ch.emplace_back("minecraft:vertical_half", std::string(f == Down ? "top" : "bottom"));
    }
    if (ch.empty()) return nullptr;
    auto const* out = bs::with(cur, ch);
    return out == &cur ? nullptr : out;
}

/// Copies the states that differ between `before` and `after` onto the upper half of a door.
void syncUpperHalf(BlockSource& region, BlockPos const& pos, Block const& before, Block const& after) {
    auto        above = step(pos, Up);
    auto const& up    = region.getBlock(above);
    if (up.getTypeName() != before.getTypeName()) return;
    bs::Changes ch;
    for (auto const& [name, value] : bs::all(after)) {
        if (name == "upper_block_bit" || name == "lepton:upper") continue;
        auto old = bs::get(before, name);
        if (old && *old != value) ch.emplace_back(name, value);
    }
    if (ch.empty()) return;
    if (auto const* b = bs::with(up, ch)) game::setBlock(region, above, *b);
}

bool isDoor(Block const& b) {
    auto const& n = b.getTypeName();
    return n.ends_with("_door") || n == "minecraft:wooden_door" || game::shapes::kindOf(b) == game::shapes::Kind::Door;
}

} // namespace

bool applyLockVanilla(BlockSource& region, BlockPos const& pos, Player& player, LockProfile const& profile, int, Vec3 const&, int aux) {
    auto const& cur = region.getBlock(pos);
    if (cur.isAir()) return false;
    // Beds are two blocks with a block entity placed by the bed item: re-orienting the foot alone
    // would break them.
    if (cur.getTypeName() == "minecraft:bed") return false;
    auto const* next = vanillaLocked(region, pos, player, cur, profile, aux);
    if (!next || !next->canSurvive(region, pos)) return false;
    Block const& before = cur;
    if (!game::setBlock(region, pos, *next)) return false;
    if (isDoor(before)) syncUpperHalf(region, pos, before, *next);
    return true;
}

bool applyLockCustom(BlockSource& region, BlockPos const& pos, LockProfile const& profile) {
    auto const& cur  = region.getBlock(pos);
    auto const* next = customLocked(cur, profile);
    if (!next) return false;
    Block const& before = cur;
    if (!game::setBlock(region, pos, *next)) return false;
    if (isDoor(before)) syncUpperHalf(region, pos, before, *next);
    game::shapes::updateShape(region, pos);
    game::shapes::queueUpdate(region, pos);
    return true;
}

Block const& lockedState(BlockSource& region, BlockPos const& pos, Player& player, Block const& b, int aux) {
    auto profile = lockProfileOf(player);
    if (!profile) return b;
    Block const* out = b.getTypeName().starts_with("minecraft:") ? vanillaLocked(region, pos, player, b, *profile, aux)
                                                                 : customLocked(b, *profile);
    return out ? *out : b;
}

// ---- command and message ------------------------------------------------------------------------

namespace {

std::string describe(Player const& player, LockProfile const& p) {
    std::string dir = trFor(player, "lp.placement.dir." + std::string(dirName(p.facing)), std::string(dirName(p.facing)));
    if (p.half < 0) return dir;
    return dir + " / "
         + (p.half == 1 ? trFor(player, "lp.placement.half.top", "top") : trFor(player, "lp.placement.half.bottom", "bottom"));
}

} // namespace

LockProfile currentProfile(Player& player) {
    auto& region = player.getDimensionBlockSource();
    Vec3  look   = player.getViewVector(1.0f);
    Ray   ray    = clip(region, player.getEyePos(), look, reachOf(player), &player);
    return profileFor(ray, look);
}

std::optional<LockProfile> toggleLock(Player& player, LockProfile const& next, std::string const& keyName) {
    auto cur = lockProfileOf(player);
    if (cur && *cur == next) {
        setLockProfile(player, std::nullopt, keyName);
        return std::nullopt;
    }
    setLockProfile(player, next, keyName);
    return next;
}

class LockRotationModule {
public:
    void install() {
        mHandler = net::onServer<LockMsg>([](Player& player, LockMsg const& m) {
            if (!cfg().enableLockRotation) return;
            if (!m.locked) {
                setLockProfile(player, std::nullopt, m.key);
                return;
            }
            if (m.facing < 0 || m.facing > 5 || m.half < -1 || m.half > 1) return; // a client may lie
            std::string key = m.key.substr(0, 32);
            setLockProfile(player, LockProfile{m.facing, m.half}, key);
        });
        mLogout = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDisconnectEvent>(
            [](ll::event::PlayerDisconnectEvent& ev) { profiles().erase(uuidOf(ev.self())); }
        );
    }
    void uninstall() {
        net::removeHandler(mHandler);
        ll::event::EventBus::getInstance().removeListener(mLogout);
        profiles().clear();
    }

private:
    net::HandlerId         mHandler = 0;
    ll::event::ListenerPtr mLogout;
};

} // namespace lp::feature::placement

LP_REGISTER_MODULE(lp::feature::placement::LockRotationModule);

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    using namespace lp::feature::placement;
    auto run = [](CommandOrigin const& origin, CommandOutput& out, bool clear) {
        auto* player = lp::commands::playerOf(origin);
        if (!player) {
            out.error("Players only");
            return;
        }
        if (!cfg().enableLockRotation) {
            out.error(lp::trFor(*player, "lp.placement.lock_disabled", "Rotation Lock is disabled."));
            return;
        }
        std::optional<LockProfile> now;
        if (clear) setLockProfile(*player, std::nullopt, {});
        else now = toggleLock(*player, currentProfile(*player), {});
        if (now) out.success(lp::trFor(*player, "lp.placement.lock_set", "Rotation locked: {}", describe(*player, *now)));
        else out.success(lp::trFor(*player, "lp.placement.lock_cleared", "Rotation lock cleared."));
    };
    cmd.overload().text("lockrotation").execute([run](CommandOrigin const& o, CommandOutput& out) { run(o, out, false); });
    cmd.overload().text("lockrotation").text("clear").execute([run](CommandOrigin const& o, CommandOutput& out) {
        run(o, out, true);
    });
});
