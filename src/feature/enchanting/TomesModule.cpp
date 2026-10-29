// AncientTomes + ColorRunes runtime: loot injection, the anvil / smithing helper, the deepslate
// tweak, the librarian exchange, `/quark enchanting ...` commands.

#include <random>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/shared_types/legacy/ContainerType.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/server/ServerPlayer.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/SynchedActorDataEntityWrapper.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerUISlot.h"
#include "mc/world/containers/managers/models/AnvilContainerManagerModel.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/trading/MerchantRecipe.h"
#include "mc/world/item/trading/MerchantRecipeList.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/storage/loot/LootPool.h"
#include "mc/world/level/storage/loot/LootTable.h"
#include "mc/world/level/storage/loot/entries/LootPoolEntry.h"

#include "feature/enchanting/Config.h"
#include "feature/enchanting/Enchants.h"
#include "feature/enchanting/Messages.h"
#include "feature/enchanting/Tomes.h"
#include "feature/enchanting/Util.h"
#include "game/BlockStates.h"
#include "game/Blocks.h"
#include "game/Entities.h"
#include "game/Forms.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

namespace lp::feature::enchanting {

// Command parameters need external linkage.
struct TomeCommandParams {
    std::string enchant;
};
struct RuneCommandParams {
    std::string color;
};

namespace {

std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}
int randInt(int bound) { return bound <= 0 ? 0 : std::uniform_int_distribution<int>(0, bound - 1)(rng()); }

// ---- loot injection ----------------------------------------------------------------------------

thread_local LootTable* gFillTable   = nullptr;
thread_local bool       gPoolHandled = false;

struct Bounds {
    float min, max;
};

LL_TYPE_INSTANCE_HOOK(
    LootFillHook,
    ll::memory::HookPriority::Normal,
    LootTable,
    &LootTable::fill,
    void,
    ::Container&        container,
    ::Random&           random,
    ::LootTableContext& context
) {
    auto* prevTable   = gFillTable;
    bool  prevHandled = gPoolHandled;
    gFillTable        = this;
    gPoolHandled      = false;
    origin(container, random, context);
    bool handled = gPoolHandled;
    gFillTable   = prevTable;
    gPoolHandled = prevHandled;
    if (handled) return;
    // Fallback (the pools were not visited through addRandomItems): one roll per extra entry.
    auto extras = lootExtrasFor(mDir);
    if (extras.empty() || mPools->empty()) return;
    int poolWeight = 0;
    for (auto const& e : *mPools->front()->mEntries) poolWeight += e ? std::max(0, static_cast<int>(e->mWeight)) : 0;
    for (auto const& x : extras) {
        if (randInt(poolWeight + x.weight) < poolWeight) continue;
        auto item = makeLootItem(x.what);
        if (game::isEmpty(item)) continue;
        std::vector<int> empty;
        for (int i = 0; i < container.getContainerSize(); ++i)
            if (game::isEmpty(container.getItem(i))) empty.push_back(i);
        if (empty.empty()) continue;
        container.setItem(empty[static_cast<size_t>(randInt(static_cast<int>(empty.size())))], item);
    }
}

LL_TYPE_INSTANCE_HOOK(
    LootPoolHook,
    ll::memory::HookPriority::Normal,
    LootPool,
    &LootPool::addRandomItems,
    void,
    std::vector<::ItemStack>& result,
    ::Random&                 random,
    ::LootTableContext&       context
) {
    size_t before = result.size();
    origin(result, random, context);
    if (!gFillTable || gFillTable->mPools->empty() || gFillTable->mPools->front().get() != this) return;
    gPoolHandled = true;
    auto extras  = lootExtrasFor(gFillTable->mDir);
    if (extras.empty()) return;
    int poolWeight = 0;
    for (auto const& e : *mEntries) poolWeight += e ? std::max(0, static_cast<int>(e->mWeight)) : 0;
    int extraWeight = 0;
    for (auto const& x : extras) extraWeight += x.weight;
    auto const& b     = *reinterpret_cast<Bounds const*>(&mRolls);
    int         lo    = std::clamp(static_cast<int>(b.min), 0, 64);
    int         hi    = std::clamp(static_cast<int>(b.max), lo, 64);
    int         rolls = lo + randInt(hi - lo + 1);
    // Each roll would have picked one of the extra entries with probability extra / (pool + extra):
    // the extra item replaces the item of that roll.
    for (int r = 0; r < rolls; ++r) {
        int pick = randInt(poolWeight + extraWeight);
        if (pick < poolWeight) continue;
        pick -= poolWeight;
        for (auto const& x : extras) {
            if (pick >= x.weight) {
                pick -= x.weight;
                continue;
            }
            auto item = makeLootItem(x.what);
            if (game::isEmpty(item)) break;
            if (result.size() > before) result.erase(result.begin() + static_cast<long long>(before) + randInt(static_cast<int>(result.size() - before)));
            result.push_back(item);
            break;
        }
    }
}

// ---- deepslate tweak ---------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    DestroyProgressHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::getDestroyProgress,
    float,
    ::Block const& block
) {
    float progress = origin(block);
    auto const& c  = cfg();
    if (!c.enableAncientTomes || !c.deepslateTweak || block.getTypeName() != "minecraft:deepslate") return progress;
    auto const& item = getSelectedItem();
    auto        id   = game::itemId(item);
    // Quark: Efficiency >= 6 and a mining speed >= 45 (diamond 8 / netherite 9 + 6^2 + 1).
    if (id != "minecraft:diamond_pickaxe" && id != "minecraft:netherite_pickaxe") return progress;
    auto const* eff = defById("minecraft:efficiency");
    if (!eff) return progress;
    auto e = enchantsOf(item);
    if (!e.contains(eff->type) || e[eff->type] < 6) return progress;
    if (c.deepslateTweakNeedsHaste2) {
        auto const* haste = getEffect(3u); // haste
        if (!haste || haste->mAmplifier < 1) return progress;
    }
    return std::max(progress, 1.0f);
}

