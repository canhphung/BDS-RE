#include "bds/network/connection.hpp"
#include "bds/network/login.hpp"
#include "bds/network/packet_ids.hpp"
#include <array>
#include <chrono>

namespace bds::protocol {
namespace {
GamePacket packet(PacketId id, Bytes body) {
    return {{static_cast<std::uint16_t>(id), 0, 0}, std::move(body)};
}
// ServerNetworkHandler::disconnectClient (0x1825660) sends
// DisconnectPacket(message, false): the client shows the message.
GamePacket disconnect_packet(std::string_view message) {
    return packet(PacketId::Disconnect, disconnect_body(false, message));
}
std::int64_t system_clock_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
// Client-supplied text must not forge extra log lines.
std::string printable(std::string_view text) {
    std::string out;
    for (const char c : text.substr(0, 64))
        out.push_back(static_cast<unsigned char>(c) < 32 || c == 127 ? '?' : c);
    return out;
}
ConnectionOutput close(std::string reason, std::vector<Bytes> messages = {}) {
    return {std::move(messages), true, std::move(reason)};
}
} // namespace

Bytes play_status_body(PlayStatus status) {
    ByteWriter writer;
    writer.big_endian_int(static_cast<std::int32_t>(status));
    return writer.take();
}

Bytes server_handshake_body(std::string_view jwt) {
    ByteWriter writer;
    writer.string(jwt);
    return writer.take();
}

Bytes resource_packs_info_body(bool must_accept) {
    ByteWriter writer;
    writer.boolean(must_accept);
    for (int list = 0; list < 2; ++list) { // behaviour packs, then resource packs
        writer.byte(0);                    // unsigned short count, little-endian
        writer.byte(0);
    }
    return writer.take();
}

Bytes resource_pack_stack_body(bool must_accept) {
    ByteWriter writer;
    writer.boolean(must_accept);
    writer.varint(0); // behaviour stack (writeVectorList: unsigned varint count)
    writer.varint(0); // resource stack
    return writer.take();
}

PackResponseBody decode_pack_response(std::span<const std::uint8_t> body) {
    ByteReader reader(body);
    PackResponseBody response{reader.byte(), {}};
    const auto count_bytes = reader.take(2);
    const auto count = static_cast<std::uint16_t>(count_bytes[0] | (count_bytes[1] << 8));
    if (count > 256)
        throw ProtocolError("too many pack ids");
    for (std::uint16_t i = 0; i < count; ++i)
        response.pack_ids.push_back(reader.string(256));
    reader.finish();
    return response;
}

Bytes disconnect_body(bool hide_screen, std::string_view message) {
    ByteWriter writer;
    writer.boolean(hide_screen);
    if (!hide_screen)
        writer.string(message);
    return writer.take();
}

GameConnection::GameConnection(auth::LoginPolicy policy, Clock clock, std::shared_ptr<ServerSecrets> secrets,
                               Options options)
    : policy_(std::move(policy)), clock_(clock ? std::move(clock) : Clock(system_clock_seconds)),
      secrets_(secrets ? std::move(secrets) : std::make_shared<ServerSecrets>()), options_(std::move(options)) {}

Bytes GameConnection::message(std::vector<GamePacket> packets) {
    auto payload = compress_packets(packets);
    if (cipher_)
        payload = cipher_->seal(payload);
    Bytes out{batch_id};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

ConnectionOutput GameConnection::receive(std::span<const std::uint8_t> raw) {
    if (state_ == State::closed)
        return {};
    try {
        if (raw.size() < 2 || raw[0] != batch_id)
            throw ProtocolError("not a game packet batch");
        auto payload = Bytes(raw.begin() + 1, raw.end());
        if (cipher_) {
            auto opened = cipher_->open(payload);
            // The original drops a packet whose checksum fails; this
            // implementation closes the connection instead (policy).
            if (!opened) {
                state_ = State::closed;
                return close("encrypted batch failed its checksum");
            }
            payload = std::move(*opened);
        }
        const auto packets = decompress_packets(payload);
        for (const auto &packet : packets) {
            if (state_ == State::spawning) {
                auto output = handle_spawning(packet);
                if (output.close || !output.messages.empty())
                    return output;
                continue;
            }
            // Split-screen sub-clients are not supported by this server yet.
            if (packet.header.sender != 0 || packet.header.target != 0) {
                state_ = State::closed;
                return close("sub-client packets are not supported");
            }
            const auto expected = state_ == State::awaiting_login       ? PacketId::Login
                                  : state_ == State::awaiting_handshake ? PacketId::ClientToServerHandshake
                                                                        : PacketId::ResourcePackClientResponse;
            if (packet.header.id != static_cast<std::uint16_t>(expected)) {
                const auto name = packet_name(packet.header.id);
                const auto phase = state_ == State::awaiting_login       ? "login"
                                   : state_ == State::awaiting_handshake ? "the encryption handshake"
                                                                         : "the resource pack exchange";
                state_ = State::closed;
                return close(std::string("unexpected packet before ") + phase + ": " +
                             (name.empty() ? "id " + std::to_string(packet.header.id) : std::string(name)));
            }
            auto output = state_ == State::awaiting_login       ? handle_login(packet)
                          : state_ == State::awaiting_handshake ? handle_handshake()
                                                                : handle_pack_response(packet);
            if (output.close || !output.messages.empty())
                return output; // one step per batch; later packets would race the key switch
        }
        return {};
    } catch (const std::invalid_argument &error) {
        state_ = State::closed;
        return close(std::string("malformed game packet: ") + error.what());
    }
}

ConnectionOutput GameConnection::handle_login(const GamePacket &login) {
    state_ = State::closed;
    const auto protocol = login_protocol(login.body);
    // Original compares against NetworkProtocolVersion: older clients get
    // LoginFailedClient, newer LoginFailedServer, each followed by
    // disconnectClient with the matching localisation key (0x1827f86/0x1827fea).
    if (protocol != network_protocol) {
        const bool older = protocol < network_protocol;
        return close("login protocol " + std::to_string(protocol) + " is not " + std::to_string(network_protocol),
                     {message({packet(PacketId::PlayStatus,
                                      play_status_body(older ? PlayStatus::LoginFailedClient : PlayStatus::LoginFailedServer)),
                               disconnect_packet(older ? outdated_client : outdated_server)})});
    }
    const auto result = auth::verify_login(decode_login_body(login.body), policy_, clock_());
    if (!result.identity)
        return close("login not authenticated: " + result.detail, {message({disconnect_packet(not_authenticated)})});
    identity_ = result.identity;
    if (options_.admission) {
        if (auto refusal = options_.admission(*identity_))
            return close("login refused: " + printable(identity_->display_name) + " (" + printable(*refusal) + ")",
                         {message({disconnect_packet(*refusal)})});
    }
    // Encryption setup (0x1828fcb-0x182928c): 16 random salt bytes, ECDH
    // between the server key and the client's identityPublicKey, a token
    // {"salt": base64(salt)} signed by the server key, sent in plaintext, then
    // SHA-256(salt + secret) enables encryption for everything after it.
    const auto client_key = crypto::base64_decode(identity_->public_key);
    const auto secret = client_key ? secrets_->key.shared_secret(*client_key, secrets_->random) : std::nullopt;
    if (!secret)
        return close("client identity key unusable for key exchange", {message({disconnect_packet(not_authenticated)})});
    const auto salt = secrets_->random.bytes(16);
    const auto token = auth::sign_jwt("{\"salt\":\"" + crypto::base64_encode(salt) + "\"}", secrets_->key, secrets_->random);
    auto handshake = message({packet(PacketId::ServerToClientHandshake, server_handshake_body(token))});
    cipher_ = std::make_unique<PacketCipher>(session_key(salt, *secret));
    state_ = State::awaiting_handshake;
    const auto &who = *identity_;
    return {{std::move(handshake)}, false,
            "login authenticated: \"" + printable(who.display_name) + "\" " +
                (who.xbox_authenticated ? "xuid=" + printable(who.xuid) : std::string("self-signed (online-mode off)")) +
                "; encryption handshake sent"};
}

ConnectionOutput GameConnection::handle_handshake() {
    // handle(ClientToServerHandshakePacket) (0x182a830) -> _onClientAuthenticated:
    // capacity/duplicate checks (not implemented: no players exist yet),
    // PlayStatus LoginSuccess, "Player connected: %s", ResourcePacksInfo.
    state_ = State::awaiting_packs;
    const auto &who = *identity_;
    return {{message({packet(PacketId::PlayStatus, play_status_body(PlayStatus::LoginSuccess)),
                      packet(PacketId::ResourcePacksInfo, resource_packs_info_body(options_.texture_packs_required))})},
            false,
            // Same text as the original: the %s is the XUID (getXuid at 0x18297e6),
            // empty for self-signed players (reference capture).
            "Player connected: " + printable(who.xuid)};
}

ConnectionOutput GameConnection::handle_pack_response(const GamePacket &received) {
    // ServerNetworkHandler::handle(ResourcePackClientResponsePacket) (0x182aba0).
    const auto response = decode_pack_response(received.body);
    switch (static_cast<PackResponse>(response.status)) {
    case PackResponse::Refused: // disconnectClient("") at 0x182ac7c
        state_ = State::closed;
        return close("client refused resource packs", {message({disconnect_packet("")})});
    case PackResponse::SendPacks:
        // The original uploads each requested pack it owns; this server owns none.
        state_ = State::closed;
        return close("client requested " + std::to_string(response.pack_ids.size()) + " packs; none are available",
                     {message({disconnect_packet("BDS-RE: resource pack download is not implemented")})});
    case PackResponse::HaveAllPacks:
        return {{message({packet(PacketId::ResourcePackStack, resource_pack_stack_body(options_.texture_packs_required))})},
                false,
                {}};
    case PackResponse::Completed:
        // sendLoginMessageLocal (0x182b0b9) -> _createNewPlayer, _sendLevelData.
        return start_game();
    }
    state_ = State::closed;
    return close("unknown resource pack response " + std::to_string(response.status));
}

ConnectionOutput GameConnection::start_game() {
    // _sendLevelData (0x182fd90) order: StartGame, SetSpawnPosition (type 0,
    // player spawn), SetTime, SetDifficulty, SetCommandsEnabled, then
    // lightning/rain LevelEvents, AdventureSettings, game rules, PlayerList,
    // abilities; _sendAdditionalLevelData then sends attributes, inventory,
    // crafting data and scoreboards, and AvailableCommands follows. Only the
    // first five are implemented.
    state_ = State::spawning;
    auto game = options_.start_game ? options_.start_game() : StartGame{};
    if (game.palette.empty())
        game.palette = vanilla_block_palette();
    const auto &settings = game.settings;
    const auto &who = *identity_;
    return {{message({packet(PacketId::StartGame, start_game_body(game)),
                      packet(PacketId::SetSpawnPosition, set_spawn_position_body(0, settings.spawn, false)),
                      packet(PacketId::SetTime, set_time_body(settings.time)),
                      packet(PacketId::SetDifficulty, set_difficulty_body(static_cast<std::uint32_t>(settings.difficulty))),
                      packet(PacketId::SetCommandsEnabled, set_commands_enabled_body(settings.commands_enabled))})},
            false,
            "StartGame sent to " + printable(who.display_name) + " (runtime id " + std::to_string(game.runtime_id) + ")"};
}

ConnectionOutput GameConnection::handle_spawning(const GamePacket &received) {
    if (received.header.sender != 0 || received.header.target != 0) {
        state_ = State::closed;
        return close("sub-client packets are not supported");
    }
    if (received.header.id != static_cast<std::uint16_t>(PacketId::RequestChunkRadius)) {
        // A joining client sends further packets (movement, settings) before it
        // spawns; they have no handler yet and are ignored rather than fatal.
        ++ignored_while_spawning_;
        return {};
    }
    // ServerNetworkHandler::handle(RequestChunkRadiusPacket) (0x1825e50) ->
    // ServerPlayer::setClientChunkRadius -> ChunkRadiusUpdated.
    const auto radius = client_chunk_radius(decode_request_chunk_radius(received.body), options_.view_distance);
    state_ = State::closed;
    const auto &who = *identity_;
    return close("chunk radius " + std::to_string(radius) + " for " + printable(who.display_name) +
                     "; closing because chunk streaming is not implemented (" +
                     std::to_string(ignored_while_spawning_) + " other packets ignored)",
                 {message({packet(PacketId::ChunkRadiusUpdated, chunk_radius_updated_body(radius)),
                           disconnect_packet("BDS-RE: StartGame sent to " + who.display_name +
                                             ". Chunk streaming and spawning are not implemented yet")})});
}
} // namespace bds::protocol
