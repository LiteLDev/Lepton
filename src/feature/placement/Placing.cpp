// U17 placement: the item-use hook every block placement goes through (Item::useOn). It runs
// the Trowel, swaps the placed item for the selected variant (VariantSelector), records where
// blocks got placed (PlayerPlacedBlockEvent while the hook is active) and applies the rotation lock
// to them (LockRotation). Reacharound placements (use-item in the air) are started here too.

#include "feature/placement/Placement.h"

#include <algorithm>
#include <random>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemUsedOnEventContext.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"

#include "game/Blocks.h"
#include "game/Items.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::placement {

namespace {

thread_local int                    gDepth   = 0;       ///< nested Item::useOn calls of our hook
thread_local std::vector<BlockPos>* gCapture = nullptr; ///< placements seen while the hook runs

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

bool inList(std::vector<std::string> const& list, std::string const& id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

/// Quark's TrowelItem#isValidTarget (tags -> config lists).
bool trowelTarget(ItemStack const& stack) {
    auto id = game::itemId(stack);
    if (inList(cfg().trowelWhitelist, id)) return !game::isEmpty(stack);
    if (inList(cfg().trowelBlacklist, id)) return false;
    return !game::isEmpty(stack) && blockOfItem(stack) != nullptr;
}

/// After a placement by `player` at `pos`: the rotation lock.
void afterPlacement(Player& player, BlockPos const& pos, int face, Vec3 const& click, int aux) {
    auto profile = lockProfileOf(player);
    if (!profile) return;
    auto& region = player.getDimensionBlockSource();
    if (region.getBlock(pos).getTypeName().starts_with("minecraft:")) {
        applyLockVanilla(region, pos, player, *profile, face, click, aux);
        return;
    }
    // Lepton blocks: after game/Shapes finished the placement on the next tick.
    int  dim = static_cast<int>(region.getDimensionId());
    auto id  = player.getOrCreateUniqueID();
    auto p   = *profile;
    later(1, [dim, pos, id, p] {
        auto* r = game::regionOf(dim);
        if (!r) return;
        applyLockCustom(*r, pos, p);
        (void)id;
    });
}

/// The click relative to the clicked block, whether the game passed it absolute or relative.
Vec3 relativeClick(Vec3 const& hit, BlockPos const& clicked) {
    Vec3 rel{hit.x - static_cast<float>(clicked.x), hit.y - static_cast<float>(clicked.y), hit.z - static_cast<float>(clicked.z)};
    auto in = [](float v) { return v >= -0.01f && v <= 1.01f; };
    if (in(rel.x) && in(rel.y) && in(rel.z)) return rel;
    return hit;
}

using UseOnFn = std::function<InteractionResult(ItemStack&)>;

/// Quark's TrowelItem#useOn: a random hotbar block is used in the trowel's place.
InteractionResult useTrowel(Player& player, ItemStack& trowel, UseOnFn const& useWith, InteractionResult pass) {
    auto&            inv = player.getInventory();
    std::vector<int> targets;
    for (int i = 0; i < 9; ++i)
        if (trowelTarget(inv.getItem(i))) targets.push_back(i);
    if (targets.empty()) return pass;

    int       slot = targets[std::uniform_int_distribution<size_t>(0, targets.size() - 1)(rng())];
    ItemStack keep = trowel; // the hand slot is overwritten below
    ItemStack toPlace = inv.getItem(slot);
    std::string placedId = game::itemId(toPlace);

    player.setSelectedItem(toPlace);
    auto&             held = const_cast<ItemStack&>(player.getSelectedItem());
    InteractionResult res  = useWith(held);
    ItemStack         after = player.getSelectedItem();

    if (res.mSuccess) {
        auto tag = keep.mUserData ? keep.mUserData->clone() : std::make_unique<CompoundTag>();
        tag->putString("LastStack", placedId);
        keep.setUserData(std::move(tag));
        if (cfg().maxDamage > 0 && !player.isCreative()) keep.hurtAndBreak(1, &player);
        log().info("placement: trowel placed {}", placedId);
    }
    player.setSelectedItem(keep);
    inv.setItem(slot, after);
    trowel = keep;
    player.sendInventory(false);
    return res;
}

} // namespace

void notePlacement(BlockPos const& pos) {
    if (gCapture) gCapture->push_back(pos);
}

LL_TYPE_INSTANCE_HOOK(
    PlacementUseOnHook,
    ll::memory::HookPriority::Normal,
    Item,
    &Item::useOn,
    InteractionResult,
    ItemStack&             item,
    Actor&                 entity,
    BlockPos const&        pos,
    uchar                  face,
    HandSlot               hand,
    Vec3 const&            click,
    ItemUsedOnEventContext ctx
) {
    if (gDepth > 0 || entity.isClientSide() || !entity.isPlayer() || game::isEmpty(item))
        return origin(item, entity, pos, face, hand, click, ctx);
    auto& player = static_cast<Player&>(entity);

    if (game::itemId(item) == kHammer && variantsEnabled() && cfg().enableHammer) {
        if (useHammer(player, pos, face, relativeClick(click, pos))) {
            InteractionResult done{};
            done.mSuccess = true;
            done.mSwing   = true;
            return done;
        }
        return origin(item, entity, pos, face, hand, click, ctx);
    }

    // The trowel uses another item: that use comes back through this hook (depth 0 again).
    if (cfg().enableTrowel && game::itemId(item) == kTrowel) {
        InteractionResult pass{};
        return useTrowel(
            player,
            item,
            [&](ItemStack& stack) {
                auto* it = stack.mItem.get();
                return it ? it->useOn(stack, entity, pos, face, hand, click, ctx) : pass;
            },
            pass
        );
    }

    std::vector<BlockPos> placed;
    auto*                 prevCapture = gCapture;
    gCapture                          = &placed;
    ++gDepth;
    int               aux = item.getAuxValue();
    InteractionResult res{};
    try {
        std::string variant;
        if (variantsEnabled()) {
            if (auto const* b = blockOfItem(item)) {
                auto saved = savedVariant(player);
                if (!saved.empty()) variant = variantOf(b->getTypeName(), saved);
            }
        }
        ItemStack vs = variant.empty() ? ItemStack() : game::makeItem(variant, item.mCount);
        if (!game::isEmpty(vs) && vs.mItem.get()) {
            int before = vs.mCount;
            res        = vs.mItem->useOn(vs, entity, pos, face, hand, click, ctx);
            int used   = before - static_cast<int>(vs.mCount);
            if (used > 0 && !player.isCreative()) {
                if (used >= item.mCount) item = ItemStack();
                else item.remove(used);
            }
            if (!placed.empty()) log().info("placement: placed variant {}", variant);
        } else {
            res = origin(item, entity, pos, face, hand, click, ctx);
        }
    } catch (...) {
        --gDepth;
        gCapture = prevCapture;
        throw;
    }
    --gDepth;
    gCapture = prevCapture;
    for (auto const& p : placed) afterPlacement(player, p, face, click, aux);
    return res;
}

// ---- ReacharoundPlacing ----------------------------------------------------------------------

bool doReacharound(Player& player) {
    auto target = reacharoundTarget(player);
    if (!target) return false;
    GameMode* gm = player.mGameMode.get();
    if (!gm) return false;
    auto&       region = player.getDimensionBlockSource();
    auto const& before = region.getBlock(target->pos);
    auto&       held   = const_cast<ItemStack&>(player.getSelectedItem());
    std::string id     = game::itemId(held);
    Vec3        hit{0.5f, 1.0f, 0.5f}; // Quark: the target's centre + 0.5 y
    gm->useItemOn(held, target->pos, static_cast<uchar>(target->face), hit, HandSlot::Mainhand, &before, true);
    auto const& now = region.getBlock(target->pos);
    if (&now == &before) return false;
    playPlaceSound(region, target->pos, now, player);
    player.sendInventory(false);
    log().info(
        "placement: reacharound placed {} at {} {} {} ({})",
        now.getTypeName(),
        target->pos.x,
        target->pos.y,
        target->pos.z,
        target->face == Down ? "vertical" : "horizontal"
    );
    (void)id;
    return true;
}

class PlacingModule {
public:
    void install() {
        PlacementUseOnHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mPlaced   = bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([](ll::event::PlayerPlacedBlockEvent& ev) {
            if (!ev.self().isClientSide()) notePlacement(ev.pos());
        });
        mUse = bus.emplaceListener<ll::event::PlayerUseItemEvent>([](ll::event::PlayerUseItemEvent& ev) {
            auto& player = ev.self();
            if (player.isClientSide() || !cfg().enableReacharoundPlacing) return;
            if (doReacharound(player)) ev.cancel();
        });
    }
    void uninstall() {
        PlacementUseOnHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        bus.removeListener(mPlaced);
        bus.removeListener(mUse);
    }

private:
    ll::event::ListenerPtr mPlaced;
    ll::event::ListenerPtr mUse;
};

} // namespace lp::feature::placement

LP_REGISTER_MODULE(lp::feature::placement::PlacingModule);