// ---- anvil / smithing helper -------------------------------------------------------------------

struct Offer {
    std::string signature;
    Craft       craft;
    bool        anvil = false;
    std::string left, right, extra; ///< SNBT of the inputs (anvil: left/right; smithing: base/addition/template)
    core::GlobalPos station;
};

std::map<std::string, std::string>& lastSignature() {
    static std::map<std::string, std::string> m;
    return m;
}
std::map<std::string, Offer>& pendingForms() {
    static std::map<std::string, Offer> m;
    return m;
}

Container& uiContainer(Player& p) { return p.mPlayerUIContainer; }

/// Selftests: simulated players get no server-side container manager, so a test names the open station.
std::map<std::string, std::pair<SharedTypes::Legacy::ContainerType, BlockPos>>& forcedStations() {
    static std::map<std::string, std::pair<SharedTypes::Legacy::ContainerType, BlockPos>> m;
    return m;
}

std::optional<Offer> currentOffer(Player& p) {
    auto const& cm = p.mContainerManager;
    SharedTypes::Legacy::ContainerType type;
    BlockPos                           stationPos;
    if (cm) {
        type = cm->getContainerType();
        if (type == SharedTypes::Legacy::ContainerType::Anvil) stationPos = static_cast<AnvilContainerManagerModel&>(*cm).mBlockPos;
    } else if (auto it = forcedStations().find(uuidOf(p)); it != forcedStations().end()) {
        type       = it->second.first;
        stationPos = it->second.second;
    } else {
        return std::nullopt;
    }
    auto& ui  = uiContainer(p);
    Offer o;
    if (type == SharedTypes::Legacy::ContainerType::Anvil) {
        auto const& left  = ui.getItem(static_cast<int>(PlayerUISlot::AnvilInput));
        auto const& right = ui.getItem(static_cast<int>(PlayerUISlot::AnvilMaterial));
        auto        c     = anvilCraft(left, right);
        if (!c) return std::nullopt;
        o.craft   = *c;
        o.anvil   = true;
        o.left    = game::itemToSnbt(left);
        o.right   = game::itemToSnbt(right);
        o.station = game::globalPos(p.getDimensionBlockSource(), stationPos);
    } else if (type == SharedTypes::Legacy::ContainerType::SmithingTable) {
        auto const& base     = ui.getItem(static_cast<int>(PlayerUISlot::SmithingTableInput));
        auto const& addition = ui.getItem(static_cast<int>(PlayerUISlot::SmithingTableMaterial));
        auto const& templ    = ui.getItem(static_cast<int>(PlayerUISlot::SmithingTableTemplate));
        auto        c        = smithingCraft(templ, base, addition);
        if (!c) return std::nullopt;
        o.craft = *c;
        o.left  = game::itemToSnbt(base);
        o.right = game::itemToSnbt(addition);
        o.extra = game::itemToSnbt(templ);
    } else {
        return std::nullopt;
    }
    o.signature = o.left + "|" + o.right + "|" + o.extra + "|" + std::to_string(o.craft.cost);
    return o;
}

