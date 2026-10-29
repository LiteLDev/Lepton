// Server side of U18 cosmetics (docs/spec/cosmetics.md §3): synced skin ids for animal variants,
// carried chickens and bound armor stands, shiny sparkles, MoreVillagers types and boat trades.
#include "feature/cosmetics/Cosmetics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <unordered_set>
#include <vector>

#include "nlohmann/json.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/ActorInitializationMethod.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/deps/vanilla_components/ActorDataDirtyFlagsComponent.h"
#include "mc/entity/components/SynchedActorDataComponent.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/SynchedActorData.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include "game/Entities.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::cosmetics {

namespace {

constexpr auto kSkinId      = static_cast<ushort>(ActorDataIDs::SkinId);
constexpr auto kMarkVariant = static_cast<ushort>(ActorDataIDs::MarkVariant);

bool serverSide(Actor const& a) { return a.mLevel && !a.getLevel().isClientSide(); }

DataItem* dataItem(Actor const& a, ushort id) {
    auto&                                   w     = const_cast<Actor&>(a).mEntityData.get();
    SynchedActorDataComponent&              comp  = *w.mData;
    std::vector<std::unique_ptr<DataItem>>& items = *comp.mData->mItemsArray;
    if (id < items.size() && items[id] && items[id]->getId() == id) return items[id].get();
    for (auto& it : items)
        if (it && it->getId() == id) return it.get();
    return nullptr;
}

int readInt(Actor const& a, ushort id) {
    auto* item = dataItem(a, id);
    if (!item) return 0;
    auto v = item->getData<int>();
    return v ? *v : 0;
}

/// Writes an int data item and marks it dirty (sent to clients with the next actor data update).
/// `SynchedActorData::set<int>` is not exported, so this does the same by hand.
bool writeInt(Actor& a, ushort id, int value) {
    auto* item = dataItem(a, id);
    if (!item) return false;
    auto cur = item->getData<int>();
    if (!cur) return false;
    if (*cur == value) return true;
    item->setData(static_cast<int>(value));
    auto&                      w    = a.mEntityData.get();
    SynchedActorDataComponent&    comp  = *w.mData;
    ActorDataDirtyFlagsComponent& dirty = *w.mDirtyFlags;
    comp.mData->mDirtyFlags->set(id);
    dirty.mDirtyFlags->set(id);
    return true;
}

// ---- GrabChickens settings (U25's config/farming.json) -------------------------------------------

std::optional<FarmingView>& farmingOverride() {
    static std::optional<FarmingView> v;
    return v;
}
FarmingView& farmingCache() {
    static FarmingView v;
    return v;
}

void reloadFarming() {
    FarmingView v;
    try {
        std::ifstream in(lp::config::configDir() / "farming.json");
        if (in) {
            auto j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_object()) {
                v.enableGrabChickens = j.value("enableGrabChickens", true);
                v.chickenOffset      = j.value("chickenOffset", 0.0);
            }
        }
    } catch (...) {}
    farmingCache() = v;
}

// ---- villagers ------------------------------------------------------------------------------------

bool isVillager(Actor const& a) {
    auto t = a.getEntityTypeId();
    return t == ActorType::VillagerV2 || t == ActorType::ZombieVillagerV2;
}

/// Villagers spawned/born in the last ticks, checked against their biome on the next tick.
std::vector<int64_t>& newVillagers() {
    static std::vector<int64_t> v;
    return v;
}

void tagByBiome(Actor& villager) {
    if (villager.hasTag(std::string(kBeachTag)) || villager.hasTag(std::string(kOceanTag))) return;
    auto&       region = villager.getDimensionBlockSource();
    auto const& pos    = villager.getPosition();
    BlockPos    bp{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)), static_cast<int>(std::floor(pos.z))};
    auto        type = villagerTypeForBiome(region.getBiome(bp).mHash->getString());
    if (type.empty()) return;
    villager.addTag(std::string(type == "beach" ? kBeachTag : kOceanTag));
    log().info("cosmetics: {} villager at {} {} {}", type, bp.x, bp.y, bp.z);
}

// ---- the pass -------------------------------------------------------------------------------------

std::string customNameOf(Actor const& a) { return a.getNameTag(); }

int chickenFlags(Actor& chicken) {
    auto const fv = farmingView();
    if (!fv.enableGrabChickens) return 0;
    Actor* vehicle = chicken.getVehicle();
    if (!vehicle || !vehicle->isPlayer()) return 0;
    int flags = kCarriedFlag;
    if (vehicle->getPassengerIndex(chicken) > 0) flags |= kSecondSeat;
    flags += kOffsetUnit * chickenOffsetCode(fv.chickenOffset);
    return flags;
}

void sparkle(Actor& a, std::mt19937& rng) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    auto const&                            box   = a.getAABB();
    double                                 angle = u(rng) * 2 * 3.14159265358979;
    double                                 dist  = u(rng) * 0.5 + 0.25;
    double                                 x     = (box.min.x + box.max.x) / 2 + std::cos(angle) * dist;
    double                                 z     = (box.min.z + box.max.z) / 2 + std::sin(angle) * dist;
    double                                 y     = box.max.y + (u(rng) - 0.5) * 0.2;
    auto&                                  dim   = a.getDimension();
    a.getLevel().spawnParticleEffect(
        "minecraft:villager_happy",
        Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
        &dim
    );
}

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

ll::event::ListenerPtr gTick;
uint64_t               gTicks = 0;

} // namespace

FarmingView farmingView() { return farmingOverride() ? *farmingOverride() : farmingCache(); }

