#pragma once
#include "bds/network/login.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bds::auth {
// Root key the original trusts for Xbox Live chains (literal at 0x242b423).
inline constexpr std::string_view mojang_root_key =
    "MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE8ELkixyLcwlZryUQcu1TvPOmI2B7vX83ndnWRUaXm74wFfa5f/lwQNTfrLVHa2PmenpGI6JhIMUJaWZrjmMj90NoKNFSNBuKdm8rYiXsfaz3K36x/1U26HpG0ZxK/V1V";

struct LoginPolicy {
    bool online_mode = true;
    std::vector<std::string> trusted_keys{std::string(mojang_root_key)};
};

// Identity derived only from signatures checked by verify_login.
struct Identity {
    bool xbox_authenticated = false; // chain anchored in a trusted key
    std::string display_name;
    std::string identity;   // extraData.identity (UUID string)
    std::string xuid;       // empty unless xbox_authenticated
    std::string public_key; // leaf identityPublicKey (base64 DER), used for encryption
};

enum class Failure { malformed, untrusted, expired };
struct Result {
    std::optional<Identity> identity;
    Failure failure = Failure::malformed;
    std::string detail; // server log only
};

// Reconstructs ServerNetworkHandler::handle(LoginPacket) authentication
// (0x18280a0-0x1828371): ConnectionRequest::verify against trusted keys at
// `now` (epoch seconds); when that fails and online mode is off, the
// self-signed path (ConnectionRequest::verifySelfSigned 0x18f42a0).
Result verify_login(const UnverifiedLoginBody &body, const LoginPolicy &policy, std::int64_t now);
} // namespace bds::auth
