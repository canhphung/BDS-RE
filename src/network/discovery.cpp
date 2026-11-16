#include "bds/network/discovery.hpp"
#include "bds/network/raknet.hpp"
#include "bds/core/logger.hpp"
#include "bds/network/batch.hpp"
#include "bds/network/connection.hpp"
#include <map>
#include <algorithm>
#include <charconv>
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace bds {
namespace {
constexpr std::array<std::uint8_t, 16> magic = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                                                0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};
void big_endian(std::vector<std::uint8_t> &out, std::uint64_t value, unsigned width) {
    for (unsigned i = width; i > 0; --i)
        out.push_back(static_cast<std::uint8_t>(value >> (8 * (i - 1))));
}
std::string field(std::string text) {
    for (char &c : text)
        if (c == ';' || static_cast<unsigned char>(c) < 32)
            c = ' ';
    if (text.size() > 256) {
        std::size_t end = 256;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
            --end;
        text.resize(end);
    }
    return text;
}
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void close_socket(Socket s) {
    if (s != invalid)
        closesocket(s);
}
enum class ReceiveError { drained, skip, fatal };
ReceiveError receive_error() {
    switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK:
        return ReceiveError::drained;
    // An ICMP port-unreachable for an earlier send, or an oversized datagram,
    // concerns one peer only; keep draining the queue for everyone else.
    case WSAEINTR:
    case WSAECONNRESET:
    case WSAEMSGSIZE:
        return ReceiveError::skip;
    default:
        return ReceiveError::fatal;
    }
}
bool family_unavailable() {
    const auto error = WSAGetLastError();
    return error == WSAEAFNOSUPPORT || error == WSAEPROTONOSUPPORT || error == WSAEADDRNOTAVAIL;
}
#else
using Socket = int;
constexpr Socket invalid = -1;
void close_socket(Socket s) {
    if (s != invalid)
        ::close(s);
}
enum class ReceiveError { drained, skip, fatal };
ReceiveError receive_error() {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return ReceiveError::drained;
    if (errno == EINTR || errno == ECONNREFUSED)
        return ReceiveError::skip;
    return ReceiveError::fatal;
}
bool family_unavailable() {
    return errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT || errno == EADDRNOTAVAIL;
}
#endif
// The host lacks this address family (e.g. IPv6 disabled). Distinct from a
// port conflict, which stays fatal.
struct FamilyUnavailable : std::runtime_error {
    using std::runtime_error::runtime_error;
};
raknet::Address peer_address(const sockaddr_storage &storage) {
    raknet::Address out;
    if (storage.ss_family == AF_INET) {
        const auto &a = reinterpret_cast<const sockaddr_in &>(storage);
        out.port = ntohs(a.sin_port);
        std::copy_n(reinterpret_cast<const std::uint8_t *>(&a.sin_addr), 4, out.ip.begin());
    } else {
        const auto &a = reinterpret_cast<const sockaddr_in6 &>(storage);
        out.ipv6 = true;
        out.port = ntohs(a.sin6_port);
        out.scope = a.sin6_scope_id;
        std::copy_n(reinterpret_cast<const std::uint8_t *>(&a.sin6_addr), 16, out.ip.begin());
    }
    return out;
}
void send_packet(Socket socket, const raknet::Address &to, std::span<const std::uint8_t> bytes) {
    sockaddr_storage storage{};
    int length = 0;
    if (!to.ipv6) {
        auto &a = reinterpret_cast<sockaddr_in &>(storage);
        a.sin_family = AF_INET;
        a.sin_port = htons(to.port);
        std::copy_n(to.ip.begin(), 4, reinterpret_cast<std::uint8_t *>(&a.sin_addr));
        length = sizeof(a);
    } else {
        auto &a = reinterpret_cast<sockaddr_in6 &>(storage);
        a.sin6_family = AF_INET6;
        a.sin6_port = htons(to.port);
        a.sin6_scope_id = to.scope;
        std::copy(to.ip.begin(), to.ip.end(), reinterpret_cast<std::uint8_t *>(&a.sin6_addr));
        length = sizeof(a);
    }
    sendto(socket, reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()), 0,
           reinterpret_cast<const sockaddr *>(&storage), length);
}

