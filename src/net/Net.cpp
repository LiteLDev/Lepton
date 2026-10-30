#include "net/Net.h"

#include <map>
#include <mutex>
#include <unordered_map>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/network/packet/Packet.h"
#include "ll/api/service/Bedrock.h"

#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/platform/UUID.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#ifdef LP_CLIENT
#include "ll/api/event/client/ClientExitLevelEvent.h"
#include "ll/api/event/world/ClientLevelTickEvent.h"

#include "client/Client.h"
#endif

#include "game/WorldDb.h"
#include "mod/Commands.h"
#include "mod/Log.h"
#include "mod/Main.h"
#include "mod/Module.h"
#include "selftest/SelfTest.h"

namespace lp::net {

// ---- built-in messages ---------------------------------------------------------------------------

struct Hello {
    static constexpr std::string_view kId = "lp:hello";
    int                               protocol = 0;
    std::string                       version;
};
struct Welcome {
    static constexpr std::string_view kId = "lp:welcome";
    int                               protocol = 0;
    std::string                       version;
};
/// Server -> client; the client answers `Pong` with the same nonce (used by the `net.*` selftests).
struct Ping {
    static constexpr std::string_view kId = "lp:ping";
    int64_t                           nonce = 0;
};
struct Pong {
    static constexpr std::string_view kId = "lp:pong";
    int64_t                           nonce = 0;
};
LP_REGISTER_MESSAGE(Hello);
LP_REGISTER_MESSAGE(Welcome);
LP_REGISTER_MESSAGE(Ping);
LP_REGISTER_MESSAGE(Pong);

// ---- wire packets --------------------------------------------------------------------------------

namespace wire {

constexpr uint64_t kMaxText = 4u << 20; ///< 4 MiB per field

/// One message: id + JSON payload. Two packet types so a local world (client and server in one
/// process, one handler per packet type) knows the direction.
template <class Derived>
class MessagePacket : public ll::network::PacketBase<Derived> {
public:
    std::string id;
    std::string payload;

