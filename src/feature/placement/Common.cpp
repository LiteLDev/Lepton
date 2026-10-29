// U17 placement: config, directions, ray tracing and the small server helpers shared by the
// modules.

#include "feature/placement/Placement.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/phys/HitResult.h"

#include "game/Items.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

LP_REGISTER_CONFIG("placement", lp::feature::placement::PlacementConfig);

namespace lp::feature::placement {

PlacementConfig const& cfg() { return config::get<PlacementConfig>(); }

double leniency() { return std::clamp(cfg().leniency, 0.0, 1.0); }
double fallSpeed() { return std::min(cfg().fallSpeed, 0.0); }

// ---- directions -----------------------------------------------------------------------------

namespace {
constexpr int kStep[6][3] = {
    {0,  -1, 0 },
    {0,  1,  0 },
    {0,  0,  -1},
    {0,  0,  1 },
    {-1, 0,  0 },
    {1,  0,  0 },
};
constexpr std::string_view kNames[6] = {"down", "up", "north", "south", "west", "east"};
} // namespace

int oppositeDir(int d) { return d ^ 1; }
std::string_view dirName(int d) { return d >= 0 && d < 6 ? kNames[d] : "north"; }
bool             isHorizontal(int d) { return d >= 2 && d < 6; }

BlockPos step(BlockPos const& p, int d, int n) {
    if (d < 0 || d > 5) return p;
    return {p.x + kStep[d][0] * n, p.y + kStep[d][1] * n, p.z + kStep[d][2] * n};
}

int horizontalFromYaw(float yaw) {
    // Java 2D data value: 0 S, 1 W, 2 N, 3 E
    int idx = static_cast<int>(std::floor(yaw / 90.0f + 0.5f)) & 3;
    constexpr int kMap[4] = {South, West, North, East};
    return kMap[idx];
}

int nearestDir(Vec3 const& v) {
    int   best  = North;
    float bestD = -std::numeric_limits<float>::infinity();
    for (int d = 0; d < 6; ++d) {
        float dot = v.x * kStep[d][0] + v.y * kStep[d][1] + v.z * kStep[d][2];
        if (dot > bestD) {
            bestD = dot;
            best  = d;
        }
    }
    return best;
}

std::vector<int> nearestLookingDirections(float pitch, float yaw) {
    // Java Direction#orderedByNearest
    constexpr float kRad = 3.14159265358979f / 180.0f;
    float           f    = pitch * kRad;
    float           f1   = -yaw * kRad;
    float           f2 = std::sin(f), f3 = std::cos(f), f4 = std::sin(f1), f5 = std::cos(f1);
    bool            east  = f4 > 0.0f;
    bool            up    = f2 < 0.0f;
    bool            south = f5 > 0.0f;
    float           f6    = east ? f4 : -f4;
    float           f7    = up ? -f2 : f2;
    float           f8    = south ? f5 : -f5;
    float           f9    = f6 * f3;
    float           f10   = f8 * f3;
    int             d0    = east ? East : West;
    int             d1    = up ? Up : Down;
    int             d2    = south ? South : North;
    auto            arr   = [](int a, int b, int c) {
        return std::vector<int>{a, b, c, oppositeDir(c), oppositeDir(b), oppositeDir(a)};
    };
    if (f6 > f8) {
        if (f7 > f9) return arr(d1, d0, d2);
        return f10 > f7 ? arr(d0, d2, d1) : arr(d0, d1, d2);
    }
    if (f7 > f10) return arr(d1, d2, d0);
    return f9 > f7 ? arr(d2, d0, d1) : arr(d2, d1, d0);
}

// ---- blocks and items --------------------------------------------------------------------------

std::string idOf(Block const& b) { return b.getTypeName(); }

bool hasTag(Block const& b, std::string_view tag) { return b.hasTag(HashedString(std::string(tag))); }

bool replaceableAt(BlockSource& region, BlockPos const& pos) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    if (!region.hasChunksAt(pos, 0, false)) return false;
    auto const& b = region.getBlock(pos);
    return b.isAir() || b.getBlockType().canBeBuiltOver(b, region, pos);
}

