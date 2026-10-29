#include "game/Shapes.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/common/FacingID.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/GlobalPos.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::game::shapes {

namespace bs     = blockstates;
using SoundEvent = SharedTypes::Legacy::LevelSoundEvent;

namespace {
constexpr std::string_view kDir[4] = {"north", "east", "south", "west"}; // clockwise order
constexpr char const*      kFacing = "minecraft:cardinal_direction";
constexpr char const*      kHalf   = "minecraft:vertical_half";

bool hasTag(Block const& b, char const* tag) { return b.hasTag(HashedString(tag)); }

int dirIndex(std::string_view d) {
    for (int i = 0; i < 4; ++i)
        if (kDir[i] == d) return i;
    return -1;
}

std::pair<int, int> dirStep(std::string_view d) {
    if (d == "north") return {0, -1};
    if (d == "south") return {0, 1};
    if (d == "west") return {-1, 0};
    if (d == "east") return {1, 0};
    return {0, 0};
}

bool sameAxis(std::string_view a, std::string_view b) {
    int i = dirIndex(a), j = dirIndex(b);
    return i >= 0 && j >= 0 && (i & 1) == (j & 1);
}

std::string const& nameOf(Block const& b) { return b.getTypeName(); }
bool               endsWith(std::string_view s, std::string_view suffix) { return s.ends_with(suffix); }

/// A full, solid, opaque cube (what Java calls a sturdy face for connections).
bool isSolidFull(Block const& b) {
    if (b.isAir() || kindOf(b) != Kind::None) return false;
    return static_cast<bool>(b.mCachedComponentData->mIsSolid);
}

bool setIfChanged(BlockSource& region, BlockPos const& pos, Block const& current, Block const* next) {
    if (!next || *next == current) return false;
    return game::setBlock(region, pos, *next);
}

// ---- stairs ----------------------------------------------------------------------------------

struct StairInfo {
    std::string facing;
    bool        top;
};

/// Facing (ascending side) and half of a lepton or vanilla stair.
std::optional<StairInfo> stairInfo(Block const& b) {
    if (kindOf(b) == Kind::Stairs) {
        auto f = bs::getString(b, kFacing);
        auto h = bs::getString(b, kHalf);
        if (!f || !h) return std::nullopt;
        return StairInfo{*f, *h == "top"};
    }
    if (!endsWith(nameOf(b), "_stairs")) return std::nullopt;
    auto wd = bs::getInt(b, "weirdo_direction");
    if (!wd) return std::nullopt;
    static constexpr char const* kWeirdo[4] = {"east", "west", "south", "north"};
    return StairInfo{kWeirdo[*wd & 3], bs::getBool(b, "upside_down_bit").value_or(false)};
}

/// Java StairBlock#getStairsShape.
std::string stairShape(BlockSource& region, BlockPos const& pos, StairInfo const& self) {
    auto canTakeShape = [&](std::string_view side) {
        auto other = stairInfo(region.getBlock(neighbor(pos, side)));
        return !other || other->facing != self.facing || other->top != self.top;
    };
    if (auto front = stairInfo(region.getBlock(neighbor(pos, self.facing))); front && front->top == self.top) {
        if (!sameAxis(front->facing, self.facing) && canTakeShape(opposite(front->facing))) {
            return front->facing == counterClockwise(self.facing) ? "outer_left" : "outer_right";
        }
    }
    if (auto back = stairInfo(region.getBlock(neighbor(pos, opposite(self.facing)))); back && back->top == self.top) {
        if (!sameAxis(back->facing, self.facing) && canTakeShape(back->facing)) {
            return back->facing == counterClockwise(self.facing) ? "inner_left" : "inner_right";
        }
    }
    return "straight";
}

// ---- connections -----------------------------------------------------------------------------

bool isGate(Block const& b) {
    return kindOf(b) == Kind::FenceGate || nameOf(b).find("fence_gate") != std::string::npos;
}
bool isWallLike(Block const& b) { return kindOf(b) == Kind::Wall || b.getBlockType().isWallBlock(); }
bool isPaneLike(Block const& b) { return kindOf(b) == Kind::Pane || b.getBlockType().isThinFenceBlock(); }
bool isFenceLike(Block const& b) { return kindOf(b) == Kind::Fence || b.getBlockType().isFenceBlock(); }
bool isWooden(Block const& b) {
    if (kindOf(b) != Kind::None) return hasTag(b, "lepton:wooden");
    return nameOf(b) != "minecraft:nether_brick_fence";
}

/// A gate at `b` lets something connect from direction `dir` (towards the gate).
bool gateAccepts(Block const& b, std::string_view dir) {
    auto f = facingOf(b);
    return f && !sameAxis(*f, dir);
}

bool fenceConnects(Block const& self, Block const& other, std::string_view dir) {
    if (isFenceLike(other)) return isWooden(self) == isWooden(other);
    if (isGate(other)) return gateAccepts(other, dir);
    return isSolidFull(other);
}

bool paneConnects(Block const& other) { return isPaneLike(other) || isWallLike(other) || isSolidFull(other); }

bool wallConnects(Block const& other, std::string_view dir) {
    if (isWallLike(other) || isPaneLike(other)) return true;
    if (isGate(other)) return gateAccepts(other, dir);
    return isSolidFull(other);
}

/// Connection state of a wall above, in direction `dir` (none/short/tall), lepton or vanilla.
std::string wallSideOf(Block const& b, std::string_view dir) {
    if (kindOf(b) == Kind::Wall) return bs::getString(b, "lepton:" + std::string(dir)).value_or("none");
    return bs::getString(b, "wall_connection_type_" + std::string(dir)).value_or("none");
}
bool wallPostOf(Block const& b) {
    if (kindOf(b) == Kind::Wall) return bs::getBool(b, "lepton:post").value_or(false);
    return bs::getBool(b, "wall_post_bit").value_or(false);
}

// ---- module state ----------------------------------------------------------------------------

struct Click {
    BlockPos clicked;
    int      face = -1;
    Vec3     hit; ///< relative to `clicked`
    uint64_t tick = 0;
};

struct Task {
    uint64_t              due;
    std::function<void()> fn;
};

struct State {
    uint64_t                                                tick = 0;
    std::set<core::GlobalPos>                               queue;
    std::vector<Task>                                       tasks;
    std::unordered_map<int64_t, Click>                      clicks;  ///< by player unique id
    std::map<core::GlobalPos, uint64_t>                     plates;  ///< pressed plate -> last tick stood on
    std::map<core::GlobalPos, uint64_t>                     buttons; ///< pressed button -> release tick
    std::map<core::GlobalPos, bool>                         powered; ///< last redstone state seen per block
    std::map<std::pair<int64_t, core::GlobalPos>, uint64_t> debounce;
};
State& st() {
    static State s;
    return s;
}

void later(uint64_t delay, std::function<void()> fn) {
    st().tasks.push_back({st().tick + std::max<uint64_t>(delay, 1), std::move(fn)});
}

Player* playerById(int64_t id) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    ActorUniqueID uid;
    uid.rawID = id;
    return level->getPlayer(uid);
}

