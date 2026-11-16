#include "bds/network/binary.hpp"
#include <bit>
#include <limits>

namespace bds::protocol {
std::span<const std::uint8_t> ByteReader::take(std::size_t count) {
    if (count > bytes_.size())
        throw ProtocolError("truncated field");
    auto result = bytes_.first(count);
    bytes_ = bytes_.subspan(count);
    return result;
}
bool ByteReader::boolean() {
    const auto value = byte();
    if (value > 1)
        throw ProtocolError("invalid boolean");
    return value == 1;
}
std::uint32_t ByteReader::varint() {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const auto current = byte();
        if (i == 4 && (current & 0xf0) != 0)
            throw ProtocolError("varint exceeds 32 bits");
        value |= static_cast<std::uint32_t>(current & 0x7f) << (7 * i);
        if ((current & 0x80) == 0) {
            if (i > 0 && current == 0)
                throw ProtocolError("overlong varint");
            return value;
        }
    }
    throw ProtocolError("unterminated varint");
}
std::uint64_t ByteReader::varint64() {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 10; ++i) {
        const auto current = byte();
        if (i == 9 && (current & 0xfe)) throw ProtocolError("varint exceeds 64 bits");
        value |= std::uint64_t(current & 0x7f) << (7 * i);
        if (!(current & 0x80)) {
            if (i && !current) throw ProtocolError("overlong varint64");
            return value;
        }
    }
    throw ProtocolError("unterminated varint64");
}
std::int32_t ByteReader::signed_varint() {
    const auto bits = varint();
    return static_cast<std::int32_t>((bits >> 1) ^ (0U - (bits & 1U)));
}
std::int32_t ByteReader::big_endian_int() {
    const auto bytes = take(4);
    std::uint32_t value = 0;
    for (auto byte : bytes)
        value = (value << 8) | byte;
    return static_cast<std::int32_t>(value);
}
std::string ByteReader::string(std::size_t limit) {
    const auto length = varint();
    if (length > limit)
        throw ProtocolError("string exceeds limit");
    const auto bytes = take(length);
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}
void ByteReader::finish() const {
    if (!bytes_.empty())
        throw ProtocolError("trailing bytes");
}
void ByteWriter::varint(std::uint32_t value) {
    while (value >= 0x80) {
        byte(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    byte(static_cast<std::uint8_t>(value));
}
void ByteWriter::varint64(std::uint64_t value) {
    while (value >= 0x80) {
        byte(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    byte(static_cast<std::uint8_t>(value));
}
void ByteWriter::signed_varint(std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    varint((bits << 1) ^ (value < 0 ? 0xffffffffU : 0U));
}
void ByteWriter::signed_varint64(std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    varint64((bits << 1) ^ (value < 0 ? ~std::uint64_t{0} : 0U));
}
void ByteWriter::little_endian(std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        byte(static_cast<std::uint8_t>(value >> (8 * i)));
}
void ByteWriter::float32(float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    little_endian(std::bit_cast<std::uint32_t>(value), 4);
}
void ByteWriter::big_endian_int(std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    for (int shift = 24; shift >= 0; shift -= 8)
        byte(static_cast<std::uint8_t>(bits >> shift));
}
void ByteWriter::string(std::string_view value) {
    if (value.size() > 0xffffffffU)
        throw std::length_error("string too long");
    varint(static_cast<std::uint32_t>(value.size()));
    raw(std::span(reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
}
} // namespace bds::protocol
