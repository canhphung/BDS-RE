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
Bytes batch_of(std::vector<GamePacket> packets) {
    return encode_batch(packets);
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

Bytes disconnect_body(bool hide_screen, std::string_view message) {
    ByteWriter writer;
    writer.boolean(hide_screen);
    if (!hide_screen)
        writer.string(message);
    return writer.take();
}

GameConnection::GameConnection(auth::LoginPolicy policy, Clock clock)
    : policy_(std::move(policy)), clock_(clock ? std::move(clock) : Clock(system_clock_seconds)) {}

ConnectionOutput GameConnection::receive(std::span<const std::uint8_t> message) {
    if (state_ == State::closed)
        return {};
    try {
        const auto packets = decode_batch(message);
        for (const auto &packet : packets) {
            // Split-screen sub-clients are not supported by this server yet.
            if (packet.header.sender != 0 || packet.header.target != 0) {
                state_ = State::closed;
                return close("sub-client packets are not supported");
            }
            if (state_ != State::awaiting_login || packet.header.id != static_cast<std::uint16_t>(PacketId::Login)) {
                state_ = State::closed;
                const auto name = packet_name(packet.header.id);
                return close("unexpected packet before login: " +
                             (name.empty() ? "id " + std::to_string(packet.header.id) : std::string(name)));
            }
            auto output = handle_login(packet);
            if (output.close)
                return output;
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
                     {batch_of({packet(PacketId::PlayStatus,
                                       play_status_body(older ? PlayStatus::LoginFailedClient : PlayStatus::LoginFailedServer)),
                                disconnect_packet(older ? outdated_client : outdated_server)})});
    }
    const auto result = auth::verify_login(decode_login_body(login.body), policy_, clock_());
    if (!result.identity)
        return close("login not authenticated: " + result.detail, {batch_of({disconnect_packet(not_authenticated)})});
    identity_ = result.identity;
    const auto &who = *identity_;
    std::string description = "login authenticated: \"" + printable(who.display_name) + "\" " +
                              (who.xbox_authenticated ? "xuid=" + printable(who.xuid) : std::string("self-signed (online-mode off)"));
    // Next original step is ServerToClientHandshake (encryption), not implemented yet.
    return close(description + "; encryption handshake not implemented",
                 {batch_of({disconnect_packet("BDS-RE: authenticated as " + who.display_name +
                                              ", but the encryption handshake is not implemented yet")})});
}
} // namespace bds::protocol