/// Removes one matching stack (by SNBT, count ignored) from the UI slot or else the inventory.
bool takeInput(Player& p, int uiSlot, std::string const& snbt, int count) {
    if (snbt == "{}") return true;
    auto want = game::itemFromSnbt(snbt);
    if (game::isEmpty(want)) return true;
    auto  same = [&](ItemStack const& s) {
        if (game::isEmpty(s) || game::itemId(s) != game::itemId(want)) return false;
        ItemStack a = s, b = want;
        a.set(1), b.set(1);
        return game::itemToSnbt(a) == game::itemToSnbt(b);
    };
    auto& ui = uiContainer(p);
    if (uiSlot >= 0) {
        auto const& cur = ui.getItem(uiSlot);
        if (same(cur) && cur.mCount >= count) {
            ItemStack rest = cur;
            rest.remove(count);
            ui.setItem(uiSlot, rest.mCount > 0 ? rest : ItemStack{});
            return true;
        }
    }
    auto& pinv = game::inv::playerInventory(p);
    return game::inv::remove(pinv, same, count, game::inv::kPlayerAll) == count;
}

void damageAnvil(core::GlobalPos const& pos) {
    auto* region = game::regionOf(pos.dim);
    if (!region) return;
    auto const& block = region->getBlock(game::blockPos(pos));
    auto        id    = block.getTypeName();
    char const* next  = nullptr;
    if (id == "minecraft:anvil") next = "minecraft:chipped_anvil";
    else if (id == "minecraft:chipped_anvil") next = "minecraft:damaged_anvil";
    else if (id != "minecraft:damaged_anvil") return;
    if (!next) {
        game::clearBlock(*region, game::blockPos(pos));
        playSound(pos, static_cast<int>(SharedTypes::Legacy::LevelSoundEvent::AnvilUse), 0.5f);
        return;
    }
    if (auto const* nb = game::blockstates::of(next)) {
        auto const* withDir = game::blockstates::copyStates(block, *nb);
        game::setBlock(*region, game::blockPos(pos), withDir ? *withDir : *nb);
    }
}

bool applyOffer(Player& p, Offer const& o) {
    bool cr = creative(p);
    if (!cr && playerLevel(p) < o.craft.cost) {
        p.sendMessage(trFor(p, "lp.enchanting.craft.too_expensive", "§cYou need {} levels.", o.craft.cost));
        return false;
    }
    bool ok;
    if (o.anvil) {
        ok = takeInput(p, static_cast<int>(PlayerUISlot::AnvilInput), o.left, game::itemFromSnbt(o.left).mCount)
          && takeInput(p, static_cast<int>(PlayerUISlot::AnvilMaterial), o.right, 1);
    } else {
        ok = takeInput(p, static_cast<int>(PlayerUISlot::SmithingTableInput), o.left, 1)
          && takeInput(p, static_cast<int>(PlayerUISlot::SmithingTableMaterial), o.right, 1)
          && takeInput(p, static_cast<int>(PlayerUISlot::SmithingTableTemplate), o.extra, 1);
    }
    if (!ok) {
        game::inv::sync(p);
        return false;
    }
    auto result = o.craft.result;
    if (o.craft.usesTome && cfg().curseGear) addRandomCurse(result);
    if (!cr) addLevels(p, -o.craft.cost);
    giveOrDrop(p, result);
    game::inv::sync(p);
    if (o.anvil) {
        playSound(o.station, static_cast<int>(SharedTypes::Legacy::LevelSoundEvent::AnvilUse));
        if (!cr && randInt(100) < 12) damageAnvil(o.station);
    } else {
        playSound(game::globalPos(p.getDimensionBlockSource(), game::blockPosOf(p.getPosition())),
                  static_cast<int>(SharedTypes::Legacy::LevelSoundEvent::SmithingTableUse));
    }
    log().info("enchanting: {} applied {} -> {}", p.getRealName(), o.craft.kind, game::itemId(result));
    if (p.mContainerManager) static_cast<ServerPlayer&>(p).initiateContainerClose();
    forcedStations().erase(uuidOf(p));
    return true;
}