    void write(BinaryStream& stream) const override {
        stream.writeString(id, nullptr, nullptr);
        stream.writeString(payload, nullptr, nullptr);
    }
    Bedrock::Result<void> read(ReadOnlyBinaryStream& stream) override {
        auto a = stream.getString(kMaxText);
        if (!a) return nonstd::make_unexpected(a.error());
        auto b = stream.getString(kMaxText);
        if (!b) return nonstd::make_unexpected(b.error());
        id      = std::move(*a);
        payload = std::move(*b);
        return {};
    }
};

class LeptonToServerPacket : public MessagePacket<LeptonToServerPacket> {};
class LeptonToClientPacket : public MessagePacket<LeptonToClientPacket> {};

} // namespace wire

// ---- state ---------------------------------------------------------------------------------------

namespace {

struct ClientInfo {
    int         protocol = 0;
    std::string version;
};

struct Handlers {
    std::map<std::string, std::vector<std::pair<HandlerId, detail::ServerRaw>>, std::less<>> server;
    std::map<std::string, std::vector<std::pair<HandlerId, detail::ClientRaw>>, std::less<>> client;
    HandlerId                                                                                next = 0;
};
Handlers& handlers() {
    static Handlers h;
    return h;
}
std::map<std::string, std::string, std::less<>>& messageTypes() {
    static std::map<std::string, std::string, std::less<>> m;
    return m;
}
std::vector<std::string>& duplicateIds() {
    static std::vector<std::string> v;
    return v;
}

// server thread
std::unordered_map<std::string, ClientInfo>&                                    clients() {
    static std::unordered_map<std::string, ClientInfo> m;
    return m;
}
std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>& outbox() {
    static std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> m;
    return m;
}
std::unordered_map<std::string, int64_t>& lastPong() {
    static std::unordered_map<std::string, int64_t> m;
    return m;
}
std::vector<std::function<void(Player&)>>& joinedListeners() {
    static std::vector<std::function<void(Player&)>> v;
    return v;
}

// client thread
bool                               gServerReady = false;
int                                gHelloTicks  = 0;
int                                gHelloSent   = 0;
std::vector<std::function<void()>>& readyListeners() {
    static std::vector<std::function<void()>> v;
    return v;
}

std::string uuidOf(Player const& p) { return p.getUuid().asString(); }

std::string modVersion() {
    auto const& v = Mod::getInstance().getSelf().getManifest().version;
    return v ? v->to_string() : std::string("0.0.0");
}

/// The (primary) player of a connection. Split-screen sub-clients (consoles only) share the
/// connection and are not told apart: the runtime packet does not carry the sub-client id.
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

template <class Fn>
void guarded(std::string_view what, std::string_view id, Fn&& fn) {
    try {
        fn();
    } catch (std::exception const& e) {
        log().error("net {} handler for {} failed: {}", what, id, e.what());
    }
}

void dispatchServer(Player& player, std::string_view id, std::string_view payload) {
    auto it = handlers().server.find(id);
    if (it == handlers().server.end()) {
        log().debug("net: no server handler for {}", id);
        return;
    }
    auto j = nlohmann::json::parse(payload, nullptr, false);
    if (j.is_discarded()) {
        log().warn("net: malformed {} from {}", id, player.getRealName());
        return;
    }
    auto list = it->second; // handlers may add/remove handlers
    for (auto const& [hid, fn] : list) guarded("server", id, [&] { fn(player, j); });
}

void dispatchClient(std::string_view id, std::string_view payload) {
    auto it = handlers().client.find(id);
    if (it == handlers().client.end()) {
        log().debug("net: no client handler for {}", id);
        return;
    }
    auto j = nlohmann::json::parse(payload, nullptr, false);
    if (j.is_discarded()) {
        log().warn("net: malformed {} from the server", id);
        return;
    }
    auto list = it->second;
    for (auto const& [hid, fn] : list) guarded("client", id, [&] { fn(j); });
}

} // namespace

namespace wire {

class LeptonToServerHandler : public ll::network::PacketHandlerBase<LeptonToServerHandler, LeptonToServerPacket> {
public:
    void handlePacket(NetworkIdentifier const& source, NetEventCallback&, LeptonToServerPacket const& packet) const {
        auto* player = playerByNetworkId(source);
        if (!player) {
            log().debug("net: {} from an unknown connection", packet.id);
            return;
        }
        // Until the handshake succeeded (same protocol) only the hello is accepted.
        if (packet.id != Hello::kId && !hasClientMod(*player)) {
            log().debug("net: {} from {} before the handshake: dropped", packet.id, player->getRealName());
            return;
        }
        dispatchServer(*player, packet.id, packet.payload);
    }
};

class LeptonToClientHandler : public ll::network::PacketHandlerBase<LeptonToClientHandler, LeptonToClientPacket> {
public:
    void handlePacket(NetworkIdentifier const&, NetEventCallback&, LeptonToClientPacket const& packet) const {
#ifdef LP_CLIENT
        dispatchClient(packet.id, packet.payload);
#else
        (void)packet;
#endif
    }
};

} // namespace wire

// ---- public API ----------------------------------------------------------------------------------

bool hasClientMod(Player const& player) {
    auto it = clients().find(uuidOf(player));
    return it != clients().end() && it->second.protocol == kProtocol;
}

std::string clientModVersion(Player const& player) {
    auto it = clients().find(uuidOf(player));
    return it == clients().end() ? std::string{} : it->second.version;
}

std::vector<Player*> modPlayers() {
    std::vector<Player*> out;
    auto                 level = ll::service::getLevel();
    if (!level) return out;
    level->forEachPlayer([&](Player& p) {
        if (hasClientMod(p)) out.push_back(&p);
        return true;
    });
    return out;
}

void onClientModJoined(std::function<void(Player&)> fn) { joinedListeners().push_back(std::move(fn)); }

bool serverHasMod() { return gServerReady; }
void onServerModReady(std::function<void()> fn) { readyListeners().push_back(std::move(fn)); }

void removeHandler(HandlerId id) {
    for (auto& [k, v] : handlers().server) std::erase_if(v, [id](auto const& e) { return e.first == id; });
    for (auto& [k, v] : handlers().client) std::erase_if(v, [id](auto const& e) { return e.first == id; });
}

namespace detail {

void registerMessage(std::string_view id, std::string_view type) {
    auto [it, inserted] = messageTypes().emplace(std::string(id), std::string(type));
    if (!inserted && it->second != type) duplicateIds().push_back(std::string(id) + " (" + it->second + ", " + std::string(type) + ")");
}

HandlerId addServerHandler(std::string_view id, ServerRaw fn) {
    auto hid = ++handlers().next;
    handlers().server[std::string(id)].emplace_back(hid, std::move(fn));
    return hid;
}

HandlerId addClientHandler(std::string_view id, ClientRaw fn) {
    auto hid = ++handlers().next;
    handlers().client[std::string(id)].emplace_back(hid, std::move(fn));
    return hid;
}

bool sendToClientRaw(Player& player, std::string_view id, std::string payload) {
    if (!hasClientMod(player) && id != Welcome::kId) return false;
    if (player.isSimulated()) {
        outbox()[uuidOf(player)].emplace_back(std::string(id), std::move(payload));
        return true;
    }
    wire::LeptonToClientPacket pkt;
    pkt.id      = std::string(id);
    pkt.payload = std::move(payload);
    pkt.sendTo(player);
    return true;
}

bool sendToServerRaw(std::string_view id, std::string payload) {
#ifdef LP_CLIENT
    if (!client::inLevel()) return false;
    if (!gServerReady && id != Hello::kId) return false;
    wire::LeptonToServerPacket pkt;
    pkt.id      = std::string(id);
    pkt.payload = std::move(payload);
    pkt.sendToServer();
    return true;
#else
    (void)id;
    (void)payload;
    return false;
#endif
}

void logDecodeError(std::string_view id, std::string_view error) { log().warn("net: cannot decode {}: {}", id, error); }

} // namespace detail

namespace testing {

void setClientMod(Player& player, bool hasMod) {
    if (hasMod) clients()[uuidOf(player)] = ClientInfo{kProtocol, "test"};
    else clients().erase(uuidOf(player));
}

std::vector<std::pair<std::string, std::string>> takeOutbox(Player const& player) {
    auto it = outbox().find(uuidOf(player));
    if (it == outbox().end()) return {};
    auto v = std::move(it->second);
    outbox().erase(it);
    return v;
}

void receiveFromClientRaw(Player& player, std::string_view id, std::string_view payload) {
    dispatchServer(player, id, payload);
}

} // namespace testing

// ---- module --------------------------------------------------------------------------------------

namespace {
void serverHello(Player& p, Hello const& m) {
    bool compatible = m.protocol == kProtocol;
    auto key        = uuidOf(p);
    bool fresh      = !clients().contains(key);
    clients()[key]  = ClientInfo{m.protocol, m.version};
    detail::sendToClientRaw(p, Welcome::kId, detail::encode(Welcome{kProtocol, modVersion()}));
    if (!fresh) return;
    if (!compatible) {
        log().warn("Lepton client mod of {} speaks protocol {} (server {}): treated as vanilla", p.getRealName(), m.protocol, kProtocol);
        return;
    }
    log().info("Lepton client mod handshake: {} (version {}, protocol {})", p.getRealName(), m.version, m.protocol);
    for (auto const& fn : joinedListeners()) guarded("joined", Hello::kId, [&] { fn(p); });
}

#ifdef LP_CLIENT
void clientWelcome(Welcome const& m) {
    if (gServerReady) return;
    if (m.protocol != kProtocol) {
        log().warn("Lepton server speaks protocol {} (client {}): companion features off", m.protocol, kProtocol);
        gHelloSent = 1000; // stop asking
        return;
    }
    gServerReady = true;
    log().info("Lepton server handshake: version {}, protocol {}", m.version, m.protocol);
    for (auto const& fn : readyListeners()) guarded("ready", Welcome::kId, [&] { fn(); });
}

void clientTick() {
    if (gServerReady || gHelloSent >= 15 || !client::currentPlayer()) return;
    if (gHelloTicks-- > 0) return;
    gHelloTicks = 40;
    ++gHelloSent;
    detail::sendToServerRaw(Hello::kId, detail::encode(Hello{kProtocol, modVersion()}));
}

void clientReset() {
    gServerReady = false;
    gHelloTicks  = 20;
    gHelloSent   = 0;
}
#endif

struct NetStatusParams {};
} // namespace

class NetModule {
public:
    void install() {
        for (auto const& d : duplicateIds()) log().error("net: duplicate message id {}", d);
        auto& bus = ll::event::EventBus::getInstance();
        mListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>([](ll::event::PlayerDisconnectEvent& ev) {
            clients().erase(uuidOf(ev.self()));
            outbox().erase(uuidOf(ev.self()));
        }));
        mHandlers.push_back(onServer<Hello>(serverHello));
        mHandlers.push_back(onServer<Pong>([](Player& p, Pong const& m) {
            lastPong()[uuidOf(p)] = m.nonce;
            log().info("net: pong {} from {}", m.nonce, p.getRealName());
        }));
        game::db::onWorldClosed([] {
            clients().clear();
            outbox().clear();
        });
#ifdef LP_CLIENT
        clientReset();
        mHandlers.push_back(onClient<Welcome>(clientWelcome));
        mHandlers.push_back(onClient<Ping>([](Ping const& m) { sendToServer(Pong{m.nonce}); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientLevelTickEvent>([](auto&) { clientTick(); }));
        mListeners.push_back(bus.emplaceListener<ll::event::ClientExitLevelEvent>([](auto&) { clientReset(); }));
#endif
    }
    void uninstall() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
        for (auto h : mHandlers) removeHandler(h);
        mHandlers.clear();
    }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
    std::vector<HandlerId>              mHandlers;
};

namespace {
/// `/quark net`: who runs the client mod (op / console).
LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("net").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
        auto level = ll::service::getLevel();
        int  total = 0, mods = 0;
        if (level) {
            level->forEachPlayer([&](Player& p) {
                if (p.isSimulated()) return true;
                ++total;
                bool mod = hasClientMod(p);
                if (mod) ++mods;
                auto line = "Lepton net: " + p.getRealName() + " mod=" + (mod ? "yes" : "no") + " version=" + clientModVersion(p);
                log().info("{}", line);
                output.success(line);
                return true;
            });
        }
        auto summary = "Lepton net: " + std::to_string(mods) + " of " + std::to_string(total) + " player(s) run the client mod";
        log().info("{}", summary);
        output.success(summary);
    });
});

