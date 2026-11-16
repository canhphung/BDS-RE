#include "bds/network/raknet.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace bds::raknet {
namespace {
constexpr std::uint32_t mask = 0xffffff;
constexpr std::size_t memory_limit = 1024 * 1024;
constexpr std::array<std::uint8_t, 16> magic = {0,   255, 255, 0,   254, 254, 254, 254,
                                                253, 253, 253, 253, 18,  52,  86,  120};
void require(bool value) {
    if (!value)
        throw std::invalid_argument("invalid or over-limit RakNet packet");
}
std::uint32_t next(std::uint32_t value) {
    return (value + 1) & mask;
}
std::uint64_t elapsed(std::uint64_t now, std::uint64_t then) {
    return now >= then ? now - then : 0;
}
void integer(Bytes &out, std::uint64_t value, unsigned width, bool little = false) {
    for (unsigned i = 0; i < width; ++i)
        out.push_back(static_cast<std::uint8_t>(value >> (8 * (little ? i : width - i - 1))));
}
class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::span<const std::uint8_t> take(std::size_t count) {
        require(count <= bytes_.size());
        auto result = bytes_.first(count);
        bytes_ = bytes_.subspan(count);
        return result;
    }
    std::uint64_t integer(unsigned count, bool little = false) {
        auto bytes = take(count);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= static_cast<std::uint64_t>(bytes[i]) << (8 * (little ? i : count - i - 1));
        return value;
    }
    std::uint32_t triad() {
        return static_cast<std::uint32_t>(integer(3, true));
    }
    std::uint8_t byte() {
        return static_cast<std::uint8_t>(integer(1));
    }
    bool empty() const {
        return bytes_.empty();
    }
    std::size_t size() const {
        return bytes_.size();
    }
    void finish() const {
        require(empty());
    }
    void magic_bytes() {
        auto bytes = take(16);
        require(std::equal(bytes.begin(), bytes.end(), magic.begin()));
    }

