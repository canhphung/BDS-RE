#pragma once
#include "bds/server/access.hpp"
#include "bds/server/properties.hpp"
#include "bds/world/world.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace bds {
std::vector<std::uint8_t> discovery_pong(std::span<const std::uint8_t> ping,std::uint64_t guid,const std::string& advertisement);
std::string server_advertisement(const Properties& properties,std::uint64_t guid);
class DiscoveryServer {
public:
 explicit DiscoveryServer(const Properties& properties);
 ~DiscoveryServer();
 DiscoveryServer(const DiscoveryServer&)=delete;
 DiscoveryServer& operator=(const DiscoveryServer&)=delete;
 // `access` backs whitelist admission and may be updated (XUID fill-in).
 // The simulation-owned World supplies current age and chunk contents.
 void poll(const Properties& properties, AccessLists& access, const World& world);
 // Port actually bound for the family, or nullopt when the host lacks it.
 std::optional<unsigned> bound_port(bool ipv6) const;
private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
