#include "bds/network/batch.hpp"
#include <zlib.h>
#include <algorithm>
#include <limits>
#include <memory>

namespace bds::protocol {
namespace {
// zlib's uInt counts are 32 bits; every buffer here is bounded well below that.
constexpr std::size_t max_chunk = std::numeric_limits<uInt>::max();
}

Bytes zlib_compress(std::span<const std::uint8_t> input, int level) {
    if (input.size() > max_chunk)
        throw std::length_error("batch too large to compress");
    z_stream stream{};
    if (deflateInit2(&stream, level, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("deflateInit2 failed");
    std::unique_ptr<z_stream, int (*)(z_stream *)> guard(&stream, deflateEnd);
    Bytes output(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_in = const_cast<Bytef *>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END)
        throw std::runtime_error("deflate did not finish");
    output.resize(stream.total_out);
    return output;
}

Bytes zlib_decompress(std::span<const std::uint8_t> input, std::size_t limit) {
    if (input.empty() || input.size() > max_chunk || limit > max_chunk)
        throw ProtocolError("invalid compressed batch size");
    z_stream stream{};
    if (inflateInit2(&stream, 15) != Z_OK)
        throw std::runtime_error("inflateInit2 failed");
    std::unique_ptr<z_stream, int (*)(z_stream *)> guard(&stream, inflateEnd);
    stream.next_in = const_cast<Bytef *>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    Bytes output;
    // Grow geometrically but never past limit + 1, so a decompression bomb is
    // detected after at most limit + 1 bytes of output.
    std::size_t capacity = std::min<std::size_t>(limit + 1, std::max<std::size_t>(input.size() * 4, 4096));
    while (true) {
        output.resize(capacity);
        stream.next_out = output.data() + stream.total_out;
        stream.avail_out = static_cast<uInt>(capacity - stream.total_out);
        const auto status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_STREAM_END)
            break;
        if (status != Z_OK && status != Z_BUF_ERROR)
            throw ProtocolError("corrupt compressed batch");
        if (stream.total_out > limit)
            throw ProtocolError("decompressed batch exceeds limit");
        if (stream.avail_out != 0)
            throw ProtocolError("truncated compressed batch");
        if (capacity == limit + 1)
            throw ProtocolError("decompressed batch exceeds limit");
        capacity = std::min(limit + 1, capacity * 2);
    }
    if (stream.total_out > limit)
        throw ProtocolError("decompressed batch exceeds limit");
    if (stream.avail_in != 0)
        throw ProtocolError("trailing bytes after compressed batch");
    output.resize(stream.total_out);
    return output;
}

std::uint32_t encode_header(PacketHeader header) {
    if (header.id > 0x3ff || header.sender > 3 || header.target > 3)
        throw std::invalid_argument("packet header field out of range");
    return header.id | (static_cast<std::uint32_t>(header.sender) << 10) |
           (static_cast<std::uint32_t>(header.target) << 12);
}

PacketHeader decode_header(std::uint32_t value) {
    if (value >> 14)
        throw ProtocolError("packet header has unknown bits");
    return {static_cast<std::uint16_t>(value & 0x3ff), static_cast<std::uint8_t>((value >> 10) & 3),
            static_cast<std::uint8_t>((value >> 12) & 3)};
}

std::vector<GamePacket> decode_batch(std::span<const std::uint8_t> message, const BatchLimits &limits) {
    if (message.size() < 2 || message[0] != batch_id)
        throw ProtocolError("not a game packet batch");
    const auto inflated = zlib_decompress(message.subspan(1), limits.max_decompressed);
    ByteReader batch(inflated);
    std::vector<GamePacket> packets;
    while (!batch.empty()) {
        if (packets.size() == limits.max_packets)
            throw ProtocolError("too many packets in batch");
        const auto length = batch.varint();
        ByteReader packet(batch.take(length));
        GamePacket decoded{decode_header(packet.varint()), {}};
        const auto body = packet.remaining();
        decoded.body.assign(body.begin(), body.end());
        packets.push_back(std::move(decoded));
    }
    if (packets.empty())
        throw ProtocolError("empty game packet batch");
    return packets;
}

Bytes encode_batch(std::span<const GamePacket> packets) {
    ByteWriter batch;
    for (const auto &packet : packets) {
        ByteWriter encoded;
        encoded.varint(encode_header(packet.header));
        encoded.raw(packet.body);
        if (encoded.bytes().size() > 0xffffffffU)
            throw std::length_error("game packet too large");
        batch.varint(static_cast<std::uint32_t>(encoded.bytes().size()));
        batch.raw(encoded.bytes());
    }
    auto compressed = zlib_compress(batch.bytes());
    Bytes message{batch_id};
    message.insert(message.end(), compressed.begin(), compressed.end());
    return message;
}
} // namespace bds::protocol
