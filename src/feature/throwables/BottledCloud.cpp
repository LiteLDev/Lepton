// Bottled clouds (Quark BottledCloudModule): bottle a cloud at cloud height, place a temporary
// cloud block anywhere, build on it.

#include "feature/throwables/Throwables.h"

#include <functional>
#include <map>
#include <vector>

#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"

#include "core/GlobalPos.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"

namespace lp::feature::throwables::cloud {

namespace {

constexpr int         kLife  = 200;
constexpr char const* kDbKey = "lp:throwables:clouds";

struct State {
    std::map<core::GlobalPos, int>      clouds; ///< position -> ticks left
    std::vector<std::function<void()>>  later;
    std::map<int64_t, uint64_t>         lastUse;
    uint64_t                            tick  = 0;
    bool                                dirty = false;
};
State& st() {
    static State s;
    return s;
}

bool isCloud(Block const& b) { return b.getTypeName() == kCloud; }

int sizeFor(int ticksLeft) { return std::clamp((kLife - ticksLeft) / 20, 0, 9); }

/// Turns the first glass bottle from `start` on into a bottled cloud.
void fillBottle(Player& player, int start) {
    auto& pinv = game::inv::playerInventory(player);
    for (int i = std::max(0, start); i < pinv.getContainerSize(); ++i) {
        auto s = pinv.getItem(i);
        if (game::itemId(s) != "minecraft:glass_bottle") continue;
        s.remove(1);
        pinv.setItem(i, s);
        giveOrDrop(player, game::makeItem(kBottledCloud));
        return;
    }
}

bool debounced(Player& player) {
    auto  id = player.getOrCreateUniqueID().rawID;
    auto& m  = st().lastUse;
    if (auto it = m.find(id); it != m.end() && st().tick - it->second < 4) return true;
    m[id] = st().tick;
    return false;
}

bool atCloudLevel(Player& player) {
    double y = player.getFeetPos().y;
    return y > cfg().cloudLevelBottom && y < cfg().cloudLevelTop;
}

void bottleCloud(int64_t pid) {
    auto* p = playerById(pid);
    if (!p) return;
    auto held = p->getSelectedItem();
    if (game::itemId(held) != "minecraft:glass_bottle") return;
    held.remove(1);
    setSelected(*p, held);
    giveOrDrop(*p, game::makeItem(kBottledCloud));
    log().info("throwables: cloud bottled by {}", p->getRealName());
}

bool placeCloud(int64_t pid) {
    auto* p = playerById(pid);
    if (!p) return false;
    auto held = p->getSelectedItem();
    if (game::itemId(held) != kBottledCloud) return false;
    float reach = isCreative(*p) ? 5.0f : 4.5f;
    auto  ray   = rayFromEyes(*p, reach, true);
    auto& region = p->getDimensionBlockSource();
    BlockPos pos{ray.x, ray.y, ray.z};
    if (ray.hit && !region.getBlock(pos).isAir()) {
        static int const dx[6] = {0, 0, 0, 0, -1, 1}, dy[6] = {-1, 1, 0, 0, 0, 0}, dz[6] = {0, 0, -1, 1, 0, 0};
        int f = std::clamp(ray.face, 0, 5);
        pos   = BlockPos{pos.x + dx[f], pos.y + dy[f], pos.z + dz[f]};
    }
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight() || !region.getBlock(pos).isAir()) return false;
    auto const* block = game::blockstates::of(kCloud, {{"lepton:size", 0}});
    if (!block || !game::setBlock(region, pos, *block)) return false;
    st().clouds[game::globalPos(region, pos)] = kLife;
    st().dirty                                = true;
    if (!isCreative(*p)) {
        held.remove(1);
        if (game::isEmpty(held)) setSelected(*p, game::makeItem("minecraft:glass_bottle"));
        else {
            setSelected(*p, held);
            giveOrDrop(*p, game::makeItem("minecraft:glass_bottle"));
        }
    }
    p->startItemCooldown(HashedString("lepton_bottled_cloud"), 10, true);
    log().info("throwables: cloud placed at {} {} {}", pos.x, pos.y, pos.z);
    return true;
}

} // namespace

int cloudCount() { return static_cast<int>(st().clouds.size()); }

bool onUse(Player& player, ItemStack& item) {
    if (!cfg().enableBottledCloud) return false;
    auto id  = game::itemId(item);
    auto pid = player.getOrCreateUniqueID().rawID;
    if (id == "minecraft:glass_bottle" && atCloudLevel(player)) {
        if (debounced(player)) return true;
        st().later.push_back([pid] { bottleCloud(pid); });
        return true;
    }
    if (id == kBottledCloud) {
        if (debounced(player)) return true;
        if (player.getItemCooldownLeft(HashedString("lepton_bottled_cloud")) > 0) return true;
        st().later.push_back([pid] { placeCloud(pid); });
        return true;
    }
    return false;
}