Socket bind_socket(int family, unsigned port) {
    const char *name = family == AF_INET ? "IPv4" : "IPv6";
    Socket s = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (s == invalid) {
        if (family_unavailable())
            throw FamilyUnavailable(std::string(name) + " not supported");
        throw std::runtime_error(std::string("cannot create ") + name + " UDP socket");
    }
    try {
#ifdef _WIN32
        BOOL exclusive = TRUE;
        if (setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) != 0)
            throw std::runtime_error("UDP exclusive bind failed");
        u_long nonblocking = 1;
        if (ioctlsocket(s, FIONBIO, &nonblocking) != 0)
            throw std::runtime_error("UDP nonblocking failed");
#else
        if (fcntl(s, F_SETFL, O_NONBLOCK) != 0)
            throw std::runtime_error("UDP nonblocking failed");
#endif
        if (family == AF_INET) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<unsigned short>(port));
            address.sin_addr.s_addr = htonl(INADDR_ANY);
            if (::bind(s, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
                throw std::runtime_error("cannot bind configured IPv4 port");
        } else {
            int only = 1;
            if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&only),
                           sizeof(only)) != 0)
                throw std::runtime_error("IPv6-only setting failed");
            sockaddr_in6 address{};
            address.sin6_family = AF_INET6;
            address.sin6_port = htons(static_cast<unsigned short>(port));
            address.sin6_addr = in6addr_any;
            if (::bind(s, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
                if (family_unavailable())
                    throw FamilyUnavailable("IPv6 not supported");
                throw std::runtime_error("cannot bind configured IPv6 port");
            }
        }
    } catch (...) {
        close_socket(s);
        throw;
    }
    return s;
}
} // namespace
std::vector<std::uint8_t> discovery_pong(std::span<const std::uint8_t> ping, std::uint64_t guid,
                                         const std::string &advertisement) {
    if ((ping.size() != 25 && ping.size() != 33) || (ping[0] != 1 && ping[0] != 2) ||
        !std::equal(magic.begin(), magic.end(), ping.begin() + 9) || advertisement.size() > 1024)
        return {};
    std::vector<std::uint8_t> out;
    out.reserve(35 + advertisement.size());
    out.push_back(0x1c);
    out.insert(out.end(), ping.begin() + 1, ping.begin() + 9);
    big_endian(out, guid, 8);
    out.insert(out.end(), magic.begin(), magic.end());
    big_endian(out, advertisement.size(), 2);
    out.insert(out.end(), advertisement.begin(), advertisement.end());
    return out;
}
std::string server_advertisement(const Properties &p, std::uint64_t guid) {
    constexpr const char *modes[] = {"Survival", "Creative", "Adventure"};
    return "MCPE;" + field(p.server_name) + ";282;1.6.0;0;" + std::to_string(p.max_players) + ";" +
           std::to_string(guid) + ";" + field(p.level_name) + ";" +
           // Final field: the original Linux dedicated server advertises 1
           // (reference capture); its AppPlatform comparison at 0x1827a75 is
           // otherwise unresolved.
           modes[p.gamemode <= 2 ? p.gamemode : 0] + ";1;";
}
struct DiscoveryServer::Impl {
    std::array<Socket, 2> sockets{invalid, invalid};
    std::uint64_t guid = 0;
    std::unique_ptr<raknet::Transport> transport;
    // Minecraft protocol state per transport peer; never touches the World.
    std::map<raknet::Address, protocol::GameConnection> connections;
    // One server key pair for all handshakes, generated at startup.
    std::shared_ptr<protocol::ServerSecrets> secrets = std::make_shared<protocol::ServerSecrets>();
    // Valid during poll(); connections only call admission from within poll.
    const Properties *properties = nullptr;
    AccessLists *access = nullptr;
    std::uint64_t world_age = 0;
    // Level::getNextRuntimeID / getNewUniqueID analogue; ids are never reused.
    std::int64_t next_entity_id = 1;

