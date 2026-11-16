#include "bds/network/crypto.hpp"
#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>
#include <algorithm>
#include <stdexcept>
#include <string>

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
// Loads secp384r1 and the public point from a strict P-384 SPKI.
bool load_public(std::span<const std::uint8_t> public_key, Group &group, Point &point) {
    if (public_key.size() != p384_spki_prefix.size() + point_size ||
        !std::equal(p384_spki_prefix.begin(), p384_spki_prefix.end(), public_key.begin()))
        return false;
    const auto encoded = public_key.subspan(p384_spki_prefix.size());
    return mbedtls_ecp_group_load(&group.value, MBEDTLS_ECP_DP_SECP384R1) == 0 &&
           mbedtls_ecp_point_read_binary(&group.value, &point.value, encoded.data(), encoded.size()) == 0 &&
           mbedtls_ecp_check_pubkey(&group.value, &point.value) == 0;
}

void require_ok(int status, const char *what) {
    if (status != 0)
        throw std::runtime_error(std::string(what) + " failed (Mbed TLS " + std::to_string(status) + ")");
}
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
    if (signature.size() != 96)
        return false;
    Group group;
    Point point;
    Mpi r, s;
    if (!load_public(public_key, group, point) || mbedtls_mpi_read_binary(&r.value, signature.data(), 48) != 0 ||
        mbedtls_mpi_read_binary(&s.value, signature.data() + 48, 48) != 0)
        return false;
    const auto digest = sha384(message);
    return mbedtls_ecdsa_verify(&group.value, digest.data(), digest.size(), &point.value, &r.value, &s.value) == 0;
}

std::string base64url_encode(std::span<const std::uint8_t> data) {
    auto text = base64_encode(data);
    for (auto &c : text)
        c = c == '+' ? '-' : c == '/' ? '_' : c;
    while (!text.empty() && text.back() == '=')
        text.pop_back();
    return text;
}

struct Random::Impl {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    Impl() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
        static constexpr char personalisation[] = "bds-re";
        const auto status = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                                  reinterpret_cast<const unsigned char *>(personalisation),
                                                  sizeof(personalisation) - 1);
        if (status != 0) {
            release();
            require_ok(status, "random seeding");
        }
    }
    ~Impl() { release(); }
    void release() {
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
    static int generate(void *self, unsigned char *out, std::size_t size) {
        return mbedtls_ctr_drbg_random(&static_cast<Impl *>(self)->drbg, out, size);
    }
};
Random::Random() : impl_(std::make_unique<Impl>()) {}
Random::~Random() = default;
Bytes Random::bytes(std::size_t count) {
    Bytes out(count);
    require_ok(mbedtls_ctr_drbg_random(&impl_->drbg, out.data(), out.size()), "random generation");
    return out;
}

struct P384Key::Impl {
    Group group;
    Mpi d;
    Point q;
};
P384Key::P384Key() : impl_(std::make_unique<Impl>()) {
    require_ok(mbedtls_ecp_group_load(&impl_->group.value, MBEDTLS_ECP_DP_SECP384R1), "loading secp384r1");
}
P384Key::P384Key(P384Key &&) noexcept = default;
P384Key &P384Key::operator=(P384Key &&) noexcept = default;
P384Key::~P384Key() = default;

P384Key P384Key::generate(Random &random) {
    P384Key key;
    require_ok(mbedtls_ecp_gen_keypair(&key.impl_->group.value, &key.impl_->d.value, &key.impl_->q.value,
                                       Random::Impl::generate, random.impl_.get()),
               "P-384 key generation");
    return key;
}

P384Key P384Key::from_private(std::span<const std::uint8_t> scalar) {
    P384Key key;
    auto &impl = *key.impl_;
    require_ok(mbedtls_mpi_read_binary(&impl.d.value, scalar.data(), scalar.size()), "reading private scalar");
    require_ok(mbedtls_ecp_check_privkey(&impl.group.value, &impl.d.value), "checking private scalar");
    // Deterministic public point derivation; no blinding RNG is needed for a test import.
    Random random;
    require_ok(mbedtls_ecp_mul(&impl.group.value, &impl.q.value, &impl.d.value, &impl.group.value.G,
                               Random::Impl::generate, random.impl_.get()),
               "deriving public point");
    return key;
}

Bytes P384Key::public_key() const {
    Bytes out(p384_spki_prefix.begin(), p384_spki_prefix.end());
    std::array<std::uint8_t, point_size> point{};
    std::size_t length = 0;
    require_ok(mbedtls_ecp_point_write_binary(&impl_->group.value, &impl_->q.value, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                              &length, point.data(), point.size()),
               "encoding public key");
    if (length != point.size())
        throw std::runtime_error("unexpected P-384 point size");
    out.insert(out.end(), point.begin(), point.end());
    return out;
}

Bytes P384Key::sign_es384(std::span<const std::uint8_t> message, Random &random) const {
    const auto digest = sha384(message);
    Mpi r, s;
    require_ok(mbedtls_ecdsa_sign(&impl_->group.value, &r.value, &s.value, &impl_->d.value, digest.data(), digest.size(),
                                  Random::Impl::generate, random.impl_.get()),
               "ES384 signing");
    Bytes out(96);
    require_ok(mbedtls_mpi_write_binary(&r.value, out.data(), 48), "encoding r");
    require_ok(mbedtls_mpi_write_binary(&s.value, out.data() + 48, 48), "encoding s");
    return out;
}

std::optional<Bytes> P384Key::shared_secret(std::span<const std::uint8_t> peer_public_key, Random &random) const {
    Group group;
    Point peer;
    if (!load_public(peer_public_key, group, peer))
        return std::nullopt;
    Mpi z;
    if (mbedtls_ecdh_compute_shared(&impl_->group.value, &z.value, &peer.value, &impl_->d.value,
                                    Random::Impl::generate, random.impl_.get()) != 0)
        return std::nullopt;
    Bytes out(48);
    require_ok(mbedtls_mpi_write_binary(&z.value, out.data(), out.size()), "encoding shared secret");
    return out;
}

struct AesCfb8::Impl {
    mbedtls_aes_context aes;
    std::array<unsigned char, 16> iv{};
    Impl() { mbedtls_aes_init(&aes); }
    ~Impl() { mbedtls_aes_free(&aes); }
    void run(int mode, std::span<std::uint8_t> data) {
        // In-place operation is supported by mbedtls_aes_crypt_cfb8.
        require_ok(mbedtls_aes_crypt_cfb8(&aes, mode, data.size(), iv.data(), data.data(), data.data()), "AES-CFB8");
    }
};
AesCfb8::AesCfb8(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv)
    : impl_(std::make_unique<Impl>()) {
    if (key.size() != 32 || iv.size() != 16)
        throw std::invalid_argument("AES-256-CFB8 needs a 32-byte key and 16-byte IV");
    // CFB uses the forward cipher for both directions.
    require_ok(mbedtls_aes_setkey_enc(&impl_->aes, key.data(), 256), "AES key schedule");
    std::copy(iv.begin(), iv.end(), impl_->iv.begin());
}
AesCfb8::~AesCfb8() = default;
void AesCfb8::encrypt(std::span<std::uint8_t> data) {
    impl_->run(MBEDTLS_AES_ENCRYPT, data);
}
void AesCfb8::decrypt(std::span<std::uint8_t> data) {
    impl_->run(MBEDTLS_AES_DECRYPT, data);
}
} // namespace bds::crypto
