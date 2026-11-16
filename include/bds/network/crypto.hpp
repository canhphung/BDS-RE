#pragma once
#include <array>
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

// Verifies an ES384 JWT signature: SHA-384 over `message`, `signature` is the
// raw 96-byte r||s form, `public_key` a DER SubjectPublicKeyInfo for a P-384
// key (the decoded `x5u`/`identityPublicKey` value). Returns false for any
// malformed key, malformed signature or mismatch.
bool verify_es384(std::span<const std::uint8_t> public_key, std::span<const std::uint8_t> message,
                  std::span<const std::uint8_t> signature);
} // namespace bds::crypto
