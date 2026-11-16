#pragma once
#include <cstdint>
#include <span>
#include <string>
namespace bds {
// Decoded packet fields only. This type carries no verified identity or permissions.
struct UnverifiedLoginBody {
 std::string certificate_chain;
 std::string client_token;
};
// Excludes Bedrock packet header, batch compression and RakNet framing.
// Rejects incompatible protocols, malformed lengths and trailing bytes.
UnverifiedLoginBody decode_login_body(std::span<const std::uint8_t> payload);
}
