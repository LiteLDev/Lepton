// Feeding Trough (Quark automation/FeedingTroughModule): a 9-slot container; animals nearby walk
// to it, eat the food they breed with and sometimes fall in love. No XP from trough breeding.

#include <cmath>
#include <map>
#include <random>
#include <set>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/BlockChangedEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/entity/components_json_legacy/BreedableComponent.h"
#include "mc/entity/components_json_legacy/NavigationComponent.h"
#include "mc/entity/definitions/BreedableDefinition.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorDefinitionBreedItem.h"
#include "mc/world/actor/ActorEvent.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/navigation/PathNavigation.h"
#include "mc/world/item/ItemDescriptor.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"

#include "feature/logistics/Logistics.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Items.h"
#include "game/VirtualContainer.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::logistics {
namespace trough {

using core::GlobalPos;
constexpr int kSize = 9;

struct Pointer {
    GlobalPos pos;
    int       eatCooldown = 0;
    int       giveUp      = 20 * 20;
    int       navTick     = 0;
};

struct State {
    std::set<GlobalPos>         known;    ///< trough positions
    std::map<int64_t, Pointer>  pointers; ///< animal unique id -> trough it goes to
    std::set<int64_t>           troughLove; ///< animals put in love by a trough (no XP)
    std::map<GlobalPos, uint64_t> rng;
    std::mt19937_64             random{std::random_device{}()};
    bool                        loaded = false;
    int64_t                     tick   = 0;
};
State& st() {
    static State s;
    return s;
}
constexpr char const* kIndexKey = "lp:logistics:troughs";

void ensureLoaded() {
    auto& s = st();
    if (s.loaded || !game::db::available()) return;
    s.loaded = true;
    if (auto j = game::db::load(kIndexKey); j && j->is_array())
        for (auto const& k : *j)
            if (auto pos = k.is_string() ? parseKey(k.get<std::string>()) : std::nullopt) s.known.insert(*pos);
}

void saveIndex() {
    if (!st().loaded) return;
    nlohmann::json idx = nlohmann::json::array();
    for (auto const& p : st().known) idx.push_back(p.key());
    game::db::save(kIndexKey, idx);
}

/// FeedingTroughBlockEntity#setChanged: keeps lepton:full in sync with the contents.
void updateFull(GlobalPos const& p) {
    auto* r = loadedRegion(p);
    if (!r) return;
    auto        bp = game::blockPos(p);
    auto const& b  = r->getBlock(bp);
    if (game::blockId(b) != kFeedingTrough) return;
    bool full = false;
    for (auto const& s : vc::items(p, kSize))
        if (!game::isEmpty(s)) full = true;
    if (game::blockstates::getBool(b, "lepton:full").value_or(false) == full) return;
    if (auto const* n = game::blockstates::with(b, {{"lepton:full", full}})) game::setBlock(*r, bp, *n);
}

BreedableComponent* breedable(Actor& a) {
    auto c = a.getEntityContext().tryGetComponent<BreedableComponent>();
    return c ? c.as_ptr() : nullptr;
}

/// Animal#isFood: one of the animal's breed items.
bool isFood(Actor& a, ItemStack const& stack) {
    if (game::isEmpty(stack)) return false;
    auto* b = breedable(a);
    if (!b || !b->mStaticData) return false;
    for (auto const& item : b->mStaticData->mBreedItems.get())
        if (item.mItemDescriptor->sameItem(stack, false)) return true;
    return false;
}

/// Animal#canFallInLove && age == 0.
bool canFallInLove(Actor& a) {
    auto* b = breedable(a);
    return b && a.isAlive() && !a.isBaby() && b->mLoveTimer <= 0 && b->mBreedCooldown <= 0;
}

int foodSlot(Actor& a, std::vector<ItemStack> const& items) {
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (isFood(a, items[static_cast<size_t>(i)])) return i;
    return -1;
}

double specialRand(GlobalPos const& p) {
    auto& seed = st().rng[p];
    std::mt19937_64 r(seed);
    seed = r();
    return std::uniform_real_distribution<double>(0, 1)(r);
}

void setInLove(Actor& a) {
    auto* b = breedable(a);
    if (!b) return;
    b->mLoveTimer = 600;
    b->mLoveCause.get() = ActorUniqueID{};
    st().troughLove.insert(game::entities::idOf(a));
    if (auto level = ll::service::getLevel()) level->broadcastActorEvent(a, ActorEvent::InLoveHearts, 0, std::nullopt);
    BreedableComponent::_spawnLoveParticles(a);
}

enum class Fed { Fed, Secs, None };

/// FeedingTroughBlockEntity#tryFeedingAnimal.
Fed tryFeed(BlockSource& region, GlobalPos const& p, Actor& animal) {
    auto items = vc::items(p, kSize);
    int  slot  = foodSlot(animal, items);
    if (slot < 0) return Fed::None;
    auto& stack = items[static_cast<size_t>(slot)];
    int   data  = (static_cast<int>(stack.getId()) << 16) | (static_cast<int>(stack.getAuxValue()) & 0xffff);
    animal.playSound(region, SharedTypes::Legacy::LevelSoundEvent::Eat, animal.getPosition(), data);
    if (auto level = ll::service::getLevel()) level->broadcastActorEvent(animal, ActorEvent::Feed, data, std::nullopt);
    stack.set(stack.mCount - 1);
    if (stack.mCount <= 0) stack = ItemStack{};
    vc::setItems(p, items);
    updateFull(p);
    if (specialRand(p) < cfg().loveChance) {
        float r = static_cast<float>(cfg().range);
        AABB  box{Vec3{p.x - r, p.y - r, p.z - r}, Vec3{p.x + 1 + r, p.y + 1 + r, p.z + 1 + r}};
        int   animals = 0;
        for (auto* a : game::entities::inBox(region, box))
            if (a && breedable(*a)) ++animals;
        if (animals <= cfg().maxAnimals) setInLove(animal);
        return Fed::Secs;
    }
    return Fed::Fed;
}

bool hasFoodFor(GlobalPos const& p, Actor& a) { return foodSlot(a, vc::items(p, kSize)) >= 0; }

/// The animal can see the trough (Quark's clip from the eyes to the trough's floor).
bool canSee(BlockSource& region, Actor& a, GlobalPos const& p) {
    Vec3  from = a.getPosition();
    from.y += a.getAABB().max.y - a.getAABB().min.y - 0.15f;
    Vec3  to{p.x + 0.5f, p.y + 0.0625f, p.z + 0.5f};
    Vec3  d     = to - from;
    float len   = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    int   steps = static_cast<int>(len * 4) + 1;
    for (int i = 1; i < steps; ++i) {
        float    t = static_cast<float>(i) / static_cast<float>(steps);
        BlockPos bp{static_cast<int>(std::floor(from.x + d.x * t)), static_cast<int>(std::floor(from.y + d.y * t)),
                    static_cast<int>(std::floor(from.z + d.z * t))};
        if (bp.x == p.x && bp.y == p.y && bp.z == p.z) return true;
        auto const& b = region.getBlock(bp);
        if (b.isAir()) continue;
        AABB shape = b.getBlockType().getCollisionShape(b, region, bp, nullptr);
        if (shape.max.y - shape.min.y > 0 && shape.max.x - shape.min.x > 0) return false;
    }
    return true;
}

void walkTo(Actor& a, GlobalPos const& p) {
    if (!a.hasCategory(ActorCategory::Mob)) return;
    auto nav = a.getEntityContext().tryGetComponent<NavigationComponent>();
    if (!nav || !nav->mNavigation.get()) return;
    nav->mNavigation.get()->moveTo(*nav, static_cast<Mob&>(a), Vec3{p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}, 1.25f);
}

/// TroughPointer#find: the closest trough in range with food the animal wants.
std::optional<GlobalPos> find(Actor& a, int dim) {
    double r2   = cfg().range * cfg().range;
    auto   pos  = a.getPosition();
    double best = r2 + 1;
    std::optional<GlobalPos> out;
    for (auto const& p : st().known) {
        if (p.dim != dim) continue;
        double dx = p.x + 0.5 - pos.x, dy = p.y + 0.5 - pos.y, dz = p.z + 0.5 - pos.z;
        double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > r2 || d2 >= best) continue;
        if (idAt(p) != kFeedingTrough || !hasFoodFor(p, a)) continue;
        best = d2;
        out  = p;
    }
    return out;
}

