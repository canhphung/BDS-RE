#pragma once
#include <array>
#include <memory>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Thin wrapper over the vendored Mbed TLS subset; no Mbed TLS types leak out.
namespace bds::crypto {
using Bytes = std::vector<std::uint8_t>;

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data);
std::array<std::uint8_t, 48> sha384(std::span<const std::uint8_t> data);

// Strict RFC 4648 decoders. `base64` requires canonical padding; `base64url`
// requires no padding (JWT segments). Both reject any other character.
std::optional<Bytes> base64_decode(std::string_view text);
std::optional<Bytes> base64url_decode(std::string_view text);
std::string base64_encode(std::span<const std::uint8_t> data);
std::string base64url_encode(std::span<const std::uint8_t> data);

// Verifies an ES384 JWT signature: SHA-384 over `message`, `signature` is the
// raw 96-byte r||s form, `public_key` a DER SubjectPublicKeyInfo for a P-384
// key (the decoded `x5u`/`identityPublicKey` value). Returns false for any
// malformed key, malformed signature or mismatch.
bool verify_es384(std::span<const std::uint8_t> public_key, std::span<const std::uint8_t> message,
                  std::span<const std::uint8_t> signature);

// CTR-DRBG seeded from the platform entropy source. Not thread-safe.
class Random {
  public:
    Random();
    ~Random();
    Random(const Random &) = delete;
    Random &operator=(const Random &) = delete;
    Bytes bytes(std::size_t count);

  private:
    friend class P384Key;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// P-384 private key for ES384 signing and ECDH (Crypto::Asymmetric in the original).
class P384Key {
  public:
    static P384Key generate(Random &random);
    // Big-endian private scalar; for tests with externally generated keys.
    static P384Key from_private(std::span<const std::uint8_t> scalar);
    P384Key(P384Key &&) noexcept;
    P384Key &operator=(P384Key &&) noexcept;
    ~P384Key();
    // DER SubjectPublicKeyInfo, the form carried base64-encoded in x5u.
    Bytes public_key() const;
    // Raw 96-byte r||s signature over SHA-384(message).
    Bytes sign_es384(std::span<const std::uint8_t> message, Random &random) const;
    // ECDH shared secret: the 48-byte big-endian X coordinate (as OpenSSL
    // EVP_PKEY_derive returns it). nullopt for a malformed or invalid peer key.
    std::optional<Bytes> shared_secret(std::span<const std::uint8_t> peer_public_key, Random &random) const;

  private:
    P384Key();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// AES-256 in 8-bit cipher feedback mode with a persistent stream state, as
// OpenSSL EVP_aes_256_cfb8 keeps it across EncryptUpdate calls.
class AesCfb8 {
  public:
    AesCfb8(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv);
    ~AesCfb8();
    AesCfb8(const AesCfb8 &) = delete;
    AesCfb8 &operator=(const AesCfb8 &) = delete;
    void encrypt(std::span<std::uint8_t> data);
    void decrypt(std::span<std::uint8_t> data);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace bds::crypto
