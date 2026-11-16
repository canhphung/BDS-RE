#include "bds/network/encryption.hpp"
#include <algorithm>

namespace bds::protocol {
std::array<std::uint8_t, 32> session_key(std::span<const std::uint8_t> salt, std::span<const std::uint8_t> secret) {
    Bytes input(salt.begin(), salt.end());
    input.insert(input.end(), secret.begin(), secret.end());
    return crypto::sha256(input);
}

std::array<std::uint8_t, 8> packet_checksum(std::uint64_t counter, std::span<const std::uint8_t> data,
                                            std::span<const std::uint8_t> key) {
    Bytes input;
    input.reserve(8 + data.size() + key.size());
    for (int i = 0; i < 8; ++i)
        input.push_back(static_cast<std::uint8_t>(counter >> (8 * i)));
    input.insert(input.end(), data.begin(), data.end());
    input.insert(input.end(), key.begin(), key.end());
    const auto digest = crypto::sha256(input);
    std::array<std::uint8_t, 8> result{};
    std::copy_n(digest.begin(), result.size(), result.begin());
    return result;
}

PacketCipher::PacketCipher(const std::array<std::uint8_t, 32> &key)
    : key_(key), send_(key, std::span(key).first(16)), receive_(key, std::span(key).first(16)) {}

Bytes PacketCipher::seal(std::span<const std::uint8_t> payload) {
    Bytes out(payload.begin(), payload.end());
    const auto checksum = packet_checksum(sent_++, payload, key_);
    out.insert(out.end(), checksum.begin(), checksum.end());
    send_.encrypt(out);
    return out;
}

std::optional<Bytes> PacketCipher::open(std::span<const std::uint8_t> ciphertext) {
    Bytes plain(ciphertext.begin(), ciphertext.end());
    // The cipher stream advances even for a rejected packet, as the original
    // decrypts before checking.
    receive_.decrypt(plain);
    if (plain.size() < 8)
        return std::nullopt;
    const auto body = std::span(plain).first(plain.size() - 8);
    const auto expected = packet_checksum(received_++, body, key_);
    if (!std::equal(expected.begin(), expected.end(), plain.end() - 8))
        return std::nullopt;
    plain.resize(plain.size() - 8);
    return plain;
}
} // namespace bds::protocol