void playSound(BlockSource& region, BlockPos const& pos, SoundEvent event, Player* by) {
    if (!by) return; // sounds need an actor identifier; redstone-driven changes are silent
    auto level = ll::service::getLevel();
    if (!level) return;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    level->broadcastSoundEvent(region, event, at, region.getBlock(pos), ActorSoundIdentifier(*by), false, std::nullopt);
}

/// `hit` relative to `clicked`, whether the game passed it absolute or relative.
Vec3 relativeHit(Vec3 const& hit, BlockPos const& clicked) {
    Vec3 rel{
        hit.x - static_cast<float>(clicked.x),
        hit.y - static_cast<float>(clicked.y),
        hit.z - static_cast<float>(clicked.z)
    };
    auto in = [](float v) { return v >= -0.01f && v <= 1.01f; };
    if (in(rel.x) && in(rel.y) && in(rel.z)) return rel;
    return hit;
}

int pressTicksOf(Block const& b) {
    for (int t : {4, 10, 15, 20, 30, 40, 100}) {
        if (hasTag(b, ("lepton:press_ticks_" + std::to_string(t)).c_str())) return t;
    }
    return kindOf(b) == Kind::PressurePlate ? 20 : 30;
}

// ---- per-kind updates ------------------------------------------------------------------------

bool updateStairs(BlockSource& region, BlockPos const& pos, Block const& b) {
    auto self = stairInfo(b);
    if (!self) return false;
    return setIfChanged(
        region,
        pos,
        b,
        bs::with(
            b,
            {
                {"lepton:shape", stairShape(region, pos, *self)}
    }
        )
    );
}

bool updateFenceOrPane(BlockSource& region, BlockPos const& pos, Block const& b, bool pane) {
    bs::Changes ch;
    for (auto d : kDir) {
        auto const& other = region.getBlock(neighbor(pos, d));
        bool        c     = pane ? paneConnects(other) : fenceConnects(b, other, opposite(d));
        ch.emplace_back("lepton:" + std::string(d), c);
    }
    return setIfChanged(region, pos, b, bs::with(b, ch));
}

