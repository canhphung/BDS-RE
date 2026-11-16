#include "bds/network/raknet.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace bds::raknet;
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("RakNet invariant failed");
}
Bytes ack(std::uint32_t seq, bool nack = false) {
    return {static_cast<std::uint8_t>(nack ? 0xa0 : 0xc0),
            0,
            1,
            1,
            static_cast<std::uint8_t>(seq),
            static_cast<std::uint8_t>(seq >> 8),
            static_cast<std::uint8_t>(seq >> 16)};
}
int main() {
    try {
        check(sequence_distance(0, 0xffffff) == 1 && sequence_distance(0xffffff, 0) == -1 &&
              sequence_distance(5, 2) == 3);
        Address a;
        a.ip = {127, 0, 0, 1};
        a.port = 19132;
        check(encode_address(a) == Bytes({4, 128, 255, 255, 254, 74, 188}));
        Channel sender(1200), receiver(1200);
        const Bytes hello{'h', 'e', 'l', 'l', 'o'};
        auto packet = sender.send(hello, 0);
        check(packet.size() == 1 && sender.pending() == 1);
        // Independent exact RakNet frame fixture: flags, LE triads, BE bit count.
        check(packet[0] ==
              Bytes({0x84, 0, 0, 0, 0x60, 0, 40, 0, 0, 0, 0, 0, 0, 0, 'h', 'e', 'l', 'l', 'o'}));
        auto result = receiver.receive(packet[0], 1);
        check(result.valid && result.messages == std::vector<Bytes>{hello});
        auto duplicate = receiver.receive(packet[0], 2);
        check(duplicate.valid && duplicate.messages.empty() && !duplicate.outgoing.empty());
        for (const auto &response : result.outgoing)
            check(sender.receive(response, 2).valid);
        check(sender.pending() == 0 && sender.poll(5000).empty());
        // Original bitstream readers ignore alignment padding bits.
        auto padding = packet[0];
        padding[0] |= 3;
        padding[4] |= 15;
        Channel padding_receiver(1200);
        check(padding_receiver.receive(padding, 0).messages == std::vector<Bytes>{hello});
        Channel padded_ack(1200);
        padded_ack.send(hello, 0);
        auto padded = ack(0);
        padded[0] |= 0x1f;
        check(padded_ack.receive(padded, 1).valid && padded_ack.pending() == 0);
        // Reordered messages must wait for order zero, then drain exactly once.
        Channel ordered_sender(1200), ordered_receiver(1200);
        const Bytes first{1}, second{2}, third{3};
        auto one = ordered_sender.send(first, 0), two = ordered_sender.send(second, 0),
             three = ordered_sender.send(third, 0);
        auto gap = ordered_receiver.receive(three[0], 1);
        check(gap.valid && gap.messages.empty() && gap.outgoing.size() == 2 &&
              gap.outgoing[1][0] == 0xa0);
        auto retransmit = ordered_sender.receive(gap.outgoing[1], 2);
        check(retransmit.valid && retransmit.outgoing.size() == 2);
        check(ordered_receiver.receive(one[0], 3).messages == std::vector<Bytes>{first});
        check(ordered_receiver.receive(two[0], 4).messages == std::vector<Bytes>({second, third}));
        // Missing ACK triggers timed retransmission with unchanged sequence/payload.
        Channel loss(1200);
        auto original = loss.send(hello, 100);
        check(loss.poll(349).empty());
        check(loss.poll(350) == original);
        check(loss.receive(ack(0), 351).valid && loss.pending() == 0);
        Channel exhausted(1200);
        exhausted.send(hello, 0);
        for (unsigned i = 1; i <= 9; ++i)
            exhausted.poll(i * 2500);
        check(!exhausted.alive());
        // Multi-fragment message arrives out of order and with duplicated fragments.
        Bytes large(6000);
        for (std::size_t i = 0; i < large.size(); ++i)
            large[i] = static_cast<std::uint8_t>(i);
        Channel fragment_sender(576), fragment_receiver(576);
        auto fragments = fragment_sender.send(large, 0);
        check(fragments.size() > 2);
        std::vector<Bytes> delivered;
        for (auto i = fragments.rbegin(); i != fragments.rend(); ++i) {
            auto r = fragment_receiver.receive(*i, 1);
            check(r.valid);
            for (auto &m : r.messages)
                delivered.push_back(m);
            check(fragment_receiver.receive(*i, 2).messages.empty());
        }
        check(delivered == std::vector<Bytes>{large});
        // Malformed range, excessive fragmentation and non-byte payloads close safely.
        Channel empty_ack(1200);
        check(empty_ack.receive(Bytes{0xc0, 0, 0}, 0).valid);
        Channel bad_ack(1200);
        check(!bad_ack.receive(Bytes{0xc0, 0, 1, 0, 2, 0, 0, 1, 0, 0}, 0).valid);
        auto bad_fragment = fragments[0];
        bad_fragment[14] = 0x7f;
        Channel invalid_fragment(576);
        check(!invalid_fragment.receive(bad_fragment, 0).valid);
        auto partial = packet[0];
        partial[6] = 39;
        Channel invalid_bits(1200);
        check(!invalid_bits.receive(partial, 0).valid);
        for (std::size_t n = 0; n < packet[0].size(); ++n) {
            Channel truncated(1200);
            check(!truncated.receive(std::span(packet[0]).first(n), 0).valid);
        }
        // Ordered channel state is independent; reliable and unreliable paths coexist.
        Channel mixed(1200), mixed_receiver(1200);
        auto unreliable = mixed.send(first, 0, 0);
        auto reliable = mixed.send(second, 0, 2);
        check(mixed_receiver.receive(reliable[0], 1).messages == std::vector<Bytes>{second});
        check(mixed_receiver.receive(unreliable[0], 2).messages == std::vector<Bytes>{first});
        // Future sequenced packets must not skip a missing reliable ordered packet.
        Channel sequence_receiver(1200);
        Bytes seq_packet = {0x84, 1, 0, 0, 0x80, 0, 8, 1, 0, 0, 5, 0, 0, 1, 0, 0, 0, 'S'};
        check(sequence_receiver.receive(seq_packet, 0).messages.empty());
        check(
            sequence_receiver.receive(Bytes{0x84, 0, 0, 0, 0x60, 0, 8, 0, 0, 0, 0, 0, 0, 0, 'A'}, 1)
                .messages == std::vector<Bytes>({Bytes{'A'}, Bytes{'S'}}));
        check(
            sequence_receiver.receive(Bytes{0x84, 2, 0, 0, 0x60, 0, 8, 2, 0, 0, 1, 0, 0, 0, 'B'}, 2)
                .messages == std::vector<Bytes>{Bytes{'B'}});
        // Open handshakes are bounded, retry-idempotent and do not authenticate users.
        Transport transport(0x0102030405060708ULL, 1);
        Address remote = a;
        remote.port = 10000;
        Bytes request2 = {7,   0,   255, 255, 0,  254, 254, 254, 254,
                          253, 253, 253, 253, 18, 52,  86,  120};
        const auto server_address = encode_address(a);
        request2.insert(request2.end(), server_address.begin(), server_address.end());
        request2.insert(request2.end(), {4, 176, 0, 0, 0, 0, 0, 0, 0, 1});
        auto open = transport.receive(remote, request2, 0);
        check(open.outgoing.size() == 1 && open.outgoing[0].bytes[0] == 8 &&
              transport.peers() == 1);
        check(transport.receive(remote, request2, 1).outgoing[0].bytes == open.outgoing[0].bytes);
        auto other = remote;
        other.port = 10001;
        check(transport.receive(other, request2, 2).outgoing.empty() && transport.peers() == 1);
        auto malformed = request2;
        malformed.pop_back();
        check(transport.receive(remote, malformed, 3).outgoing.empty() && transport.peers() == 1);
        transport.poll(15001);
        check(transport.peers() == 0);
        // Timeout incomplete assembly rather than retaining memory indefinitely.
        Channel expiry(576);
        check(expiry.receive(fragments[0], 0).valid);
        expiry.poll(10000);
        check(!expiry.alive());
        std::cout << "RakNet golden codec, ordered delivery, loss, ACK/NACK, reassembly and bounds "
                     "passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
