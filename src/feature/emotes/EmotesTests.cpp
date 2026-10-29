// Selftests of U19 (emotes.*, pat.* names are all "emotes.*"). Test positions: dx 1900..1999.

#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/server/SimulatedPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"

#include "feature/emotes/Builtins.h"
#include "feature/emotes/Emotes.h"
#include "feature/emotes/EmotesConfig.h"
#include "feature/emotes/PatTheDogs.h"
#include "game/Entities.h"
#include "game/Forms.h"
#include "game/Inventory.h"
#include "game/Items.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "net/Net.h"
#include "selftest/SelfTest.h"

namespace {

using lp::selftest::Context;
namespace em  = lp::feature::emotes;
namespace pat = lp::feature::emotes::pat;

/// Runs a test with Quark's defaults (whatever config/emotes.json says) and restores the
/// configured values afterwards.
struct ConfigGuard {
    em::EmotesConfig saved = lp::config::get<em::EmotesConfig>();
    ConfigGuard() { lp::config::mut<em::EmotesConfig>() = em::EmotesConfig{}; }
    ~ConfigGuard() { lp::config::mut<em::EmotesConfig>() = saved; }
    em::EmotesConfig& operator*() { return lp::config::mut<em::EmotesConfig>(); }
    em::EmotesConfig* operator->() { return &lp::config::mut<em::EmotesConfig>(); }
};

bool sentTo(Player const& p, std::string const& animation) {
    for (auto const& s : em::testing::takeSentAnimations())
        if (s.player == p.getOrCreateUniqueID().rawID && s.animation == animation) return true;
    return false;
}

bool outboxHas(Player const& p, std::string_view id, std::string_view fragment) {
    for (auto const& [mid, payload] : lp::net::testing::takeOutbox(p))
        if (mid == id && payload.find(fragment) != std::string::npos) return true;
    return false;
}

Actor* spawnAt(Context& ctx, std::string const& type, int dx) {
    auto pos = ctx.testPos(dx);
    auto* a  = lp::game::entities::spawn(pos.dim, type, Vec3(pos.x + 0.5f, static_cast<float>(pos.y), pos.z + 0.5f));
    ctx.require(a != nullptr, "spawned " + type);
    return a;
}

void prepareHand(Player& p) {
    auto& inv = lp::game::inv::playerInventory(p);
    inv.setItem(p.getSelectedItemSlot(), ItemStack{});
    p.setSneaking(true);
}

} // namespace

// ---- Emotes ----------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("emotes.builtins", [](Context& ctx) {
    ctx.check(em::builtinEmotes().size() == 18, "Quark ships 18 emotes");
    auto def = em::defaultEnabledEmotes();
    ctx.check(def.size() == 12 && def.front() == "no" && def.back() == "facepalm", "the 12 default emotes in Quark's order");
    for (auto const& b : em::builtinEmotes()) ctx.check(em::compiledBuiltin(b.id) && em::compiledBuiltin(b.id)->root, std::string(b.id) + " compiles");
    ctx.check(em::compiledBuiltin("dab")->tier == 1 && em::compiledBuiltin("zombie")->tier == 2, "patreon tiers");
});

LP_REGISTER_SELFTEST("emotes.play", [](Context& ctx) {
    ConfigGuard cfg;
    auto&       p = ctx.player();
    lp::net::testing::setClientMod(p, true);
    (void)lp::net::testing::takeOutbox(p);
    (void)em::testing::takeSentAnimations();
    ctx.check(em::playEmote(p, "wave") == em::PlayResult::Played, "wave plays");
    ctx.check(sentTo(p, "animation.lepton.emote.wave"), "the animation is sent for the player");
    ctx.check(outboxHas(p, "emotes:play", "\"wave\""), "mod clients get emotes:play");
    ctx.check(em::playEmote(p, "custom:My_Dance-2") == em::PlayResult::Played, "custom emotes are relayed");
    ctx.check(sentTo(p, "animation.lepton.emote.custom.My_Dance-2"), "custom animation id");
    lp::net::testing::setClientMod(p, false);
});

