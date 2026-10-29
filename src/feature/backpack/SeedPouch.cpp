// Quark's SeedPouch module (docs/spec/backpack.md §3.3): one seed type up to `maxItems`, planting
// (3x3 when sneaking), auto pickup, chest insert; Bedrock replacements for the bundle-style clicks.

#include "feature/backpack/Backpack.h"

#include <array>
#include <cmath>
#include <map>
#include <random>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/event/player/PlayerPickUpItemEvent.h"
#include "ll/api/event/player/PlayerUseItemEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/Container.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemUsedOnEventContext.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"

#include "feature/backpack/Messages.h"
#include "game/Blocks.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::feature::backpack {

/// Command parameters (external linkage for reflection).
struct SeedPouchParams {
    std::string item;
};

namespace {

constexpr char const* kDataKey = "lepton:pouch";

/// Quark's #quark:seed_pouch_holdable as Bedrock ids (kept in sync with tools/pack/backpack.ps1).
constexpr std::array<std::string_view, 29> kHoldable{
    "minecraft:wheat_seeds",     "minecraft:beetroot_seeds",   "minecraft:melon_seeds",    "minecraft:pumpkin_seeds",
    "minecraft:torchflower_seeds", "minecraft:pitcher_pod",    "minecraft:potato",         "minecraft:carrot",
    "minecraft:brown_mushroom",  "minecraft:red_mushroom",     "minecraft:warped_fungus",  "minecraft:crimson_fungus",
    "minecraft:oak_sapling",     "minecraft:spruce_sapling",   "minecraft:birch_sapling",  "minecraft:jungle_sapling",
    "minecraft:acacia_sapling",  "minecraft:dark_oak_sapling", "minecraft:cherry_sapling", "minecraft:pale_oak_sapling",
    "minecraft:mangrove_propagule", "minecraft:azalea",        "minecraft:flowering_azalea", "minecraft:sweet_berries",
    "minecraft:kelp",            "minecraft:bamboo",           "minecraft:sugar_cane",     "minecraft:nether_wart",
    "minecraft:cocoa_beans",
};

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
float frand() { return std::uniform_real_distribution<float>(0, 1)(rng()); }

bool verticalPlacement(ItemStackBase const& s) {
    return isItem(s, "minecraft:cocoa_beans") || hasItemTag(s, "lepton:seed_pouch_vertical_placement");
}

ItemStack contentItem(PouchContents const& c, int count) {
    auto s = game::itemFromSnbt(c.snbt);
    if (!game::isEmpty(s)) s.set(std::max(1, std::min(count, static_cast<int>(s.getMaxStackSize()))));
    return s;
}

std::string prettyName(std::string const& id) {
    auto        path = id.substr(id.find(':') + 1);
    std::string out;
    bool        up = true;
    for (char ch : path) {
        if (ch == '_') {
            out += ' ';
            up = true;
        } else {
            out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
            up = false;
        }
    }
    return out;
}

void playSound(Player& p, SharedTypes::Legacy::LevelSoundEvent ev) {
    p.playSound(p.getDimensionBlockSource(), ev, p.getPosition(), -1);
}

void actionBar(Player& p, std::string const& text) {
    auto        pkt = TextPacket{};
    TextPacketPayload::MessageOnly message;
    message.mType = TextPacketType::Tip;
    message.mMessage->assign(text);
    pkt.mBody = message;
    pkt.sendTo(p);
}

void storeSelected(Player& p, ItemStack const& pouch) {
    p.setSelectedItem(pouch);
    game::inv::sync(p);
}

/// Quark's placeSeed: uses one item of the contents on `pos`. True when the use succeeded.
bool placeSeed(Player& player, PouchContents& c, BlockPos const& pos, int face) {
    if (c.empty()) return false;
    ItemStack restore = player.getSelectedItem();
    player.setSelectedItem(contentItem(c, c.count));
    auto&             held   = const_cast<ItemStack&>(player.getSelectedItem());
    int               before = held.mCount;
    static const Vec3 clicks[6]{
        {0.5f, 0.0f, 0.5f},
        {0.5f, 1.0f, 0.5f},
        {0.5f, 0.5f, 0.0f},
        {0.5f, 0.5f, 1.0f},
        {0.0f, 0.5f, 0.5f},
        {1.0f, 0.5f, 0.5f}
    };
    ItemUsedOnEventContext ctx{};
    ctx.mNotify       = NotifyItemUsedOnEvent::Notify;
    ctx.mIsFirstEvent = true;
    auto res          = held.useOn(player, pos, static_cast<uchar>(face), HandSlot::Mainhand, clicks[std::clamp(face, 0, 5)], ctx);
    int  used         = before - (game::isEmpty(held) ? 0 : held.mCount);
    player.setSelectedItem(restore);
    bool ok = used > 0 || (player.isCreative() && res.mSuccess);
    if (used > 0 && !player.isCreative()) {
        c.count -= 1;
        if (c.count <= 0) c = {};
    }
    return ok;
}

int insertIntoChest(Player& player, PouchContents& c, BlockPos const& pos) {
    auto& region = player.getDimensionBlockSource();
    auto  id     = game::blockIdAt(region, pos);
    if (id != "minecraft:chest" && id != "minecraft:trapped_chest") return -1;
    auto* ba = region.getBlockEntity(pos);
    if (!ba || ba->getType() != BlockActorType::Chest) return -1;
    auto* container = static_cast<ChestBlockActor*>(ba)->getContainer();
    if (!container) return -1;
    int total = c.count, remaining = c.count;
    while (remaining > 0) {
        auto stack = contentItem(c, remaining);
        int  n     = stack.mCount;
        int  moved = game::inv::insert(*container, stack, game::inv::all(*container));
        remaining -= moved;
        if (moved < n) break;
    }
    int inserted = total - remaining;
    if (inserted == 0) {
        actionBar(player, "§c" + trFor(player, "lp.backpack.pouch_full_chest", "The inventory you are trying to insert into is full..."));
    } else {
        c.count -= inserted;
        if (c.count <= 0) c = {};
    }
    return inserted;
}

std::map<std::string, int>& lastUse() { ///< player uuid -> tick of the last pouch use
    static std::map<std::string, int> m;
    return m;
}
int gTick = 0;

bool cooldown(Player& p, int ticks) {
    auto  uuid = p.getUuid().asString();
    auto  it   = lastUse().find(uuid);
    if (it != lastUse().end() && gTick - it->second < ticks) return true;
    lastUse()[uuid] = gTick;
    return false;
}

} // namespace