bool updateWall(BlockSource& region, BlockPos const& pos, Block const& b) {
    auto const& above     = region.getBlock(neighbor(pos, "up"));
    bool        aboveFull = isSolidFull(above);
    bool        aboveWall = isWallLike(above);
    std::string side[4];
    for (int i = 0; i < 4; ++i) {
        auto const& other = region.getBlock(neighbor(pos, kDir[i]));
        if (!wallConnects(other, opposite(kDir[i]))) {
            side[i] = "none";
            continue;
        }
        bool tall = aboveFull || (aboveWall && wallSideOf(above, kDir[i]) != "none");
        side[i]   = tall ? "tall" : "short";
    }
    // Java WallBlock#shouldRaisePost
    bool post;
    if (aboveWall && wallPostOf(above)) {
        post = true;
    } else {
        bool n = side[0] == "none", e = side[1] == "none", s = side[2] == "none", w = side[3] == "none";
        if ((n && s && e && w) || n != s || e != w) post = true;
        else if ((side[0] == "tall" && side[2] == "tall") || (side[1] == "tall" && side[3] == "tall")) post = false;
        else post = aboveFull;
    }
    bs::Changes ch{
        {"lepton:post", post}
    };
    for (int i = 0; i < 4; ++i) ch.emplace_back("lepton:" + std::string(kDir[i]), side[i]);
    return setIfChanged(region, pos, b, bs::with(b, ch));
}

/// Redstone for doors/trapdoors/gates: opens on a rising edge, closes on a falling edge.
bool updateRedstone(BlockSource& region, BlockPos const& pos, Block const& b, bool powered) {
    auto  key  = globalPos(region, pos);
    auto& seen = st().powered;
    auto  it   = seen.find(key);
    bool  was  = it != seen.end() && it->second;
    bool  open = bs::getBool(b, "lepton:open").value_or(false);
    if (powered) seen[key] = true;
    else if (it != seen.end()) seen.erase(it);
    if (powered && !was && !open) return setOpen(region, pos, true);
    if (!powered && was && open) return setOpen(region, pos, false);
    return false;
}

bool updateDoor(BlockSource& region, BlockPos const& pos, Block const& b) {
    bool        upper   = bs::getBool(b, "lepton:upper").value_or(false);
    auto        partner = neighbor(pos, upper ? "down" : "up");
    auto const& p       = region.getBlock(partner);
    if (nameOf(p) != nameOf(b) || bs::getBool(p, "lepton:upper").value_or(false) == upper) {
        return game::clearBlock(region, pos); // the other half is gone: no drop (it dropped the door)
    }
    if (upper) {
        // the upper half mirrors the lower half's facing / open / hinge
        auto const* synced = bs::with(
            b,
            {
                {kFacing,              bs::getString(p, kFacing).value_or("north")           },
                {"lepton:open",        bs::getBool(p,   "lepton:open").value_or(false)       },
                {"lepton:hinge_right", bs::getBool(p,   "lepton:hinge_right").value_or(false)}
        }
        );
        return setIfChanged(region, pos, b, synced);
    }
    return updateRedstone(region, pos, b, isPowered(region, pos) || isPowered(region, partner));
}

bool updateGate(BlockSource& region, BlockPos const& pos, Block const& b) {
    auto facing  = facingOf(b).value_or("north");
    bool inWall  = isWallLike(region.getBlock(neighbor(pos, clockwise(facing))))
                || isWallLike(region.getBlock(neighbor(pos, counterClockwise(facing))));
    bool changed = setIfChanged(
        region,
        pos,
        b,
        bs::with(
            b,
            {
                {"lepton:in_wall", inWall}
    }
        )
    );
    auto const& now = region.getBlock(pos);
    return updateRedstone(region, pos, now, isPowered(region, pos)) || changed;
}

// ---- interaction -----------------------------------------------------------------------------

/// The slab at `pos` can take the second half `item` places (Java SlabBlock#canBeReplaced and
/// Quark's VerticalSlabBlock#canBeReplaced). `onClicked`: `pos` is the clicked block.
bool canMerge(Block const& b, std::string const& itemId, int face, Vec3 const& hit, bool onClicked) {
    if (nameOf(b) != itemId || isDouble(b)) return false;
    auto k = kindOf(b);
    if (k == Kind::Slab) {
        if (!onClicked) return true;
        bool top        = bs::getString(b, kHalf).value_or("bottom") == "top";
        bool horizontal = face >= 2;
        if (!top) return face == 1 || (hit.y > 0.5f && horizontal);
        return face == 0 || (hit.y < 0.5f && horizontal);
    }
    if (k == Kind::VerticalSlab) {
        auto type = bs::getString(b, "lepton:type").value_or("north");
        return onClicked ? faceName(face) == type : faceName(face) != type;
    }
    return false;
}

void mergeLater(BlockSource& region, BlockPos const& pos, std::string const& itemId, int64_t playerId) {
    int dim = static_cast<int>(region.getDimensionId());
    later(1, [dim, pos, itemId, playerId] {
        auto* r = regionOf(dim);
        if (!r) return;
        auto const& b = r->getBlock(pos);
        if (nameOf(b) != itemId || isDouble(b)) return;
        auto const* d = kindOf(b) == Kind::Slab ? bs::with(
                                                      b,
                                                      {
                                                          {"lepton:double", true}
        }
                                                  )
                                                : bs::with(b, {{"lepton:type", "double"}});
        if (!d || !game::setBlock(*r, pos, *d)) return;
        if (auto* p = playerById(playerId)) {
            playSound(*r, pos, SoundEvent::Place, p);
            if (!p->isCreative()) {
                ItemStack item = p->getSelectedItem();
                if (!item.isNull() && item.getTypeName() == itemId) {
                    item.remove(1);
                    p->setSelectedItem(item);
                    p->sendInventory(false);
                }
            }
        }
    });
}

