// Game-packet batch, header and pre-login connection tests. Fixtures were
// generated independently with Python's struct and zlib modules.
#include "bds/network/batch.hpp"
#include "bds/network/connection.hpp"
#include "bds/network/packet_ids.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

using namespace bds::protocol;
namespace {
void check(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(std::string("check failed: ") + what);
}
template <class F> void rejects(F f, const char *what) {
    bool caught = false;
    try {
        f();
    } catch (const std::invalid_argument &) {
        caught = true;
    }
    check(caught, what);
}

const Bytes login_body = {0, 0, 1, 26, 25, 12, 0, 0, 0, 123, 34, 99, 104, 97, 105, 110, 34, 58, 91, 93, 125,
                          5, 0, 0, 0, 97, 46, 98, 46, 99};
// b'\xfe' + zlib.compress(batch, 7): Login (header 0x01) then id 0x13 with
// sender 1 and target 2 carrying body 01 02.
const Bytes python_batch = {254, 120, 218, 147, 103, 100, 96, 96, 148, 146, 228, 97, 96, 96, 168,
                            86, 74, 206, 72, 204, 204, 83, 178, 138, 142, 173, 101, 5, 242, 19,
                            245, 146, 244, 146, 89, 38, 123, 48, 50, 1, 0, 113, 229, 6, 251};

void binary_codec() {
    for (std::uint32_t value : {0U, 1U, 127U, 128U, 300U, 16383U, 16384U, 0x7fffffffU, 0xffffffffU}) {
        ByteWriter writer;
        writer.varint(value);
        ByteReader reader(writer.bytes());
        check(reader.varint() == value, "varint round trip");
        reader.finish();
    }
    const Bytes overlong = {0x80, 0x00};
    rejects([&] { ByteReader(overlong).varint(); }, "overlong varint");
    const Bytes wide = {0xff, 0xff, 0xff, 0xff, 0x1f};
    rejects([&] { ByteReader(wide).varint(); }, "varint beyond 32 bits");
    const Bytes unterminated = {0x80, 0x80};
    rejects([&] { ByteReader(unterminated).varint(); }, "unterminated varint");
    ByteWriter writer;
    writer.big_endian_int(-2);
    writer.string("hey");
    writer.boolean(true);
    check(writer.bytes() == Bytes({0xff, 0xff, 0xff, 0xfe, 3, 'h', 'e', 'y', 1}), "binary writer layout");
    ByteReader reader(writer.bytes());
    check(reader.big_endian_int() == -2 && reader.string(3) == "hey" && reader.boolean(), "binary reader");
    ByteReader limited(std::span(writer.bytes()).subspan(4));
    rejects([&] { limited.string(2); }, "string limit");
    const Bytes bad_bool = {2};
    rejects([&] { ByteReader(bad_bool).boolean(); }, "boolean domain");
}

void headers() {
    for (std::uint16_t id : {0, 1, 0x73, 0x3ff})
        for (std::uint8_t sender = 0; sender < 4; ++sender)
            for (std::uint8_t target = 0; target < 4; ++target) {
                const PacketHeader header{id, sender, target};
                check(decode_header(encode_header(header)) == header, "header round trip");
            }
    check(encode_header({0x13, 1, 2}) == 0x2413, "header bit layout");
    rejects([] { decode_header(1U << 14); }, "unknown header bits");
    rejects([] { encode_header({0x400, 0, 0}); }, "id beyond 10 bits");
}

void compression() {
    std::mt19937 random(1234);
    for (std::size_t size : {std::size_t{1}, std::size_t{100}, std::size_t{70000}}) {
        Bytes data(size);
        for (auto &byte : data)
            byte = static_cast<std::uint8_t>(random() % 7); // compressible
        const auto compressed = zlib_compress(data);
        // Level 7 selects FLEVEL 3 in the zlib header, as the original's ZlibCompressor(7) does.
        check(compressed.size() >= 2 && compressed[0] == 0x78 && compressed[1] == 0xda, "zlib header for level 7");
        check(zlib_decompress(compressed, size) == data, "zlib round trip");
        rejects([&] { zlib_decompress(compressed, size - 1); }, "decompression limit");
        for (std::size_t cut = 0; cut < compressed.size(); cut += 1 + compressed.size() / 50)
            rejects([&] { zlib_decompress(std::span(compressed).first(cut), size); }, "truncated stream");
        auto trailing = compressed;
        trailing.push_back(0);
        rejects([&] { zlib_decompress(trailing, size); }, "trailing bytes");
        auto corrupt = compressed;
        corrupt.back() ^= 1; // Adler-32 mismatch
        rejects([&] { zlib_decompress(corrupt, size); }, "checksum mismatch");
    }
    // Decompression bomb: 64 MiB of zeros must stop at the 4 MiB limit.
    const Bytes zeros(64 * 1024 * 1024);
    const auto bomb = zlib_compress(zeros, 9);
    rejects([&] { zlib_decompress(bomb, 4 * 1024 * 1024); }, "decompression bomb");
    const Bytes raw_deflate = {0x03, 0x00}; // empty raw deflate, no zlib header
    rejects([&] { zlib_decompress(raw_deflate, 16); }, "raw deflate without zlib header");
}

void batches() {
    const auto packets = decode_batch(python_batch);
    check(packets.size() == 2, "python batch packet count");
    check(packets[0].header == PacketHeader{1, 0, 0} && packets[0].body == login_body, "python login packet");
    check(packets[1].header == PacketHeader{0x13, 1, 2} && packets[1].body == Bytes({1, 2}), "python second packet");
    const auto encoded = encode_batch(packets);
    check(encoded[0] == batch_id && decode_batch(encoded).size() == 2, "batch round trip");
    check(decode_batch(encoded)[1].body == packets[1].body, "batch body round trip");
    auto wrong_id = python_batch;
    wrong_id[0] = 0xfd;
    rejects([&] { decode_batch(wrong_id); }, "non-batch id");
    rejects([&] { decode_batch(std::span(python_batch).first(1)); }, "empty batch payload");
    const Bytes empty_inner = {batch_id, 0x78, 0xda, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01};
    rejects([&] { decode_batch(empty_inner); }, "batch with no packets");
    // A length prefix pointing past the inflated data.
    auto overrun_inner = zlib_compress(Bytes{5, 1});
    Bytes overrun_message{batch_id};
    overrun_message.insert(overrun_message.end(), overrun_inner.begin(), overrun_inner.end());
    rejects([&] { decode_batch(overrun_message); }, "packet length overrun");
    BatchLimits few;
    few.max_packets = 1;
    rejects([&] { decode_batch(python_batch, few); }, "packet count limit");
}

void packet_table(const char *evidence) {
    std::ifstream input(evidence);
    check(static_cast<bool>(input), "open packet_ids.json");
    const auto json = nlohmann::json::parse(input);
    std::size_t named = 0;
    for (const auto &entry : json.at("packets")) {
        const auto id = entry.at("id").get<std::uint16_t>();
        check(packet_name(id) == entry.at("class").get<std::string>(), "generated header matches evidence");
        ++named;
    }
    check(named == 114 && packet_name(0x10).empty() && packet_name(0).empty() && packet_name(0x74).empty(),
          "packet id coverage");
    check(static_cast<std::uint16_t>(PacketId::Login) == 1 && static_cast<std::uint16_t>(PacketId::StartGame) == 0x0b,
          "known ids");
}

GamePacket only(const ConnectionOutput &output) {
    check(output.messages.size() == 1, "one response message");
    const auto packets = decode_batch(output.messages[0]);
    check(packets.size() == 1, "one response packet");
    return packets[0];
}

Bytes login_batch(Bytes body) {
    const std::array packets{GamePacket{{static_cast<std::uint16_t>(PacketId::Login), 0, 0}, std::move(body)}};
    return encode_batch(packets);
}

void connections() {
    for (auto [protocol, status] : {std::pair{281, 1}, std::pair{283, 2}, std::pair{-5, 1}}) {
        GameConnection connection;
        auto body = login_body;
        const auto value = static_cast<std::uint32_t>(protocol);
        for (int i = 0; i < 4; ++i)
            body[i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
        const auto output = connection.receive(login_batch(body));
        check(output.close && connection.state() == GameConnection::State::closed, "version mismatch closes");
        // PlayStatus then disconnectClient with the matching localisation key.
        check(output.messages.size() == 1, "one response message");
        const auto replies = decode_batch(output.messages[0]);
        check(replies.size() == 2, "PlayStatus and Disconnect");
        check(replies[0].header.id == static_cast<std::uint16_t>(PacketId::PlayStatus), "PlayStatus reply");
        check(replies[0].body == Bytes({0, 0, 0, static_cast<std::uint8_t>(status)}), "PlayStatus value");
        check(replies[1].header.id == static_cast<std::uint16_t>(PacketId::Disconnect) &&
                  replies[1].body == disconnect_body(false, status == 1 ? outdated_client : outdated_server),
              "outdated Disconnect reason");
        check(connection.receive(login_batch(login_body)).messages.empty(), "closed connection ignores input");
    }
    {
        GameConnection connection;
        const auto output = connection.receive(python_batch);
        const auto reply = only(output);
        check(output.close && reply.header.id == static_cast<std::uint16_t>(PacketId::Disconnect), "Disconnect reply");
        ByteReader reader(reply.body);
        check(!reader.boolean(), "disconnect screen shown");
        // The fixture's empty chain cannot authenticate (signed logins: auth_tests).
        check(reader.string(1024) == not_authenticated, "disconnect message");
        reader.finish();
    }
    {
        GameConnection connection;
        const std::array packets{GamePacket{{static_cast<std::uint16_t>(PacketId::Text), 0, 0}, {0}}};
        const auto output = connection.receive(encode_batch(packets));
        check(output.close && output.messages.empty() && output.reason.find("TextPacket") != std::string::npos,
              "packet before login rejected");
    }
    {
        GameConnection connection;
        auto body = login_body;
        body.push_back(0); // trailing byte in the length-prefixed outer body
        const auto output = connection.receive(login_batch(body));
        check(output.close && output.messages.empty(), "malformed login closes without reply");
    }
    {
        GameConnection connection;
        const Bytes garbage = {batch_id, 1, 2, 3};
        check(connection.receive(garbage).close, "corrupt batch closes");
    }
    check(disconnect_body(true, "ignored") == Bytes({1}), "hidden disconnect omits message");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: batch_tests <packet_ids.json>");
        binary_codec();
        headers();
        compression();
        batches();
        packet_table(argv[1]);
        connections();
        std::cout << "batch, header, zlib, packet-id table and pre-login connection checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
