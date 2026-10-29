// U19 Emotes, server side: validation, the animation broadcast, the mod-client messages, the
// vanilla-client form and `/quark emote`. See Emotes.h for the flow and docs/spec/emotes.md.

#include "feature/emotes/Emotes.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>

#include "ll/api/chrono/GameChrono.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/coro/CoroTask.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "ll/api/service/Bedrock.h"

#include "mc/legacy/ActorRuntimeID.h"
#include "mc/molang/MolangVersion.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/AnimateEntityPacket.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"
#include "mc/legacy/ActorUniqueID.h"

#include "feature/emotes/Builtins.h"
#include "feature/emotes/EmoteScript.h"
#include "feature/emotes/EmotesConfig.h"
#include "game/Forms.h"
#include "mod/Commands.h"
#include "mod/Config.h"
#include "mod/I18n.h"
#include "mod/Log.h"
#include "mod/Module.h"
#include "net/Net.h"

LP_REGISTER_CONFIG("emotes", lp::feature::emotes::EmotesConfig);
LP_REGISTER_MESSAGE(lp::feature::emotes::EmoteRequest);
LP_REGISTER_MESSAGE(lp::feature::emotes::EmotePlay);
LP_REGISTER_MESSAGE(lp::feature::emotes::EmoteList);

namespace lp::feature::emotes {

// Command parameters need external linkage.
struct EmoteCommandParams {
    std::string name;
};

namespace {

EmotesConfig const& cfg() { return config::get<EmotesConfig>(); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::mutex                          gSentMutex;
std::vector<testing::SentAnimation> gSent;

/// Plays `animation` on the player for every client that sees them (and the player).
void sendAnimation(Player& player, std::string const& animation) {
    auto pkt = MinecraftPackets::createPacket(MinecraftPacketIds::TriggerAnimation);
    if (pkt) {
        auto& anim                   = static_cast<AnimateEntityPacket&>(*pkt);
        anim.mRuntimeIds             = std::vector<ActorRuntimeID>{player.getRuntimeID()};
        anim.mAnimation              = animation;
        anim.mNextState              = "default";
        anim.mStopExpression         = std::string(script::kStopExpression);
        anim.mStopExpressionVersion  = MolangVersion::Latest;
        anim.mController             = std::string(script::kController);
        anim.mBlendOutTime           = 0.f;
        // Everyone who sees the player, then the player itself (exactly once).
        pkt->sendTo(static_cast<Actor const&>(player), player);
        if (!player.isSimulatedPlayer()) pkt->sendTo(player);
    }
    std::lock_guard lock(gSentMutex);
    if (gSent.size() > 64) gSent.erase(gSent.begin());
    gSent.push_back({player.getOrCreateUniqueID().rawID, animation});
}

void sendList(Player& player) {
    if (!net::hasClientMod(player)) return;
    EmoteList list;
    list.enabled = cfg().enableEmotes;
    if (list.enabled) list.emotes = availableFor(player);
    list.tier = tierOf(player);
    net::sendToClient(player, list);
}

/// After the handshake: on the next tick, so the welcome stays the first message.
void sendListLater(Player& player) {
    auto id = player.getOrCreateUniqueID().rawID;
    ll::coro::keepThis([id]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(1);
        auto level = ll::service::getLevel();
        if (!level) co_return;
        ActorUniqueID uid;
        uid.rawID = id;
        if (auto* p = level->getPlayer(uid)) sendList(*p);
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}

/// Requests from one client are accepted at most every 5 ticks (Quark has no limit; this only
/// stops a modified client from flooding every other client with emotes).
std::unordered_map<std::int64_t, std::uint64_t> gLastRequest;

bool throttled(Player const& p) {
    auto level = ll::service::getLevel();
    if (!level) return false;
    auto now  = level->getCurrentTick().tickID;
    auto id   = p.getOrCreateUniqueID().rawID;
    auto it   = gLastRequest.find(id);
    if (it != gLastRequest.end() && now - it->second < 5) return true;
    gLastRequest[id] = now;
    return false;
}

} // namespace

int tierOf(Player const& player) {
    auto const& tiers = cfg().emoteTiers;
    if (auto it = tiers.find(lower(player.getRealName())); it != tiers.end()) return it->second;
    if (auto it = tiers.find(player.getRealName()); it != tiers.end()) return it->second;
    auto xuid = const_cast<Player&>(player).getXuid();
    if (!xuid.empty())
        if (auto it = tiers.find(xuid); it != tiers.end()) return it->second;
    return cfg().defaultEmoteTier;
}

std::vector<std::string> availableFor(Player const& player) {
    std::vector<std::string> out;
    for (auto const& id : cfg().enabledEmotes) {
        auto const* b = findBuiltin(id);
        if (b && !b->patreon && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    int tier = tierOf(player);
    for (auto const& b : builtinEmotes()) {
        if (!b.patreon) continue;
        auto const* e = compiledBuiltin(b.id);
        if (e && e->tier <= tier) out.emplace_back(b.id);
    }
    return out;
}

bool isValidCustomId(std::string_view id) {
    if (!id.starts_with("custom:")) return false;
    auto name = id.substr(7);
    if (name.empty() || name.size() > 64 || name.front() == '.') return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
    }) && name.find("..") == std::string_view::npos;
}

PlayResult playEmote(Player& player, std::string const& emote) {
    if (!cfg().enableEmotes) return PlayResult::Disabled;
    int tier = tierOf(player);
    if (!isValidCustomId(emote)) {
        auto const* b = findBuiltin(emote);
        if (!b) return PlayResult::Unknown;
        if (!b->patreon) {
            auto const& en = cfg().enabledEmotes;
            if (std::find(en.begin(), en.end(), emote) == en.end()) return PlayResult::NotEnabled;
        }
        auto const* e = compiledBuiltin(emote);
        if (!e || e->tier > tier) return PlayResult::TierTooLow;
    }
    sendAnimation(player, script::animationId(emote));
    EmotePlay msg{emote, player.getOrCreateUniqueID().rawID, player.getRealName(), tier};
    net::broadcast(msg);
    log().info("emotes: {} plays {}", player.getRealName(), emote);
    return PlayResult::Played;
}

std::string emoteName(Player const& player, std::string const& emote) {
    if (emote.starts_with("custom:")) return emote.substr(7);
    std::string fallback = emote;
    if (!fallback.empty()) fallback[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(fallback[0])));
    return trFor(player, "quark.emote." + emote, fallback);
}

void openEmoteForm(Player& player) {
    if (!cfg().enableEmotes) {
        player.sendMessage(trFor(player, "lp.emotes.disabled", "Emotes are disabled on this server."));
        return;
    }
    game::forms::SimpleForm form(trFor(player, "quark.gui.button.emotes", "Emotes"),
                                 trFor(player, "lp.emotes.form.content", "Pick an emote to play it."));
    for (auto const& id : availableFor(player)) {
        auto const* b     = findBuiltin(id);
        auto        label = emoteName(player, id);
        if (b && b->patreon) label += " " + trFor(player, "lp.emotes.form.patreon", "(Patreon)");
        form.button(label, [id](Player& p) { playEmote(p, id); }, "textures/lepton/emotes/" + id);
    }
    form.sendTo(player);
    log().info("emotes: form sent to {}", player.getRealName());
}

std::vector<testing::SentAnimation> testing::takeSentAnimations() {
    std::lock_guard lock(gSentMutex);
    return std::exchange(gSent, {});
}

namespace {

std::string resultMessage(Player const& p, PlayResult r, std::string const& emote) {
    switch (r) {
    case PlayResult::Played: return {};
    case PlayResult::Disabled: return trFor(p, "lp.emotes.disabled", "Emotes are disabled on this server.");
    case PlayResult::Unknown: return trFor(p, "lp.emotes.unknown", "Unknown emote: {}", emote);
    case PlayResult::NotEnabled: return trFor(p, "lp.emotes.not_enabled", "The {} emote is disabled on this server.", emote);
    case PlayResult::TierTooLow: return trFor(p, "lp.emotes.tier", "The {} emote is a Patreon emote.", emote);
    }
    return {};
}

} // namespace

LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("emote").execute([](CommandOrigin const& origin, CommandOutput& output) {
        auto* p = commands::playerOf(origin);
        if (!p) return output.error(tr("lp.emotes.players_only", "Only players can use emotes."));
        openEmoteForm(*p);
        output.success("");
    });
    cmd.overload<EmoteCommandParams>().text("emote").required("name").execute(
        [](CommandOrigin const& origin, CommandOutput& output, EmoteCommandParams const& params) {
            auto* p = commands::playerOf(origin);
            if (!p) return output.error(tr("lp.emotes.players_only", "Only players can use emotes."));
            auto r = playEmote(*p, params.name);
            if (r == PlayResult::Played) return output.success("");
            output.error(resultMessage(*p, r, params.name));
        }
    );
});

class EmotesModule {
public:
    void install() {
        mRequest = net::onServer<EmoteRequest>([](Player& p, EmoteRequest const& m) {
            if (m.emote.size() > 80 || throttled(p)) return;
            auto r = playEmote(p, m.emote);
            if (r != PlayResult::Played) log().debug("emotes: refused {} for {} ({})", m.emote, p.getRealName(), static_cast<int>(r));
        });
        mInstalled = true;
        static bool once = false; // these registries have no removal: register once per process
        if (!once) {
            once = true;
            net::onClientModJoined([this](Player& p) {
                if (mInstalled) sendListLater(p);
            });
            config::onReload([this] {
                if (!mInstalled) return;
                for (auto* p : net::modPlayers())
                    if (p) sendList(*p);
            });
        }
    }
    void uninstall() {
        net::removeHandler(mRequest);
        mInstalled = false;
    }

private:
    net::HandlerId mRequest   = 0;
    bool           mInstalled = false;
};

} // namespace lp::feature::emotes

LP_REGISTER_MODULE(lp::feature::emotes::EmotesModule);