  private:
    std::span<const std::uint8_t> bytes_;
};
Address address(Reader &r) {
    Address a;
    const auto version = r.byte();
    require(version == 4 || version == 6);
    a.ipv6 = version == 6;
    if (version == 4) {
        for (unsigned i = 0; i < 4; ++i)
            a.ip[i] = static_cast<std::uint8_t>(~r.byte());
        a.port = static_cast<std::uint16_t>(r.integer(2));
    } else {
        const auto family = r.integer(2, true);
        require(family == 10 || family == 23);
        a.port = static_cast<std::uint16_t>(r.integer(2));
        r.take(4);
        auto ip = r.take(16);
        std::copy(ip.begin(), ip.end(), a.ip.begin());
        a.scope = static_cast<std::uint32_t>(r.integer(4, true));
    }
    return a;
}
void append(Bytes &out, std::span<const std::uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}
Bytes offline(std::uint8_t id, std::uint64_t guid) {
    Bytes out{id};
    append(out, magic);
    integer(out, guid, 8);
    return out;
}
struct Frame {
    unsigned reliability = 0;
    bool split = false;
    std::uint32_t reliable = 0, sequence = 0, order = 0, count = 0, index = 0;
    std::uint16_t split_id = 0;
    std::uint8_t channel = 0;
    Bytes body;
};
bool reliable(unsigned mode) {
    return mode == 2 || mode == 3 || mode == 4;
}
bool ordered(unsigned mode) {
    return mode == 1 || mode == 3 || mode == 4;
}
bool sequenced(unsigned mode) {
    return mode == 1 || mode == 4;
}
std::vector<Frame> frames(Reader &r) {
    std::vector<Frame> result;
    while (!r.empty()) {
        require(result.size() < 64);
        Frame f;
        auto flags = r.byte();
        f.reliability = flags >> 5;
        f.split = (flags & 16) != 0;
        require(f.reliability <= 4);
        const auto bits = r.integer(2);
        require(bits > 0 && bits % 8 == 0);
        if (reliable(f.reliability))
            f.reliable = r.triad();
        if (sequenced(f.reliability))
            f.sequence = r.triad();
        if (ordered(f.reliability)) {
            f.order = r.triad();
            f.channel = r.byte();
            require(f.channel < 32);
        }
        if (f.split) {
            f.count = static_cast<std::uint32_t>(r.integer(4));
            f.split_id = static_cast<std::uint16_t>(r.integer(2));
            f.index = static_cast<std::uint32_t>(r.integer(4));
            require(f.count > 0 && f.count <= 1024 && f.index < f.count && reliable(f.reliability));
        }
        auto bytes = r.take(static_cast<std::size_t>(bits / 8));
        f.body.assign(bytes.begin(), bytes.end());
        result.push_back(std::move(f));
    }
    require(!result.empty());
    return result;
}
Bytes encode_frame(const Frame &f) {
    require(!f.body.empty() && f.body.size() <= 8191);
    Bytes out{static_cast<std::uint8_t>((f.reliability << 5) | (f.split ? 16 : 0))};
    integer(out, f.body.size() * 8, 2);
    if (reliable(f.reliability))
        integer(out, f.reliable, 3, true);
    if (sequenced(f.reliability))
        integer(out, f.sequence, 3, true);
    if (ordered(f.reliability)) {
        integer(out, f.order, 3, true);
        out.push_back(f.channel);
    }
    if (f.split) {
        integer(out, f.count, 4);
        integer(out, f.split_id, 2);
        integer(out, f.index, 4);
    }
    append(out, f.body);
    return out;
}
Bytes receipt(bool nack, std::span<const std::uint32_t> sequences) {
    Bytes out{static_cast<std::uint8_t>(nack ? 0xa0 : 0xc0)};
    integer(out, sequences.size(), 2);
    for (auto value : sequences) {
        out.push_back(1);
        integer(out, value, 3, true);
    }
    return out;
}
std::vector<std::uint32_t> receipt_sequences(Reader &r) {
    const auto count = r.integer(2);
    require(count <= 512);
    std::vector<std::uint32_t> result;
    for (unsigned i = 0; i < count; ++i) {
        const auto single = r.byte();
        require(single <= 1);
        const auto low = r.triad(), high = single ? low : r.triad();
        require(high >= low && high - low < 4096 && result.size() + high - low + 1 <= 4096);
        for (auto n = low; n <= high; ++n)
            result.push_back(n);
    }
    r.finish();
    return result;
}
class Window {
  public:
    bool contains(std::uint32_t n) const {
        return values_.contains(n);
    }
    bool stale(std::uint32_t n) const {
        return latest_ && sequence_distance(n, *latest_) <= -4096;
    }
    void add(std::uint32_t n) {
        if (!latest_ || sequence_distance(n, *latest_) > 0)
            latest_ = n;
        values_.insert(n);
        for (auto i = values_.begin(); i != values_.end();) {
            if (stale(*i))
                i = values_.erase(i);
            else
                ++i;
        }
    }
    std::optional<std::uint32_t> latest() const {
        return latest_;
    }