bool debounced(int64_t player, core::GlobalPos const& at) {
    auto& m   = st().debounce;
    auto  key = std::make_pair(player, at);
    auto  it  = m.find(key);
    if (it != m.end() && st().tick - it->second < 5) return true;
    m[key] = st().tick;
    return false;
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto& region    = player.getDimensionBlockSource();
    auto  pos       = ev.blockPos();
    int   face      = static_cast<int>(ev.face());
    Vec3  hit       = relativeHit(ev.clickPos(), pos);
    auto  id        = player.getOrCreateUniqueID().rawID;
    st().clicks[id] = Click{pos, face, hit, st().tick};

    auto const& b      = region.getBlock(pos);
    auto const& item   = ev.item();
    auto        itemId = item.isNull() ? std::string() : item.getTypeName();

    // slab + same slab = double
    if (!itemId.empty()) {
        if (canMerge(b, itemId, face, hit, true)) {
            ev.cancel();
            mergeLater(region, pos, itemId, id);
            return;
        }
        if (face >= 0 && face <= 5) {
            auto target = neighbor(pos, faceName(face));
            if (canMerge(region.getBlock(target), itemId, face, hit, false)) {
                ev.cancel();
                mergeLater(region, target, itemId, id);
                return;
            }
        }
    }

    // sneaking with an item in hand uses the item instead of the block (Java)
    if (player.isSneaking() && !item.isNull()) return;

    auto k = kindOf(b);
    if (k == Kind::Door || k == Kind::Trapdoor || k == Kind::FenceGate) {
        if (hasTag(b, "lepton:redstone_only")) return;
        ev.cancel();
        if (debounced(id, globalPos(region, pos))) return;
        int dim = static_cast<int>(region.getDimensionId());
        later(1, [dim, pos, id] {
            auto* r = regionOf(dim);
            if (!r) return;
            bool open = bs::getBool(r->getBlock(pos), "lepton:open").value_or(false);
            setOpen(*r, pos, !open, playerById(id));
        });
        return;
    }
    if (k == Kind::Button) {
        ev.cancel();
        if (debounced(id, globalPos(region, pos))) return;
        int dim = static_cast<int>(region.getDimensionId());
        later(1, [dim, pos, id] {
            auto* r = regionOf(dim);
            if (!r) return;
            if (press(*r, pos)) playSound(*r, pos, SoundEvent::ButtonClickOn, playerById(id));
        });
        return;
    }
    // axe on a strippable block
    if (hasTag(b, "lepton:strippable") && itemId.ends_with("_axe")) {
        auto const& name     = nameOf(b);
        auto        colon    = name.find(':');
        std::string stripped = name.substr(0, colon + 1) + "stripped_" + name.substr(colon + 1);
        if (!bs::of(stripped)) return;
        ev.cancel();
        int dim = static_cast<int>(region.getDimensionId());
        later(1, [dim, pos, id, name, stripped] {
            auto* r = regionOf(dim);
            if (!r) return;
            auto const& cur = r->getBlock(pos);
            if (nameOf(cur) != name) return;
            auto const* to = bs::of(stripped);
            if (!to) return;
            game::setBlock(*r, pos, *bs::copyStates(cur, *to));
            if (auto* p = playerById(id)) {
                playSound(*r, pos, SoundEvent::ItemUseOn, p);
                if (!p->isCreative()) {
                    ItemStack axe = p->getSelectedItem();
                    if (!axe.isNull() && axe.getTypeName().ends_with("_axe")) {
                        axe.hurtAndBreak(1, p);
                        p->setSelectedItem(axe);
                        p->sendInventory(false);
                    }
                }
            }
        });
    }
}