/// TroughPointer#valid.
bool valid(Actor& a, Pointer& ptr, int dim) {
    if (!a.isAlive() || dim != ptr.pos.dim) return false;
    auto  pos = a.getPosition();
    double dx = ptr.pos.x - std::floor(pos.x), dy = ptr.pos.y - std::floor(pos.y), dz = ptr.pos.z - std::floor(pos.z);
    if (dx * dx + dy * dy + dz * dz > cfg().range * cfg().range) return false;
    if (ptr.eatCooldown == 1 || ptr.giveUp <= 0) return false;
    if (ptr.eatCooldown != 0) return true;
    return idAt(ptr.pos) == kFeedingTrough && hasFoodFor(ptr.pos, a);
}

void tickAnimals() {
    auto& s = st();
    if (s.known.empty() && s.pointers.empty()) return;
    // animals with a trough: every tick (Quark ticks the pointer on every tempt check)
    std::map<int64_t, std::pair<Actor*, int>> animals;
    for (auto it = s.pointers.begin(); it != s.pointers.end();) {
        auto* a = game::entities::find(it->first);
        if (!a) {
            it = s.pointers.erase(it);
            continue;
        }
        animals[it->first] = {a, game::globalPos(a->getDimensionBlockSource(), BlockPos{0, 0, 0}).dim};
        ++it;
    }
    // animals around troughs that hold food look for one: the broad scan runs every kScanTicks
    // ticks with the look chance compounded over those ticks
    constexpr int kScanTicks = 4;
    double        look       = std::clamp(cfg().lookChance * 20, 0.0, 1.0);
    double        lookScan   = 1 - std::pow(1 - look, kScanTicks);
    if (s.tick % kScanTicks == 0) {
        for (auto const& p : s.known) {
            auto* region = loadedRegion(p);
            if (!region) continue;
            if (game::blockIdAt(*region, game::blockPos(p)) != kFeedingTrough) continue;
            bool food = false;
            for (auto const& it : vc::items(p, kSize)) food = food || !game::isEmpty(it);
            if (!food) continue;
            float r = static_cast<float>(cfg().range);
            AABB  box{Vec3{p.x - r, p.y - r, p.z - r}, Vec3{p.x + 1 + r, p.y + 1 + r, p.z + 1 + r}};
            for (auto* a : game::entities::inBox(*region, box))
                if (a && breedable(*a)) animals.try_emplace(game::entities::idOf(*a), a, p.dim);
        }
    }
    std::uniform_real_distribution<double> unit(0, 1);
    for (auto& [id, entry] : animals) {
        auto [a, dim] = entry;
        if (!canFallInLove(*a)) {
            s.pointers.erase(id);
            continue;
        }
        auto it = s.pointers.find(id);
        if (it != s.pointers.end() && !valid(*a, it->second, dim)) {
            s.pointers.erase(it);
            it = s.pointers.end();
            continue;
        }
        if (it == s.pointers.end()) {
            if (s.tick % kScanTicks != 0 || unit(s.random) > lookScan) continue;
            auto found = find(*a, dim);
            if (!found) continue;
            it = s.pointers.emplace(id, Pointer{*found}).first;
        }
        auto& ptr    = it->second;
        auto* region = game::regionOf(ptr.pos.dim);
        if (!region) continue;
        // TroughPointer#tryEatingOrTickCooldown
        --ptr.giveUp;
        if (ptr.eatCooldown == 0) {
            float width = a->getAABB().max.x - a->getAABB().min.x;
            float feed  = 0.5f + width * 1.8f;
            auto  pos   = a->getPosition();
            float dx = ptr.pos.x + 0.5f - pos.x, dy = ptr.pos.y + 0.5f - pos.y, dz = ptr.pos.z + 0.5f - pos.z;
            if (dx * dx + dy * dy + dz * dz < feed * feed) {
                switch (tryFeed(*region, ptr.pos, *a)) {
                case Fed::Fed:
                    ptr.eatCooldown = std::max(1, cfg().cooldown);
                    log().info("feeding trough {} fed an animal", ptr.pos.key());
                    break;
                case Fed::Secs:
                    ptr.eatCooldown = 1;
                    log().info("feeding trough {} fed an animal (love chance rolled)", ptr.pos.key());
                    break;
                default:
                    break;
                }
            }
        } else {
            --ptr.eatCooldown;
        }
        if (ptr.eatCooldown == 0 && --ptr.navTick <= 0 && canSee(*region, *a, ptr.pos)) {
            ptr.navTick = 10;
            walkTo(*a, ptr.pos);
        }
    }
    // forget the trough-love marks once the love is over
    for (auto it = s.troughLove.begin(); it != s.troughLove.end();) {
        auto* a = game::entities::find(*it);
        auto* b = a ? breedable(*a) : nullptr;
        if (!b || (b->mLoveTimer <= 0 && s.tick % 200 == 0)) it = s.troughLove.erase(it);
        else ++it;
    }
}