bool onUseOnBlock(Player& player, ItemStack& item, BlockPos const& pos, int /*face*/) {
    if (!cfg().enableBottledCloud) return false;
    auto& region = player.getDimensionBlockSource();
    auto  id     = game::itemId(item);
    auto  pid    = player.getOrCreateUniqueID().rawID;
    if (!isCloud(region.getBlock(pos))) {
        // using the items on a block behaves as using them in the air (Quark's use())
        if (id == kBottledCloud || (id == "minecraft:glass_bottle" && atCloudLevel(player))) return onUse(player, item);
        return false;
    }
    if (debounced(player)) return true;
    auto gp = game::globalPos(region, pos);
    if (id == "minecraft:glass_bottle") {
        int slot = player.getSelectedItemSlot();
        st().later.push_back([pid, gp, slot] {
            auto* p = playerById(pid);
            auto* r = game::regionOf(gp.dim);
            if (!p || !r || !isCloud(r->getBlock(game::blockPos(gp)))) return;
            fillBottle(*p, slot);
            game::clearBlock(*r, game::blockPos(gp));
            st().clouds.erase(gp);
            st().dirty = true;
        });
        return true;
    }
    Block const* place = item.mBlock;
    if (!place || place->isAir()) return false;
    st().later.push_back([pid, gp, itemId = id] {
        auto* p = playerById(pid);
        auto* r = game::regionOf(gp.dim);
        if (!p || !r) return;
        BlockPos bp = game::blockPos(gp);
        if (!isCloud(r->getBlock(bp))) return;
        auto held = p->getSelectedItem();
        if (game::itemId(held) != itemId || !held.mBlock) return;
        Block const& b = *held.mBlock;
        if (!b.canSurvive(*r, bp)) return;
        game::setBlock(*r, bp, b);
        st().clouds.erase(gp);
        st().dirty = true;
        if (auto level = ll::service::getLevel()) {
            Vec3 c{bp.x + 0.5f, bp.y + 0.5f, bp.z + 0.5f};
            level->broadcastSoundEvent(*r, SharedTypes::Legacy::LevelSoundEvent::Place, c, b, ActorSoundIdentifier(*p), false, std::nullopt);
        }
        if (!isCreative(*p)) {
            held.remove(1);
            setSelected(*p, held);
            fillBottle(*p, 0);
        }
        log().info("throwables: block placed on a cloud");
    });
    return true;
}

void tick() {
    auto& s = st();
    ++s.tick;
    auto tasks = std::move(s.later);
    s.later.clear();
    for (auto& t : tasks) t();
    for (auto it = s.clouds.begin(); it != s.clouds.end();) {
        auto const& p      = it->first;
        auto*       region = game::regionOf(p.dim);
        BlockPos    bp     = game::blockPos(p);
        if (!region || !region->hasChunksAt(bp, 0, false)) {
            ++it;
            continue;
        }
        auto const& b = region->getBlock(bp);
        if (!isCloud(b)) {
            it      = s.clouds.erase(it);
            s.dirty = true;
            continue;
        }
        int left = --it->second;
        s.dirty  = true;
        if (left <= 0) {
            game::clearBlock(*region, bp);
            it = s.clouds.erase(it);
            continue;
        }
        if (left % 20 == 0) {
            int puffs = 10 - (kLife - left) / 20;
            for (int i = 0; i < puffs; ++i)
                particle("lepton:cloud_puff", Vec3{bp.x + float(randomDouble()), bp.y + float(randomDouble()), bp.z + float(randomDouble())}, p.dim);
        }
        int size = sizeFor(left);
        if (game::blockstates::getInt(b, "lepton:size").value_or(-1) != size) {
            if (auto const* n = game::blockstates::with(b, {{"lepton:size", size}})) game::setBlock(*region, bp, *n);
        }
        ++it;
    }
}

void onWorldReady() {
    st() = State{};
    auto v = game::db::load(kDbKey);
    if (!v || !v->is_array()) return;
    for (auto const& j : *v)
        if (j.is_array() && j.size() == 5)
            st().clouds[core::GlobalPos{j[0].get<int>(), j[1].get<int>(), j[2].get<int>(), j[3].get<int>()}] = j[4].get<int>();
}

void onWorldSave() {
    if (!game::db::available() || !st().dirty) return;
    auto arr = nlohmann::json::array();
    for (auto const& [p, t] : st().clouds) arr.push_back({p.dim, p.x, p.y, p.z, t});
    game::db::save(kDbKey, arr);
    st().dirty = false;
}

void onWorldClosed() { st() = State{}; }

} // namespace lp::feature::throwables::cloud

namespace lp::feature::throwables::cloud {
void ageAll(int ticksLeft) {
    for (auto& [p, t] : st().clouds) t = ticksLeft;
}
} // namespace lp::feature::throwables::cloud