/// Java DoorBlock#getHinge. `hit` is relative to the door's lower block.
bool doorHingeRight(
    BlockSource&       region,
    BlockPos const&    pos,
    std::string const& facing,
    Block const&       door,
    Vec3 const&        hit
) {
    auto full        = [&](BlockPos const& p) { return isSolidFull(region.getBlock(p)); };
    auto isDoorLower = [&](BlockPos const& p) {
        auto const& o = region.getBlock(p);
        return nameOf(o) == nameOf(door) && !bs::getBool(o, "lepton:upper").value_or(false);
    };
    auto left      = neighbor(pos, counterClockwise(facing));
    auto right     = neighbor(pos, clockwise(facing));
    int  i         = (full(left) ? -1 : 0) + (full(neighbor(left, "up")) ? -1 : 0) + (full(right) ? 1 : 0)
                   + (full(neighbor(right, "up")) ? 1 : 0);
    bool leftDoor  = isDoorLower(left);
    bool rightDoor = isDoorLower(right);
    if ((!leftDoor || rightDoor) && i <= 0) {
        if ((!rightDoor || leftDoor) && i >= 0) {
            auto [j, k] = dirStep(facing);
            double d0 = hit.x, d1 = hit.z;
            bool   leftSide = (j >= 0 || !(d1 < 0.5)) && (j <= 0 || !(d1 > 0.5)) && (k >= 0 || !(d0 > 0.5))
                           && (k <= 0 || !(d0 < 0.5));
            return !leftSide;
        }
        return false;
    }
    return true;
}

/// Completes a placement once the block is in the world (Java getStateForPlacement parts that
/// traits can't express). `face`/`hit`: the click, `hit` relative to the placed block.
void finishPlacement(BlockSource& region, BlockPos const& pos, Player& player, int face, Vec3 hit) {
    auto const& b = region.getBlock(pos);
    auto        k = kindOf(b);
    std::string look(lookDirection(player.getRotation().y));

    switch (k) {
    case Kind::VerticalSlab:
        setIfChanged(
            region,
            pos,
            b,
            bs::with(
                b,
                {
                    {"lepton:type", verticalSlabType(face, hit)}
        }
            )
        );
        break;
    case Kind::Stairs:
    case Kind::FenceGate:
        setIfChanged(
            region,
            pos,
            b,
            bs::with(
                b,
                {
                    {kFacing, look}
        }
            )
        );
        updateShape(region, pos);
        break;
    case Kind::Trapdoor: {
        // Java TrapDoorBlock#getStateForPlacement
        bool        side   = face >= 2;
        std::string facing = side ? std::string(faceName(face)) : std::string(opposite(look));
        std::string half   = side ? (hit.y > 0.5f ? "top" : "bottom") : (face == 1 ? "bottom" : "top");
        setIfChanged(
            region,
            pos,
            b,
            bs::with(
                b,
                {
                    {kFacing, facing},
                    {kHalf,   half  }
        }
            )
        );
        break;
    }
    case Kind::Door: {
        auto above = neighbor(pos, "up");
        if (!game::replaceable(region, above)) {
            game::clearBlock(
                region,
                pos
            ); // no room for the upper half (the game keeps the item: creative or refund below)
            if (!player.isCreative()) {
                ItemStack item = player.getSelectedItem();
                if (item.isNull()) {
                    item.reinit(nameOf(b), 1, 0);
                    player.setSelectedItem(item);
                } else if (item.getTypeName() == nameOf(b)) {
                    item.add(1);
                    player.setSelectedItem(item);
                }
                player.sendInventory(false);
            }
            break;
        }
        bool        hinge = doorHingeRight(region, pos, look, b, hit);
        auto const* lower = bs::with(
            b,
            {
                {kFacing,              look },
                {"lepton:upper",       false},
                {"lepton:hinge_right", hinge},
                {"lepton:open",        false}
        }
        );
        auto const* upper = bs::with(
            b,
            {
                {kFacing,              look },
                {"lepton:upper",       true },
                {"lepton:hinge_right", hinge},
                {"lepton:open",        false}
        }
        );
        if (lower && upper) {
            game::setBlock(region, above, *upper); // upper first: the lower's update must see it
            game::setBlock(region, pos, *lower);
        }
        break;
    }
    default:
        queueUpdate(region, pos);
        break;
    }
}

// A door needs room for its upper half (Java refuses the placement otherwise).
void onPlacing(ll::event::PlayerPlacingBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto const& item = player.getSelectedItem();
    if (item.isNull()) return;
    auto const* block = bs::of(item.getTypeName());
    if (!block || kindOf(*block) != Kind::Door) return;
    if (!game::replaceable(player.getDimensionBlockSource(), neighbor(ev.pos(), "up"))) ev.cancel();
}

// PlayerPlacedBlockEvent comes from the "try place" gameplay event, before the block is in the
// world: the click is captured now and the placement finished on the next tick.
void onPlaced(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide()) return;
    auto k = kindOf(ev.placedBlock());
    if (k == Kind::None) return;
    auto pos   = ev.pos();
    auto id    = player.getOrCreateUniqueID().rawID;
    auto it    = st().clicks.find(id);
    bool click = it != st().clicks.end() && st().tick - it->second.tick <= 2;
    int  face  = click ? it->second.face : 1;
    Vec3 hit   = click ? it->second.hit : Vec3{0.5f, 1.f, 0.5f};
    if (click) {
        // relative to the placed block instead of the clicked one
        auto const& c = it->second.clicked;
        hit           = Vec3{
            hit.x + static_cast<float>(c.x - pos.x),
            hit.y + static_cast<float>(c.y - pos.y),
            hit.z + static_cast<float>(c.z - pos.z)
        };
    }
    int         dim  = static_cast<int>(player.getDimensionBlockSource().getDimensionId());
    std::string name = ev.placedBlock().getTypeName();
    later(1, [dim, pos, id, face, hit, name] {
        auto* r = regionOf(dim);
        auto* p = playerById(id);
        if (!r || !p || nameOf(r->getBlock(pos)) != name) return;
        finishPlacement(*r, pos, *p, face, hit);
    });
}

