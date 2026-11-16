#include "bds/network/auth.hpp"
#include "bds/network/crypto.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace bds::auth {
namespace {
using Json = nlohmann::json;
constexpr std::size_t max_chain = 8;          // implementation policy; Xbox chains have 3
constexpr std::size_t max_token = 64 * 1024;  // implementation policy

struct Malformed : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

// WebToken: header.payload.signature (WebToken::WebToken 0x19dbd40 splits in 3).
struct Token {
    Json header, payload;
    std::string signing_input;
    crypto::Bytes signature;
};

Json decode_json_segment(std::string_view segment) {
    const auto bytes = crypto::base64url_decode(segment);
    if (!bytes)
        throw Malformed("invalid base64url JWT segment");
    auto json = Json::parse(bytes->begin(), bytes->end(), nullptr, false);
    if (!json.is_object())
        throw Malformed("JWT segment is not a JSON object");
    return json;
}

Token parse_token(std::string_view text) {
    if (text.size() > max_token)
        throw Malformed("JWT exceeds size limit");
    const auto first = text.find('.');
    const auto second = first == text.npos ? text.npos : text.find('.', first + 1);
    if (second == text.npos || text.find('.', second + 1) != text.npos)
        throw Malformed("JWT must have three segments");
    Token token{decode_json_segment(text.substr(0, first)), decode_json_segment(text.substr(first + 1, second - first - 1)),
                std::string(text.substr(0, second)), {}};
    auto signature = crypto::base64url_decode(text.substr(second + 1));
    if (!signature)
        throw Malformed("invalid JWT signature encoding");
    token.signature = std::move(*signature);
    return token;
}

std::string string_field(const Json &object, const char *key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::int64_t integer_field(const Json &object, const char *key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer())
        return 0; // Json::Value::asInt64 of a missing member is 0
    return found->get<std::int64_t>();
}

// WebToken::verify (0x19dca50): header x5u must be one of `trusted`; the
// algorithm must be supported (only ES384 here, by policy); the signature is
// checked with the x5u key.
bool verify_token(const Token &token, const std::vector<std::string> &trusted) {
    const auto x5u = string_field(token.header, "x5u");
    if (x5u.empty() || std::find(trusted.begin(), trusted.end(), x5u) == trusted.end())
        return false;
    if (string_field(token.header, "alg") != "ES384")
        return false;
    const auto key = crypto::base64_decode(x5u);
    if (!key)
        return false;
    const auto *input = reinterpret_cast<const std::uint8_t *>(token.signing_input.data());
    return crypto::verify_es384(*key, std::span(input, token.signing_input.size()), token.signature);
}

// Certificate: a token whose signature was accepted, plus its verified parent.
struct Certificate {
    const Token *token;
    std::unique_ptr<Certificate> parent;
    std::string identity_key() const { return string_field(token->payload, "identityPublicKey"); }
    bool authority() const {
        const auto found = token->payload.find("certificateAuthority");
        return found != token->payload.end() && found->is_boolean() && found->get<bool>();
    }
};

// UnverifiedCertificate::verify (0x19e41d0): verify this token against
// `trusted`; verify the parent recursively; if this token failed and the
// verified parent is a certificate authority, retry with the parent's
// identityPublicKey added to the trusted list.
std::unique_ptr<Certificate> verify_chain(const std::vector<Token> &chain, std::size_t index,
                                          const std::vector<std::string> &trusted) {
    bool ok = verify_token(chain[index], trusted);
    std::unique_ptr<Certificate> parent = index > 0 ? verify_chain(chain, index - 1, trusted) : nullptr;
    if (parent && parent->authority() && !ok) {
        auto extended = trusted;
        extended.push_back(parent->identity_key());
        ok = verify_token(chain[index], extended);
    }
    if (!ok)
        return nullptr;
    return std::make_unique<Certificate>(Certificate{&chain[index], std::move(parent)});
}

// Certificate::validate (0x19e4f40): parent valid, 0 < nbf <= now,
// 0 < exp, now <= exp and nbf <= exp.
bool validate(const Certificate &certificate, std::int64_t now) {
    if (certificate.parent && !validate(*certificate.parent, now))
        return false;
    const auto nbf = integer_field(certificate.token->payload, "nbf");
    const auto exp = integer_field(certificate.token->payload, "exp");
    return nbf > 0 && nbf <= now && exp > 0 && exp >= now && exp >= nbf;
}

std::vector<Token> parse_chain(const std::string &text) {
    // UnverifiedCertificate::fromString (0x19e3bc0) reads the "chain" array.
    const auto json = Json::parse(text, nullptr, false);
    if (!json.is_object() || !json.contains("chain") || !json["chain"].is_array())
        throw Malformed("certificate chain must be an object with a chain array");
    const auto &array = json["chain"];
    if (array.empty() || array.size() > max_chain)
        throw Malformed("certificate chain length outside policy");
    std::vector<Token> chain;
    for (const auto &entry : array) {
        if (!entry.is_string())
            throw Malformed("chain entries must be strings");
        chain.push_back(parse_token(entry.get<std::string>()));
    }
    return chain;
}

Identity identity_of(const Certificate &leaf, bool xbox) {
    Identity identity;
    identity.xbox_authenticated = xbox;
    identity.public_key = leaf.identity_key();
    const auto extra = leaf.token->payload.find("extraData");
    if (extra != leaf.token->payload.end() && extra->is_object()) {
        identity.display_name = string_field(*extra, "displayName");
        identity.identity = string_field(*extra, "identity");
        if (xbox)
            identity.xuid = string_field(*extra, "XUID");
    }
    return identity;
}

// One verification pass: chain against `trusted`, client token against the
// leaf identity key, then validity times at `now`.
std::unique_ptr<Certificate> verify_request(const std::vector<Token> &chain, const Token &client,
                                            const std::vector<std::string> &trusted, std::int64_t now, bool &expired) {
    auto leaf = verify_chain(chain, chain.size() - 1, trusted);
    if (!leaf || leaf->identity_key().empty() || !verify_token(client, {leaf->identity_key()}))
        return nullptr;
    if (!validate(*leaf, now)) {
        expired = true;
        return nullptr;
    }
    return leaf;
}
} // namespace