/// Breeding caused by a trough gives no XP (FeedingTroughModule#onBreed / onOrbSpawn).
LL_TYPE_STATIC_HOOK(
    BreedXpHook,
    ll::memory::HookPriority::Normal,
    BreedableComponent,
    &BreedableComponent::_spawnExperienceOrbs,
    void,
    ::Actor& owner
) {
    auto& love = st().troughLove;
    if (!love.empty()) {
        if (love.contains(game::entities::idOf(owner))) return;
        if (auto partner = owner.mInLovePartner->rawID; partner != 0 && love.contains(partner)) return;
    }
    origin(owner);
}

/// Falling onto a full trough deals 20% of the fall damage (FeedingTroughBlock#fallOn).
LL_TYPE_INSTANCE_HOOK(
    TroughFallHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::handleFallDamage,
    void,
    float                fallDistance,
    float                multiplier,
    ::ActorDamageSource  source
) {
    if (cfg().enableFeedingTrough && !getDimensionBlockSource().getLevel().isClientSide()) {
        auto     pos = getPosition();
        BlockPos below{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y - 0.2f)), static_cast<int>(std::floor(pos.z))};
        auto const& b = getDimensionBlockSource().getBlock(below);
        if (game::blockId(b) == kFeedingTrough && game::blockstates::getBool(b, "lepton:full").value_or(false)) multiplier *= 0.2f;
    }
    origin(fallDistance, multiplier, std::move(source));
}

