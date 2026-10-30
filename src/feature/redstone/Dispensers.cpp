// U10 redstone: DispensersPlaceBlocks. A dispenser that would throw out a block item (no dispense
// behaviour, or its behaviour failed) places the block in front instead; a block that can't be placed
// stays in the dispenser (fail click). Hooked at DispenserBlock::ejectItem, the vanilla "default
// behaviour" Bedrock dispensers fall back to.

#include "feature/redstone/Redstone.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/Container.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/DispenserBlock.h"
#include "mc/world/phys/AABB.h"

#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "mod/Log.h"

namespace lp::feature::redstone {

namespace bs = game::blockstates;

namespace {

int  gPlacements = 0;
bool gHooked     = false;

/// Items that plant / place a block without being a block item on Bedrock (Java ItemNameBlockItems).
std::string_view plantedBlock(std::string_view item) {
    static std::map<std::string_view, std::string_view> const k{
        {"minecraft:wheat_seeds",       "minecraft:wheat"            },
        {"minecraft:beetroot_seeds",    "minecraft:beetroot"         },
        {"minecraft:pumpkin_seeds",     "minecraft:pumpkin_stem"     },
        {"minecraft:melon_seeds",       "minecraft:melon_stem"       },
        {"minecraft:carrot",            "minecraft:carrots"          },
        {"minecraft:potato",            "minecraft:potatoes"         },
        {"minecraft:torchflower_seeds", "minecraft:torchflower_crop" },
        {"minecraft:pitcher_pod",       "minecraft:pitcher_crop"     },
        {"minecraft:nether_wart",       "minecraft:nether_wart"      },
        {"minecraft:sweet_berries",     "minecraft:sweet_berry_bush" },
        {"minecraft:glow_berries",      "minecraft:cave_vines"       },
        {"minecraft:redstone",          "minecraft:redstone_wire"    },
        {"minecraft:string",            "minecraft:trip_wire"        },
        {"minecraft:sugar_cane",        "minecraft:reeds"            },
        {"minecraft:kelp",              "minecraft:kelp"             },
        {"minecraft:cake",              "minecraft:cake"             },
        {"minecraft:flower_pot",        "minecraft:flower_pot"       },
        {"minecraft:repeater",          "minecraft:unpowered_repeater"},
        {"minecraft:comparator",        "minecraft:unpowered_comparator"},
        {"minecraft:hopper",            "minecraft:hopper"           },
        {"minecraft:cauldron",          "minecraft:cauldron"         },
        {"minecraft:brewing_stand",     "minecraft:brewing_stand"    },
        {"minecraft:campfire",          "minecraft:campfire"         },
        {"minecraft:soul_campfire",     "minecraft:soul_campfire"    },
        {"minecraft:chain",             "minecraft:chain"            },
        {"minecraft:iron_chain",        "minecraft:iron_chain"       },
    };
    auto it = k.find(item);
    return it == k.end() ? std::string_view{} : it->second;
}

/// Java's optional dispense behaviours of block items (kept as they are when !wrapExistingBehaviors).
bool hasOptionalBehaviour(std::string_view item) {
    return item.ends_with("shulker_box") || item == "minecraft:carved_pumpkin" || item.ends_with("_skull")
        || item.ends_with("_head") || item == "minecraft:skull";
}

bool blacklisted(std::string_view blockId, std::string_view itemId) {
    for (auto const& e : cfg().blacklist) {
        if (e == blockId || e == itemId) return true;
        // Java ids of the default list that differ on Bedrock (flowing / still liquids)
        if (e == "minecraft:water" && (blockId == "minecraft:flowing_water")) return true;
        if (e == "minecraft:lava" && (blockId == "minecraft:flowing_lava")) return true;
    }
    return false;
}

std::string horizontalName(int face) { return std::string(faceToName(face)); }

/// The block as a player looking along the dispenser's direction would place it (Quark's
/// NotStupidDirectionalPlaceContext: blocks face back towards the dispenser, stairs turned around).
Block const* orient(Block const& block, int face) {
    bool         horizontal = face >= 2;
    int          towards    = horizontal ? opposite(face) : 3; // Java: horizontal direction NORTH -> facing SOUTH
    Block const* b          = &block;
    auto         apply      = [&](std::string const& state, bs::Value const& v) {
        if (!bs::get(*b, state)) return;
        if (auto const* n = bs::with(*b, {{state, v}})) b = n;
    };
    static constexpr char const* axes[3] = {"x", "y", "z"};
    int axis = face <= 1 ? 1 : face <= 3 ? 2 : 0;
    apply("pillar_axis", std::string(axes[axis]));
    apply("facing_direction", face);
    apply("minecraft:facing_direction", std::string(faceToName(face)));
    apply("minecraft:block_face", std::string(faceToName(face)));
    if (horizontal) {
        apply("minecraft:cardinal_direction", horizontalName(towards));
        // vanilla stairs: weirdo_direction 0 east, 1 west, 2 south, 3 north (the high side)
        static constexpr int weirdo[6] = {0, 0, 3, 2, 1, 0};
        apply("weirdo_direction", weirdo[towards]);
        // legacy "direction": 0 south, 1 west, 2 north, 3 east
        static constexpr int legacy[6] = {0, 0, 2, 0, 1, 3};
        apply("direction", legacy[towards]);
    }
    return b;
}

bool obstructed(BlockSource& region, BlockPos const& pos, Block const& b) {
    if (!hasCollision(region, pos, b)) return false;
    AABB box{
        Vec3{static_cast<float>(pos.x),     static_cast<float>(pos.y),     static_cast<float>(pos.z)    },
        Vec3{static_cast<float>(pos.x) + 1, static_cast<float>(pos.y) + 1, static_cast<float>(pos.z) + 1}
    };
    for (auto* a : game::entities::inBox(region, box)) {
        auto t = game::entities::typeOf(*a);
        if (t == "minecraft:item" || t == "minecraft:xp_orb" || t == "minecraft:arrow") continue;
        return true;
    }
    return false;
}

void placeSound(BlockSource& region, BlockPos const& pos, Block const& b) {
    auto level = ll::service::getLevel();
    if (!level) return;
    Player* any = nullptr;
    int     dim = dimOf(region);
    level->forEachPlayer([&](Player& p) {
        if (static_cast<int>(p.getDimensionId()) != dim) return true;
        any = &p;
        return false;
    });
    if (!any) return;
    Vec3 at{static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y) + 0.5f, static_cast<float>(pos.z) + 0.5f};
    level->broadcastSoundEvent(region, SharedTypes::Legacy::LevelSoundEvent::Place, at, static_cast<int>(b.mNetworkId),
                               ActorSoundIdentifier(*any), false, std::nullopt);
}

/// Places `block` at `pos` like BlockItem#place. False if it can't be placed there.
bool place(BlockSource& region, BlockPos const& pos, Block const& block, int face) {
    if (pos.y < region.getMinHeight() || pos.y >= region.getMaxHeight()) return false;
    if (!game::replaceable(region, pos)) return false;
    Block const* b = orient(block, face);
    bool upperPart = bs::get(*b, "upper_block_bit").has_value();
    if (upperPart) {
        BlockPos up = offset(pos, 1);
        if (up.y >= region.getMaxHeight() || !game::replaceable(region, up)) return false;
        if (auto const* lower = bs::with(*b, {{"upper_block_bit", false}})) b = lower;
    }
    if (!b->getBlockType().mayPlace(region, pos)) return false;
    // a lone lower half never survives; mayPlace already checked the support and the space above
    if (!upperPart && !b->canSurvive(region, pos)) return false;
    if (obstructed(region, pos, *b)) return false;
    if (upperPart) {
        // both halves first (no updates in between, a lone half would pop), then the neighbour updates
        auto const* upper = bs::with(*b, {{"upper_block_bit", true}});
        if (!upper) return false;
        BlockChangeContext ctx{};
        if (!region.setBlock(pos, *b, 2, nullptr, ctx)) return false;
        region.setBlock(offset(pos, 1), *upper, 2, nullptr, ctx);
        region.updateNeighborsAt(pos);
        region.updateNeighborsAt(offset(pos, 1));
    } else if (!game::setBlock(region, pos, *b)) return false;
    placeSound(region, pos, *b);
    return true;
}

LL_TYPE_STATIC_HOOK(
    DispenserEjectHook,
    ll::memory::HookPriority::Normal,
    DispenserBlock,
    static_cast<void (*)(BlockSource&, Vec3 const&, uchar, ItemStack const&, Container&, int, int)>(&DispenserBlock::ejectItem),
    void,
    BlockSource&     region,
    Vec3 const&      pos,
    uchar            face,
    ItemStack const& item,
    Container&       container,
    int              slot,
    int              countLimit
) {
    auto const& c = cfg();
    if (!installed() || !c.enableDispensersPlaceBlocks || !isServerRegion(region) || game::isEmpty(item) || face > 5)
        return origin(region, pos, face, item, container, slot, countLimit);
    BlockPos front{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    BlockPos dispenser = offset(front, opposite(face));
    if (region.getBlock(dispenser).getTypeName() != "minecraft:dispenser") return origin(region, pos, face, item, container, slot, countLimit);
    auto         itemId = game::itemId(item);
    Block const* block  = nullptr;
    if (auto planted = plantedBlock(itemId); !planted.empty()) block = bs::of(planted);
    else if (item.mBlock && !item.mBlock->isAir()) block = item.mBlock;
    else block = bs::of(itemId); // items placing the block of the same id (doors, cakes, flower pots, ...)
    if (!block) return origin(region, pos, face, item, container, slot, countLimit);
    // beds need a placer for their head part and colour: Bedrock's own behaviour (throwing) stays
    if (bs::get(*block, "head_piece_bit")) return origin(region, pos, face, item, container, slot, countLimit);
    auto const& blockId = block->getTypeName();
    if (blacklisted(blockId, itemId) || (!c.wrapExistingBehaviors && hasOptionalBehaviour(itemId)))
        return origin(region, pos, face, item, container, slot, countLimit);
    if (place(region, front, *block, face)) {
        container.removeItem(slot, 1);
        ++gPlacements;
        log().info("redstone: dispenser placed {} at {} {} {}", blockId, front.x, front.y, front.z);
        return;
    }
    // Quark: a block that can't be placed stays in the dispenser
    levelEvent(region, static_cast<int>(SharedTypes::Legacy::LevelEvent::SoundClickFail), dispenser);
}

} // namespace

void installDispensers() {
    if (!gHooked) {
        DispenserEjectHook::hook();
        gHooked = true;
    }
}
void uninstallDispensers() {
    if (gHooked) {
        DispenserEjectHook::unhook();
        gHooked = false;
    }
}

int testing::dispenserPlacements() { return gPlacements; }

} // namespace lp::feature::redstone
