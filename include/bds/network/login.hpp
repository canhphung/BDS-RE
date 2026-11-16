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
// Leading signed big-endian protocol field. LoginPacket::read (0x187e910) reads
// it before deciding whether the remaining body is understood.
std::int32_t login_protocol(std::span<const std::uint8_t> payload);
}