LP_REGISTER_SELFTEST("emotes.validation", [](Context& ctx) {
    ConfigGuard cfg;
    auto&       p = ctx.player();
    ctx.check(em::playEmote(p, "moonwalk") == em::PlayResult::Unknown, "unknown emotes are refused");
    ctx.check(em::playEmote(p, "custom:../x") == em::PlayResult::Unknown, "custom names can't leave the emote folder");
    ctx.check(em::playEmote(p, "dance") == em::PlayResult::TierTooLow, "patreon emotes need a tier");
    cfg->emoteTiers[p.getRealName()] = 1;
    ctx.check(em::tierOf(p) == 1, "emoteTiers by name");
    ctx.check(em::playEmote(p, "dance") == em::PlayResult::Played, "tier 1 plays dance");
    ctx.check(em::playEmote(p, "jet") == em::PlayResult::TierTooLow, "tier 1 can't play jet (tier 2)");
    auto avail = em::availableFor(p);
    ctx.check(avail.size() == 15 && avail[12] == "dance" && avail[14] == "dab", "available: 12 defaults + 3 tier-1 emotes");
    cfg->emoteTiers.clear();
    cfg->defaultEmoteTier = 2;
    ctx.check(em::availableFor(p).size() == 18, "defaultEmoteTier 2 unlocks every emote");
    cfg->enabledEmotes = {"yes", "wave"};
    ctx.check(em::playEmote(p, "no") == em::PlayResult::NotEnabled, "emotes removed from enabledEmotes are refused");
    auto list = em::availableFor(p);
    ctx.check(list.size() == 8 && list[0] == "yes" && list[1] == "wave", "the menu follows enabledEmotes' order");
    cfg->enableEmotes = false;
    ctx.check(em::playEmote(p, "yes") == em::PlayResult::Disabled, "a disabled module plays nothing");
});

LP_REGISTER_SELFTEST("emotes.request", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto&       p = ctx.player();
    lp::net::testing::setClientMod(p, true);
    em::testing::resetThrottle();
    (void)em::testing::takeSentAnimations();
    lp::net::testing::receiveFromClient(p, em::EmoteRequest{"yes"});
    ctx.check(sentTo(p, "animation.lepton.emote.yes"), "a client request plays the emote");
    lp::net::testing::receiveFromClient(p, em::EmoteRequest{"no"});
    ctx.check(em::testing::takeSentAnimations().empty(), "requests within 5 ticks are dropped");
    co_await ctx.ticks(6);
    lp::net::testing::receiveFromClient(p, em::EmoteRequest{"tpose"});
    ctx.check(em::testing::takeSentAnimations().empty(), "a client can't bypass the tier");
    co_await ctx.ticks(6);
    lp::net::testing::receiveFromClient(p, em::EmoteRequest{"no"});
    ctx.check(sentTo(p, "animation.lepton.emote.no"), "later requests play again");
    lp::net::testing::setClientMod(p, false);
});

LP_REGISTER_SELFTEST("emotes.form", [](Context& ctx) {
    ConfigGuard cfg;
    auto&       p = ctx.player();
    em::testing::resetThrottle();
    (void)em::testing::takeSentAnimations();
    em::openEmoteForm(p);
    ctx.check(lp::game::forms::testing::pendingTitle(p) == lp::trFor(p, "quark.gui.button.emotes", "Emotes"), "the emote form opens");
    ctx.check(lp::game::forms::testing::click(p, 2), "pick the third button");
    ctx.check(sentTo(p, "animation.lepton.emote.wave"), "the form plays the picked emote (wave)");
    em::openEmoteForm(p);
    ctx.check(lp::game::forms::testing::click(p, 2), "pick again at once");
    ctx.check(em::testing::takeSentAnimations().empty(), "a second pick within 5 ticks is dropped");
});

