// Quark's TotemOfHolding module (docs/spec/backpack.md §3.2): on death the player's drops go into
// a floating `lepton:totem` entity (its inventory component holds them, saved with the entity);
// hitting it gives them back; Dark Souls mode; the dying animation.

#include "feature/backpack/Backpack.h"

#include <map>
#include <random>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerAttackEvent.h"
#include "ll/api/command/CommandHandle.h"

#include "mc/server/ServerPlayer.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include "game/Blocks.h"
#include "game/Entities.h"
#include "mod/Commands.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"

namespace lp::feature::backpack {

namespace {

using SharedTypes::Legacy::ArmorSlot;
constexpr int kDeathTime = 40; // Quark's TotemOfHoldingEntity.DEATH_TIME

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

Container* containerOf(Actor& actor) {
    auto comp = actor.getEntityContext().tryGetComponent<ContainerComponent>();
    return comp ? &comp.as_ptr()->mContainer.get() : nullptr;
}

std::string playerKey(std::string const& uuid) { return "lp:backpack:player:" + uuid; }
std::string totemKey(int64_t id) { return "lp:backpack:totem:" + std::to_string(id); }

std::map<std::string, int64_t>& lastTotems() { ///< player uuid -> last totem id (cache of the DB)
    static std::map<std::string, int64_t> m;
    return m;
}
std::map<int64_t, std::string>& owners() { ///< totem id -> owner uuid (cache of the DB)
    static std::map<int64_t, std::string> m;
    return m;
}
std::map<int64_t, int>& dying() { ///< totem id -> death ticks
    static std::map<int64_t, int> m;
    return m;
}

int64_t lastTotemOf(std::string const& uuid) {
    if (auto it = lastTotems().find(uuid); it != lastTotems().end()) return it->second;
    int64_t id = 0;
    if (auto j = game::db::load(playerKey(uuid)); j && j->is_object()) {
        try {
            id = std::stoll(j->value("lastTotem", std::string("0")));
        } catch (...) {}
    }
    return lastTotems()[uuid] = id;
}

void recordDeath(Player& player, int64_t totem) {
    auto uuid = player.getUuid().asString();
    lastTotems()[uuid] = totem;
    auto pos = player.getFeetPos();
    game::db::save(
        playerKey(uuid),
        {
            {"lastTotem", totem ? std::to_string(totem) : std::string()},
            {"deathX",    static_cast<int>(std::floor(pos.x))           },
            {"deathZ",    static_cast<int>(std::floor(pos.z))           },
            {"deathDim",  static_cast<int>(player.getDimensionId())     },
    }
    );
}

Player* onlinePlayer(std::string const& uuid) {
    auto level = ll::service::getLevel();
    return level && !uuid.empty() ? level->getPlayer(mce::UUID::fromString(uuid)) : nullptr;
}

bool keepInventory() {
    auto level = ll::service::getLevel();
    if (!level) return false;
    GameRuleId id;
    id.mValue = static_cast<int>(GameRules::GameRulesIndex::KeepInventory);
    return level->getGameRules().getBool(id, false);
}

/// Quark: `enableOnPK || !(killer instanceof Player) || killer == player`.
bool allowedFor(Player& player, ActorDamageSource const& source) {
    if (cfg().enableOnPK) return true;
    auto id = source.getDamagingEntityUniqueID();
    if (id.rawID == -1 || id.rawID == 0) id = source.getEntityUniqueID();
    if (id.rawID == -1 || id.rawID == 0) return true;
    auto  level  = ll::service::getLevel();
    auto* killer = level ? level->fetchEntity(id, false) : nullptr;
    if (!killer || !killer->isPlayer()) return true;
    return killer == &player;
}

void spawnStack(BlockSource& region, ItemStack const& stack, Vec3 const& pos) {
    if (game::isEmpty(stack)) return;
    if (auto level = ll::service::getLevel()) level->getSpawner().spawnItem(region, stack, nullptr, pos, 0);
}

/// Armour slot of a vanilla armour piece / the backpack (Quark: `instanceof ArmorItem`).
std::optional<ArmorSlot> armorSlotOf(ItemStack const& stack) {
    if (game::isEmpty(stack)) return std::nullopt;
    auto id = stack.getTypeName();
    if (id == kBackpack) return ArmorSlot::Torso;
    if (!id.starts_with("minecraft:")) return std::nullopt;
    if (id.ends_with("_helmet")) return ArmorSlot::Head;
    if (id.ends_with("_chestplate")) return ArmorSlot::Torso;
    if (id.ends_with("_leggings")) return ArmorSlot::Legs;
    if (id.ends_with("_boots")) return ArmorSlot::Feet;
    return std::nullopt;
}

bool hasBinding(ItemStack const& stack) {
    return !game::isEmpty(stack) && EnchantUtils::getEnchantLevel(Enchant::Type::CurseOfBinding, stack) > 0;
}

void particles(Actor& totem, int count) {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto  pos = totem.getPosition();
    auto* dim = &totem.getDimension();
    for (int i = 0; i < std::min(count, 16); ++i) {
        level->spawnParticleEffect("minecraft:critical_hit_emitter", {pos.x, pos.y + 0.5f, pos.z}, dim);
        level->spawnParticleEffect("minecraft:enchanting_table_particle", {pos.x, pos.y + 0.5f, pos.z}, dim);
    }
}

void dropEverythingAndDie(Actor& totem) {
    auto id = game::entities::idOf(totem);
    if (auto* c = containerOf(totem)) {
        if (!cfg().destroyLostItems) {
            for (auto const& s : game::inv::snapshot(*c, game::inv::all(*c)))
                spawnStack(totem.getDimensionBlockSource(), s, totem.getPosition());
        }
        c->removeAllItems();
    }
    if (auto* owner = onlinePlayer(totemOwner(id))) {
        owner->sendMessage(
            "§l" + trFor(*owner, "quark.misc.totem_darksouls", "You feel a sense of dread as a previous Totem of Holding shatters")
        );
    }
    log().info("totem: {} shattered (dark souls)", id);
    owners().erase(id);
    dying().erase(id);
    game::db::erase(totemKey(id));
    game::entities::despawn(totem);
}

void tickTotems() {
    for (auto* totem : game::entities::ofType(kTotem)) {
        auto id = game::entities::idOf(*totem);
        if (cfg().darkSoulsMode) {
            auto owner = totemOwner(id);
            if (auto* p = onlinePlayer(owner); p && lastTotemOf(owner) != id) {
                dropEverythingAndDie(*totem);
                continue;
            }
        }
        auto* c     = containerOf(*totem);
        bool  empty = !c || c->isEmpty();
        auto  it    = dying().find(id);
        if (empty && it == dying().end()) {
            game::entities::triggerEvent(*totem, "lepton:start_dying");
            it = dying().emplace(id, 0).first;
        }
        if (it != dying().end()) {
            if (it->second > kDeathTime) {
                dying().erase(it);
                owners().erase(id);
                game::db::erase(totemKey(id));
                game::entities::despawn(*totem);
                log().info("totem: {} faded", id);
            } else {
                ++it->second;
            }
        }
    }
}

/// Before the game drops anything: Quark's onPlayerDrops (lowest priority, so after other mods'
/// changes) moves the drops into the totem. keepInventory: no drops, "no totem" is recorded.
LL_TYPE_INSTANCE_HOOK(PlayerDieHook, ll::memory::HookPriority::Normal, ServerPlayer, &ServerPlayer::$die, void, ActorDamageSource const& source) {
    if (cfg().enableTotemOfHolding && !isDead()) {
        try {
            if (keepInventory()) {
                if (allowedFor(*this, source)) recordDeath(*this, 0);
            } else {
                spawnTotemFor(*this, source);
            }
        } catch (std::exception const& e) {
            log().error("totem: spawning failed: {}", e.what());
        }
    }
    origin(source);
}

} // namespace

int64_t lastTotemOf(Player const& player) { return lastTotemOf(player.getUuid().asString()); }

bool keepInventoryOn() { return keepInventory(); }

std::string totemOwner(int64_t totemId) {
    if (auto it = owners().find(totemId); it != owners().end()) return it->second;
    std::string owner;
    if (auto j = game::db::load(totemKey(totemId)); j && j->is_object()) owner = j->value("owner", std::string());
    return owners()[totemId] = owner;
}

std::vector<ItemStack> totemItems(Actor& totem) {
    std::vector<ItemStack> out;
    if (auto* c = containerOf(totem))
        for (auto& s : game::inv::snapshot(*c, game::inv::all(*c)))
            if (!game::isEmpty(s)) out.push_back(std::move(s));
    return out;
}

int64_t spawnTotemFor(Player& player, ActorDamageSource const& source) {
    if (player.isClientSide() || !cfg().enableTotemOfHolding || !allowedFor(player, source)) return 0;
    return spawnTotemNow(player);
}

int64_t spawnTotemNow(Player& player) {
    if (player.isClientSide() || !cfg().enableTotemOfHolding) return 0;
    flushBackpack(player);
    player.clearVanishEnchantedItemsOnDeath();

    // Drop order of Java's Inventory.dropAll: main inventory, armour, off-hand.
    std::vector<ItemStack> drops;
    auto&                  pinv = game::inv::playerInventory(player);
    for (int i = 0; i < game::inv::kPlayerSlots; ++i)
        if (!game::isEmpty(pinv.getItem(i))) drops.push_back(pinv.getItem(i));
    for (int a = 0; a < 4; ++a)
        if (!game::isEmpty(player.getArmor(static_cast<ArmorSlot>(a)))) drops.push_back(player.getArmor(static_cast<ArmorSlot>(a)));
    if (!game::isEmpty(player.getOffhandSlot())) drops.push_back(player.getOffhandSlot());

    if (drops.empty()) {
        recordDeath(player, 0);
        return 0;
    }
    auto& region = player.getDimensionBlockSource();
    auto  feet   = player.getFeetPos();
    Vec3  at{feet.x, std::max(static_cast<float>(region.getMinHeight() + 3), feet.y + 1), feet.z};
    auto* totem = game::entities::spawn(region, kTotem, at);
    auto* c     = totem ? containerOf(*totem) : nullptr;
    if (!c || c->getContainerSize() < static_cast<int>(drops.size())) {
        if (totem) game::entities::despawn(*totem);
        log().error("totem: could not spawn {} (behaviour pack missing?)", kTotem);
        return 0;
    }
    for (size_t i = 0; i < drops.size(); ++i) c->setItem(static_cast<int>(i), drops[i]);
    totem->setNameTag(player.getRealName());

    for (int i = 0; i < game::inv::kPlayerSlots; ++i) pinv.setItem(i, ItemStack{});
    for (int a = 0; a < 4; ++a) player.setArmor(static_cast<ArmorSlot>(a), ItemStack{});
    player.setOffhandSlot(ItemStack{});
    game::inv::sync(player);

    auto id   = game::entities::idOf(*totem);
    auto uuid = player.getUuid().asString();
    owners()[id] = uuid;
    game::db::save(totemKey(id), {{"owner", uuid}});
    recordDeath(player, id);
    log().info("totem: {} spawned for {} with {} stacks", id, player.getRealName(), drops.size());
    return id;
}

int collectFromTotem(Player& player, Actor& totem) {
    if (player.isClientSide() || !cfg().enableTotemOfHolding) return 0;
    auto id = game::entities::idOf(totem);
    if (!cfg().allowAnyoneToCollect && !player.isCreative() && totemOwner(id) != player.getUuid().asString()) return 0;
    auto* c = containerOf(totem);
    if (!c) return 0;
    std::vector<int> slots;
    for (int i = 0; i < c->getContainerSize(); ++i)
        if (!game::isEmpty(c->getItem(i))) slots.push_back(i);
    int drops = static_cast<int>(slots.size());
    if (!cfg().dropAllItemsOnHit) drops = std::min(drops, 3 + std::uniform_int_distribution<int>(0, 3)(rng()));

    auto& pinv = game::inv::playerInventory(player);
    for (int k = 0; k < drops; ++k) {
        ItemStack stack = c->getItem(slots[static_cast<size_t>(k)]);
        c->setItem(slots[static_cast<size_t>(k)], ItemStack{});
        bool placed = false;
        if (auto slot = armorSlotOf(stack)) {
            ItemStack curr = player.getArmor(*slot);
            if (game::isEmpty(curr)) {
                player.setArmor(*slot, stack);
                placed = true;
            } else if (!isItem(curr, kBackpack) && !hasBinding(curr) && hasBinding(stack)) {
                player.setArmor(*slot, stack);
                stack = curr;
            }
        } else if (isItem(stack, "minecraft:shield")) {
            if (game::isEmpty(player.getOffhandSlot())) {
                player.setOffhandSlot(stack);
                placed = true;
            }
        }
        if (!placed) {
            game::inv::insert(pinv, stack, game::inv::kPlayerAll);
            if (!game::isEmpty(stack)) spawnStack(totem.getDimensionBlockSource(), stack, totem.getPosition());
        }
    }
    game::inv::sync(player);
    particles(totem, drops);
    if (drops > 0) log().info("totem: {} collected {} stacks from {}", player.getRealName(), drops, id);
    return drops;
}

// `/quark debug totem` (operators, e2e): a totem 3 blocks in front of the player holding a few
// stacks and owned by nobody (Dark Souls mode ignores it).
LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("debug").text("totem").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = commands::playerOf(origin);
        if (!p || !commands::isAdmin(origin)) return output.error("Players with operator permission only.");
        auto feet = p->getFeetPos();
        auto dir  = game::facingOffset(game::playerFacing(*p));
        Vec3 at{feet.x + dir.first * 3.0f, feet.y + 1.0f, feet.z + dir.second * 3.0f};
        auto* totem = game::entities::spawn(p->getDimensionBlockSource(), kTotem, at);
        auto* c     = totem ? containerOf(*totem) : nullptr;
        if (!c) return output.error("Could not spawn a totem.");
        c->setItem(0, game::makeItem("minecraft:diamond", 3));
        c->setItem(1, game::makeItem("minecraft:iron_helmet"));
        c->setItem(2, game::makeItem("minecraft:bread", 5));
        totem->setNameTag(p->getRealName());
        log().info("totem: debug totem {} spawned", game::entities::idOf(*totem));
        output.success("Totem spawned.");
    });
});

class TotemModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            if (cfg().enableTotemOfHolding) tickTotems();
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerAttackEvent>([](ll::event::PlayerAttackEvent& ev) {
            auto& target = ev.target();
            if (game::entities::typeOf(target) != kTotem) return;
            ev.cancel();
            if (!ev.self().isClientSide()) collectFromTotem(ev.self(), target);
        }));
        PlayerDieHook::hook();
        game::db::onWorldClosed([] {
            lastTotems().clear();
            owners().clear();
            dying().clear();
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        PlayerDieHook::unhook();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace lp::feature::backpack

LP_REGISTER_MODULE(lp::feature::backpack::TotemModule);
