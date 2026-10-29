// Quark's Backpack module (docs/spec/backpack.md §3.1): the worn backpack's 27 slots, opened in
// place of the inventory as a chest screen served by a helper entity; lock / spill rules; dropped
// backpacks spill; armour-stand swapping; ravager hide drops; recipe toggles.

#include "feature/backpack/Backpack.h"

#include <map>
#include <random>
#include <set>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/entity/MobDieEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/entity/components_json_legacy/ContainerComponent.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContextFactory.h"
#include "mc/world/item/crafting/Recipes.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

#include "feature/backpack/Messages.h"
#include "game/Entities.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

LP_REGISTER_CONFIG("backpack", lp::feature::backpack::BackpackConfig);

namespace lp::feature::backpack {

BackpackConfig const& cfg() { return config::get<BackpackConfig>(); }

bool isItem(ItemStackBase const& stack, std::string_view id) {
    return !game::isEmpty(stack) && stack.getTypeName() == id;
}

bool hasItemTag(ItemStackBase const& stack, std::string_view tag) {
    if (game::isEmpty(stack)) return false;
    auto* item = stack.mItem.get();
    return item && item->hasTag(HashedString(std::string(tag)));
}

namespace {

using SharedTypes::Legacy::ArmorSlot;
constexpr char const* kDataKey = "lepton:backpack";

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

void spawnStack(Actor& at, ItemStack const& stack, Vec3 const& pos) {
    if (game::isEmpty(stack)) return;
    auto level = ll::service::getLevel();
    if (!level) return;
    level->getSpawner().spawnItem(at.getDimensionBlockSource(), stack, nullptr, pos, 10);
}

bool canIgnoreEquip(Player const& p) { return cfg().superOpMode || p.isCreative() || p.isSpectator(); }

} // namespace

// ---- item data ------------------------------------------------------------------------------------

std::vector<ItemStack> backpackItems(ItemStackBase const& backpack) {
    std::vector<ItemStack> out(kBackpackSlots);
    auto const& ud = backpack.mUserData;
    if (!ud || !ud->contains(kDataKey, Tag::Type::Compound)) return out;
    auto const& inner = (*ud)[kDataKey].get<CompoundTag>();
    for (int i = 0; i < kBackpackSlots; ++i) {
        auto key = std::to_string(i);
        if (!inner.contains(key, Tag::Type::Compound)) continue;
        out[static_cast<size_t>(i)] = ItemStack::fromTag(inner[key].get<CompoundTag>());
    }
    return out;
}

void setBackpackItems(ItemStack& backpack, std::vector<ItemStack> const& items) {
    auto        tag = backpack.mUserData ? backpack.mUserData->clone() : std::make_unique<CompoundTag>();
    CompoundTag inner;
    bool        any = false;
    for (size_t i = 0; i < items.size() && i < static_cast<size_t>(kBackpackSlots); ++i) {
        if (game::isEmpty(items[i])) continue;
        auto ctx = SaveContextFactory::createCloneSaveContext();
        auto t   = items[i].save(*ctx);
        if (!t) continue;
        inner.putCompound(std::to_string(i), std::move(*t));
        any = true;
    }
    if (any) tag->putCompound(kDataKey, std::move(inner));
    else tag->remove(kDataKey);
    if (tag->empty()) backpack.setUserData(nullptr);
    else backpack.setUserData(std::move(tag));
}

bool backpackHasItems(ItemStackBase const& backpack) {
    if (!isItem(backpack, kBackpack)) return false;
    auto const& ud = backpack.mUserData;
    return ud && ud->contains(kDataKey, Tag::Type::Compound) && !(*ud)[kDataKey].get<CompoundTag>().empty();
}

bool wearsBackpack(Player const& player) { return isItem(player.getArmor(ArmorSlot::Torso), kBackpack); }

int ravagerHideDrop(int lootingLevel, double random) {
    int    amount = cfg().baseRavagerHideDrop;
    double chance = lootingLevel * cfg().extraChancePerLooting;
    // Quark compares the chance with the base drop (not with 1); kept as is.
    while (chance > cfg().baseRavagerHideDrop) {
        chance -= 1;
        ++amount;
    }
    if (chance > 0 && random < chance) ++amount;
    return amount;
}

// ---- backpack screen ------------------------------------------------------------------------------

namespace {

constexpr int kOpenDelay = 2; ///< ticks between spawning the helper and opening its screen

struct Session {
    int64_t                helper = 0;
    std::vector<ItemStack> last;
    int                    grace = 10; ///< ticks before the screen must be seen open
};
std::map<std::string, Session>& sessions() {
    static std::map<std::string, Session> m;
    return m;
}

ContainerComponent* componentOf(Actor& actor) {
    auto comp = actor.getEntityContext().tryGetComponent<ContainerComponent>();
    return comp ? comp.as_ptr() : nullptr;
}
Container* containerOf(Actor& actor) {
    auto* comp = componentOf(actor);
    return comp ? &comp->mContainer.get() : nullptr;
}

bool hasOpen(Player const& player, int64_t entity) {
    auto const& manager = player.mContainerManager.get();
    if (!manager) return false;
    auto const& owner = manager->mScreenContext->mOwner.get();
    auto const* id    = std::get_if<ActorUniqueID>(&owner);
    return id && id->rawID == entity;
}

Player* playerByUuid(std::string const& uuid) {
    auto level = ll::service::getLevel();
    return level ? level->getPlayer(mce::UUID::fromString(uuid)) : nullptr;
}

/// Writes the helper's contents into the worn backpack. When the backpack is no longer worn
/// (death, /clear, ...) the item it became already holds the contents (written on every change),
/// so the helper's copy is discarded instead of being duplicated.
void writeBack(Player& p, std::vector<ItemStack> const& items) {
    ItemStack worn = p.getArmor(ArmorSlot::Torso);
    if (!isItem(worn, kBackpack)) return;
    setBackpackItems(worn, items);
    p.setArmor(ArmorSlot::Torso, worn);
}

void finish(std::string const& uuid, bool closeScreen) {
    auto it = sessions().find(uuid);
    if (it == sessions().end()) return;
    auto  s      = std::move(it->second);
    sessions().erase(it);
    auto* p      = playerByUuid(uuid);
    auto* helper = game::entities::find(s.helper);
    if (helper) {
        if (auto* c = containerOf(*helper)) {
            s.last = game::inv::snapshot(*c, game::inv::all(*c));
            c->removeAllItems();
        }
        if (p && closeScreen && hasOpen(*p, s.helper)) static_cast<ServerPlayer*>(p)->initiateContainerClose();
        game::entities::despawn(*helper);
    }
    if (p) {
        writeBack(*p, s.last);
        net::sendToClient(*p, BackpackScreenMsg{false});
        log().info("backpack: closed by {}", p->getRealName());
    }
    // Without the player (gone already) the item keeps what was written on the last change;
    // disconnects are finished earlier, while the player still exists (PlayerDisconnectEvent).
}

/// Pushes items the backpack can't hold back to the player (or drops them).
void rejectBlocked(Player& p, Container& c) {
    bool moved = false;
    for (int i = 0; i < c.getContainerSize(); ++i) {
        ItemStack stack = c.getItem(i);
        if (game::isEmpty(stack) || !hasItemTag(stack, "lepton:backpack_blocked")) continue;
        c.setItem(i, ItemStack{});
        game::inv::insert(game::inv::playerInventory(p), stack, game::inv::kPlayerAll);
        if (!game::isEmpty(stack)) spawnStack(p, stack, p.getPosition());
        moved = true;
    }
    if (moved) game::inv::sync(p);
}

bool sameStack(ItemStack const& a, ItemStack const& b) {
    if (game::isEmpty(a) || game::isEmpty(b)) return game::isEmpty(a) && game::isEmpty(b);
    return a.mCount == b.mCount && a.matchesItem(b) && a.hasSameUserData(b);
}

void tickSessions() {
    std::vector<std::pair<std::string, bool>> ended;
    for (auto& [uuid, s] : sessions()) {
        auto* p      = playerByUuid(uuid);
        auto* helper = game::entities::find(s.helper);
        auto* c      = helper ? containerOf(*helper) : nullptr;
        if (!p || !c) {
            ended.emplace_back(uuid, false);
            continue;
        }
        if (!wearsBackpack(*p) || !cfg().enableBackpack) {
            ended.emplace_back(uuid, true);
            continue;
        }
        rejectBlocked(*p, *c);
        auto now     = game::inv::snapshot(*c, game::inv::all(*c));
        bool changed = now.size() != s.last.size();
        for (size_t i = 0; !changed && i < now.size(); ++i) changed = !sameStack(now[i], s.last[i]);
        if (changed) {
            s.last = now;
            writeBack(*p, now);
        }
        bool open = hasOpen(*p, s.helper);
        if (open) s.grace = 0;
        else if (s.grace-- <= 0) ended.emplace_back(uuid, false);
    }
    for (auto const& [uuid, close] : ended) finish(uuid, close);
}

void sweepStrays() {
    std::set<int64_t> owned;
    for (auto const& [uuid, s] : sessions()) owned.insert(s.helper);
    for (auto* a : game::entities::ofType(kStorageEntity)) {
        if (owned.contains(game::entities::idOf(*a))) continue;
        if (auto* c = containerOf(*a)) c->removeAllItems(); // the backpack item holds the real contents
        game::entities::despawn(*a);
    }
}

} // namespace

bool backpackOpen(Player const& player) { return sessions().contains(player.getUuid().asString()); }

Container* backpackContainer(Player const& player) {
    auto it = sessions().find(player.getUuid().asString());
    if (it == sessions().end()) return nullptr;
    auto* helper = game::entities::find(it->second.helper);
    return helper ? containerOf(*helper) : nullptr;
}

bool openBackpack(Player& player) {
    if (player.isClientSide() || !cfg().enableBackpack || !wearsBackpack(player)) return false;
    auto uuid = player.getUuid().asString();
    if (sessions().contains(uuid)) {
        finish(uuid, true); // already open: closing it is all a second request does
        return false;
    }
    if (player.mContainerManager.get()) return false; // another screen is open
    auto  pos    = player.getFeetPos();
    auto* helper = game::entities::spawn(player.getDimensionBlockSource(), kStorageEntity, {pos.x, pos.y + 0.1f, pos.z});
    if (!helper) {
        log().error("backpack: could not spawn {} (behaviour pack missing?)", kStorageEntity);
        return false;
    }
    auto* comp = componentOf(*helper);
    if (!comp) {
        game::entities::despawn(*helper);
        return false;
    }
    auto& c     = comp->mContainer.get();
    auto  items = backpackItems(player.getArmor(ArmorSlot::Torso));
    for (int i = 0; i < c.getContainerSize() && i < kBackpackSlots; ++i) c.setItem(i, items[static_cast<size_t>(i)]);
    // The client titles an entity's container screen with the entity's name.
    auto title = trFor(player, "item.quark.backpack", "Backpack");
    comp->setCustomName(title);
    helper->setNameTag(title);
    Session s;
    s.helper = game::entities::idOf(*helper);
    s.last   = game::inv::snapshot(c, game::inv::all(c));
    s.grace       = 10 + kOpenDelay;
    auto helperId = s.helper;
    sessions()[uuid] = std::move(s);
    // The client ignores the screen of an actor it has not been told about yet: open it once the
    // helper's spawn has reached the client.
    ll::coro::keepThis([uuid, helperId]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(kOpenDelay);
        auto it = sessions().find(uuid);
        if (it == sessions().end() || it->second.helper != helperId) co_return;
        auto* pl     = playerByUuid(uuid);
        auto* helper = game::entities::find(helperId);
        if (!pl || !helper || pl->mContainerManager.get()) {
            finish(uuid, false);
            co_return;
        }
        helper->openContainerComponent(*pl);
        net::sendToClient(*pl, BackpackScreenMsg{true});
        log().info("backpack: opened by {}", pl->getRealName());
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
    return true;
}

void closeBackpack(Player& player) { finish(player.getUuid().asString(), true); }

void flushBackpack(Player& player) { closeBackpack(player); }

// ---- per-player rules (Quark's BackpackItem.inventoryTick) -------------------------------------

namespace {

std::map<std::string, int>& wornFull() { ///< uuid -> ticks the worn full backpack has been missing
    static std::map<std::string, int> m;
    return m;
}

void tickContents(Player& p, ItemStack& backpack) {
    auto items   = backpackItems(backpack);
    bool changed = false;
    auto level   = ll::service::getLevel();
    if (!level) return;
    for (size_t i = 0; i < items.size(); ++i) {
        auto& s = items[i];
        if (game::isEmpty(s)) continue;
        auto* item = s.mItem.get();
        if (!item) continue;
        ItemStack before = s;
        item->inventoryTick(s, *level, p, static_cast<int>(i), false);
        if (before.mCount != s.mCount || !before.hasSameUserData(s)) changed = true;
    }
    if (changed) {
        setBackpackItems(backpack, items);
        p.setArmor(ArmorSlot::Torso, backpack);
    }
}

void setWarning(Player& p, ItemStack& stack, bool on) {
    auto lore = stack.getCustomLore();
    if (on) {
        std::vector<std::string> want{
            "§r§c" + trFor(p, "item.quark.backpack.warning.line_1", "WARNING"),
            trFor(p, "item.quark.backpack.warning.line_2",
                  "This backpack has items in it, it will be wiped if its not equipped whilst in survival.")};
        if (lore != want) stack.setCustomLore(want);
    } else if (!lore.empty()) {
        stack.clearCustomLore();
    }
}

void tickPlayer(Player& p) {
    auto uuid   = p.getUuid().asString();
    bool ignore = canIgnoreEquip(p);
    auto& pinv  = game::inv::playerInventory(p);

    // Lock: the worn non-empty backpack goes back to the chest slot (Quark: hidden Curse of Binding).
    ItemStack chest = p.getArmor(ArmorSlot::Torso);
    if (isItem(chest, kBackpack)) wornFull().erase(uuid); // still worn (maybe emptied): nothing to restore
    if (auto it = wornFull().find(uuid); it != wornFull().end()) {
        bool restored = false;
        if (!ignore) {
            for (int i = 0; i < game::inv::kPlayerSlots && !restored; ++i) {
                ItemStack s = pinv.getItem(i);
                if (!backpackHasItems(s)) continue;
                pinv.setItem(i, chest);
                p.setArmor(ArmorSlot::Torso, s);
                restored = true;
            }
            if (!restored && backpackHasItems(p.getOffhandSlot())) {
                ItemStack s = p.getOffhandSlot();
                p.setOffhandSlot(chest);
                p.setArmor(ArmorSlot::Torso, s);
                restored = true;
            }
        }
        if (restored) {
            game::inv::sync(p);
            log().info("backpack: {} can't take off a backpack with items", p.getRealName());
            wornFull().erase(it);
        } else if (ignore || ++it->second > 20) {
            wornFull().erase(it); // gone for good (dropped, creative, ...)
        }
        chest = p.getArmor(ArmorSlot::Torso);
    }

    bool chestFull = backpackHasItems(chest);
    if (chestFull && !ignore) wornFull()[uuid] = 0;
    else if (ignore) wornFull().erase(uuid);

    // The worn one: ticking contents, no warning.
    if (isItem(chest, kBackpack)) {
        if (chestFull && cfg().itemsInBackpackTick && !backpackOpen(p)) tickContents(p, chest);
        if (!chest.getCustomLore().empty() && !cfg().superOpMode) {
            ItemStack c2 = p.getArmor(ArmorSlot::Torso);
            setWarning(p, c2, false);
            p.setArmor(ArmorSlot::Torso, c2);
        }
    }

    // Backpacks elsewhere in the inventory.
    bool dirty = false;
    auto check = [&](ItemStack& s) -> bool {
        if (!isItem(s, kBackpack)) return false;
        bool full = backpackHasItems(s);
        if (full && !ignore) {
            for (auto const& item : backpackItems(s)) spawnStack(p, item, p.getPosition());
            setBackpackItems(s, {});
            log().info("backpack: spilled the contents of an unworn backpack of {}", p.getRealName());
            return true;
        }
        bool warn = full && !cfg().superOpMode && (p.isCreative() || p.isSpectator());
        auto before = s.getCustomLore();
        setWarning(p, s, warn);
        return before != s.getCustomLore();
    };
    for (int i = 0; i < game::inv::kPlayerSlots; ++i) {
        ItemStack s = pinv.getItem(i);
        if (!isItem(s, kBackpack)) continue;
        if (check(s)) {
            pinv.setItem(i, s);
            dirty = true;
        }
    }
    if (ItemStack off = p.getOffhandSlot(); isItem(off, kBackpack) && check(off)) {
        p.setOffhandSlot(off);
        dirty = true;
    }
    if (dirty) game::inv::sync(p);
}

// ---- hooks ----------------------------------------------------------------------------------------

int gTick = 0;
/// uuid -> last tick of the window in which the player's next inventory request opens the normal
/// inventory (the mod client's "Inventory" button: the client asks for it itself).
std::map<std::string, int>& inventoryBypass() {
    static std::map<std::string, int> m;
    return m;
}

bool takeBypass(Player const& p) {
    auto it = inventoryBypass().find(p.getUuid().asString());
    if (it == inventoryBypass().end()) return false;
    bool active = it->second >= gTick;
    inventoryBypass().erase(it);
    return active;
}

LL_TYPE_INSTANCE_HOOK(
    OpenInventoryHook,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$openInventory,
    void
) {
    if (takeBypass(*this)) {
        log().info("backpack: {} switched to the inventory", getRealName());
    } else if (cfg().enableBackpack && wearsBackpack(*this) && !isCreative() && !isSpectator() && !isSneaking()) {
        if (openBackpack(*this)) return;
    }
    origin();
}

LL_TYPE_INSTANCE_HOOK(ItemActorTickHook, ll::memory::HookPriority::Normal, ItemActor, &ItemActor::postNormalTick, void) {
    origin();
    auto& stack = item();
    if (!stack.mUserData || !cfg().enableBackpack || cfg().superOpMode || isClientSide()) return;
    if (!backpackHasItems(stack)) return;
    auto pos = getPosition();
    for (auto const& s : backpackItems(stack)) spawnStack(*this, s, pos);
    setBackpackItems(stack, {});
    log().info("backpack: a dropped backpack spilled its contents");
}

LL_TYPE_INSTANCE_HOOK(
    PlayerInteractHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    InteractionResult,
    Actor&      actor,
    Vec3 const& location
) {
    if (!isClientSide() && cfg().enableBackpack && cfg().allowArmorStandUnloading
        && game::entities::typeOf(actor) == "minecraft:armor_stand" && !isSneaking()
        && game::isEmpty(getSelectedItem()) && game::isEmpty(getOffhandSlot())) {
        ItemStack mine   = getArmor(ArmorSlot::Torso);
        ItemStack theirs = actor.getArmor(ArmorSlot::Torso);
        if (isItem(mine, kBackpack) || isItem(theirs, kBackpack)) {
            actor.setArmor(ArmorSlot::Torso, mine);
            setArmor(ArmorSlot::Torso, theirs);
            wornFull().erase(getUuid().asString());
            playSound(getDimensionBlockSource(), SharedTypes::Legacy::LevelSoundEvent::EquipLeather, getPosition(), -1);
            log().info("backpack: {} swapped backpacks with an armor stand", getRealName());
            InteractionResult r{};
            r.mSuccess = true;
            r.mSwing   = true;
            return r;
        }
    }
    return origin(actor, location);
}

bool recipeDisabled(std::string const& id) {
    auto const& c = cfg();
    if (id.starts_with("lepton:backpack_no_hide")) return !c.enableBackpack || c.enableRavagerHide;
    if (id.starts_with("lepton:backpack")) return !c.enableBackpack || !c.enableRavagerHide;
    if (id.starts_with("lepton:bonded_ravager_hide")) return !c.enableBackpack || !c.enableRavagerHide;
    if (id.starts_with("lepton:seed_pouch_")) return !c.enableSeedPouch;
    return false;
}

LL_TYPE_INSTANCE_HOOK(
    RecipeLoadHook,
    ll::memory::HookPriority::Normal,
    Recipes,
    &Recipes::_loadRecipe,
    bool,
    std::string const&      recipeId,
    RecipeType              type,
    Json::Value const&      objData,
    MinEngineVersion const& minEngineVersion,
    SemVersion const&       formatVersion,
    bool                    isBaseGamePack,
    Recipes::Buffers&       buffers
) {
    if (recipeId.starts_with("lepton:") && recipeDisabled(recipeId)) {
        log().info("backpack: recipe {} disabled by config", recipeId);
        return true;
    }
    return origin(recipeId, type, objData, minEngineVersion, formatVersion, isBaseGamePack, buffers);
}

int lootingOf(ActorDamageSource const& source) {
    auto level = ll::service::getLevel();
    if (!level) return 0;
    auto id = source.getDamagingEntityUniqueID();
    if (id.rawID == -1 || id.rawID == 0) id = source.getEntityUniqueID();
    auto* killer = level->fetchEntity(id, false);
    if (!killer) return 0;
    auto const& weapon = killer->getCarriedItem();
    if (game::isEmpty(weapon)) return 0;
    return std::max(
        EnchantUtils::getEnchantLevel(Enchant::Type::Looting, weapon),
        EnchantUtils::getEnchantLevel(Enchant::Type::Fortune, weapon)
    );
}

} // namespace

class BackpackModule {
public:
    // Recipes load with the world (BDS: before mods are enabled), so the toggle hook goes in at load.
    void load() {
        RecipeLoadHook::hook();
        mRecipeHooked = true;
    }

    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([this](auto&) {
            ++gTick;
            if (!sessions().empty()) tickSessions();
            if (gTick % 100 == 0) sweepStrays();
            if (!cfg().enableBackpack) return;
            if (auto level = ll::service::getLevel()) {
                level->forEachPlayer([](Player& p) {
                    if (!p.isClientSide()) tickPlayer(p);
                    return true;
                });
            }
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::MobDieEvent>([](ll::event::MobDieEvent& ev) {
            auto& mob = ev.self();
            if (mob.isClientSide() || !cfg().enableBackpack || !cfg().enableRavagerHide) return;
            if (game::entities::typeOf(mob) != "minecraft:ravager") return;
            int  looting = lootingOf(ev.source());
            int  amount  = ravagerHideDrop(looting, std::uniform_real_distribution<double>(0, 1)(rng()));
            auto stack   = game::makeItem(kRavagerHide, amount);
            spawnStack(mob, stack, mob.getPosition());
            log().info("backpack: a ravager dropped {} ravager hide", amount);
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            // While the player still exists: the screen's last moves go back into the worn item.
            if (!ev.self().isClientSide()) flushBackpack(ev.self());
        }));
        OpenInventoryHook::hook();
        ItemActorTickHook::hook();
        PlayerInteractHook::hook();
        if (!mRecipeHooked) {
            RecipeLoadHook::hook();
            mRecipeHooked = true;
        }
        game::db::onWorldClosed([] {
            sessions().clear(); // helpers of a closed world are gone with it
            wornFull().clear();
            inventoryBypass().clear();
        });

        mSwitch = net::onServer<BackpackSwitchMsg>([](Player& p, BackpackSwitchMsg const& m) {
            auto uuid = p.getUuid().asString();
            if (m.target != "inventory" && m.target != "backpack") return;
            log().info("backpack: {} asked for the {} screen", p.getRealName(), m.target);
            if (m.target == "inventory") {
                if (!backpackOpen(p)) return;
                // The client opens its inventory itself once the backpack screen closed (the game
                // shows no inventory the server opens on its own): let that request through.
                inventoryBypass()[uuid] = gTick + 100;
                closeBackpack(p);
            } else if (m.target == "backpack") {
                if (!wearsBackpack(p) || backpackOpen(p)) return;
                if (p.mContainerManager.get()) static_cast<ServerPlayer&>(p).initiateContainerClose();
                ll::coro::keepThis([uuid]() -> ll::coro::CoroTask<> {
                    co_await ll::chrono::ticks(3);
                    if (auto* pl = playerByUuid(uuid)) openBackpack(*pl);
                }).launch(ll::thread::ServerThreadExecutor::getDefault());
            }
        });
    }

    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        net::removeHandler(mSwitch);
        std::vector<std::string> open;
        for (auto const& [uuid, s] : sessions()) open.push_back(uuid);
        for (auto const& uuid : open) finish(uuid, true);
        OpenInventoryHook::unhook();
        ItemActorTickHook::unhook();
        PlayerInteractHook::unhook();
        RecipeLoadHook::unhook();
        mRecipeHooked = false;
    }

private:
    bool                                mRecipeHooked = false;
    std::vector<ll::event::ListenerPtr> mListeners;
    net::HandlerId                      mSwitch = 0;
};

} // namespace lp::feature::backpack

LP_REGISTER_MODULE(lp::feature::backpack::BackpackModule);