    protocol::StartGame start_game() {
        const auto &p = *properties;
        protocol::StartGame game;
        game.unique_id = next_entity_id;
        game.runtime_id = static_cast<std::uint64_t>(next_entity_id++);
        game.player_game_type = static_cast<std::int32_t>(p.gamemode);
        auto &s = game.settings;
        // The seed is only advertised: this server does not generate terrain.
        // A purely numeric level-seed is sent as-is, anything else as 0 (policy).
        std::int32_t seed = 0;
        const auto *begin = p.seed.data();
        if (!p.seed.empty() && std::from_chars(begin, begin + p.seed.size(), seed).ptr != begin + p.seed.size())
            seed = 0;
        s.seed = seed;
        s.generator = p.level_type == "FLAT"     ? protocol::Generator::Flat
                      : p.level_type == "LEGACY" ? protocol::Generator::Legacy
                                                 : protocol::Generator::Overworld;
        s.game_type = static_cast<std::int32_t>(p.gamemode);
        s.difficulty = static_cast<std::int32_t>(p.difficulty);
        // The original sent time == current tick for a fresh world (reference capture).
        s.time = static_cast<std::int32_t>(world_age);
        s.commands_enabled = p.cheats;
        s.texture_packs_required = p.texturepack_required;
        s.default_player_permission = p.permission == "visitor" ? 0 : p.permission == "operator" ? 2 : 1;
        s.server_chunk_tick_range = static_cast<std::int32_t>(p.tick_distance);
        game.x = static_cast<float>(s.spawn.x) + 0.5f;
        game.y = static_cast<float>(s.spawn.y) + 1.62f; // player position is eye height
        game.z = static_cast<float>(s.spawn.z) + 0.5f;
        game.level_id = p.level_name;
        game.level_name = p.level_name;
        game.current_tick = world_age;
        // The original sends a random version-4 UUID here (reference capture).
        auto bytes = secrets->random.bytes(16);
        bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0f) | 0x40);
        bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3f) | 0x80);
        static constexpr char digits[] = "0123456789abcdef";
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                game.multiplayer_correlation_id.push_back('-');
            game.multiplayer_correlation_id.push_back(digits[bytes[i] >> 4]);
            game.multiplayer_correlation_id.push_back(digits[bytes[i] & 15]);
        }
        return game;
    }
    std::optional<std::string> admit(const auth::Identity &identity) {
        // Login handler 0x1828b2f-0x1828bf5: when the whitelist is enabled,
        // Whitelist::tryUpdateEntries then Whitelist::isAllowed, else
        // disconnectionScreen.notAllowed.
        if (!properties || !access || !properties->whitelist)
            return std::nullopt;
        const auto xuid = identity.xbox_authenticated ? identity.xuid : std::string();
        try {
            access->try_update_entries(identity.identity, xuid, identity.display_name);
        } catch (const std::exception &error) {
            log(std::string("whitelist update failed: ") + error.what());
        }
        if (access->is_allowed(identity.identity, xuid))
            return std::nullopt;
        return std::string(protocol::not_allowed);
    }
#ifdef _WIN32
    bool initialized = false;
