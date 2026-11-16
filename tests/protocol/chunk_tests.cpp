#include "bds/network/batch.hpp"
#include "bds/network/chunk.hpp"
#include "bds/network/level_packets.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace bds;
using namespace bds::protocol;
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("network chunk invariant failed");
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const std::invalid_argument &) {
        caught = true;
    }
    check(caught);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2);
        std::ifstream file(argv[1], std::ios::binary);
        check(static_cast<bool>(file));
        const Bytes compressed((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        const auto corpus = zlib_decompress(compressed, 16 * 1024 * 1024);
        ByteReader in(corpus);
        unsigned count = 0;
        std::uint64_t hash = 14695981039346656037ULL;
        while (!in.empty()) {
            const auto body = in.take(in.varint());
            const auto chunk = decode_full_chunk(body);
            check(full_chunk_body(chunk) == Bytes(body.begin(), body.end()));
            ++count;
            for (const auto &section : chunk.sections)
                for (const auto &layer : section)
                    for (unsigned x = 0; x < 16; ++x)
                        for (unsigned z = 0; z < 16; ++z)
                            for (unsigned y = 0; y < 16; ++y) {
                                const auto id = layer.block(x, y, z);
                                for (unsigned j = 0; j < 4; ++j)
                                    hash = (hash ^ static_cast<std::uint8_t>(id >> (j * 8))) *
                                           1099511628211ULL;
                            }
        }
        check(count == 139);
        // FNV computed by the independent Python chunk reader from the reference corpus.
        check(hash == 1276220901254225206ULL);
        for (unsigned size : {1U, 2U, 3U, 5U, 9U, 17U, 33U, 65U, 257U, 4096U}) {
            std::array<std::uint32_t, 4096> cells{};
            for (unsigned i = 0; i < 4096; ++i)
                cells[i] = i % size;
            const auto packed = pack_storage(cells);
            for (unsigned i = 0; i < 4096; ++i)
                check(packed.block(i >> 8, i & 15, (i >> 4) & 15) == cells[i]);
        }
        World world;
        world.set_block({-1, 17, -16}, 1);
        world.set_block({-16, 255, -1}, 4);
        const auto encoded = full_chunk_body(network_chunk(world, -1, -1));
        const auto decoded = decode_full_chunk(encoded);
        check(decoded.x == -1 && decoded.z == -1 && decoded.sections.size() == 16);
        check(decoded.sections[1][0].block(15, 1, 0) == runtime_id("minecraft:stone", 0).value());
        check(decoded.sections[15][0].block(0, 15, 15) ==
              runtime_id("minecraft:bedrock", 0).value());
        check(decoded.heights[15] == 18 && decoded.heights[240] == 256);
        for (std::size_t n = 0; n < encoded.size(); n += 43)
            rejects([&] { decode_full_chunk(std::span(encoded).first(n)); });
        auto bad = decoded;
        bad.sections[0][0].bits = 7;
        rejects([&] { full_chunk_body(bad); });
        bad = decoded;
        bad.sections[0][0].palette = {999999};
        rejects([&] { full_chunk_body(bad); });
        world.set_block({0, 1, 0}, 65535);
        rejects([&] { network_chunk(world, 0, 0); });
        std::cout << "139 original chunks match byte-for-byte and independent cell digest; palette "
                     "widths, bounds and world mapping passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
