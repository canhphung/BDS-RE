#pragma once
#include "bds/network/binary.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace bds::protocol {
// RakNet application message ID for a compressed game-packet batch
// (RakNetInstance::runEvents compares it at 0x1926143).
inline constexpr std::uint8_t batch_id = 0xfe;
// Util::compress builds leveldb::ZlibCompressor(7): zlib header, 15-bit window.
inline constexpr int batch_compression_level = 7;

struct BatchLimits {
    std::size_t max_decompressed = 4 * 1024 * 1024;
    std::size_t max_packets = 1024;
};

// zlib (RFC 1950) helpers. decompress rejects corrupt streams, trailing data
// and output beyond `limit` without allocating more than `limit` bytes.
Bytes zlib_compress(std::span<const std::uint8_t> input, int level = batch_compression_level);
Bytes zlib_decompress(std::span<const std::uint8_t> input, std::size_t limit);

// PacketHeader is one unsigned varint: bits 0-9 packet id, 10-11 sender
// sub-client, 12-13 target sub-client (PacketHeader getters 0x1883980,
// 0x18839a0, 0x18839d0). Higher bits are rejected as an implementation policy.
struct PacketHeader {
    std::uint16_t id = 0;
    std::uint8_t sender = 0, target = 0;
    auto operator<=>(const PacketHeader &) const = default;
};
std::uint32_t encode_header(PacketHeader header);
PacketHeader decode_header(std::uint32_t value);

struct GamePacket {
    PacketHeader header;
    Bytes body;
};

// `message` starts with batch_id. The inflated batch is a sequence of
// varint-length strings (BatchedNetworkPeer::receivePacket 0x18e3c80), each
// holding a header varint followed by the packet body.
std::vector<GamePacket> decode_batch(std::span<const std::uint8_t> message, const BatchLimits &limits = {});
Bytes encode_batch(std::span<const GamePacket> packets);
// The zlib payload alone, without the 0xfe id. EncryptedNetworkPeer sits
// between CompressedNetworkPeer and the RakNet peer (NetworkHandler::Connection
// constructor 0x18819f0), so encryption applies to exactly this payload.
Bytes compress_packets(std::span<const GamePacket> packets);
std::vector<GamePacket> decompress_packets(std::span<const std::uint8_t> payload, const BatchLimits &limits = {});
} // namespace bds::protocol