#endif
    Impl(const Properties &p) {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("WSAStartup failed");
        initialized = true;
#endif
        try {
            std::random_device random;
            guid = (static_cast<std::uint64_t>(random()) << 32) | random();
            // Mirrors RakPeerHelper::peerStartup (0x192f670): when one family is
            // unsupported the original retries with the other family only. Other
            // bind failures, such as a port already in use, remain fatal here.
            const std::array<int, 2> families{AF_INET, AF_INET6};
            const std::array<unsigned, 2> ports{p.port, p.port_v6};
            for (std::size_t i = 0; i < sockets.size(); ++i) {
                try {
                    sockets[i] = bind_socket(families[i], ports[i]);
                } catch (const FamilyUnavailable &) {
                    sockets[i] = invalid;
                }
            }
            if (sockets[0] == invalid && sockets[1] == invalid)
                throw std::runtime_error("neither IPv4 nor IPv6 UDP is supported");
            transport = std::make_unique<raknet::Transport>(guid);
        } catch (...) {
            cleanup();
            throw;
        }
    }
    void cleanup() {
        for (auto s : sockets)
            close_socket(s);
        sockets = {invalid, invalid};
#ifdef _WIN32
        if (initialized) {
            WSACleanup();
            initialized = false;
        }
#endif
    }
    ~Impl() {
        cleanup();
    }
};
DiscoveryServer::DiscoveryServer(const Properties &p) : impl_(std::make_unique<Impl>(p)) {}
DiscoveryServer::~DiscoveryServer() = default;
void DiscoveryServer::poll(const Properties &p, AccessLists &access, std::uint64_t world_age) {
    impl_->properties = &p;
    impl_->access = &access;
    impl_->world_age = world_age;
    const auto advertisement = server_advertisement(p, impl_->guid);
    const auto now =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
    std::erase_if(impl_->connections, [&](const auto &entry) { return !impl_->transport->connected(entry.first); });
    for (const auto &outgoing : impl_->transport->poll(now)) {
        const auto socket = impl_->sockets[outgoing.address.ipv6 ? 1 : 0];
        if (socket != invalid)
            send_packet(socket, outgoing.address, outgoing.bytes);
    }
    for (auto socket : impl_->sockets) {
        if (socket == invalid)
            continue;
        for (unsigned packet = 0; packet < 32; ++packet) {
            std::array<std::uint8_t, 2048> bytes{};
            sockaddr_storage sender{};
#ifdef _WIN32
            int length = sizeof(sender);
            const auto count = recvfrom(socket, reinterpret_cast<char *>(bytes.data()),
                                        static_cast<int>(bytes.size()), 0,
                                        reinterpret_cast<sockaddr *>(&sender), &length);
#else
            socklen_t length = sizeof(sender);
            const auto count = recvfrom(socket, bytes.data(), bytes.size(), 0,
                                        reinterpret_cast<sockaddr *>(&sender), &length);
#endif
            if (count < 0) {
                const auto error = receive_error();
                if (error == ReceiveError::drained)
                    break;
                if (error == ReceiveError::skip)
                    continue;
                throw std::runtime_error("UDP receive failed");
            }
            const auto incoming = std::span(bytes).first(static_cast<std::size_t>(count));
            auto response = discovery_pong(incoming, impl_->guid, advertisement);
            const auto from = peer_address(sender);
            if (!response.empty()) {
                send_packet(socket, from, response);
                continue;
            }
            auto result = impl_->transport->receive(from, incoming, now);
            for (const auto &outgoing : result.outgoing)
                send_packet(socket, outgoing.address, outgoing.bytes);
            // A transport connection is not a login: only 0xfe batches reach the
            // Minecraft protocol layer, and it never creates a player yet.
            for (const auto &application : result.applications) {
                protocol::ConnectionOutput output;
                if (application.bytes.empty() || application.bytes[0] != protocol::batch_id)
                    output = {{}, true, "non-batch application message"};
                else
                    output = impl_->connections
                                 .try_emplace(from, auth::LoginPolicy{p.online_mode, {std::string(auth::mojang_root_key)}},
                                              protocol::GameConnection::Clock{}, impl_->secrets,
                                              protocol::GameConnection::Options{
                                                  p.texturepack_required,
                                                  [impl = impl_.get()](const auth::Identity &identity) {
                                                      return impl->admit(identity);
                                                  },
                                                  [impl = impl_.get()] { return impl->start_game(); },
                                                  static_cast<std::int32_t>(p.view_distance)})
                                 .first->second.receive(application.bytes);
                for (const auto &message : output.messages)
                    for (const auto &outgoing : impl_->transport->send(from, message, now))
                        send_packet(socket, outgoing.address, outgoing.bytes);
                if (!output.close && !output.reason.empty())
                    log(output.reason);
                if (output.close) {
                    log("closing connection: " + output.reason);
                    for (const auto &outgoing : impl_->transport->disconnect(from, now))
                        send_packet(socket, outgoing.address, outgoing.bytes);
                    impl_->connections.erase(from);
                    break;
                }
            }
        }
    }
}
std::optional<unsigned> DiscoveryServer::bound_port(bool ipv6) const {
    const auto socket = impl_->sockets[ipv6 ? 1 : 0];
    if (socket == invalid)
        return std::nullopt;
    sockaddr_storage storage{};
#ifdef _WIN32
    int length = sizeof(storage);
#else
    socklen_t length = sizeof(storage);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr *>(&storage), &length) != 0)
        throw std::runtime_error("cannot query bound UDP port");
    return peer_address(storage).port;
}
} // namespace bds
