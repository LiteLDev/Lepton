// Abacus (Quark tools/AbacusModule): bind a block, then measure the Manhattan distance to the block
// the player looks at. Server side: binding, the count (action bar for vanilla clients,
// `gadgets:abacus` for mod clients) and the outline of the measured legs (particles only the
// holder sees). See docs/spec/gadgets.md.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/network/packet/SpawnParticleEffectPacket.h"
#include "mc/util/MolangScriptArg.h"
#include "mc/util/MolangVariableMap.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/HitResult.h"

#include "feature/gadgets/Gadgets.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "net/Net.h"

namespace lp::feature::gadgets::abacus {

namespace {

constexpr int kUnbound = -999; // Quark's DEFAULT_Y

struct Shown {
    int      count   = -2;
    uint64_t sentAt  = 0;
    uint64_t drawnAt = 0;
};

ll::event::ListenerPtr                 gUse;
std::unordered_map<int64_t, uint64_t>  gLastUse;
std::unordered_map<int64_t, Shown>     gShown;

bool isAbacus(ItemStackBase const& s) { return game::itemId(s) == kAbacus; }

std::optional<BlockPos> boundPos(ItemStackBase const& s) {
    auto y = getInt(s, "boundPosY");
    if (!y || *y == kUnbound) return std::nullopt;
    return BlockPos(getInt(s, "boundPosX").value_or(0), *y, getInt(s, "boundPosZ").value_or(0));
}

std::string label(int count) { return count < kAbacusMax ? std::to_string(count + 1) : std::to_string(kAbacusMax) + "+"; }

/// A MolangVariableMap (its default constructor is not exported: the members are built in place).
class VarMap {
public:
    VarMap() {
        auto* m = map();
        std::construct_at(&m->mMapFromVariableIndexToVariableArrayOffset);
        std::construct_at(&m->mVariables);
        m->mHasPublicVariables = false;
    }
    ~VarMap() { map()->~MolangVariableMap(); }
    VarMap(VarMap const&)            = delete;
    VarMap& operator=(VarMap const&) = delete;
    MolangVariableMap* map() { return reinterpret_cast<MolangVariableMap*>(mRaw); }
    void set(char const* name, float v) { map()->setMolangVariable(HashedString(name), MolangScriptArg(v)); }

private:
    alignas(MolangVariableMap) unsigned char mRaw[sizeof(MolangVariableMap)];
};

/// One edge from `p` to `q`: a thin `lepton:abacus_highlight` quad that only `player` sees, in the
/// configured highlight colour.
void segment(Player& player, Vec3 const& p, Vec3 const& q) {
    auto const& c = cfg().highlightColor;
    VarMap      vars;
    vars.set("variable.color_r", static_cast<float>(c.r));
    vars.set("variable.color_g", static_cast<float>(c.g));
    vars.set("variable.color_b", static_cast<float>(c.b));
    vars.set("variable.color_a", static_cast<float>(c.a));
    vars.set("variable.len", p.distanceTo(q) / 2);
    vars.set("variable.dir_x", q.x - p.x);
    vars.set("variable.dir_y", q.y - p.y);
    vars.set("variable.dir_z", q.z - p.z);
    Vec3 mid{(p.x + q.x) / 2, (p.y + q.y) / 2, (p.z + q.z) / 2};
    SpawnParticleEffectPacket pkt(SpawnParticleEffectPacketPayload(
        mid, "lepton:abacus_highlight", static_cast<uchar>(player.getDimensionId()), *vars.map()
    ));
    pkt.sendTo(player);
}

/// Draws the 12 edges of the box [a, b] (world coordinates).
int box(Player& player, Vec3 a, Vec3 b, int budget) {
    Vec3 lo{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    Vec3 hi{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    int  used = 0;
    auto line = [&](Vec3 p, Vec3 q) {
        if (used >= budget) return;
        segment(player, p, q);
        ++used;
    };
    float xs[2] = {lo.x, hi.x}, ys[2] = {lo.y, hi.y}, zs[2] = {lo.z, hi.z};
    for (float y : ys)
        for (float z : zs) line({lo.x, y, z}, {hi.x, y, z});
    for (float x : xs)
        for (float z : zs) line({x, lo.y, z}, {x, hi.y, z});
    for (float x : xs)
        for (float y : ys) line({x, y, lo.z}, {x, y, hi.z});
    return used;
}

/// Quark's highlight: the bound block plus the x, then y, then z leg towards the looked-at block.
void outline(Player& player, BlockPos const& bound, BlockPos const& target) {
    auto v      = [](BlockPos const& p) { return Vec3{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)}; };
    int  budget = 48;
    int  dx = target.x - bound.x, dy = target.y - bound.y, dz = target.z - bound.z;
    Vec3 b0 = v(bound);
    budget -= box(player, b0, b0 + Vec3{1, 1, 1}, budget);
    // expandTowards(d): the box grows by d on the side of the sign of d
    auto grow = [](Vec3 lo, Vec3 hi, Vec3 d) {
        if (d.x < 0) lo.x += d.x; else hi.x += d.x;
        if (d.y < 0) lo.y += d.y; else hi.y += d.y;
        if (d.z < 0) lo.z += d.z; else hi.z += d.z;
        return std::pair{lo, hi};
    };
    if (dx != 0) {
        auto [lo, hi] = grow(b0, b0 + Vec3{1, 1, 1}, Vec3{static_cast<float>(dx), 0, 0});
        budget -= box(player, lo, hi, budget);
    }
    if (dy != 0) {
        Vec3 o        = b0 + Vec3{static_cast<float>(dx), 0, 0};
        auto [lo, hi] = grow(o, o + Vec3{1, 1, 1}, Vec3{0, static_cast<float>(dy), 0});
        budget -= box(player, lo, hi, budget);
    }
    if (dz != 0) {
        Vec3 o        = b0 + Vec3{static_cast<float>(dx), static_cast<float>(dy), 0};
        auto [lo, hi] = grow(o, o + Vec3{1, 1, 1}, Vec3{0, 0, static_cast<float>(dz)});
        budget -= box(player, lo, hi, budget);
    }
}

void onInteract(ll::event::PlayerInteractBlockEvent& ev) {
    auto& player = ev.self();
    if (player.isClientSide() || !cfg().enableAbacus) return;
    if (!isAbacus(ev.item())) return;
    auto id = player.getOrCreateUniqueID().rawID;
    auto t  = now();
    if (auto it = gLastUse.find(id); it != gLastUse.end() && t - it->second < 5) return;
    gLastUse[id] = t;
    BlockPos pos = ev.blockPos();
    later(1, [id, pos] {
        if (auto* p = playerById(id)) onUse(*p, pos);
    });
}

} // namespace

int countFor(ItemStackBase const& stack, BlockPos const& target, bool targetIsAir) {
    auto pos = boundPos(stack);
    if (!pos || targetIsAir) return -1;
    int d = std::abs(target.x - pos->x) + std::abs(target.y - pos->y) + std::abs(target.z - pos->z);
    return std::clamp(d, 0, kAbacusMax);
}

void onUse(Player& player, BlockPos const& pos) {
    ItemStack s = player.getSelectedItem();
    if (!isAbacus(s)) return;
    if (boundPos(s)) {
        putInt(s, "boundPosX", 0);
        putInt(s, "boundPosY", kUnbound);
        putInt(s, "boundPosZ", 0);
    } else {
        putInt(s, "boundPosX", pos.x);
        putInt(s, "boundPosY", pos.y);
        putInt(s, "boundPosZ", pos.z);
    }
    player.setSelectedItem(s);
    game::inv::sync(player);
    log().info("gadgets: abacus {} by {}", boundPos(s) ? "bound" : "unbound", player.getRealName());
}

void install() {
    gUse = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerInteractBlockEvent>(onInteract);
}

void uninstall() {
    if (gUse) ll::event::EventBus::getInstance().removeListener(gUse);
    gUse.reset();
    gShown.clear();
    gLastUse.clear();
}

void tick() {
    if (now() % 2 != 0) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    bool enabled = cfg().enableAbacus;
    level->forEachPlayer([&](Player& p) {
        auto      id   = p.getOrCreateUniqueID().rawID;
        auto&     shown = gShown[id];
        ItemStack held = p.getSelectedItem();
        if (!isAbacus(held)) held = p.getOffhandSlot();
        int      count = -1;
        BlockPos target;
        if (enabled && isAbacus(held) && boundPos(held)) {
            float reach = p.isCreative() ? 6.0f : 5.0f;
            auto  hit   = p.traceRay(reach, false, true);
            if (hit.mType == HitResultType::Tile) {
                target = hit.mBlock;
                count  = countFor(held, target, p.getDimensionBlockSource().getBlock(target).isAir());
            }
        }
        bool changed = count != shown.count;
        if (net::hasClientMod(p)) {
            if (changed) net::sendToClient(p, AbacusMessage{count});
        } else if (count >= 0 && (changed || now() - shown.sentAt >= 30)) {
            actionBar(p, label(count));
            shown.sentAt = now();
        } else if (count < 0 && changed && shown.count >= 0) {
            actionBar(p, " ");
        }
        if (count >= 0 && (changed || now() - shown.drawnAt >= 10)) {
            outline(p, *boundPos(held), target);
            shown.drawnAt = now();
        }
        shown.count = count;
        return true;
    });
}

} // namespace lp::feature::gadgets::abacus
