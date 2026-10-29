// `/quark cosmetics ...` (operators): previews used by the client e2e scenario and by server admins.
//   gallery          every animal skin, the pride bees, beach/ocean (zombie) villagers and a bound
//                    armor stand in rows in front of the player, each in an invisible barrier cage,
//                    with forced skins
//   buckets          one axolotl bucket per colour and a few tropical fish buckets in the inventory
//   pumpkin <bool>   wears a carved pumpkin, with Curse of Vanishing when true
//   carry            seats the nearest chicken on the player (as GrabChickens does)
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/Container.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"

#include "feature/cosmetics/Cosmetics.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/Log.h"

namespace lp::feature::cosmetics {

/// Parameters of `/quark cosmetics view <row>`.
struct CosmeticsViewParams {
    int row = 0;
};

/// Parameters of `/quark cosmetics pumpkin <cursed>` (command parameter types need external linkage).
struct CosmeticsPumpkinParams {
    bool cursed = false;
};

std::unordered_map<int64_t, int>& forcedSkins() {
    static std::unordered_map<int64_t, int> m;
    return m;
}

namespace {

namespace ent = lp::game::entities;

struct Entry {
    char const* type;
    int         skin; ///< forced SkinId (-1 = leave it to the rules)
    char const* tag = nullptr;
};

std::vector<std::vector<Entry>> rows() {
    std::vector<std::vector<Entry>> r;
    r.push_back({{"minecraft:cow", 0}, {"minecraft:cow", 1}, {"minecraft:cow", 2}, {"minecraft:cow", 3}, {"minecraft:cow", 4}, {"minecraft:cow", kShinySkin}});
    r.push_back({{"minecraft:pig", 0}, {"minecraft:pig", 1}, {"minecraft:pig", 2}, {"minecraft:pig", 3}, {"minecraft:pig", kShinySkin}});
    std::vector<Entry> chickens;
    for (int i = 0; i <= 6; ++i) chickens.push_back({"minecraft:chicken", i});
    chickens.push_back({"minecraft:chicken", kShinySkin});
    r.push_back(chickens);
    std::vector<Entry> allays;
    for (int i = 0; i <= 5; ++i) allays.push_back({"minecraft:allay", i});
    allays.push_back({"minecraft:allay", kShinySkin});
    r.push_back(allays);
    r.push_back({{"minecraft:rabbit", kShinySkin}, {"minecraft:llama", kShinySkin}, {"minecraft:dolphin", kShinySkin}, {"minecraft:slime", kShinySkin},
                 {"minecraft:armadillo", kShinySkin}, {"minecraft:frog", kShinySkin}});
    std::vector<Entry> bees;
    for (int i = 1; i <= static_cast<int>(kBeeVariants.size()); ++i) {
        bees.push_back({"minecraft:bee", i});
        if (bees.size() == 9) r.push_back(bees), bees.clear();
    }
    if (!bees.empty()) r.push_back(bees);
    r.push_back({{"minecraft:villager_v2", -1, kBeachTag.data()}, {"minecraft:villager_v2", -1, kOceanTag.data()},
                 {"minecraft:zombie_villager_v2", -1, kBeachTag.data()}, {"minecraft:zombie_villager_v2", -1, kOceanTag.data()},
                 {"minecraft:armor_stand", -1}});
    return r;
}

/// Where the last gallery was built (views are relative to it).
std::optional<std::pair<BlockPos, int>> gGallery;

/// An invisible barrier cage (walls two blocks high and a roof) around a mob's cell.
int cage(BlockSource& region, BlockPos const& at) {
    Block const* barrier = game::blockWith("minecraft:barrier");
    if (!barrier) return 0;
    int placed = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = 0; dy <= 2; ++dy) {
                if (dx == 0 && dz == 0 && dy < 2) continue;
                BlockPos p{at.x + dx, at.y + dy, at.z + dz};
                if (game::replaceable(region, p) && game::setBlock(region, p, *barrier)) ++placed;
            }
    return placed;
}

ItemStack enchanted(char const* id, Enchant::Type type) {
    auto                item = game::makeItem(id);
    EnchantmentInstance e;
    e.mEnchantType = type;
    e.mLevel       = 1;
    EnchantUtils::applyEnchant(item, e, false);
    return item;
}

ItemStack bucketWith(char const* id, std::vector<std::pair<char const*, int>> const& ints) {
    auto item = game::makeItem(id);
    if (item.isNull()) return item;
    auto tag = item.mUserData ? item.mUserData->clone() : std::make_unique<CompoundTag>();
    for (auto const& [k, v] : ints) tag->putInt(k, v);
    item.setUserData(std::move(tag));
    return item;
}

/// Lets a chicken ride a player while `carry` seats it (the vanilla player only takes parrots).
bool gSeating = false;
LL_TYPE_INSTANCE_HOOK(CarryHook, ll::memory::HookPriority::Low, Player, &Player::$canAddPassenger, bool, Actor& passenger) {
    if (gSeating && passenger.getEntityTypeId() == ActorType::Chicken) return true;
    return origin(passenger);
}

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("cosmetics").text("gallery").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = lp::commands::playerOf(origin);
        if (!p || !lp::commands::isAdmin(origin)) return output.error("Players with operator permission only.");
        auto&    region = p->getDimensionBlockSource();
        BlockPos feet   = game::blockPosOf(p->getPosition());
        feet.y          = static_cast<int>(std::floor(p->getAABB().min.y + 0.01f));
        int  dim        = static_cast<int>(p->getDimensionId());
        int  count      = 0;
        int  barriers   = 0;
        auto all        = rows();
        gGallery        = std::make_pair(feet, dim);
        for (size_t row = 0; row < all.size(); ++row) {
            for (size_t col = 0; col < all[row].size(); ++col) {
                auto const& e = all[row][col];
                BlockPos    cell{feet.x - 8 + static_cast<int>(col) * 2, feet.y, feet.z + 6 + static_cast<int>(row) * 3};
                barriers += cage(region, cell);
                Vec3   at{static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y), static_cast<float>(cell.z) + 0.5f};
                Actor* a = ent::spawn(dim, e.type, at);
                if (!a) continue;
                if (e.tag) a->addTag(e.tag);
                if (e.skin >= 0) forcedSkins()[ent::idOf(*a)] = e.skin;
                if (std::string_view(e.type) == "minecraft:armor_stand")
                    a->setArmor(SharedTypes::Legacy::ArmorSlot::Head, enchanted("minecraft:player_head", Enchant::Type::CurseOfBinding));
                applyTo(*a);
                ++count;
            }
        }
        log().info("cosmetics: gallery of {} mobs in {} rows ({} barriers)", count, all.size(), barriers);
        output.success("Spawned " + std::to_string(count) + " cosmetics mobs.");
    });

    cmd.overload<CosmeticsViewParams>().text("cosmetics").text("view").required("row").execute(
        [](CommandOrigin const& origin, CommandOutput& output, CosmeticsViewParams const& params) {
            auto* p = lp::commands::playerOf(origin);
            if (!p || !lp::commands::isAdmin(origin)) return output.error("Players with operator permission only.");
            if (!gGallery) return output.error("No gallery yet.");
            auto [base, dim] = *gGallery;
            // Behind the row, raised, looking south (+z) and down at it.
            Vec3 eye{static_cast<float>(base.x) + 0.5f, static_cast<float>(base.y) + 3.f, static_cast<float>(base.z + 6 + params.row * 3) - 6.f};
            p->teleport(eye, p->getDimensionId(), Vec2{25.f, 0.f});
            log().info("cosmetics: viewing gallery row {}", params.row);
            output.success("Viewing row " + std::to_string(params.row) + ".");
        }
    );

    cmd.overload().text("cosmetics").text("buckets").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = lp::commands::playerOf(origin);
        if (!p || !lp::commands::isAdmin(origin)) return output.error("Players with operator permission only.");
        std::vector<ItemStack> items;
        for (int v = 0; v < 5; ++v) items.push_back(bucketWith("minecraft:axolotl_bucket", {{"Variant", v}}));
        // Tropical fish: shape, pattern, base colour, pattern colour.
        int const fish[][4]{{0, 0, 1, 14}, {0, 3, 3, 4}, {1, 1, 0, 11}, {1, 5, 10, 5}};
        for (auto const& f : fish)
            items.push_back(bucketWith("minecraft:tropical_fish_bucket", {{"Variant", f[0]}, {"MarkVariant", f[1]}, {"Color", f[2]}, {"Color2", f[3]}}));
        auto& inv  = game::inv::playerInventory(*p);
        int   slot = 0;
        for (auto& it : items) {
            if (it.isNull()) continue;
            inv.setItem(slot++, it);
        }
        game::inv::sync(*p);
        log().info("cosmetics: gave {} buckets", slot);
        output.success("Gave " + std::to_string(slot) + " buckets.");
    });

    cmd.overload<CosmeticsPumpkinParams>().text("cosmetics").text("pumpkin").required("cursed").execute(
        [](CommandOrigin const& origin, CommandOutput& output, CosmeticsPumpkinParams const& params) {
            auto* p = lp::commands::playerOf(origin);
            if (!p || !lp::commands::isAdmin(origin)) return output.error("Players with operator permission only.");
            auto item = params.cursed ? enchanted("minecraft:carved_pumpkin", Enchant::Type::CurseOfVanishing) : game::makeItem("minecraft:carved_pumpkin");
            p->setArmor(SharedTypes::Legacy::ArmorSlot::Head, item);
            log().info("cosmetics: wearing a {}carved pumpkin", params.cursed ? "cursed " : "");
            output.success("Pumpkin on.");
        }
    );

    cmd.overload().text("cosmetics").text("carry").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = lp::commands::playerOf(origin);
        if (!p || !lp::commands::isAdmin(origin)) return output.error("Players with operator permission only.");
        auto& region = p->getDimensionBlockSource();
        auto  pos    = p->getPosition();
        AABB  box{Vec3{pos.x - 8, pos.y - 4, pos.z - 8}, Vec3{pos.x + 8, pos.y + 4, pos.z + 8}};
        Actor* chicken = nullptr;
        for (auto* a : ent::inBox(region, box, "minecraft:chicken"))
            if (!a->isBaby() && !a->getVehicle()) {
                chicken = a;
                break;
            }
        if (!chicken) chicken = ent::spawn(static_cast<int>(p->getDimensionId()), "minecraft:chicken", pos);
        if (!chicken) return output.error("No chicken.");
        CarryHook::hook();
        gSeating = true;
        bool ok  = chicken->startRiding(*p, true);
        gSeating = false;
        CarryHook::unhook();
        applyTo(*chicken);
        if (ok) {
            p->positionPassenger(*chicken);
            auto const& cp = chicken->getPosition();
            auto const& pp = p->getPosition();
            log().info("cosmetics: chicken at {:.2f} {:.2f} {:.2f} from the player's feet (yaw {:.0f})", cp.x - pp.x, cp.y - p->getAABB().min.y,
                       cp.z - pp.z, p->getRotation().y);
        }
        log().info("cosmetics: chicken carried: {}", ok);
        if (ok) output.success("Carrying a chicken.");
        else output.error("The chicken could not ride.");
    });
});

} // namespace

} // namespace lp::feature::cosmetics
