#include "bds/network/connection.hpp"
#include "bds/network/login.hpp"
#include "bds/network/packet_ids.hpp"
#include <array>

namespace bds::protocol {
namespace {
Bytes batch_of(PacketId id, Bytes body) {
    const std::array packets{GamePacket{{static_cast<std::uint16_t>(id), 0, 0}, std::move(body)}};
    return encode_batch(packets);
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

ConnectionOutput GameConnection::handle_login(const GamePacket &packet) {
    state_ = State::closed;
    const auto protocol = login_protocol(packet.body);
    // Original compares against NetworkProtocolVersion: older clients get
    // LoginFailedClient, newer clients LoginFailedServer.
    if (protocol != network_protocol) {
        const auto status = protocol < network_protocol ? PlayStatus::LoginFailedClient : PlayStatus::LoginFailedServer;
        return close("login protocol " + std::to_string(protocol) + " is not " + std::to_string(network_protocol),
                     {batch_of(PacketId::PlayStatus, play_status_body(status))});
    }
    // Structural validation only; the body carries no trusted identity.
    decode_login_body(packet.body);
    return close("login received; authentication is not implemented",
                 {batch_of(PacketId::Disconnect,
                           disconnect_body(false, "BDS-RE: login authentication is not implemented yet"))});
}
} // namespace bds::protocol
