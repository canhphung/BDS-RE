#include "bds/network/crypto.hpp"
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <algorithm>
#include <stdexcept>

namespace bds::crypto {
namespace {
// DER SubjectPublicKeyInfo header for an uncompressed P-384 point:
// SEQUENCE { SEQUENCE { id-ecPublicKey, secp384r1 }, BIT STRING (0 unused) }.
constexpr std::array<std::uint8_t, 23> p384_spki_prefix = {0x30, 0x76, 0x30, 0x10, 0x06, 0x07, 0x2a, 0x86,
                                                           0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x05, 0x2b,
                                                           0x81, 0x04, 0x00, 0x22, 0x03, 0x62, 0x00};
constexpr std::size_t point_size = 97; // 0x04 || X || Y

constexpr std::string_view standard_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view url_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::optional<Bytes> decode(std::string_view text, std::string_view alphabet, bool padded) {
    std::size_t length = text.size();
    std::size_t padding = 0;
    if (padded) {
        if (length % 4 != 0)
            return std::nullopt;
        while (padding < 2 && length > 0 && text[length - 1] == '=') {
            --length;
            ++padding;
        }
    }
    // A lone trailing sextet cannot encode a whole byte.
    if (length % 4 == 1)
        return std::nullopt;
    if (padded && padding != (4 - length % 4) % 4)
        return std::nullopt;
    Bytes out;
    out.reserve(length * 3 / 4);
    std::uint32_t buffer = 0;
    unsigned bits = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const auto index = alphabet.find(text[i]);
        if (index == std::string_view::npos)
            return std::nullopt;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(index);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>(buffer >> bits));
        }
    }
    // Leftover bits must be zero so that every byte string has one encoding.
    if (bits > 0 && (buffer & ((1U << bits) - 1)) != 0)
        return std::nullopt;
    return out;
}

struct Mpi {
    mbedtls_mpi value;
    Mpi() { mbedtls_mpi_init(&value); }
    ~Mpi() { mbedtls_mpi_free(&value); }
    Mpi(const Mpi &) = delete;
    Mpi &operator=(const Mpi &) = delete;
};
struct Group {
    mbedtls_ecp_group value;
    Group() { mbedtls_ecp_group_init(&value); }
    ~Group() { mbedtls_ecp_group_free(&value); }
    Group(const Group &) = delete;
    Group &operator=(const Group &) = delete;
};
struct Point {
    mbedtls_ecp_point value;
    Point() { mbedtls_ecp_point_init(&value); }
    ~Point() { mbedtls_ecp_point_free(&value); }
    Point(const Point &) = delete;
    Point &operator=(const Point &) = delete;
};
} // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
    std::array<std::uint8_t, 32> digest{};
    if (mbedtls_sha256(data.data(), data.size(), digest.data(), 0) != 0)
        throw std::runtime_error("sha256 failed");
    return digest;
}

std::array<std::uint8_t, 48> sha384(std::span<const std::uint8_t> data) {
    std::array<std::uint8_t, 48> digest{};
    if (mbedtls_sha512(data.data(), data.size(), digest.data(), 1) != 0)
        throw std::runtime_error("sha384 failed");
    return digest;
}

std::optional<Bytes> base64_decode(std::string_view text) {
    return decode(text, standard_alphabet, true);
}

std::optional<Bytes> base64url_decode(std::string_view text) {
    return decode(text, url_alphabet, false);
}

std::string base64_encode(std::span<const std::uint8_t> data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16 |
                                    static_cast<std::uint32_t>(data[i + 1]) << 8 | data[i + 2];
        for (int shift = 18; shift >= 0; shift -= 6)
            out.push_back(standard_alphabet[(chunk >> shift) & 63]);
    }
    const auto rest = data.size() - i;
    if (rest > 0) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16 |
                                    (rest == 2 ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0U);
        out.push_back(standard_alphabet[(chunk >> 18) & 63]);
        out.push_back(standard_alphabet[(chunk >> 12) & 63]);
        out.push_back(rest == 2 ? standard_alphabet[(chunk >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

bool verify_es384(std::span<const std::uint8_t> public_key, std::span<const std::uint8_t> message,
                  std::span<const std::uint8_t> signature) {
    if (public_key.size() != p384_spki_prefix.size() + point_size || signature.size() != 96 ||
        !std::equal(p384_spki_prefix.begin(), p384_spki_prefix.end(), public_key.begin()))
        return false;
    Group group;
    Point point;
    Mpi r, s;
    const auto encoded_point = public_key.subspan(p384_spki_prefix.size());
    if (mbedtls_ecp_group_load(&group.value, MBEDTLS_ECP_DP_SECP384R1) != 0 ||
        mbedtls_ecp_point_read_binary(&group.value, &point.value, encoded_point.data(), encoded_point.size()) != 0 ||
        mbedtls_ecp_check_pubkey(&group.value, &point.value) != 0 ||
        mbedtls_mpi_read_binary(&r.value, signature.data(), 48) != 0 ||
        mbedtls_mpi_read_binary(&s.value, signature.data() + 48, 48) != 0)
        return false;
    const auto digest = sha384(message);
    return mbedtls_ecdsa_verify(&group.value, digest.data(), digest.size(), &point.value, &r.value, &s.value) == 0;
}
} // namespace bds::crypto