class TroughPort final : public Port {
public:
    explicit TroughPort(GlobalPos p) : mPos(p), mItems(vc::items(p, kSize)) {}
    int              size() const override { return kSize; }
    ItemStack const& get(int slot) const override { return mItems[static_cast<size_t>(slot)]; }
    void             set(int slot, ItemStack const& item) override {
        mItems[static_cast<size_t>(slot)] = item;
        vc::setItems(mPos, mItems);
        updateFull(mPos);
    }

private:
    GlobalPos              mPos;
    std::vector<ItemStack> mItems;
};

vc::Spec specFor() {
    vc::Spec spec;
    spec.size          = kSize;
    spec.screen        = vc::Screen::Dispenser;
    spec.titleKey      = "quark.container.feeding_trough";
    spec.titleFallback = "Feeding Trough";
    spec.onChanged     = [](GlobalPos const& p) { updateFull(p); };
    spec.canOpen       = [](Player&, GlobalPos const&) { return cfg().enableFeedingTrough; };
    return spec;
}

class TroughModule {
public:
    void install() {
        game::db::onWorldReady([] { ensureLoaded(); });
        game::db::onWorldSave([] { saveIndex(); });
        game::db::onWorldClosed([] {
            auto& s = st();
            s.known.clear();
            s.pointers.clear();
            s.troughLove.clear();
            s.rng.clear();
            s.loaded = false;
        });
        ensureLoaded();
        vc::bindBlock(kFeedingTrough, specFor());
        registerPort(kFeedingTrough, [](GlobalPos const& p) -> std::unique_ptr<Port> {
            if (!cfg().enableFeedingTrough) return nullptr;
            return std::make_unique<TroughPort>(p);
        });
        BreedXpHook::hook();
        TroughFallHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& ev) {
            if (ev.layer() != 0) return;
            auto& region = ev.blockSource();
            if (region.getLevel().isClientSide()) return;
            auto const& prev = ev.previousBlock().getTypeName();
            auto const& now  = ev.newBlock().getTypeName();
            if (prev == now) return;
            auto pos = game::globalPos(region, ev.pos());
            if (now == kFeedingTrough) st().known.insert(pos);
            else if (prev == kFeedingTrough) st().known.erase(pos);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            if (!cfg().enableFeedingTrough) return;
            ensureLoaded();
            ++st().tick;
            tickAnimals();
        }));
    }
    void uninstall() {
        vc::unbindBlock(kFeedingTrough);
        unregisterPort(kFeedingTrough);
        BreedXpHook::unhook();
        TroughFallHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        saveIndex();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace trough

bool trough::open(Player& player, core::GlobalPos const& pos) {
    if (!cfg().enableFeedingTrough || idAt(pos) != kFeedingTrough) return false;
    return vc::open(player, pos, specFor());
}

} // namespace lp::feature::logistics

LP_REGISTER_MODULE(lp::feature::logistics::trough::TroughModule);
