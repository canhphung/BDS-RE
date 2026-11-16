#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bds::raknet {
using Bytes = std::vector<std::uint8_t>;
struct Address {
    bool ipv6 = false;
    std::array<std::uint8_t, 16> ip{};
    std::uint16_t port = 0;
    std::uint32_t scope = 0;
    auto operator<=>(const Address &) const = default;
};
Bytes encode_address(const Address &address);
int sequence_distance(std::uint32_t a, std::uint32_t b);
struct ChannelResult {
    bool valid = true;
    std::vector<Bytes> outgoing, messages;
};
// Bounded RakNet 9 channel. All times are monotonic milliseconds.
class Channel {
  public:
    explicit Channel(unsigned mtu);
    ~Channel();
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;
    ChannelResult receive(std::span<const std::uint8_t> datagram, std::uint64_t now);
    std::vector<Bytes> send(std::span<const std::uint8_t> message, std::uint64_t now,
                            unsigned reliability = 3);
    std::vector<Bytes> poll(std::uint64_t now);
    bool alive() const;
    std::size_t pending() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct Outgoing {
    Address address;
    Bytes bytes;
};
struct Application {
    Address address;
    Bytes bytes;
};
struct TransportResult {
    std::vector<Outgoing> outgoing;
    std::vector<Application> applications;
    // Bounded by one received datagram; no payloads, tokens or keys.
    std::vector<std::string> events;
};
// A connected transport peer is NOT an authenticated Minecraft player.
class Transport {
  public:
    explicit Transport(std::uint64_t guid, unsigned peer_limit = 64);
    ~Transport();
    TransportResult receive(const Address &from, std::span<const std::uint8_t> datagram,
                            std::uint64_t now);
    std::vector<Outgoing> poll(std::uint64_t now);
    std::vector<Outgoing> disconnect(const Address &address, std::uint64_t now);
    // Reliable-ordered application message to a transport-connected peer.
    // Returns nothing if the peer is unknown, not connected, or closing.
    std::vector<Outgoing> send(const Address &address, std::span<const std::uint8_t> message,
                               std::uint64_t now);
    // True while the peer completed the handshake and is not closing.
    bool connected(const Address &address) const;
    std::size_t pending(const Address &address) const;
    void authenticated(const Address &address);
    std::size_t peers() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace bds::raknet