// ---- contents -------------------------------------------------------------------------------------

PouchContents pouchContents(ItemStackBase const& pouch) {
    PouchContents c;
    auto const&   ud = pouch.mUserData;
    if (!ud || !ud->contains(kDataKey, Tag::Type::Compound)) return c;
    auto const& inner = (*ud)[kDataKey].get<CompoundTag>();
    if (!inner.contains("Item", Tag::Type::Compound) || !inner.contains("Count", Tag::Type::Int)) return c;
    c.snbt  = inner["Item"].get<CompoundTag>().toSnbt(SnbtFormat::Minimize, 0);
    c.count = inner["Count"].get<IntTag>().data;
    if (c.count <= 0) c = {};
    return c;
}

void setPouchContents(ItemStack& pouch, PouchContents const& c) {
    auto tag = pouch.mUserData ? pouch.mUserData->clone() : std::make_unique<CompoundTag>();
    if (c.empty()) {
        tag->remove(kDataKey);
    } else {
        auto item = CompoundTag::fromSnbt(c.snbt);
        if (!item) return;
        CompoundTag inner;
        inner.putCompound("Item", std::move(*item));
        inner.putInt("Count", c.count);
        tag->putCompound(kDataKey, std::move(inner));
    }
    if (tag->empty()) pouch.setUserData(nullptr);
    else pouch.setUserData(std::move(tag));
    if (c.empty()) {
        pouch.clearCustomLore();
    } else {
        auto item = contentItem(c, 1);
        int  max  = std::max(1, cfg().maxItems);
        int  bars = static_cast<int>(std::lround(c.count * 13.0 / max));
        std::string bar = (pouchFertilizer(item) ? "§7" : "§9") + std::string(static_cast<size_t>(bars), '|') + "§8"
                        + std::string(static_cast<size_t>(std::max(0, 13 - bars)), '|');
        pouch.setCustomLore({"§r§f" + prettyName(game::itemId(item)) + " x" + std::to_string(c.count), "§r" + bar});
    }
}

bool pouchHoldable(ItemStackBase const& s) {
    if (game::isEmpty(s)) return false;
    auto id = s.getTypeName();
    for (auto h : kHoldable)
        if (id == h) return true;
    return hasItemTag(s, "lepton:seed_pouch_holdable");
}

bool pouchFertilizer(ItemStackBase const& s) {
    return isItem(s, "minecraft:bone_meal") || hasItemTag(s, "lepton:seed_pouch_fertilizers");
}

bool pouchCanFit(PouchContents const& c, ItemStackBase const& other) {
    if (game::isEmpty(other) || isItem(other, kSeedPouch)) return false;
    if (c.empty()) return pouchHoldable(other) || (cfg().allowFertilizer && pouchFertilizer(other));
    return c.count < cfg().maxItems && game::canMerge(contentItem(c, 1), other);
}