void setFarmingViewForTests(std::optional<FarmingView> v) {
    farmingOverride() = v;
    if (!v) reloadFarming();
}

int skinIdOf(Actor const& actor) { return readInt(actor, kSkinId); }
int markVariantOf(Actor const& actor) { return readInt(actor, kMarkVariant); }

bool isBoundPlayerHead(ItemStackBase const& item) {
    if (item.isNull()) return false;
    auto id = item.getTypeName();
    if (id != "minecraft:player_head" && !(id == "minecraft:skull" && item.getAuxValue() == 3)) return false;
    return EnchantUtils::getEnchantLevel(Enchant::Type::CurseOfBinding, item) > 0;
}

int villagerMarkVariant(Actor const& villager) {
    auto const& c = config();
    if (!c.enableMoreVillagers) return -1;
    if (c.beachVillager && villager.hasTag(std::string(kBeachTag))) return kBeachMarkVariant;
    if (c.oceanVillager && villager.hasTag(std::string(kOceanTag))) return kOceanMarkVariant;
    return -1;
}

bool applyTo(Actor& actor) {
    if (!serverSide(actor)) return false;
    auto type = actor.getEntityTypeId();
    switch (type) {
    case ActorType::Cow:
    case ActorType::Pig:
    case ActorType::Allay:
    case ActorType::Rabbit:
    case ActorType::Llama:
    case ActorType::Dolphin:
    case ActorType::Slime:
    case ActorType::Armadillo:
    case ActorType::Frog:
    case ActorType::Bee: {
        auto id   = game::entities::idOf(actor);
        auto f    = forcedSkins().find(id);
        int  skin = f != forcedSkins().end() ? f->second : animalSkin(actor.getTypeName(), id, customNameOf(actor), actor.isBaby());
        return writeInt(actor, kSkinId, skin);
    }
    case ActorType::Chicken: {
        auto id   = game::entities::idOf(actor);
        auto f    = forcedSkins().find(id);
        int  skin = f != forcedSkins().end() ? f->second : animalSkin("minecraft:chicken", id, {}, actor.isBaby());
        return writeInt(actor, kSkinId, skin | chickenFlags(actor));
    }
    case ActorType::ArmorStand: {
        auto const& c     = config();
        bool        bound = c.enableUsesForCurses && c.bindArmorStandsWithPlayerHeads
                  && isBoundPlayerHead(actor.getArmor(SharedTypes::Legacy::ArmorSlot::Head));
        return writeInt(actor, kSkinId, bound ? 1 : 0);
    }
    case ActorType::VillagerV2:
    case ActorType::ZombieVillagerV2: {
        int want = villagerMarkVariant(actor);
        int have = readInt(actor, kMarkVariant);
        if (want >= 0) return writeInt(actor, kMarkVariant, want);
        // Disabled type: fall back to the plains look.
        if (have == kBeachMarkVariant || have == kOceanMarkVariant) return writeInt(actor, kMarkVariant, 0);
        return false;
    }
    default:
        return false;
    }
}

namespace {

void pass(bool withSparkles) {
    auto level = ll::service::getLevel();
    if (!level) return;
    for (Actor* a : level->getRuntimeActorList()) {
        if (!a) continue;
        try {
            applyTo(*a);
            if (withSparkles) {
                auto t = a->getEntityTypeId();
                if (t == ActorType::Bee || t == ActorType::Slime) continue;
                if ((skinIdOf(*a) & kSkinMask) == kShinySkin && sparkles(a->getTypeName())) sparkle(*a, rng());
            }
        } catch (std::exception const& e) {
            log().error("cosmetics: pass failed for {}: {}", a->getTypeName(), e.what());
        }
    }
}

void onTick(ll::event::ServerLevelTickEvent&) {
    ++gTicks;
    if (!newVillagers().empty()) {
        auto ids = std::move(newVillagers());
        newVillagers().clear();
        auto const& c = config();
        for (auto id : ids) {
            if (!c.enableMoreVillagers) break;
            if (auto* v = game::entities::find(id)) {
                tagByBiome(*v);
                applyTo(*v);
            }
        }
    }
    if (gTicks % 10 == 0) pass(true);
}

LL_TYPE_INSTANCE_HOOK(
    MobInitHook,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::$initializeComponents,
    void,
    ::ActorInitializationMethod method,
    ::VariantParameterList const& params
) {
    origin(method, params);
    try {
        if (!serverSide(*this)) return;
        if (isVillager(*this) && (method == ActorInitializationMethod::Spawned || method == ActorInitializationMethod::Born))
            newVillagers().push_back(game::entities::idOf(*this));
        applyTo(*this);
    } catch (std::exception const& e) {
        log().error("cosmetics: init hook failed: {}", e.what());
    }
}

class CosmeticsServer {
public:
    void install() {
        reloadFarming();
        lp::config::onReload([] { reloadFarming(); });
        game::db::onWorldReady([] { reloadFarming(); });
        game::db::onWorldClosed([] {
            newVillagers().clear();
            forcedSkins().clear();
        });
        MobInitHook::hook();
        gTick = ll::event::EventBus::getInstance().emplaceListener<ll::event::ServerLevelTickEvent>(onTick);
    }
    void uninstall() {
        ll::event::EventBus::getInstance().removeListener(gTick);
        gTick.reset();
        MobInitHook::unhook();
    }
};

} // namespace

} // namespace lp::feature::cosmetics

LP_REGISTER_MODULE(lp::feature::cosmetics::CosmeticsServer);
