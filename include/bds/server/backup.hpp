#pragma once
#include "bds/world/world.hpp"
#include <filesystem>
#include <future>
#include <string>
namespace bds {
class Backup {
public:
 enum class State {idle,preparing,ready,failed};
 explicit Backup(std::filesystem::path world):world_dir_(std::move(world)){}
 void hold(const World& world);
 void poll();void resume();
 State state() const{return state_;}
 std::filesystem::path file() const;
 std::uintmax_t length() const;
 const std::string& error() const{return error_;}
private:
 std::filesystem::path world_dir_,directory_;
 std::future<void> worker_;
 State state_=State::idle;std::uintmax_t length_=0;std::string error_;
};
}