void onBlockChanged(ll::event::BlockChangedEvent& ev) {
    auto& region = ev.blockSource();
    if (region.getLevel().isClientSide()) return;
    queueUpdate(region, ev.pos());
}

void scanPlates(Level& level) {
    auto& s = st();
    for (auto* a : level.getRuntimeActorList()) {
        if (!a || a->isClientSide()) continue;
        auto     feet   = a->getFeetPos();
        auto&    region = a->getDimensionBlockSource();
        BlockPos at{
            static_cast<int>(std::floor(feet.x)),
            static_cast<int>(std::floor(feet.y + 0.05f)),
            static_cast<int>(std::floor(feet.z))
        };
        if (!region.hasChunksAt(at, 0, false)) continue;
        auto const& b = region.getBlock(at);
        if (kindOf(b) != Kind::PressurePlate) continue;
        if (hasTag(b, "lepton:mobs_only") && !a->isPlayer() && !a->hasCategory(ActorCategory::Mob)) continue;
        if (hasTag(b, "lepton:players_only") && (!a->isPlayer() || static_cast<Player*>(a)->isSpectator())) continue;
        auto key      = globalPos(region, at);
        s.plates[key] = s.tick;
        if (!bs::getBool(b, "lepton:pressed").value_or(false)) {
            if (auto const* p = bs::with(
                    b,
                    {
                        {"lepton:pressed", true}
            }
                ))
                game::setBlock(region, at, *p);
        }
    }
    for (auto it = s.plates.begin(); it != s.plates.end();) {
        auto* region = regionOf(it->first.dim);
        if (!region) {
            it = s.plates.erase(it);
            continue;
        }
        auto        pos = game::blockPos(it->first);
        auto const& b   = region->getBlock(pos);
        if (kindOf(b) != Kind::PressurePlate) {
            it = s.plates.erase(it);
            continue;
        }
        if (s.tick - it->second > static_cast<uint64_t>(pressTicksOf(b))) {
            if (auto const* p = bs::with(
                    b,
                    {
                        {"lepton:pressed", false}
            }
                ))
                game::setBlock(*region, pos, *p);
            it = s.plates.erase(it);
            continue;
        }
        ++it;
    }
}

void onTick(ll::event::ServerLevelTickEvent& ev) {
    auto& s = st();
    ++s.tick;
    // scheduled work first (it may queue updates)
    if (!s.tasks.empty()) {
        std::vector<Task> due;
        for (auto it = s.tasks.begin(); it != s.tasks.end();) {
            if (it->due <= s.tick) {
                due.push_back(std::move(*it));
                it = s.tasks.erase(it);
            } else ++it;
        }
        for (auto& t : due) {
            try {
                t.fn();
            } catch (std::exception const& e) {
                log().error("shapes: task failed: {}", e.what());
            }
        }
    }
    // releases
    for (auto it = s.buttons.begin(); it != s.buttons.end();) {
        if (it->second > s.tick) {
            ++it;
            continue;
        }
        if (auto* region = regionOf(it->first.dim)) {
            auto        pos = game::blockPos(it->first);
            auto const& b   = region->getBlock(pos);
            if (kindOf(b) == Kind::Button)
                setIfChanged(
                    *region,
                    pos,
                    b,
                    bs::with(
                        b,
                        {
                            {"lepton:pressed", false}
                }
                    )
                );
        }
        it = s.buttons.erase(it);
    }
    if (s.tick % 2 == 0) scanPlates(ev.level());
    // neighbour updates (the updates themselves queue more; those run next tick)
    if (!s.queue.empty()) {
        auto queue = std::move(s.queue);
        s.queue.clear();
        for (auto const& gp : queue) {
            auto* region = regionOf(gp.dim);
            if (!region) continue;
            auto pos = game::blockPos(gp);
            if (pos.y < region->getMinHeight() || pos.y >= region->getMaxHeight()) continue;
            if (!region->hasChunksAt(pos, 0, false)) continue;
            updateShape(*region, pos);
        }
    }
    if (s.tick % 1200 == 0) {
        s.debounce.clear();
        std::erase_if(s.clicks, [&](auto const& kv) { return s.tick - kv.second.tick > 100; });
    }
}

} // namespace

// ---- public API ------------------------------------------------------------------------------

