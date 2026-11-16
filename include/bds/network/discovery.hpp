#pragma once
#include "bds/server/properties.hpp"
#include <cstdint>
#include <memory>
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
 void poll(const Properties& properties);
private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
