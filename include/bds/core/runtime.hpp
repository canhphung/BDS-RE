#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include "bds/core/config.hpp"
namespace bds {
// Single simulation owner. Network threads must not mutate this state.
class TickScheduler {
public:
 using Duration=std::chrono::nanoseconds;
 TickScheduler(Duration step, unsigned max_catch_up);
 unsigned advance(Duration elapsed);
 Duration remaining() const { return step_-pending_; }
private:
 Duration step_, pending_{}; unsigned limit_;
};
class ServerRuntime {
public:
 explicit ServerRuntime(Config config): config_(config) {}
 std::uint64_t run(const std::function<bool()>& stop);
private: Config config_;
};
}