bool pouchAbsorb(PouchContents& c, ItemStack& other) {
    if (!pouchCanFit(c, other)) return false;
    int toMove = std::min(cfg().maxItems - c.count, static_cast<int>(other.mCount));
    if (toMove <= 0) return false;
    if (c.empty()) {
        ItemStack one = other;
        one.set(1);
        c.snbt  = game::itemToSnbt(one);
        c.count = toMove;
    } else {
        c.count += toMove;
    }
    if (other.mCount - toMove <= 0) other = ItemStack{};
    else other.set(other.mCount - toMove);
    return true;
}

ItemStack pouchSplitStack(PouchContents& c) {
    if (c.empty()) return ItemStack{};
    auto out = contentItem(c, c.count);
    c.count -= out.mCount;
    if (c.count <= 0) c = {};
    return out;
}

// ---- use ------------------------------------------------------------------------------------------

int pouchUseOn(Player& player, BlockPos const& pos, int face) {
    if (!cfg().enableSeedPouch) return 0;
    ItemStack pouch = player.getSelectedItem();
    if (!isItem(pouch, kSeedPouch) || pouch.mCount != 1) return 0;
    auto c = pouchContents(pouch);
    if (c.empty()) return 0;
    int  start   = c.count;
    auto content = contentItem(c, 1);
    bool sneak   = player.isSneaking();

    if (sneak) insertIntoChest(player, c, pos);
    if (!sneak) {
        placeSeed(player, c, pos, face);
    } else {
        int  range = pouchHoldable(content) ? cfg().shiftRange : cfg().fertilizerShiftRange;
        range      = std::max(1, range);
        int  shift = -static_cast<int>(std::floor(range / 2.0f));
        bool vert  = verticalPlacement(content);
        for (int i = 0; i < range * range; ++i) {
            int      a = shift + i % range;
            int      b = shift + i / range;
            BlockPos at = pos;
            if (vert) {
                if (face == 2 || face == 3) at = BlockPos{pos.x + a, pos.y + b, pos.z};      // north / south (Z axis)
                else if (face == 4 || face == 5) at = BlockPos{pos.x, pos.y + a, pos.z + b}; // west / east (X axis)
            } else {
                at = BlockPos{pos.x + a, pos.y, pos.z + b};
            }
            placeSeed(player, c, at, face);
            if (c.empty()) break;
        }
    }
    setPouchContents(pouch, c);
    storeSelected(player, pouch);
    int used = start - c.count;
    if (used > 0) log().info("seed pouch: {} used {} items", player.getRealName(), used);
    return used;
}

bool pouchInsert(Player& player) {
    if (!cfg().enableSeedPouch) return false;
    ItemStack pouch = player.getSelectedItem();
    if (!isItem(pouch, kSeedPouch) || pouch.mCount != 1) return false;
    auto  c    = pouchContents(pouch);
    auto& pinv = game::inv::playerInventory(player);
    bool  done = false;
    // Quark's right-click with seeds on the cursor, for every stack of the inventory.
    for (int i = 0; i < game::inv::kPlayerSlots; ++i) {
        if (i == player.getSelectedItemSlot()) continue;
        ItemStack s = pinv.getItem(i);
        if (game::isEmpty(s) || !pouchAbsorb(c, s)) continue;
        pinv.setItem(i, s);
        done = true;
    }
    if (!done) return false;
    setPouchContents(pouch, c);
    storeSelected(player, pouch);
    playSound(player, SharedTypes::Legacy::LevelSoundEvent::BundleInsert);
    log().info("seed pouch: {} now holds {}", player.getRealName(), c.count);
    return true;
}

bool pouchTake(Player& player) {
    if (!cfg().enableSeedPouch) return false;
    ItemStack pouch = player.getSelectedItem();
    if (!isItem(pouch, kSeedPouch) || pouch.mCount != 1) return false;
    auto c = pouchContents(pouch);
    if (c.empty()) return false;
    // Quark's right-click with an empty cursor: one stack out.
    auto out = pouchSplitStack(c);
    setPouchContents(pouch, c);
    storeSelected(player, pouch);
    game::inv::insert(game::inv::playerInventory(player), out, game::inv::kPlayerAll);
    game::inv::sync(player);
    if (!game::isEmpty(out)) {
        if (auto level = ll::service::getLevel())
            level->getSpawner().spawnItem(player.getDimensionBlockSource(), out, nullptr, player.getPosition(), 10);
    }
    playSound(player, SharedTypes::Legacy::LevelSoundEvent::BundleRemoveOne);
    log().info("seed pouch: {} now holds {}", player.getRealName(), c.count);
    return true;
}

