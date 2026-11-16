#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bds::protocol {
using Bytes = std::vector<std::uint8_t>;

// Malformed or over-limit input from a peer. Callers drop the connection.
struct ProtocolError : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

// Bounded reader for Bedrock BinaryStream encodings (independent implementation).
class ByteReader {
  public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::span<const std::uint8_t> take(std::size_t count);
    std::uint8_t byte() { return take(1)[0]; }
    bool boolean();
    // Unsigned LEB128 limited to 32 bits; overlong encodings are rejected.
    std::uint32_t varint();
    std::uint64_t varint64();
    std::int32_t signed_varint();
    std::int32_t big_endian_int();
    // Unsigned-varint length followed by bytes (BinaryStream::writeString).
    std::string string(std::size_t limit);
    std::span<const std::uint8_t> remaining() const { return bytes_; }
    bool empty() const { return bytes_.empty(); }
    void finish() const;

  private:
    std::span<const std::uint8_t> bytes_;
};

class ByteWriter {
  public:
    void byte(std::uint8_t value) { bytes_.push_back(value); }
    void boolean(bool value) { byte(value ? 1 : 0); }
    void varint(std::uint32_t value);
    void varint64(std::uint64_t value);
    // Zigzag-encoded signed varints (BinaryStream::writeVarInt / writeVarInt64).
    void signed_varint(std::int32_t value);
    void signed_varint64(std::int64_t value);
    // Raw little-endian fixed-width values (BinaryStream::write of native bytes).
    void little_endian(std::uint64_t value, unsigned width);
    void float32(float value);
    void big_endian_int(std::int32_t value);
    void string(std::string_view value);
    void raw(std::span<const std::uint8_t> value) { bytes_.insert(bytes_.end(), value.begin(), value.end()); }
    const Bytes &bytes() const { return bytes_; }
    Bytes take() { return std::move(bytes_); }

  private:
    Bytes bytes_;
};
} // namespace bds::protocol
