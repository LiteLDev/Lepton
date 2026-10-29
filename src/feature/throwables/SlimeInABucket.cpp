// Slime in a bucket (Quark SlimeInABucketModule): bucket tiny slimes, release them, and the bucket
// gets excited in slime chunks.

#include "feature/throwables/Throwables.h"

#include <cmath>

#include "mc/deps/core/math/Vec3.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/StringTag.h"
#include "mc/world/level/Level.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/monster/Slime.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"

#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/I18n.h"
#include "mod/Log.h"

namespace lp::feature::throwables::slime {

namespace ent = game::entities;

namespace {

constexpr char const* kTagData = "slime_nbt";

bool isBucketOfSlime(std::string_view id) { return id == kSlimeBucket || id == kSlimeBucketHot; }

bool isTiny(Actor& slime) {
    auto const& bb = slime.getAABB();
    return (bb.max.x - bb.min.x) < 0.8f;
}

/// `to` with the item id `id` and every other data of `from` (stored slime, name).
ItemStack retype(ItemStack const& from, std::string_view id) {
    auto out = game::makeItem(id);
    if (from.mUserData) out.setUserData(from.mUserData->clone());
    return out;
}

} // namespace

bool onInteract(Player& player, Actor& target) {
    if (!cfg().enableSlimeInABucket || target.getTypeName() != "minecraft:slime" || !target.isAlive()) return false;
    if (!isTiny(target)) return false;
    bool main = game::itemId(player.getSelectedItem()) == "minecraft:bucket";
    if (!main && game::itemId(player.getOffhandSlot()) != "minecraft:bucket") return false;
    ItemStack bucket = main ? player.getSelectedItem() : player.getOffhandSlot();

    auto out = game::makeItem(kSlimeBucket);
    auto tag = std::make_unique<CompoundTag>();
    CompoundTag saved;
    target.save(saved);
    tag->putCompound(kTagData, std::move(saved));
    auto name = target.getNameTag();
    if (!name.empty()) {
        CompoundTag display;
        display.putString("Name", trFor(player, "item.quark.slime_in_a_bucket.named", "Bucket of {}", name));
        tag->putCompound("display", std::move(display));
    }
    out.setUserData(std::move(tag));

    if (bucket.mCount == 1) {
        if (main) player.setSelectedItem(out);
        else player.setOffhandSlot(out);
        player.refreshInventory();
    } else {
        bucket.remove(1);
        if (main) player.setSelectedItem(bucket);
        else player.setOffhandSlot(bucket);
        giveOrDrop(player, out);
    }
    ent::despawn(target);
    log().info("throwables: slime bucketed by {}", player.getRealName());
    return true;
}

bool onUseOnBlock(Player& player, ItemStack& item, BlockPos const& pos, int face) {
    if (!isBucketOfSlime(game::itemId(item))) return false;
    if (!cfg().enableSlimeInABucket) return true;
    static int const dx[6] = {0, 0, 0, 0, -1, 1}, dy[6] = {-1, 1, 0, 0, 0, 0}, dz[6] = {0, 0, -1, 1, 0, 0};
    int  f = std::clamp(face, 0, 5);
    Vec3 at{pos.x + 0.5f + dx[f], pos.y + 0.5f + dy[f], pos.z + 0.5f + dz[f]};
    auto& region = player.getDimensionBlockSource();
    auto* slime  = ent::spawn(region, "minecraft:slime", at);
    if (!slime) return true;
    static_cast<Slime*>(slime)->_setSlimeSize(1);
    if (item.mUserData && item.mUserData->contains(kTagData, Tag::Compound)) {
        auto const& saved = (*item.mUserData)[kTagData];
        if (saved.contains("CustomName", Tag::String)) slime->setNameTag(std::string(saved["CustomName"].get<StringTag>()));
    }
    playSound("bucket.empty_water", at, 1.0f, 1.0f);
    if (!isCreative(player)) setSelected(player, game::makeItem("minecraft:bucket"));
    log().info("throwables: slime released by {}", player.getRealName());
    return true;
}

void tick() {
    static uint64_t n = 0;
    if (++n % 10 != 0 || !cfg().enableSlimeInABucket) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    level->forEachPlayer([](Player& p) {
        Vec3 pos     = p.getPosition();
        int  cx      = static_cast<int>(std::floor(pos.x)) >> 4;
        int  cz      = static_cast<int>(std::floor(pos.z)) >> 4;
        bool excited = isSlimeChunk(cx, cz);
        auto& pinv   = game::inv::playerInventory(p);
        bool  changed = false;
        for (int i = 0; i < pinv.getContainerSize(); ++i) {
            auto const& s  = pinv.getItem(i);
            auto        id = game::itemId(s);
            if (!isBucketOfSlime(id)) continue;
            auto want = excited ? kSlimeBucketHot : kSlimeBucket;
            if (id == want) continue;
            pinv.setItem(i, retype(s, want));
            changed = true;
        }
        auto off = p.getOffhandSlot();
        if (isBucketOfSlime(game::itemId(off)) && game::itemId(off) != (excited ? kSlimeBucketHot : kSlimeBucket)) {
            p.setOffhandSlot(retype(off, excited ? kSlimeBucketHot : kSlimeBucket));
            changed = true;
        }
        if (changed) p.refreshInventory();
        return true;
    });
}

} // namespace lp::feature::throwables::slime