Kind kindOf(Block const& block) {
    auto const& name = block.getTypeName();
    if (!name.starts_with("lepton:")) return Kind::None;
    static constexpr std::pair<char const*, Kind> kTags[] = {
        {"lepton:stairs",         Kind::Stairs       },
        {"lepton:slab",           Kind::Slab         },
        {"lepton:vertical_slab",  Kind::VerticalSlab },
        {"lepton:wall",           Kind::Wall         },
        {"lepton:fence",          Kind::Fence        },
        {"lepton:fence_gate",     Kind::FenceGate    },
        {"lepton:pane",           Kind::Pane         },
        {"lepton:door",           Kind::Door         },
        {"lepton:trapdoor",       Kind::Trapdoor     },
        {"lepton:button",         Kind::Button       },
        {"lepton:pressure_plate", Kind::PressurePlate},
        {"lepton:pillar",         Kind::Pillar       },
    };
    for (auto const& [tag, kind] : kTags) {
        if (hasTag(block, tag)) return kind;
    }
    return Kind::None;
}

std::string_view kindName(Kind kind) {
    switch (kind) {
    case Kind::Stairs:
        return "stairs";
    case Kind::Slab:
        return "slab";
    case Kind::VerticalSlab:
        return "vertical_slab";
    case Kind::Wall:
        return "wall";
    case Kind::Fence:
        return "fence";
    case Kind::FenceGate:
        return "fence_gate";
    case Kind::Pane:
        return "pane";
    case Kind::Door:
        return "door";
    case Kind::Trapdoor:
        return "trapdoor";
    case Kind::Button:
        return "button";
    case Kind::PressurePlate:
        return "pressure_plate";
    case Kind::Pillar:
        return "pillar";
    default:
        return "none";
    }
}

bool isSlab(Block const& block) {
    auto k = kindOf(block);
    return k == Kind::Slab || k == Kind::VerticalSlab;
}

bool isDouble(Block const& block) {
    switch (kindOf(block)) {
    case Kind::Slab:
        return bs::getBool(block, "lepton:double").value_or(false);
    case Kind::VerticalSlab:
        return bs::getString(block, "lepton:type").value_or("") == "double";
    default:
        return false;
    }
}

std::string_view opposite(std::string_view dir) {
    if (dir == "up") return "down";
    if (dir == "down") return "up";
    int i = dirIndex(dir);
    return i < 0 ? std::string_view{} : kDir[(i + 2) & 3];
}
std::string_view clockwise(std::string_view dir) {
    int i = dirIndex(dir);
    return i < 0 ? std::string_view{} : kDir[(i + 1) & 3];
}
std::string_view counterClockwise(std::string_view dir) {
    int i = dirIndex(dir);
    return i < 0 ? std::string_view{} : kDir[(i + 3) & 3];
}

std::string_view lookDirection(float yaw) {
    // Java 2D data value from the yaw: 0 south, 1 west, 2 north, 3 east
    static constexpr std::string_view k2d[4] = {"south", "west", "north", "east"};
    return k2d[static_cast<int>(std::floor(yaw / 90.0f + 0.5f)) & 3];
}

std::string_view faceName(int face) {
    static constexpr std::string_view kFaces[6] = {"down", "up", "north", "south", "west", "east"};
    return face >= 0 && face < 6 ? kFaces[face] : std::string_view{};
}

BlockPos neighbor(BlockPos const& pos, std::string_view dir) {
    if (dir == "up") return {pos.x, pos.y + 1, pos.z};
    if (dir == "down") return {pos.x, pos.y - 1, pos.z};
    auto [dx, dz] = dirStep(dir);
    return {pos.x + dx, pos.y, pos.z + dz};
}

std::optional<std::string> facingOf(Block const& block) {
    if (auto f = bs::getString(block, kFacing)) return f;
    if (auto d = bs::getInt(block, "direction")) {
        // vanilla legacy direction: 0 south, 1 west, 2 north, 3 east
        static constexpr char const* kLegacy[4] = {"south", "west", "north", "east"};
        return std::string(kLegacy[*d & 3]);
    }
    return std::nullopt;
}

std::string verticalSlabType(int face, Vec3 const& hit) {
    if (face >= 2 && face <= 5) return std::string(faceName(face));
    // Quark: the half of the top/bottom face that was clicked
    double x = hit.x - 0.5, z = hit.z - 0.5;
    double angle = std::atan2(x, z) * -180.0 / 3.14159265358979323846;
    return std::string(opposite(lookDirection(static_cast<float>(angle))));
}

bool updateShape(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    switch (kindOf(b)) {
    case Kind::Stairs:
        return updateStairs(region, pos, b);
    case Kind::Fence:
        return updateFenceOrPane(region, pos, b, false);
    case Kind::Pane:
        return updateFenceOrPane(region, pos, b, true);
    case Kind::Wall:
        return updateWall(region, pos, b);
    case Kind::FenceGate:
        return updateGate(region, pos, b);
    case Kind::Door:
        return updateDoor(region, pos, b);
    case Kind::Trapdoor:
        return updateRedstone(region, pos, b, isPowered(region, pos));
    default:
        return false;
    }
}

