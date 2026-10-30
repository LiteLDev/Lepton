// U17 placement, ReacharoundPlacing: Quark's target search. Used by the server (placement on
// use-item in the air, Placing.cpp) and by the client mod (the crosshair indicator).

#include "feature/placement/Placement.h"

#include <algorithm>
#include <cmath>

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"

#include "game/Items.h"

namespace lp::feature::placement {

bool canReacharound(ItemStackBase const& stack) {
    if (game::isEmpty(stack)) return false;
    auto id = game::itemId(stack);
    if (std::find(cfg().blacklist.begin(), cfg().blacklist.end(), id) != cfg().blacklist.end()) return false;
    if (blockOfItem(stack) != nullptr) return true;
    if (id == kTrowel) return true; // Quark's quark:reacharound_able tag
    return std::find(cfg().whitelist.begin(), cfg().whitelist.end(), id) != cfg().whitelist.end();
}

namespace {

float distance(Vec3 const& a, Vec3 const& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::optional<ReachTarget> vertical(Player& player, BlockSource& region, Vec3 eye, Vec3 const& look, float range) {
    if (player.getRotation().x < 0) return std::nullopt; // looking up
    eye.y += static_cast<float>(leniency());
    Ray r = clip(region, eye, look, range, &player);
    if (!r.hit) return std::nullopt;
    BlockPos pos = step(r.pos, Down);
    if (player.getFeetPos().y - static_cast<float>(pos.y) > 1.0f && replaceableAt(region, pos))
        return ReachTarget{pos, Down};
    return std::nullopt;
}

std::optional<ReachTarget> horizontal(Player& player, BlockSource& region, Vec3 eye, Vec3 const& look, float range) {
    int   dir = horizontalFromYaw(player.getRotation().y);
    float l   = static_cast<float>(leniency());
    auto  s   = step(BlockPos{0, 0, 0}, dir);
    eye.x -= l * static_cast<float>(s.x);
    eye.z -= l * static_cast<float>(s.z);
    Ray r = clip(region, eye, look, range, &player);
    if (!r.hit) return std::nullopt;
    BlockPos pos  = step(r.pos, dir);
    Vec3     feet = player.getFeetPos();
    // Quark: the hit point lies below the player and the look angle is at the eyes, hence the -2
    Vec3 behind{feet.x - look.x, feet.y + 2.0f, feet.z - look.z};
    if (replaceableAt(region, pos) && distance(behind, r.location) > 0.9f) return ReachTarget{pos, oppositeDir(dir)};
    return std::nullopt;
}

} // namespace

std::optional<ReachTarget> reacharoundTarget(Player& player) {
    if (!cfg().enableReacharoundPlacing) return std::nullopt;
    if (!canReacharound(player.getSelectedItem())) return std::nullopt;
    auto& region = player.getDimensionBlockSource();
    Vec3  eye    = player.getEyePos();
    Vec3  look   = player.getViewVector(1.0f);
    float range  = reachOf(player);
    if (clip(region, eye, look, range, &player).hit) return std::nullopt;
    if (auto t = vertical(player, region, eye, look, range)) return t;
    return horizontal(player, region, eye, look, range);
}

} // namespace lp::feature::placement
