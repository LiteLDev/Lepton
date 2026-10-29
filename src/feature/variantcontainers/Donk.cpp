// Donk chest (Quark VariantChestsModule#onClickEntity / onDeath / onEntityJoinWorld): a wooden
// variant chest equips a donkey, mule or llama like a vanilla chest and remembers which chest it
// was; when the animal dies, the chest it drops is that variant chest.

#include <unordered_map>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/phys/AABB.h"
#include "mc/world/gamemode/InteractionResult.h"

#include "feature/variantcontainers/VariantContainers.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::variantcontainers {

namespace {

std::string donkKey(int64_t id) { return "lp:variantcontainers:donk:" + std::to_string(id); }

bool isChestedHorse(Actor const& a) {
    auto t = game::entities::typeOf(a);
    return t == "minecraft:donkey" || t == "minecraft:mule" || t == "minecraft:llama" || t == "minecraft:trader_llama";
}

bool isWoodenVariantChest(ItemStack const& item) {
    if (game::isEmpty(item)) return false;
    auto const* b = game::blockstates::of(game::itemId(item));
    return b && b->hasTag(HashedString("lepton:wooden_chest"));
}

struct Death {
    int      dim;
    Vec3     pos;
    std::string snbt;
    uint64_t due;
};
std::vector<Death>& deaths() {
    static std::vector<Death> v;
    return v;
}
uint64_t gTick = 0;

LL_TYPE_INSTANCE_HOOK(DonkInteractHook, ll::memory::HookPriority::Normal, Player, &Player::interact, InteractionResult, Actor& actor, Vec3 const& location) {
    if (!isClientSide() && cfg().enableVariantChests && isChestedHorse(actor) && !isSneaking()) {
        ItemStack const& held = getSelectedItem();
        if (isWoodenVariantChest(held) && !actor.getStatusFlag(ActorFlags::Chested) && actor.getStatusFlag(ActorFlags::Tamed)
            && !actor.isBaby()) {
            ItemStack one = held;
            one.set(1);
            if (game::entities::triggerEvent(actor, "minecraft:on_chest")) {
                game::db::save(donkKey(game::entities::idOf(actor)), {{"item", game::itemToSnbt(one)}});
                if (!isCreative()) {
                    ItemStack rest = held;
                    rest.set(held.mCount - 1);
                    if (rest.mCount <= 0) rest = ItemStack{};
                    setSelectedItem(rest);
                    game::inv::sync(*this);
                }
                actor.playSound(actor.getDimensionBlockSource(), SharedTypes::Legacy::LevelSoundEvent::AddChest, actor.getPosition(), -1);
                log().info("variantcontainers: {} put {} on a {}", getRealName(), game::itemId(one), game::entities::typeOf(actor));
                InteractionResult r{};
                r.mSuccess = true;
                r.mSwing   = true;
                return r;
            }
        }
    }
    return origin(actor, location);
}

/// The chest item a dead animal drops becomes its remembered variant chest.
void replaceDrop(Death const& d) {
    auto* region = game::regionOf(d.dim);
    if (!region) return;
    AABB box{Vec3{d.pos.x - 3.f, d.pos.y - 3.f, d.pos.z - 3.f}, Vec3{d.pos.x + 3.f, d.pos.y + 3.f, d.pos.z + 3.f}};
    for (auto* a : game::entities::inBox(*region, box, "minecraft:item")) {
        auto& item = static_cast<ItemActor*>(a)->item();
        if (game::itemId(item) != "minecraft:chest") continue;
        auto variant = game::itemFromSnbt(d.snbt);
        if (game::isEmpty(variant)) return;
        variant.set(item.mCount);
        item = variant;
        log().info("variantcontainers: a dead animal dropped {}", game::itemId(variant));
        return;
    }
}

} // namespace

class DonkModule {
public:
    void install() {
        DonkInteractHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>([](ll::event::MobDieEvent& ev) {
            auto& mob = ev.self();
            if (mob.isClientSide() || !isChestedHorse(mob)) return;
            auto key = donkKey(game::entities::idOf(mob));
            auto j   = game::db::load(key);
            if (!j || !j->is_object()) return;
            game::db::erase(key);
            if (!mob.getStatusFlag(ActorFlags::Chested)) return;
            deaths().push_back({static_cast<int>(mob.getDimensionId()), mob.getPosition(), j->value("item", std::string{}), gTick + 1});
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            ++gTick;
            if (deaths().empty()) return;
            auto list = std::move(deaths());
            deaths().clear();
            for (auto& d : list) {
                if (d.due > gTick) deaths().push_back(d);
                else replaceDrop(d);
            }
        }));
        game::db::onWorldClosed([] { deaths().clear(); });
    }
    void uninstall() {
        DonkInteractHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::feature::variantcontainers

LP_REGISTER_MODULE(lp::feature::variantcontainers::DonkModule);