void queueUpdate(BlockSource& region, BlockPos const& pos) {
    int   dim = static_cast<int>(region.getDimensionId());
    auto& q   = st().queue;
    q.insert(globalPos(dim, pos));
    for (auto d : {"north", "south", "west", "east", "up", "down"}) q.insert(globalPos(dim, neighbor(pos, d)));
}

bool isPowered(BlockSource& region, BlockPos const& pos) {
    for (auto d : {"north", "south", "west", "east", "up", "down"}) {
        auto const& b    = region.getBlock(neighbor(pos, d));
        auto const& name = nameOf(b);
        if (name == "minecraft:redstone_block" || name == "minecraft:redstone_torch"
            || name == "minecraft:powered_repeater" || name == "minecraft:powered_comparator") {
            return true;
        }
        if (name == "minecraft:lever" && bs::getBool(b, "open_bit").value_or(false)) return true;
        if (name.find("button") != std::string::npos && bs::getBool(b, "button_pressed_bit").value_or(false))
            return true;
        if (bs::getInt(b, "redstone_signal").value_or(0) > 0 && name != "minecraft:redstone_lamp") return true;
        if (name == "minecraft:observer" && bs::getBool(b, "powered_bit").value_or(false)) return true;
        if (auto k = kindOf(b);
            (k == Kind::Button || k == Kind::PressurePlate) && bs::getBool(b, "lepton:pressed").value_or(false)) {
            return true;
        }
    }
    return false;
}

bool setOpen(BlockSource& region, BlockPos const& pos, bool open, Player* by) {
    auto const& b = region.getBlock(pos);
    auto        k = kindOf(b);
    if (k != Kind::Door && k != Kind::Trapdoor && k != Kind::FenceGate) return false;
    if (bs::getBool(b, "lepton:open").value_or(false) == open) return false;
    if (k == Kind::Door) {
        bool        upper = bs::getBool(b, "lepton:upper").value_or(false);
        auto        lower = upper ? neighbor(pos, "down") : pos;
        auto        top   = neighbor(lower, "up");
        auto const& lb    = region.getBlock(lower);
        auto const& ub    = region.getBlock(top);
        if (auto const* n = bs::with(
                ub,
                {
                    {"lepton:open", open}
        }
            );
            n && nameOf(ub) == nameOf(b))
            game::setBlock(region, top, *n);
        if (auto const* n = bs::with(
                lb,
                {
                    {"lepton:open", open}
        }
            );
            n && nameOf(lb) == nameOf(b))
            game::setBlock(region, lower, *n);
        playSound(region, lower, open ? SoundEvent::DoorOpen : SoundEvent::DoorClose, by);
        return true;
    }
    bs::Changes ch{
        {"lepton:open", open}
    };
    if (k == Kind::FenceGate && open && by) {
        // Java: an opening gate swings away from the player
        auto look = lookDirection(by->getRotation().y);
        if (facingOf(b).value_or("") == opposite(look)) ch.emplace_back(kFacing, std::string(look));
    }
    auto const* n = bs::with(b, ch);
    if (!n || !game::setBlock(region, pos, *n)) return false;
    auto sound = k == Kind::Trapdoor ? (open ? SoundEvent::TrapdoorOpen : SoundEvent::TrapdoorClose)
                                     : (open ? SoundEvent::FenceGateOpen : SoundEvent::FenceGateClose);
    playSound(region, pos, sound, by);
    return true;
}

bool press(BlockSource& region, BlockPos const& pos) {
    auto const& b = region.getBlock(pos);
    auto        k = kindOf(b);
    if (k != Kind::Button && k != Kind::PressurePlate) return false;
    auto key = globalPos(region, pos);
    if (k == Kind::PressurePlate) st().plates[key] = st().tick;
    else st().buttons[key] = st().tick + static_cast<uint64_t>(pressTicksOf(b));
    if (bs::getBool(b, "lepton:pressed").value_or(false)) return false; // already pressed (release rescheduled)
    auto const* n = bs::with(
        b,
        {
            {"lepton:pressed", true}
    }
    );
    return n && game::setBlock(region, pos, *n);
}

// ---- module ----------------------------------------------------------------------------------

class ShapesModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>(onBlockChanged));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacingBlockEvent>(onPlacing));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(onPlaced));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick));
        static bool registered = false;
        if (!registered) {
            registered = true;
            game::db::onWorldClosed([] { st() = State{}; }); // positions belong to the closed world
        }
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        st() = State{};
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::game::shapes

LP_REGISTER_MODULE_ORDERED(lp::game::shapes::ShapesModule, lp::ModuleOrder::Services);