Block const* blockOfItem(ItemStackBase const& stack) {
    if (game::isEmpty(stack)) return nullptr;
    if (stack.mBlock) return stack.mBlock;
    // stacks read from NBT may not carry their block yet: the item's block type
    auto const* item = stack.mItem.get();
    if (!item) return nullptr;
    BlockType const* type = item->mBlockType;
    if (!type) return nullptr;
    Block const* def = type->mDefaultState;
    return def;
}

std::string itemIdOf(ItemStackBase const& stack) { return game::itemId(stack); }

// ---- ray tracing --------------------------------------------------------------------------------

Ray clip(BlockSource& region, Vec3 const& from, Vec3 const& dir, float range, Actor* actor) {
    Ray  out;
    Vec3 to{from.x + dir.x * range, from.y + dir.y * range, from.z + dir.z * range};
    auto hit = region.clip(
        from,
        to,
        false,
        ShapeType::Outline,
        static_cast<int>(std::ceil(range)) + 1,
        false,
        false,
        actor,
        [](BlockSource const&, Block const&, bool) { return true; },
        false
    );
    if (hit.mType != HitResultType::Tile) return out;
    out.hit      = true;
    out.pos      = hit.mBlock;
    out.face     = hit.mFacing;
    out.location = hit.mPos;
    return out;
}

float reachOf(Player& player) {
    GameMode* gm = player.mGameMode.get();
    if (!gm) return player.isCreative() ? 12.0f : 5.7f;
    return gm->getMaxPickRange();
}

// ---- deferred work -----------------------------------------------------------------------------

namespace {
struct Task {
    uint64_t              due;
    std::function<void()> fn;
};
struct Deferred {
    uint64_t         tick = 0;
    std::deque<Task> tasks;
};
Deferred& deferred() {
    static Deferred d;
    return d;
}
} // namespace

void later(int ticks, std::function<void()> fn) {
    deferred().tasks.push_back({deferred().tick + static_cast<uint64_t>(std::max(ticks, 1)), std::move(fn)});
}

void playPlaceSound(BlockSource& region, BlockPos const& pos, Block const& block, Actor const& by) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    level->broadcastSoundEvent(
        region,
        SharedTypes::Legacy::LevelSoundEvent::Place,
        at,
        block,
        ActorSoundIdentifier(by),
        false,
        std::nullopt
    );
}

void consumeSelected(Player& player, std::string const& itemId) {
    if (player.isCreative()) return;
    ItemStack item = player.getSelectedItem();
    if (game::isEmpty(item) || game::itemId(item) != itemId) return;
    item.remove(1);
    if (item.mCount == 0) item = ItemStack();
    player.setSelectedItem(item);
    player.sendInventory(false);
}

class PlacementCommonModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        // Low priority: after the shapes (game/Shapes) finished their own deferred placement work.
        mTick = bus.emplaceListener<ll::event::ServerLevelTickEvent>(
            [](ll::event::ServerLevelTickEvent&) {
                auto& d = deferred();
                ++d.tick;
                if (d.tasks.empty()) return;
                std::vector<Task> due;
                for (auto it = d.tasks.begin(); it != d.tasks.end();) {
                    if (it->due <= d.tick) {
                        due.push_back(std::move(*it));
                        it = d.tasks.erase(it);
                    } else ++it;
                }
                for (auto& t : due) {
                    try {
                        t.fn();
                    } catch (std::exception const& e) {
                        log().error("placement: deferred task failed: {}", e.what());
                    }
                }
            },
            ll::event::EventPriority::Low
        );
        static bool once = false;
        if (!once) {
            once = true;
            config::onReload([] { clearLockProfiles(); });
        }
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(mTick);
        deferred().tasks.clear();
    }

private:
    ll::event::ListenerPtr mTick;
};

} // namespace lp::feature::placement

LP_REGISTER_MODULE(lp::feature::placement::PlacementCommonModule);