// ---- PatTheDogs ---------------------------------------------------------------------------------------

LP_REGISTER_SELFTEST("emotes.pat_wolf", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto&       p = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(1900)), "test area loaded");
    auto* wolf = spawnAt(ctx, "minecraft:wolf", 1900);
    auto  wid  = lp::game::entities::idOf(*wolf);
    prepareHand(p);
    ctx.check(pat::tryPet(p, *wolf) == pat::PetResult::Refused, "a wild wolf can't be pet (interaction cancelled)");
    lp::game::entities::triggerEvent(*wolf, "minecraft:on_tame");
    wolf->setOwner(p.getOrCreateUniqueID());
    co_await ctx.ticks(2);
    wolf = lp::game::entities::find(wid);
    ctx.require(wolf && wolf->isTame(), "the wolf is tamed");
    auto r = p.interact(*wolf, wolf->getPosition());
    ctx.check(r.mSuccess, "sneak + empty hand pets a tamed wolf (through the interact hook)");
    ctx.check(!wolf->isSitting(), "petting doesn't make it sit");
    p.setSneaking(false);
    ctx.check(pat::tryPet(p, *wolf) == pat::PetResult::None, "not sneaking: the vanilla interaction");
    p.setSneaking(true);
    lp::game::inv::playerInventory(p).setItem(p.getSelectedItemSlot(), lp::game::makeItem("minecraft:bone"));
    ctx.check(pat::tryPet(p, *wolf) == pat::PetResult::None, "an item in hand: the vanilla interaction");
    prepareHand(p);
    // With dogsWantLove the pet time is recorded: a second pet within 20 ticks is refused.
    cfg->dogsWantLove = 40;
    ctx.check(pat::tryPet(p, *wolf) == pat::PetResult::Pet, "pet");
    ctx.check(pat::tryPet(p, *wolf) == pat::PetResult::Refused, "pet again at once: refused");
    ctx.check(!pat::needsPets(*wolf), "just pet: doesn't need pets");
    pat::testing::setLastPet(*wolf, pat::testing::currentTick() - 100);
    ctx.check(pat::needsPets(*wolf), "not pet for 100 > 40 ticks: wants love");
    cfg->dogsWantLove = -1;
    ctx.check(!pat::needsPets(*wolf), "dogsWantLove -1: never");
    pat::testing::clearLastPet(*wolf);
    p.setSneaking(false);
    lp::game::entities::despawn(*wolf);
});

LP_REGISTER_SELFTEST("emotes.pat_mobs", [](Context& ctx) -> ll::coro::CoroTask<> {
    ConfigGuard cfg;
    auto&       p = ctx.player();
    ctx.require(co_await ctx.ensureLoaded(ctx.testPos(1902)), "test area loaded");
    auto* pig   = spawnAt(ctx, "minecraft:pig", 1902);
    auto* stand = spawnAt(ctx, "minecraft:armor_stand", 1904);
    prepareHand(p);
    ctx.check(pat::tryPet(p, *pig) == pat::PetResult::None, "pigs aren't pettable by default");
    cfg->pettableAllowlist = {"minecraft:pig"};
    ctx.check(pat::tryPet(p, *pig) == pat::PetResult::Pet, "pettableAllowlist makes pigs pettable");
    cfg->pettableAllowlist.clear();
    cfg->petAllMobs = true;
    ctx.check(pat::tryPet(p, *pig) == pat::PetResult::Pet, "petAllMobs");
    ctx.check(pat::tryPet(p, *stand) == pat::PetResult::None, "the denylist wins over petAllMobs");
    cfg->enablePatTheDogs = false;
    ctx.check(pat::tryPet(p, *pig) == pat::PetResult::None, "module disabled");
    p.setSneaking(false);
    lp::game::entities::despawn(*pig);
    lp::game::entities::despawn(*stand);
});
