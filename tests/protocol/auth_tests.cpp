// Login authentication against fixtures signed by an independent ES384
// implementation (tests/protocol/make_login_fixtures.py, Python cryptography).
#include "bds/network/auth.hpp"
#include "bds/network/connection.hpp"
#include "bds/network/crypto.hpp"
#include "bds/network/packet_ids.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace bds;
using Json = nlohmann::json;
namespace {
void check(bool ok, const std::string &what) {
    if (!ok)
        throw std::runtime_error("check failed: " + what);
}
crypto::Bytes bytes(std::string_view text) {
    return crypto::Bytes(text.begin(), text.end());
}

void base64() {
    const std::pair<std::string_view, std::string_view> vectors[] = {
        {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"}, {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}};
    for (const auto &[plain, encoded] : vectors) { // RFC 4648 section 10
        check(crypto::base64_encode(bytes(plain)) == encoded, "base64 encode " + std::string(encoded));
        check(crypto::base64_decode(encoded) == bytes(plain), "base64 decode " + std::string(encoded));
        std::string url(encoded.substr(0, encoded.find('=')));
        check(crypto::base64url_decode(url) == bytes(plain), "base64url decode " + url);
    }
    for (const char *bad : {"Zg=", "Zg", "Zh==", "Zm9v=", "Z===", "Zm 9v", "Zm9v\n", "Zm-v"})
        check(!crypto::base64_decode(bad), std::string("reject base64 ") + bad);
    for (const char *bad : {"Zg==", "Z", "Zh", "Zm+v", "Zm/v"})
        check(!crypto::base64url_decode(bad), std::string("reject base64url ") + bad);
    check(crypto::base64url_decode("-_8") == crypto::Bytes({0xfb, 0xff}), "url alphabet");
}

void hashes() {
    const auto abc = bytes("abc");
    const auto h256 = crypto::sha256(abc);
    const auto h384 = crypto::sha384(abc);
    // FIPS 180-2 test vectors for "abc".
    check(crypto::base64_encode(h256) == "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=", "sha256 abc");
    check(crypto::base64_encode(h384) == "ywB1P0WjXou1oD1pmsZQBycsMqsO3tFjGotgWkP/W+2AhgcroefMI1i67KE0yCWn", "sha384 abc");
}

std::pair<std::string, crypto::Bytes> split_jwt(const std::string &jwt) {
    const auto dot = jwt.rfind('.');
    return {jwt.substr(0, dot), *crypto::base64url_decode(jwt.substr(dot + 1))};
}

void signatures(const Json &fixtures) {
    const auto key = *crypto::base64_decode(fixtures["trusted_root"].get<std::string>());
    const auto chain = Json::parse(fixtures["chain"].get<std::string>())["chain"];
    const auto [input, signature] = split_jwt(chain[1].get<std::string>()); // root-signed token
    check(crypto::verify_es384(key, bytes(input), signature), "independent ES384 signature verifies");
    auto tampered = bytes(input);
    tampered.back() ^= 1;
    check(!crypto::verify_es384(key, tampered, signature), "tampered input rejected");
    auto bad_signature = signature;
    bad_signature[10] ^= 1;
    check(!crypto::verify_es384(key, bytes(input), bad_signature), "tampered signature rejected");
    check(!crypto::verify_es384(key, bytes(input), crypto::Bytes(96, 0)), "zero signature rejected");
    check(!crypto::verify_es384(key, bytes(input), std::span(signature).first(95)), "short signature rejected");
    auto bad_key = key;
    bad_key.back() ^= 1; // point no longer on the curve
    check(!crypto::verify_es384(bad_key, bytes(input), signature), "off-curve key rejected");
    check(!crypto::verify_es384(std::span(key).first(100), bytes(input), signature), "truncated key rejected");
    const auto mojang = *crypto::base64_decode(auth::mojang_root_key);
    check(mojang.size() == 120 && !crypto::verify_es384(mojang, bytes(input), signature), "wrong key rejected");
}

auth::Result login(const Json &fixtures, const std::string &chain, const std::string &client, bool online,
                   std::int64_t now) {
    auth::LoginPolicy policy{online, {fixtures["trusted_root"].get<std::string>()}};
    return auth::verify_login({chain, client}, policy, now);
}

void chains(const Json &fixtures) {
    const auto now = fixtures["now"].get<std::int64_t>();
    const auto chain = fixtures["chain"].get<std::string>();
    const auto client = fixtures["client_token"].get<std::string>();
    const auto &cases = fixtures["cases"];

    const auto ok = login(fixtures, chain, client, true, now);
    check(ok.identity.has_value(), "trusted chain accepted: " + ok.detail);
    check(ok.identity->xbox_authenticated && ok.identity->display_name == "Steve" &&
              ok.identity->xuid == fixtures["identity"]["XUID"].get<std::string>() &&
              ok.identity->identity == fixtures["identity"]["identity"].get<std::string>(),
          "authenticated identity fields");

    // The real Mojang key does not anchor the test chain.
    const auto mojang = auth::verify_login({chain, client}, auth::LoginPolicy{}, now);
    check(!mojang.identity && mojang.failure == auth::Failure::untrusted, "test chain untrusted under Mojang key");

    // Time window: nbf 1e9, exp 2e9 (Certificate::validate bounds are inclusive).
    check(login(fixtures, chain, client, true, 1'000'000'000).identity.has_value(), "now == nbf accepted");
    check(login(fixtures, chain, client, true, 2'000'000'000).identity.has_value(), "now == exp accepted");
    const auto early = login(fixtures, chain, client, true, 999'999'999);
    check(!early.identity && early.failure == auth::Failure::expired, "before nbf rejected");
    check(!login(fixtures, chain, client, true, 2'000'000'001).identity, "after exp rejected");

    for (const char *name : {"untrusted_leaf_signer", "intermediate_not_ca", "expired_leaf", "missing_nbf", "es256_header"}) {
        const auto result = login(fixtures, cases[name].get<std::string>(), client, true, now);
        check(!result.identity, std::string("rejected: ") + name);
    }
    check(login(fixtures, cases["expired_leaf"].get<std::string>(), client, true, now).failure == auth::Failure::expired,
          "expired leaf reported as expired");
    check(!login(fixtures, chain, cases["client_token_wrong_key"].get<std::string>(), true, now).identity,
          "client token signed by another key rejected");

    // Offline mode: the Xbox chain still authenticates via trusted keys first.
    check(login(fixtures, chain, client, false, now).identity->xbox_authenticated, "online chain in offline mode");
    // Self-signed chain: only accepted with online mode off, without an XUID.
    const auto self_chain = fixtures["self_signed_chain"].get<std::string>();
    check(!login(fixtures, self_chain, client, true, now).identity, "self-signed rejected in online mode");
    const auto self = login(fixtures, self_chain, client, false, now);
    check(self.identity && !self.identity->xbox_authenticated && self.identity->xuid.empty() &&
              self.identity->display_name == "Offline",
          "self-signed accepted in offline mode: " + self.detail);
    // verifySelfSigned validates at the leaf's own nbf, independent of the server clock.
    check(login(fixtures, self_chain, client, false, 3'000'000'000).identity.has_value(), "self-signed ignores clock");
    // A self-signed leaf may not attach a client token from a different key.
    check(!login(fixtures, self_chain, cases["client_token_wrong_key"].get<std::string>(), false, now).identity,
          "self-signed with foreign client token");

    // Malformed input never authenticates.
    for (const std::string bad : {std::string("not json"), std::string("{}"), std::string("{\"chain\":[]}"),
                                  std::string("{\"chain\":[1]}"), std::string("{\"chain\":[\"a.b\"]}"),
                                  std::string("{\"chain\":[\"a.b.c\"]}")}) {
        const auto result = login(fixtures, bad, client, false, now);
        check(!result.identity && result.failure == auth::Failure::malformed, "malformed chain: " + bad);
    }
    check(!login(fixtures, chain, "x.y.z", true, now).identity, "malformed client token");
    auto long_chain = Json::parse(chain);
    for (int i = 0; i < 6; ++i)
        long_chain["chain"].push_back(long_chain["chain"][0]);
    check(!login(fixtures, long_chain.dump(), client, true, now).identity, "chain length policy");
}

crypto::Bytes login_packet_body(const std::string &chain, const std::string &client) {
    protocol::ByteWriter inner;
    for (const auto &text : {chain, client}) {
        const auto size = static_cast<std::uint32_t>(text.size());
        for (int i = 0; i < 4; ++i)
            inner.byte(static_cast<std::uint8_t>(size >> (8 * i)));
        inner.raw(bytes(text));
    }
    protocol::ByteWriter body;
    body.big_endian_int(protocol::network_protocol);
    body.varint(static_cast<std::uint32_t>(inner.bytes().size()));
    body.raw(inner.bytes());
    return body.take();
}

std::string disconnect_message(const protocol::ConnectionOutput &output) {
    check(output.close && output.messages.size() == 1, "one closing batch");
    const auto packets = protocol::decode_batch(output.messages[0]);
    check(packets.size() == 1 && packets[0].header.id == static_cast<std::uint16_t>(protocol::PacketId::Disconnect),
          "disconnect packet");
    protocol::ByteReader reader(packets[0].body);
    check(!reader.boolean(), "message shown");
    return reader.string(4096);
}

void connection(const Json &fixtures) {
    const auto now = fixtures["now"].get<std::int64_t>();
    const auto trusted = auth::LoginPolicy{true, {fixtures["trusted_root"].get<std::string>()}};
    const auto send = [&](protocol::GameConnection &connection, const std::string &chain) {
        const std::array packets{protocol::GamePacket{{1, 0, 0}, login_packet_body(chain, fixtures["client_token"].get<std::string>())}};
        return connection.receive(protocol::encode_batch(packets));
    };
    protocol::GameConnection accepted(trusted, [&] { return now; });
    const auto ok = send(accepted, fixtures["chain"].get<std::string>());
    check(accepted.identity() && accepted.identity()->display_name == "Steve", "connection keeps verified identity");
    check(disconnect_message(ok).find("encryption handshake is not implemented") != std::string::npos,
          "authenticated login explains missing encryption");
    check(ok.reason.find("xuid=2535400000000001") != std::string::npos, "authenticated log line");

    protocol::GameConnection rejected(trusted, [&] { return now; });
    check(disconnect_message(send(rejected, fixtures["cases"]["untrusted_leaf_signer"].get<std::string>())) == protocol::not_authenticated,
          "untrusted login uses the original localisation key");
    check(!rejected.identity(), "no identity after rejection");

    protocol::GameConnection expired(trusted, [] { return std::int64_t{2'100'000'000}; });
    check(disconnect_message(send(expired, fixtures["chain"].get<std::string>())) == protocol::not_authenticated, "expired login");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: auth_tests <login_fixtures.json>");
        std::ifstream input(argv[1]);
        check(static_cast<bool>(input), "open fixtures");
        const auto fixtures = Json::parse(input);
        base64();
        hashes();
        signatures(fixtures);
        chains(fixtures);
        connection(fixtures);
        std::cout << "base64, SHA-2, ES384, certificate chain, validity window and login connection checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
