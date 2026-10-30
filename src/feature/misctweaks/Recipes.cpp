// AutomaticRecipeUnlock (server side): every recipe on join, limited crafting, no recipe book.

#include <vector>

#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/world/ServerLevelTickEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/SetPlayerInventoryOptionsPacket.h"
#include "mc/server/ServerLevel.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/ServerCommandOrigin.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/InventoryLayout.h"
#include "mc/world/inventory/InventoryOptions.h"
#include "mc/world/level/Level.h"

#include "feature/misctweaks/Internal.h"
#include "game/Entities.h"
#include "mod/Log.h"

namespace lp::feature::misctweaks {

/// Runs a command as the server; returns the success count (-1 without a level).
int runServerCommand(std::string const& command) {
    auto level = ll::service::getLevel();
    if (!level) return -1;
    ServerCommandOrigin origin("lepton-misctweaks", level->asServer(), CommandPermissionLevel::Owner, DimensionType(0));
    auto out = ll::command::CommandRegistrar::getServerInstance().executeCommand(command, origin);
    return static_cast<int>(out.mSuccessCount);
}

/// Gives `player` every recipe except the ignored ones. Returns the success count of the give.
int unlockAllRecipes(Player& player) {
    auto const& c    = cfg();
    std::string name = "\"" + player.getRealName() + "\"";
    int         ok   = runServerCommand("recipe give " + name + " *");
    for (auto const& id : c.ignoredRecipes) runServerCommand("recipe take " + name + " " + id);
    if (c.forceLimitedCrafting) runServerCommand("gamerule dolimitedcrafting true");
    log().info("misctweaks: unlocked all recipes for {} (ok={})", player.getRealName(), ok);
    return ok;
}

/// Sends the inventory options without a recipe book.
void closeRecipeBook(Player& player) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::SetPlayerInventoryOptions);
    if (!pkt) return;
    auto& p       = static_cast<SetPlayerInventoryOptionsPacket&>(*pkt);
    auto& options = p.mInventoryOptions.get();
    options.mLayoutInv   = InventoryLayout::InventoryOnly;
    options.mLayoutCraft = InventoryLayout::InventoryOnly;
    pkt->sendTo(player);
}

namespace {

struct Pending {
    int64_t id;
    int     ticks;
};
std::vector<Pending>   gPending;
ll::event::ListenerPtr gJoin, gTick;

Player* asPlayer(Actor* a) { return a && a->isType(ActorType::Player) ? static_cast<Player*>(a) : nullptr; }

void onTick(ll::event::ServerLevelTickEvent&) {
    if (gPending.empty()) return;
    std::vector<Pending> keep;
    for (auto& p : gPending) {
        if (--p.ticks > 0) {
            keep.push_back(p);
            continue;
        }
        Player* player = asPlayer(game::entities::find(p.id));
        if (!player) continue;
        auto const& c = cfg();
        if (!c.enableAutomaticRecipeUnlock) continue;
        unlockAllRecipes(*player);
        if (c.disableRecipeBook) closeRecipeBook(*player);
    }
    gPending = std::move(keep);
}

Player* playerByNetworkId(NetworkIdentifier const& id) {
    auto    level = ll::service::getLevel();
    Player* found = nullptr;
    if (!level) return nullptr;
    level->forEachPlayer([&](Player& p) {
        if (p.isSimulated() || !(p.getNetworkIdentifier() == id)) return true;
        found = &p;
        return false;
    });
    return found;
}

LL_TYPE_INSTANCE_HOOK(
    InventoryOptionsHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&               source,
    SetPlayerInventoryOptionsPacket const& packet
) {
    origin(source, packet);
    auto const& c = cfg();
    if (!c.enableAutomaticRecipeUnlock || !c.disableRecipeBook) return;
    auto const& o = packet.mInventoryOptions.get();
    if (o.mLayoutInv == InventoryLayout::InventoryOnly && o.mLayoutCraft == InventoryLayout::InventoryOnly) return;
    if (auto* player = playerByNetworkId(source)) closeRecipeBook(*player);
}

} // namespace

void installRecipes() {
    auto& bus = ll::event::EventBus::getInstance();
    gJoin     = bus.emplaceListener<ll::event::PlayerJoinEvent>([](ll::event::PlayerJoinEvent& ev) {
        if (!cfg().enableAutomaticRecipeUnlock) return;
        gPending.push_back({game::entities::idOf(ev.self()), 5});
    });
    gTick     = bus.emplaceListener<ll::event::ServerLevelTickEvent>(onTick);
    InventoryOptionsHook::hook();
}

void uninstallRecipes() {
    auto& bus = ll::event::EventBus::getInstance();
    if (gJoin) bus.removeListener(gJoin);
    if (gTick) bus.removeListener(gTick);
    gJoin = gTick = nullptr;
    gPending.clear();
    InventoryOptionsHook::unhook();
}

} // namespace lp::feature::misctweaks
