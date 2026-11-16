#pragma once
#include "bds/network/auth.hpp"
#include "bds/network/batch.hpp"
#include "bds/network/encryption.hpp"
#include <memory>
#include <functional>
#include <optional>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bds::protocol {
inline constexpr std::int32_t network_protocol = 282; // SharedConstants::NetworkProtocolVersion 0x282c39c

// Only values with binary evidence are named (ServerNetworkHandler::handle
// for LoginPacket, 0x1827ef9-0x1827f09).
// LoginSuccess: _onClientAuthenticated sends PlayStatus(0) (xor %esi at 0x18297a8).
enum class PlayStatus : std::int32_t { LoginSuccess = 0, LoginFailedClient = 1, LoginFailedServer = 2 };

// Packet bodies (without header) for the server-to-client packets used so far.
Bytes play_status_body(PlayStatus status);            // PlayStatusPacket::write 0x1843960
Bytes disconnect_body(bool hide_screen, std::string_view message); // DisconnectPacket::write 0x1840f10
Bytes server_handshake_body(std::string_view jwt);                 // ServerToClientHandshakePacket: one string

// Server-wide key pair used to sign the handshake token and derive session
// keys (the handler's PrivateKeyManager), plus its random source.
struct ServerSecrets {
    crypto::Random random;
    crypto::P384Key key = crypto::P384Key::generate(random);
};

// Client-localised reasons passed to ServerNetworkHandler::disconnectClient
// (string literals in the LoginPacket handler, 0x1827f62-0x1828347).
inline constexpr std::string_view outdated_client = "disconnectionScreen.outdatedClient";
inline constexpr std::string_view outdated_server = "disconnectionScreen.outdatedServer";
inline constexpr std::string_view not_authenticated = "disconnectionScreen.notAuthenticated";

struct ConnectionOutput {
    std::vector<Bytes> messages; // RakNet application messages, each a 0xfe batch
    bool close = false;          // disconnect after sending `messages`
    std::string reason;          // server-side log text
};

// Per-peer Minecraft protocol state above RakNet. Owns no world state; gameplay
// layers will receive verified events from here once authentication exists.
class GameConnection {
  public:
    using Clock = std::function<std::int64_t()>; // epoch seconds
    explicit GameConnection(auth::LoginPolicy policy = {}, Clock clock = {},
                            std::shared_ptr<ServerSecrets> secrets = nullptr);
    enum class State { awaiting_login, awaiting_handshake, closed };
    ConnectionOutput receive(std::span<const std::uint8_t> message);
    State state() const { return state_; }
    // Set once a login passed verification; never set from unverified data.
    const std::optional<auth::Identity> &identity() const { return identity_; }

  private:
    ConnectionOutput handle_login(const GamePacket &packet);
    ConnectionOutput handle_handshake();
    // 0xfe message for `packets`, encrypted once the handshake key is set.
    Bytes message(std::vector<GamePacket> packets);
    State state_ = State::awaiting_login;
    auth::LoginPolicy policy_;
    Clock clock_;
    std::shared_ptr<ServerSecrets> secrets_;
    std::unique_ptr<PacketCipher> cipher_;
    std::optional<auth::Identity> identity_;
};
} // namespace bds::protocol