void offerForm(Player& p, Offer const& o) {
    auto uuid = uuidOf(p);
    pendingForms()[uuid] = o;
    std::string title = o.craft.kind == "rune" ? trFor(p, "lp.enchanting.craft.rune_title", "Apply Rune?")
                                               : trFor(p, "lp.enchanting.craft.tome_title", "Apply Ancient Tome?");
    std::string name;
    for (auto const& [t, l] : enchantsOf(o.craft.result))
        if (auto const* d = defOf(t)) name += (name.empty() ? "" : ", ") + trFor(p, nameKey(*d), englishName(*d)) + " " + roman(l);
    std::string body = o.craft.kind == "rune"
                         ? trFor(p, "rune.quark." + *stringTag(o.craft.result, kRuneTag), "Runic Etching")
                         : trFor(p, "lp.enchanting.craft.body", "Result: {}\nCost: {} levels", name, o.craft.cost);
    game::forms::ModalForm(title, body, trFor(p, "lp.enchanting.craft.yes", "Apply"), trFor(p, "lp.enchanting.craft.no", "Cancel"))
        .onYes([](Player& pl) {
            auto it = pendingForms().find(uuidOf(pl));
            if (it == pendingForms().end()) return;
            auto offer = it->second;
            pendingForms().erase(it);
            applyOffer(pl, offer);
        })
        .onNo([](Player& pl) { pendingForms().erase(uuidOf(pl)); })
        .onClose([](Player& pl) { pendingForms().erase(uuidOf(pl)); })
        .sendTo(p);
}

void tickStations() {
    auto level = ll::service::getLevel();
    if (!level) return;
    std::vector<std::string> seen;
    level->forEachPlayer([&](Player& p) {
        auto uuid = uuidOf(p);
        seen.push_back(uuid);
        auto offer = currentOffer(p);
        auto sig   = offer ? offer->signature : std::string{};
        auto& last = lastSignature()[uuid];
        if (sig == last) return true;
        last = sig;
        if (net::hasClientMod(p)) {
            CraftPreviewMsg m;
            if (offer) {
                m.kind       = offer->craft.kind;
                m.result     = game::itemToSnbt(offer->craft.result);
                m.cost       = offer->craft.cost;
                m.affordable = creative(p) || playerLevel(p) >= offer->craft.cost;
            }
            net::sendToClient(p, m);
        } else if (offer && !p.isSimulated()) {
            // Vanilla clients can't show the result in the vanilla screen: ask with a form.
            static_cast<ServerPlayer&>(p).initiateContainerClose();
            auto o = *offer;
            later(10, [uuid, o] {
                if (auto* pl = playerByUuid(uuid)) offerForm(*pl, o);
            });
        }
        return true;
    });
    std::erase_if(lastSignature(), [&](auto const& kv) { return std::find(seen.begin(), seen.end(), kv.first) == seen.end(); });
}

// ---- librarians --------------------------------------------------------------------------------

} // namespace

bool isLibrarian(MerchantRecipeList const& list) {
    // Bedrock lists every tier's trades (locked ones too); only librarians sell enchanted books.
    for (auto const& r : *list.mRecipeList)
        if (game::itemId(*r.mSell) == "minecraft:enchanted_book" && game::itemId(*r.mBuyB) == "minecraft:book") return true;
    return false;
}

bool isMaster(Actor const& villager) {
    // The unlocked trade tier (0 novice .. 4 master), synched as actor data.
    return villager.mEntityData->getInt(static_cast<ushort>(ActorDataIDs::TradeTier)) >= 4;
}

std::optional<MerchantRecipe> exchangeRecipe(int type) {
    auto const* d = defOf(type);
    if (!d) return std::nullopt;
    auto book = game::makeItem("minecraft:enchanted_book", 1);
    setEnchants(book, {{type, d->maxLevel}});
    auto anyTome = game::makeItem(kTomeId, 1);
    auto out     = makeTome(type);
    if (game::isEmpty(book) || game::isEmpty(anyTome) || game::isEmpty(out)) return std::nullopt;
    MerchantRecipe r{ItemInstance(anyTome), ItemInstance(book), ItemInstance(out)};
    r.mTier             = 4;
    r.mMaxUses          = 3;
    r.mTraderExp        = 3;
    r.mPriceMultiplierA = 0.2f;
    r.mPriceMultiplierB = 0.2f;
    return r;
}