  private:
    std::optional<std::uint32_t> latest_;
    std::set<std::uint32_t> values_;
};
} // namespace
int sequence_distance(std::uint32_t a, std::uint32_t b) {
    const auto delta = (a - b) & mask;
    return delta >= 0x800000 ? static_cast<int>(delta) - 0x1000000 : static_cast<int>(delta);
}
Bytes encode_address(const Address &a) {
    Bytes out{static_cast<std::uint8_t>(a.ipv6 ? 6 : 4)};
    if (!a.ipv6) {
        for (unsigned i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>(~a.ip[i]));
        integer(out, a.port, 2);
    } else {
        integer(out, 23, 2, true);
        integer(out, a.port, 2);
        integer(out, 0, 4);
        append(out, a.ip);
        integer(out, a.scope, 4, true);
    }
    return out;
}
struct Channel::Impl {
    struct Sent {
        Bytes bytes;
        std::uint64_t last;
        unsigned attempts = 1;
    };
    struct Split {
        Frame model;
        std::map<std::uint32_t, Bytes> pieces;
        std::uint64_t created;
    };
    struct Order {
        std::uint32_t expected = 0;
        std::optional<std::uint32_t> sequence;
        std::map<std::uint32_t, Bytes> waiting;
        std::map<std::uint32_t, std::pair<std::uint32_t, Bytes>> sequenced_waiting;
    };
    unsigned mtu;
    bool alive = true;
    std::uint32_t outgoing = 0, message = 0, order = 0;
    std::uint16_t split_id = 0;
    Window received, delivered;
    std::map<std::uint32_t, Sent> sent;
    std::map<std::uint16_t, Split> splits;
    std::array<Order, 32> orders;
    std::size_t buffered = 0;
    explicit Impl(unsigned size) : mtu(size) {
        require(size >= 576 && size <= 1400);
    }
    void deliver(Frame f, std::vector<Bytes> &messages) {
        if (!ordered(f.reliability)) {
            messages.push_back(std::move(f.body));
            return;
        }
        auto &stream = orders[f.channel];
        auto distance = sequence_distance(f.order, stream.expected);
        if (distance < 0)
            return;
        require(distance < 1024);
        if (sequenced(f.reliability)) {
            if (distance > 0) {
                auto previous = stream.sequenced_waiting.find(f.order);
                if (previous != stream.sequenced_waiting.end() &&
                    sequence_distance(f.sequence, previous->second.first) <= 0)
                    return;
                const auto old_size =
                    previous == stream.sequenced_waiting.end() ? 0 : previous->second.second.size();
                require(buffered - old_size + f.body.size() <= memory_limit);
                buffered = buffered - old_size + f.body.size();
                stream.sequenced_waiting.insert_or_assign(f.order,
                                                          std::pair{f.sequence, std::move(f.body)});
                return;
            }
            if (stream.sequence && sequence_distance(f.sequence, *stream.sequence) <= 0)
                return;
            stream.sequence = f.sequence;
            messages.push_back(std::move(f.body));
            return;
        }
        if (distance > 0) {
            auto existing = stream.waiting.find(f.order);
            if (existing != stream.waiting.end()) {
                require(existing->second == f.body);
                return;
            }
            require(buffered + f.body.size() <= memory_limit);
            buffered += f.body.size();
            stream.waiting.emplace(f.order, std::move(f.body));
            return;
        }
        messages.push_back(std::move(f.body));
        stream.expected = next(stream.expected);
        stream.sequence.reset();
        while (true) {
            auto sequenced = stream.sequenced_waiting.find(stream.expected);
            if (sequenced != stream.sequenced_waiting.end()) {
                buffered -= sequenced->second.second.size();
                stream.sequence = sequenced->second.first;
                messages.push_back(std::move(sequenced->second.second));
                stream.sequenced_waiting.erase(sequenced);
            }
            auto item = stream.waiting.find(stream.expected);
            if (item == stream.waiting.end())
                break;
            buffered -= item->second.size();
            messages.push_back(std::move(item->second));
            stream.waiting.erase(item);
            stream.expected = next(stream.expected);
            stream.sequence.reset();
        }
    }
    void process(Frame f, std::uint64_t now, std::vector<Bytes> &messages) {
        if (reliable(f.reliability)) {
            if (delivered.stale(f.reliable) || delivered.contains(f.reliable))
                return;
            delivered.add(f.reliable);
        }
        if (!f.split) {
            deliver(std::move(f), messages);
            return;
        }
        auto i = splits.find(f.split_id);
        if (i == splits.end()) {
            require(splits.size() < 8);
            Frame model = f;
            model.body.clear();
            i = splits.emplace(f.split_id, Split{std::move(model), {}, now}).first;
        }
        auto &split = i->second;
        const auto &model = split.model;
        require(model.count == f.count && model.reliability == f.reliability &&
                model.order == f.order && model.channel == f.channel &&
                model.sequence == f.sequence);
        auto previous = split.pieces.find(f.index);
        if (previous != split.pieces.end()) {
            require(previous->second == f.body);
            return;
        }
        require(buffered + f.body.size() <= memory_limit);
        buffered += f.body.size();
        split.pieces.emplace(f.index, std::move(f.body));
        if (split.pieces.size() != model.count)
            return;
        Frame combined = model;
        combined.split = false;
        for (auto &[index, body] : split.pieces) {
            append(combined.body, body);
            buffered -= body.size();
        }
        splits.erase(i);
        deliver(std::move(combined), messages);
    }
};
Channel::Channel(unsigned mtu) : impl_(std::make_unique<Impl>(mtu)) {}
Channel::~Channel() = default;
bool Channel::alive() const {
    return impl_->alive;
}
std::size_t Channel::pending() const {
    return impl_->sent.size();
}
ChannelResult Channel::receive(std::span<const std::uint8_t> datagram, std::uint64_t now) {
    ChannelResult result;
    auto &state = *impl_;
    if (!state.alive) {
        result.valid = false;
        return result;
    }
    try {
        require(datagram.size() <= state.mtu && !datagram.empty());
        Reader r(datagram);
        const auto header = r.byte();
        require((header & 0x80) != 0);
        if (header & 0x40) {
            if (header & 0x20)
                r.take(4);
            const auto sequences = receipt_sequences(r);
            for (auto n : sequences)
                state.sent.erase(n);
            return result;
        }
        if (header & 0x20) {
            const auto sequences = receipt_sequences(r);
            for (auto n : sequences) {
                auto i = state.sent.find(n);
                if (i != state.sent.end() && result.outgoing.size() < 64) {
                    if (i->second.attempts >= 8) {
                        state.alive = false;
                        break;
                    }
                    i->second.last = now;
                    ++i->second.attempts;
                    result.outgoing.push_back(i->second.bytes);
                }
            }
            return result;
        }
        const auto sequence = r.triad();
        auto incoming = frames(r);
        const std::array ack{sequence};
        result.outgoing.push_back(receipt(false, ack));
        if (state.received.contains(sequence) || state.received.stale(sequence))
            return result;
        auto latest = state.received.latest();
        const auto expected = latest ? next(*latest) : 0;
        const auto gap = sequence_distance(sequence, expected);
        require(gap < 4096);
        if (gap > 0) {
            std::vector<std::uint32_t> missing;
            for (int i = 0; i < gap && i < 64; ++i)
                missing.push_back((expected + static_cast<unsigned>(i)) & mask);
            result.outgoing.push_back(receipt(true, missing));
        }
        state.received.add(sequence);
        for (auto &frame : incoming)
            state.process(std::move(frame), now, result.messages);
    } catch (const std::invalid_argument &) {
        state.alive = false;
        result = {false, {}, {}};
    }
    return result;
}
std::vector<Bytes> Channel::send(std::span<const std::uint8_t> body, std::uint64_t now,
                                 unsigned reliability) {
    auto &state = *impl_;
    std::vector<Bytes> result;
    try {
        require(state.alive && !body.empty() && body.size() <= memory_limit &&
                (reliability == 0 || reliability == 2 || reliability == 3));
        const std::size_t part_size = state.mtu - 80;
        const auto count = (body.size() + part_size - 1) / part_size;
        require(count <= 256 && (reliability != 0 || count == 1));
        if (reliability != 0)
            require(state.sent.size() + count <= 256);
        const auto split_id = state.split_id++;
        const auto order = state.order;
        if (reliability == 3)
            state.order = next(state.order);
        for (std::size_t i = 0; i < count; ++i) {
            Frame f;
            f.reliability = reliability;
            f.reliable = state.message;
            state.message = next(state.message);
            f.order = order;
            f.split = count > 1;
            f.count = static_cast<std::uint32_t>(count);
            f.index = static_cast<std::uint32_t>(i);
            f.split_id = split_id;
            auto bytes =
                body.subspan(i * part_size, std::min(part_size, body.size() - i * part_size));
            f.body.assign(bytes.begin(), bytes.end());
            Bytes datagram{0x84};
            const auto sequence = state.outgoing;
            state.outgoing = next(state.outgoing);
            integer(datagram, sequence, 3, true);
            append(datagram, encode_frame(f));
            require(datagram.size() <= state.mtu - 48 && !state.sent.contains(sequence));
            if (reliability != 0)
                state.sent.emplace(sequence, Impl::Sent{datagram, now, 1});
            result.push_back(std::move(datagram));
        }
    } catch (const std::invalid_argument &) {
        state.alive = false;
        result.clear();
    }
    return result;
}
std::vector<Bytes> Channel::poll(std::uint64_t now) {
    auto &state = *impl_;
    std::vector<Bytes> result;
    if (!state.alive)
        return result;
    for (auto &[sequence, sent] : state.sent) {
        const auto timeout = std::min(2000U, 250U << (std::min(sent.attempts, 4U) - 1));
        if (elapsed(now, sent.last) < timeout)
            continue;
        if (sent.attempts >= 8) {
            state.alive = false;
            return {};
        }
        if (result.size() == 32)
            break;
        sent.last = now;
        ++sent.attempts;
        result.push_back(sent.bytes);
    }
    for (auto i = state.splits.begin(); i != state.splits.end();) {
        if (elapsed(now, i->second.created) >= 10000) {
            state.alive = false;
            return {};
        }
        ++i;
    }
    return result;
}
struct Transport::Impl {
    enum class Stage { request, incoming, connected, closing };
    struct Peer {
        std::uint64_t guid, last, created, request_time = 0, accepted_time = 0;
        unsigned mtu;
        Stage stage = Stage::request;
        Channel channel;
        Peer(std::uint64_t g, std::uint64_t time, unsigned size)
            : guid(g), last(time), created(time), mtu(size), channel(size) {}
    };
    std::uint64_t guid;
    unsigned limit;
    std::map<Address, Peer> peers;
    Impl(std::uint64_t g, unsigned count) : guid(g), limit(std::clamp(count, 1U, 64U)) {}
    void message(const Address &from, Peer &peer, const Bytes &body, std::uint64_t now,
                 TransportResult &result) {
        Reader r(body);
        const auto id = r.byte();
        Bytes reply;
        if (id == 0x09 && peer.stage == Stage::request) {
            require(r.integer(8) == peer.guid);
            peer.request_time = r.integer(8);
            require(r.byte() == 0);
            r.finish();
            peer.accepted_time = now;
            reply = {0x10};
            append(reply, encode_address(from));
            integer(reply, 0, 2);
            for (unsigned i = 0; i < 20; ++i)
                append(reply, encode_address(Address{}));
            integer(reply, peer.request_time, 8);
            integer(reply, peer.accepted_time, 8);
            peer.stage = Stage::incoming;
        } else if (id == 0x13 && peer.stage == Stage::incoming) {
            address(r);
            for (unsigned i = 0; i < 20; ++i)
                address(r);
            require(r.integer(8) == peer.request_time);
            require(r.integer(8) == peer.accepted_time);
            r.finish();
            peer.stage = Stage::connected;
        } else if (id == 0 && peer.stage == Stage::connected) {
            const auto ping = r.integer(8);
            r.finish();
            reply = {3};
            integer(reply, ping, 8);
            integer(reply, now, 8);
        } else if (id == 0x15) {
            r.finish();
            peer.stage = Stage::closing;
        } else if (id >= 0x80 && peer.stage == Stage::connected) {
            result.applications.push_back({from, body});
        } else
            throw std::invalid_argument("unexpected connection phase");
        if (!reply.empty())
            for (auto &bytes : peer.channel.send(reply, now, id == 0 ? 0 : 3))
                result.outgoing.push_back({from, std::move(bytes)});
    }
};
Transport::Transport(std::uint64_t guid, unsigned limit)
    : impl_(std::make_unique<Impl>(guid, limit)) {}