/// `/quark net ping`: sends a Ping to every mod client (they answer with a Pong, logged).
LP_REGISTER_SUBCOMMAND([](ll::command::CommandHandle& cmd) {
    cmd.overload().text("net").text("ping").execute([](CommandOrigin const& origin, CommandOutput& output) {
        if (!commands::isAdmin(origin)) return output.error("You need operator permission.");
        static int64_t nonce = 0;
        int            n     = broadcast(Ping{++nonce});
        output.success("Lepton net: ping " + std::to_string(nonce) + " sent to " + std::to_string(n) + " client(s)");
    });
});
} // namespace

// ---- selftests ------------------------------------------------------------------------------------

// Message types need external linkage (reflection), so no anonymous namespace.
struct ProbeMessage {
    static constexpr std::string_view kId = "lp:probe";
    int                               number = 0;
    std::string                       text;
    std::vector<int>                  list;
};
LP_REGISTER_MESSAGE(ProbeMessage);

namespace {
using selftest::Context;

LP_REGISTER_SELFTEST("net.messages", [](Context& ctx) {
    ctx.check(duplicateIds().empty(), "no duplicate message ids");
    for (auto id : {"lp:hello", "lp:welcome", "lp:ping", "lp:pong"})
        ctx.check(messageTypes().contains(id), std::string("built-in message registered: ") + id);
    ProbeMessage in{42, "héllo \"q\"", {1, 2, 3}};
    ProbeMessage out{};
    auto         j = nlohmann::json::parse(detail::encode(in), nullptr, false);
    ctx.require(!j.is_discarded(), "encode gives JSON");
    ctx.check(detail::decode(j, out), "decode succeeds");
    ctx.check(out.number == 42 && out.text == in.text && out.list == in.list, "round trip keeps every field");
});

LP_REGISTER_SELFTEST("net.simulated", [](Context& ctx) {
    auto& p = ctx.player();
    testing::setClientMod(p, false);
    ctx.check(!hasClientMod(p), "simulated players have no client mod");
    ctx.check(!sendToClient(p, ProbeMessage{1}), "nothing is sent to a client without the mod");
    (void)testing::takeOutbox(p);

    testing::receiveFromClient(p, Hello{kProtocol, "test"});
    ctx.check(hasClientMod(p), "hello completes the handshake");
    auto box = testing::takeOutbox(p);
    ctx.check(box.size() == 1 && box[0].first == "lp:welcome", "the server answers welcome");

    ProbeMessage got{};
    int          calls = 0;
    auto         h     = onServer<ProbeMessage>([&](Player&, ProbeMessage const& m) {
        got = m;
        ++calls;
    });
    testing::receiveFromClient(p, ProbeMessage{7, "x", {9}});
    ctx.check(calls == 1 && got.number == 7 && got.list == std::vector<int>{9}, "server handler gets the typed message");
    testing::receiveFromClientRaw(p, ProbeMessage::kId, "{not json");
    ctx.check(calls == 1, "malformed payloads are dropped");
    removeHandler(h);
    testing::receiveFromClient(p, ProbeMessage{8});
    ctx.check(calls == 1, "removed handlers are not called");

    ctx.check(sendToClient(p, ProbeMessage{5, "y"}), "sent to a mod client");
    box = testing::takeOutbox(p);
    ctx.check(box.size() == 1 && box[0].first == "lp:probe" && box[0].second.find("\"y\"") != std::string::npos, "outbox holds the payload");
    testing::setClientMod(p, false);
});

/// With a real client mod connected (client e2e: local world or remote-mod), a ping is answered.
LP_REGISTER_SELFTEST("net.client_roundtrip", [](Context& ctx) -> ll::coro::CoroTask<> {
    std::vector<std::string> real;
    std::vector<std::string> modded;
    if (auto level = ll::service::getLevel()) {
        level->forEachPlayer([&](Player& p) {
            if (p.isSimulated()) return true;
            real.push_back(uuidOf(p));
            if (hasClientMod(p)) modded.push_back(uuidOf(p));
            return true;
        });
    }
    ctx.info("real players: " + std::to_string(real.size()) + ", with the client mod: " + std::to_string(modded.size()));
    if (modded.empty()) co_return;
    static int64_t nonce = 1000;
    int64_t        want  = ++nonce;
    ctx.check(broadcast(Ping{want}) == static_cast<int>(modded.size()), "ping sent to every mod client");
    for (int i = 0; i < 100; ++i) {
        bool all = true;
        for (auto const& u : modded) all = all && lastPong()[u] == want;
        if (all) break;
        co_await ctx.ticks(1);
    }
    for (auto const& u : modded) ctx.check(lastPong()[u] == want, "pong received from " + u);
    ctx.info("ping/pong round trip ok");
});
} // namespace

} // namespace lp::net

LP_REGISTER_MODULE_ORDERED(lp::net::NetModule, lp::ModuleOrder::Net);