namespace testing {
void openStation(Player& player, bool anvil, BlockPos const& pos) {
    forcedStations()[uuidOf(player)] = {anvil ? SharedTypes::Legacy::ContainerType::Anvil : SharedTypes::Legacy::ContainerType::SmithingTable, pos};
}
void closeStation(Player& player) { forcedStations().erase(uuidOf(player)); }
} // namespace testing

bool addLibrarianExchange(Actor& villager) {
    if (!cfg().enableAncientTomes || !cfg().librariansExchangeAncientTomes) return false;
    auto key    = "lp:enchanting:librarian:" + std::to_string(game::entities::idOf(villager));
    auto* offers = villager.getTradeOffers();
    if (!offers || !isLibrarian(*offers) || !isMaster(villager) || game::db::load(key)) return false;
    auto valid = validTomeEnchants();
    if (valid.empty()) return false;
    int  type = valid[static_cast<size_t>(randInt(static_cast<int>(valid.size())))];
    auto r    = exchangeRecipe(type);
    if (!r) return false;
    offers->mRecipeList->push_back(*r);
    game::db::save(key, {{"enchant", defOf(type)->id}});
    log().info("enchanting: a master librarian now exchanges {} tomes", defOf(type)->id);
    return true;
}

namespace {

void tickLibrarians() {
    if (!cfg().enableAncientTomes || !cfg().librariansExchangeAncientTomes) return;
    for (auto* a : game::entities::ofType("minecraft:villager_v2"))
        if (a) addLibrarianExchange(*a);
}

int64_t gTick = 0;

class TomesModule {
public:
    void install() {
        LootFillHook::hook();
        LootPoolHook::hook();
        DestroyProgressHook::hook();
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::ServerLevelTickEvent>([](auto&) {
            ++gTick;
            if (gTick % 4 == 0) tickStations();
            if (gTick % 200 == 0) tickLibrarians();
        }));
        mHandlers.push_back(net::onServer<CraftMsg>([](Player& p, CraftMsg const&) {
            if (auto o = currentOffer(p)) applyOffer(p, *o);
        }));
        static bool once = false;
        if (!once) {
            once = true;
            game::db::onWorldClosed([] {
                lastSignature().clear();
                pendingForms().clear();
                forcedStations().clear();
            });
        }
    }
    void uninstall() {
        LootFillHook::unhook();
        LootPoolHook::unhook();
        DestroyProgressHook::unhook();
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        for (auto h : mHandlers) net::removeHandler(h);
        mHandlers.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    std::vector<net::HandlerId>         mHandlers;
};

// ---- commands ----------------------------------------------------------------------------------

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload<TomeCommandParams>().text("enchanting").text("tome").required("enchant").execute(
        [](CommandOrigin const& origin, CommandOutput& output, TomeCommandParams const& params) {
            auto* p = commands::playerOf(origin);
            if (!p || !commands::isAdmin(origin)) return output.error("Players with operator permission only.");
            auto const* d = defById(params.enchant);
            if (!d) return output.error("Unknown enchantment " + params.enchant);
            giveOrDrop(*p, makeTome(d->type));
            output.success("Gave an Ancient Tome of " + d->id);
        }
    );
    cmd.overload<RuneCommandParams>().text("enchanting").text("rune").required("color").execute(
        [](CommandOrigin const& origin, CommandOutput& output, RuneCommandParams const& params) {
            auto* p = commands::playerOf(origin);
            if (!p || !commands::isAdmin(origin)) return output.error("Players with operator permission only.");
            auto const& colors = runeColors();
            if (std::find(colors.begin(), colors.end(), params.color) == colors.end()) return output.error("Unknown rune colour");
            auto stack = p->getSelectedItem();
            if (!canHaveRune(stack)) return output.error("Hold an enchanted item.");
            setStringTag(stack, kRuneTag, params.color);
            auto& pinv = game::inv::playerInventory(*p);
            pinv.setItem(p->getSelectedItemSlot(), stack);
            game::inv::sync(*p);
            output.success("Applied the " + params.color + " rune");
        }
    );
});

} // namespace

} // namespace lp::feature::enchanting

LP_REGISTER_MODULE(lp::feature::enchanting::TomesModule);