bool pouchUseInAir(Player& player) { return player.isSneaking() ? pouchInsert(player) : pouchTake(player); }

namespace {

/// Quark's onItemPickup: a held pouch absorbs fitting items first.
bool absorbPickup(Player& player, ItemActor& itemActor) {
    auto& stack = itemActor.item();
    for (int hand = 0; hand < 2; ++hand) {
        ItemStack pouch = hand == 0 ? player.getSelectedItem() : player.getOffhandSlot();
        if (!isItem(pouch, kSeedPouch) || pouch.mCount != 1) continue;
        auto c = pouchContents(pouch);
        if (c.empty() || !pouchAbsorb(c, stack)) continue;
        setPouchContents(pouch, c);
        if (hand == 0) player.setSelectedItem(pouch);
        else player.setOffhandSlot(pouch);
        game::inv::sync(player);
        playSound(player, SharedTypes::Legacy::LevelSoundEvent::BundleInsert);
        if (game::isEmpty(stack)) itemActor.remove();
        log().info("seed pouch: {} picked up into the pouch ({} now)", player.getRealName(), c.count);
        return true;
    }
    return false;
}

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload<SeedPouchParams>().text("seedpouch").required("item").execute(
        [](CommandOrigin const& origin, CommandOutput& output, SeedPouchParams const& params) {
            auto* p = commands::playerOf(origin);
            if (!p || !commands::isAdmin(origin)) return output.error("Players with operator permission only.");
            auto id   = params.item.find(':') == std::string::npos ? "minecraft:" + params.item : params.item;
            auto seed = game::makeItem(id, 1);
            if (!pouchHoldable(seed) && !pouchFertilizer(seed)) return output.error("Not a seed pouch item: " + id);
            PouchContents c;
            c.snbt  = game::itemToSnbt(seed);
            c.count = cfg().maxItems;
            auto pouch = game::makeItem(kSeedPouch);
            setPouchContents(pouch, c);
            game::inv::insert(game::inv::playerInventory(*p), pouch, game::inv::kPlayerAll);
            game::inv::sync(*p);
            output.success("Gave a full seed pouch of " + id);
        }
    );
});

} // namespace

class SeedPouchModule {
public:
    void install() {
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) { ++gTick; }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
            auto& p = ev.self();
            if (p.isClientSide() || !cfg().enableSeedPouch || !isItem(p.getSelectedItem(), kSeedPouch)) return;
            if (pouchContents(p.getSelectedItem()).empty()) return;
            // Not cancelled: the block's own use (doors, chests, ...) still happens, as in Java
            // where the block is used before the item. Planting on such blocks simply fails.
            if (cooldown(p, 4)) return;
            auto uuid = p.getUuid().asString();
            auto pos  = ev.blockPos();
            int  face = static_cast<int>(ev.face());
            // Deferred to the next tick, after the game finished handling the click.
            ll::coro::keepThis([uuid, pos, face]() -> ll::coro::CoroTask<> {
                co_await ll::chrono::ticks(1);
                auto level = ll::service::getLevel();
                if (auto* pl = level ? level->getPlayer(mce::UUID::fromString(uuid)) : nullptr) pouchUseOn(*pl, pos, face);
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerUseItemEvent>([](ll::event::PlayerUseItemEvent& ev) {
            auto& p = ev.self();
            if (p.isClientSide() || !cfg().enableSeedPouch || !isItem(ev.item(), kSeedPouch)) return;
            ev.cancel();
            if (cooldown(p, 4)) return;
            auto uuid = p.getUuid().asString();
            ll::coro::keepThis([uuid]() -> ll::coro::CoroTask<> {
                co_await ll::chrono::ticks(1);
                auto level = ll::service::getLevel();
                if (auto* pl = level ? level->getPlayer(mce::UUID::fromString(uuid)) : nullptr) pouchUseInAir(*pl);
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }));
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerPickUpItemEvent>([](ll::event::PlayerPickUpItemEvent& ev) {
            auto& p = ev.self();
            if (p.isClientSide() || !cfg().enableSeedPouch) return;
            if (absorbPickup(p, ev.itemActor())) ev.cancel();
        }));
        mPouchAction = net::onServer<PouchActionMsg>([](Player& p, PouchActionMsg const& m) {
            if (!isItem(p.getSelectedItem(), kSeedPouch)) return;
            if (m.action == "insert") pouchInsert(p);
            else if (m.action == "take") pouchTake(p);
        });
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        net::removeHandler(mPouchAction);
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    net::HandlerId                      mPouchAction = 0;
};

} // namespace lp::feature::backpack

LP_REGISTER_MODULE(lp::feature::backpack::SeedPouchModule);