Result verify_login(const UnverifiedLoginBody &body, const LoginPolicy &policy, std::int64_t now) {
    try {
        const auto chain = parse_chain(body.certificate_chain);
        const auto client = parse_token(body.client_token);
        bool expired = false;
        // ConnectionRequest::verify (0x18f4030) with the trusted root keys.
        if (auto leaf = verify_request(chain, client, policy.trusted_keys, now, expired))
            return {identity_of(*leaf, true), {}, {}};
        if (policy.online_mode)
            return {std::nullopt, expired ? Failure::expired : Failure::untrusted,
                    expired ? "certificate chain outside its validity period" : "certificate chain not signed by a trusted key"};
        // verifySelfSigned (0x18f42a0) trusts the leaf's own identityPublicKey
        // and validates times at the leaf's nbf, so the server clock is not
        // consulted. The original does not re-check the client token here;
        // this implementation still requires it (stricter policy).
        const auto self_key = string_field(chain.back().payload, "identityPublicKey");
        const auto self_now = integer_field(chain.back().payload, "nbf");
        bool self_expired = false;
        if (auto leaf = verify_request(chain, client, {self_key}, self_now, self_expired))
            return {identity_of(*leaf, false), {}, {}};
        return {std::nullopt, self_expired ? Failure::expired : Failure::untrusted, "self-signed login rejected"};
    } catch (const std::invalid_argument &error) {
        return {std::nullopt, Failure::malformed, error.what()};
    }
}
} // namespace bds::auth
