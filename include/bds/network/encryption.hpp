#pragma once
#include "bds/network/binary.hpp"
#include "bds/network/crypto.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace bds::protocol {
// Session key derivation from ServerNetworkHandler::handle(LoginPacket):
// Crypto::Hash::hash(SHA-256 = HashType 2, salt + secret) (0x1829255/0x182926d).
std::array<std::uint8_t, 32> session_key(std::span<const std::uint8_t> salt, std::span<const std::uint8_t> secret);

// Packet checksum: libCrypto Crypto::Hash::HMAC::sign (0x12d20) is not RFC 2104
// HMAC; it hashes the 64-bit little-endian counter, the data and the key with
// the configured hash (SHA-256) and keeps resultSize = 8 bytes (HMAC
// constructed with 8 in EncryptedNetworkPeer::enableEncryption).
std::array<std::uint8_t, 8> packet_checksum(std::uint64_t counter, std::span<const std::uint8_t> data,
                                            std::span<const std::uint8_t> key);

// EncryptedNetworkPeer (0x1896d50): independent AES-256-CFB8 streams for each
// direction keyed with the session key, IV = first 16 key bytes; each
// payload carries an 8-byte checksum with its own send/receive counter.
class PacketCipher {
  public:
    explicit PacketCipher(const std::array<std::uint8_t, 32> &key);
    // Appends the checksum and encrypts (EncryptedNetworkPeer::sendPacket 0x1896e90).
    Bytes seal(std::span<const std::uint8_t> payload);
    // Decrypts and verifies (receivePacket 0x1897040). nullopt on a short
    // payload or checksum mismatch; the original drops such a packet.
    std::optional<Bytes> open(std::span<const std::uint8_t> ciphertext);

  private:
    std::array<std::uint8_t, 32> key_;
    crypto::AesCfb8 send_, receive_;
    std::uint64_t sent_ = 0, received_ = 0;
};
} // namespace bds::protocol
