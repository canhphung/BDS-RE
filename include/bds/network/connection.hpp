#pragma once
#include "bds/network/auth.hpp"
#include "bds/network/batch.hpp"
#include "bds/network/encryption.hpp"
#include "bds/network/level_packets.hpp"
#include <memory>
#include <functional>
#include <optional>
#include <cstdint>
#include <span>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace bds::protocol {
inline constexpr std::int32_t network_protocol = 282; // SharedConstants::NetworkProtocolVersion 0x282c39c

// Only values with binary evidence are named (ServerNetworkHandler::handle
// for LoginPacket, 0x1827ef9-0x1827f09).
// LoginSuccess: _onClientAuthenticated sends PlayStatus(0) (xor %esi at 0x18297a8).
enum class PlayStatus : std::int32_t { LoginSuccess = 0, LoginFailedClient = 1, LoginFailedServer = 2, PlayerSpawn = 3 };

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
inline constexpr std::string_view not_allowed = "disconnectionScreen.notAllowed"; // 0x281d62b

// ResourcePackClientResponse status values used by the handler's
// isResponse checks (0x182abab, 0x182b0c3, 0x182ac9d, 0x182af8f).
enum class PackResponse : std::uint8_t { Refused = 1, SendPacks = 2, HaveAllPacks = 3, Completed = 4 };

// Pack lists are empty: this server loads no packs yet. Layouts:
// ResourcePacksInfoPacket::write 0x1844650 (bool, u16 LE count + entries, twice),
// ResourcePackStackPacket::write 0x18461c0 (bool, varint count + entries, twice).
Bytes resource_packs_info_body(bool must_accept);
Bytes resource_pack_stack_body(bool must_accept);
struct PackResponseBody {
    std::uint8_t status;
    std::vector<std::string> pack_ids;
};
// ResourcePackClientResponsePacket::read 0x187d8f0: byte, u16 LE count, strings.
PackResponseBody decode_pack_response(std::span<const std::uint8_t> body);

struct ConnectionOutput {
    std::vector<Bytes> messages; // RakNet application messages, each a 0xfe batch
    bool close = false;          // disconnect after sending `messages`
    std::string reason;          // server-side log text
};

struct ConnectionOptions {
    bool texture_packs_required = false; // LevelData::isTexturepacksRequired
    // Returns a disconnect reason to refuse an authenticated player
    // (whitelist), or nullopt to admit. Runs before the encryption handshake,
    // as in the login handler (0x1828b2f-0x1828bf5).
    std::function<std::optional<std::string>(const auth::Identity &)> admission;
    // Builds the StartGame contents for a joining player (world state,
    // server.properties and a fresh entity id); default-constructed if unset.
    std::function<StartGame()> start_game;
    std::function<Bytes(std::int32_t, std::int32_t)> chunk; // FullChunkData body from the runtime World
    std::function<std::vector<GamePacket>(const auth::Identity&, StartGame&)> prepare_player;
    std::function<std::vector<GamePacket>(std::uint64_t,const GamePacket&)> gameplay;
    std::function<std::optional<std::pair<int,int>>(std::uint64_t)> center;
    std::int32_t view_distance = 10; // server.properties view-distance
};

// Per-peer Minecraft protocol state above RakNet. Owns no world state; gameplay
// layers will receive verified events from here once authentication exists.
class GameConnection {
  public:
    using Clock = std::function<std::int64_t()>; // epoch seconds
    using Options = ConnectionOptions;
    explicit GameConnection(auth::LoginPolicy policy = {}, Clock clock = {},
                            std::shared_ptr<ServerSecrets> secrets = nullptr, Options options = {});
    enum class State { awaiting_login, awaiting_handshake, awaiting_packs, spawning, streaming, awaiting_initialized, active, closed };
    ConnectionOutput poll(unsigned chunk_budget = 1);
    std::uint64_t runtime_id() const {return game_?game_->runtime_id:0;}
    // The chunk radius granted to RequestChunkRadius (Player::setChunkRadius).
    std::optional<std::int32_t> chunk_radius() const {return radius_;}
    ConnectionOutput push(std::vector<GamePacket> packets);
    ConnectionOutput receive(std::span<const std::uint8_t> message);
    State state() const { return state_; }
    // Set once a login passed verification; never set from unverified data.
    const std::optional<auth::Identity> &identity() const { return identity_; }

  private:
    ConnectionOutput handle_login(const GamePacket &packet);
    ConnectionOutput handle_handshake();
    ConnectionOutput handle_pack_response(const GamePacket &packet);
    ConnectionOutput start_game();
    ConnectionOutput handle_spawning(const GamePacket &packet);
    std::optional<StartGame> game_;
    std::vector<std::pair<std::int32_t, std::int32_t>> chunks_;
    std::size_t next_chunk_ = 0;
    bool spawn_sent_ = false;
    std::set<std::pair<int,int>> sent_chunks_;
    std::optional<std::pair<int,int>> center_;
    void schedule_view(std::pair<int,int> center);
    std::optional<std::int32_t> radius_;
    std::size_t ignored_while_spawning_ = 0;
    // 0xfe message for `packets`, encrypted once the handshake key is set.
    Bytes message(std::vector<GamePacket> packets);
    State state_ = State::awaiting_login;
    auth::LoginPolicy policy_;
    Clock clock_;
    std::shared_ptr<ServerSecrets> secrets_;
    std::unique_ptr<PacketCipher> cipher_;
    std::optional<auth::Identity> identity_;
    Options options_;
};
} // namespace bds::protocol