Transport::~Transport() = default;
std::size_t Transport::peers() const {
    return impl_->peers.size();
}
TransportResult Transport::receive(const Address &from, std::span<const std::uint8_t> bytes,
                                   std::uint64_t now) {
    bool connected_packet = false;
    TransportResult result;
    if (bytes.empty() || bytes.size() > 2048)
        return result;
    auto &state = *impl_;
    try {
        if (bytes[0] == 5) {
            require(bytes.size() >= 18);
            Reader r(bytes);
            r.byte();
            r.magic_bytes();
            const auto protocol = r.byte();
            if (protocol != 9) {
                Bytes reply{0x19, 9};
                append(reply, magic);
                integer(reply, state.guid, 8);
                result.outgoing.push_back({from, std::move(reply)});
                return result;
            }
            require(bytes.size() + 28 >= 576);
            auto reply = offline(6, state.guid);
            reply.push_back(0);
            integer(reply, std::min<std::size_t>(1400, bytes.size() + 28), 2);
            result.outgoing.push_back({from, std::move(reply)});
            return result;
        }
        if (bytes[0] == 7) {
            Reader r(bytes);
            r.byte();
            r.magic_bytes();
            const auto server = address(r);
            require(server.ipv6 == from.ipv6);
            const auto mtu = static_cast<unsigned>(r.integer(2));
            const auto guid = r.integer(8);
            r.finish();
            require(mtu >= 576 && mtu <= 1400);
            auto peer = state.peers.find(from);
            if (peer == state.peers.end()) {
                require(state.peers.size() < state.limit);
                peer = state.peers.try_emplace(from, guid, now, mtu).first;
            } else
                require(peer->second.guid == guid && peer->second.mtu == mtu);
            peer->second.last = now;
            auto reply = offline(8, state.guid);
            append(reply, encode_address(from));
            integer(reply, mtu, 2);
            reply.push_back(0);
            result.outgoing.push_back({from, std::move(reply)});
            return result;
        }
        auto item = state.peers.find(from);
        if (item == state.peers.end())
            return result;
        connected_packet = true;
        auto &peer = item->second;
        auto incoming = peer.channel.receive(bytes, now);
        if (!incoming.valid || !peer.channel.alive()) {
            state.peers.erase(item);
            return result;
        }
        peer.last = now;
        for (auto &packet : incoming.outgoing)
            result.outgoing.push_back({from, std::move(packet)});
        for (const auto &message : incoming.messages) {
            require(result.applications.size() < 64);
            state.message(from, peer, message, now, result);
        }
        if (!peer.channel.alive())
            state.peers.erase(item);
    } catch (const std::invalid_argument &) {
        result.applications.clear();
        if (connected_packet)
            state.peers.erase(from);
    }
    return result;
}
std::vector<Outgoing> Transport::poll(std::uint64_t now) {
    std::vector<Outgoing> result;
    auto &peers = impl_->peers;
    for (auto i = peers.begin(); i != peers.end();) {
        auto &peer = i->second;
        if (elapsed(now, peer.created) >= 30000 || elapsed(now, peer.last) >= 15000 ||
            (peer.stage == Impl::Stage::closing && peer.channel.pending() == 0)) {
            i = peers.erase(i);
            continue;
        }
        auto packets = peer.channel.poll(now);
        if (!peer.channel.alive()) {
            i = peers.erase(i);
            continue;
        }
        for (auto &bytes : packets)
            result.push_back({i->first, std::move(bytes)});
        ++i;
    }
    return result;
}
std::vector<Outgoing> Transport::disconnect(const Address &from, std::uint64_t now) {
    std::vector<Outgoing> result;
    auto i = impl_->peers.find(from);
    if (i == impl_->peers.end())
        return result;
    const std::array<std::uint8_t, 1> body{0x15};
    for (auto &bytes : i->second.channel.send(body, now))
        result.push_back({from, std::move(bytes)});
    i->second.stage = Impl::Stage::closing;
    return result;
}
} // namespace bds::raknet
