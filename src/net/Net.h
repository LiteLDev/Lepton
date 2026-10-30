#pragma once

// Companion protocol between the Lepton server (BDS or a local world's integrated server) and the
// Lepton client mod.
//
// Transport: LeviLamina custom runtime packets (`ll::network::PacketBase`, carried on the wire
// as `MinecraftPacketIds::LeviLaminaRuntimePacket` + a 64-bit hash of the packet type). Two
// packet types exist, one per direction (client->server, server->client), each holding a
// message id and a JSON payload. They work over RakNet/NetherNet between BDS and a modded
// client, and through the loopback sender in local worlds.
//
// Handshake: after joining a world the client mod sends `lp:hello` (protocol, version) until
// the server answers `lp:welcome`. The server then knows `hasClientMod(player)`; the client knows
// `serverHasMod()`. The server never sends Lepton packets to a player without the client mod, so
// vanilla clients are never sent unknown packets (they get the fallbacks: forms, vanilla
// screens). A server without LeviLamina/Lepton ignores `lp:hello` (it is never answered).
//
// Messages are reflectable aggregates with a unique `kId` ("<unit>:<name>"), serialized with
// LeviLamina reflection (JSON). They need external linkage (not in an anonymous namespace):
//
//     struct SortRequest {
//         static constexpr std::string_view kId = "inventory:sort";
//         int  container = 0;
//         bool playerInventory = false;
//     };
//     LP_REGISTER_MESSAGE(SortRequest);                  // .cpp, once per message type
//
//     // server (install()):
//     mSortHandler = net::onServer<SortRequest>([](Player& p, SortRequest const& m) { ... });
//     // client (install(), client build):
//     net::sendToServer(SortRequest{1, false});
//     // server -> client
//     if (net::hasClientMod(player)) net::sendToClient(player, SomeMessage{...});
//
// Threads: server handlers run on the server thread, client handlers on the client thread.
// In a local world both sides live in one process; a message id may have handlers on both sides.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"

#include "ll/api/reflection/Deserialization.h"
#include "ll/api/reflection/Serialization.h"

#include "mod/Registry.h"

class Player;

namespace lp::net {

/// Bumped on incompatible protocol changes. Peers with another protocol are treated as vanilla.
inline constexpr int kProtocol = 1;

template <class T>
concept Message = requires {
    { T::kId } -> std::convertible_to<std::string_view>;
};

// ---- server side -------------------------------------------------------------------------------

/// The player's client runs the Lepton client mod with our protocol (handshake done).
[[nodiscard]] bool hasClientMod(Player const& player);
/// The player's client mod version ("" without the mod).
[[nodiscard]] std::string clientModVersion(Player const& player);
/// Online players with the client mod.
[[nodiscard]] std::vector<Player*> modPlayers();
/// Called (server thread) when a player's client mod completes the handshake.
void onClientModJoined(std::function<void(Player&)> fn);

// ---- client side (client build; no-ops on the server build) -------------------------------------

/// The server we are connected to runs Lepton with our protocol (handshake done).
[[nodiscard]] bool serverHasMod();
/// Called (client thread) when the server answers the handshake.
void onServerModReady(std::function<void()> fn);

// ---- handlers ------------------------------------------------------------------------------------

using HandlerId = int;
/// Removes a handler added with `onServer` / `onClient`.
void removeHandler(HandlerId id);

namespace detail {
using ServerRaw = std::function<void(Player&, nlohmann::json const&)>;
using ClientRaw = std::function<void(nlohmann::json const&)>;

void      registerMessage(std::string_view id, std::string_view type);
HandlerId addServerHandler(std::string_view id, ServerRaw fn);
HandlerId addClientHandler(std::string_view id, ClientRaw fn);
bool      sendToClientRaw(Player& player, std::string_view id, std::string payload);
bool      sendToServerRaw(std::string_view id, std::string payload);
void      logDecodeError(std::string_view id, std::string_view error);

template <Message T>
std::string encode(T const& msg) {
    auto j = ll::reflection::serialize<nlohmann::ordered_json>(msg);
    return j ? j->dump() : std::string("{}");
}
template <Message T>
bool decode(nlohmann::json const& j, T& out) {
    auto r = ll::reflection::deserialize(out, j);
    if (!r) logDecodeError(T::kId, r.error().message());
    return static_cast<bool>(r);
}

template <Message T>
struct MessageRegistrar {
    explicit MessageRegistrar(std::string_view type) { registerMessage(T::kId, type); }
};
} // namespace detail

/// Server: handles `T` sent by a client mod. Returns an id for `removeHandler`.
template <Message T>
HandlerId onServer(std::function<void(Player&, T const&)> fn) {
    return detail::addServerHandler(T::kId, [fn = std::move(fn)](Player& p, nlohmann::json const& j) {
        T msg{};
        if (detail::decode(j, msg)) fn(p, msg);
    });
}

/// Client: handles `T` sent by the server.
template <Message T>
HandlerId onClient(std::function<void(T const&)> fn) {
    return detail::addClientHandler(T::kId, [fn = std::move(fn)](nlohmann::json const& j) {
        T msg{};
        if (detail::decode(j, msg)) fn(msg);
    });
}

/// Server: sends `msg` to the player's client mod. False (nothing sent) without the client mod.
template <Message T>
bool sendToClient(Player& player, T const& msg) {
    return detail::sendToClientRaw(player, T::kId, detail::encode(msg));
}

/// Server: sends `msg` to every player with the client mod. Returns how many got it.
template <Message T>
int broadcast(T const& msg) {
    int n = 0;
    for (auto* p : modPlayers())
        if (p && sendToClient(*p, msg)) ++n;
    return n;
}

/// Client: sends `msg` to the server. False when not connected or the server has no Lepton.
template <Message T>
bool sendToServer(T const& msg) {
    return detail::sendToServerRaw(T::kId, detail::encode(msg));
}

/// Test seam for server selftests with SimulatedPlayers (they have no network connection).
namespace testing {
/// Marks a (simulated) player as having the client mod, or not.
void setClientMod(Player& player, bool hasMod);
/// Messages sent to a simulated player since the last call: (id, JSON payload).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> takeOutbox(Player const& player);
/// Delivers a message as if the player's client had sent it.
template <Message T>
void receiveFromClient(Player& player, T const& msg);
void receiveFromClientRaw(Player& player, std::string_view id, std::string_view payload);
} // namespace testing

template <Message T>
void testing::receiveFromClient(Player& player, T const& msg) {
    receiveFromClientRaw(player, T::kId, detail::encode(msg));
}

} // namespace lp::net

/// Registers message type `T` (unique `T::kId`, checked at load). Put it in one .cpp file.
#define LP_REGISTER_MESSAGE(T) static ::lp::net::detail::MessageRegistrar<T> LP_UNIQUE(lp_message_)(#T)
