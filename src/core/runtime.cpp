#include "bds/core/runtime.hpp"
#include <stdexcept>
#include "bds/world/world.hpp"
#include "bds/world/store.hpp"
#include "bds/core/logger.hpp"
#include <thread>
namespace bds {
TickScheduler::TickScheduler(Duration step,unsigned limit):step_(step),limit_(limit) {
 if(step<=Duration::zero() || limit==0) throw std::invalid_argument("invalid scheduler");
}
unsigned TickScheduler::advance(Duration elapsed) {
 if(elapsed<Duration::zero()) throw std::invalid_argument("negative elapsed time");
 // Decompose before addition, so even Duration::max() cannot overflow.
 auto whole=elapsed/step_;
 auto fraction=elapsed%step_;
 bool carry=fraction>=step_-pending_;
 pending_=carry ? fraction-(step_-pending_) : pending_+fraction;
 if(whole>=limit_) return limit_; // discard excess whole ticks, keep phase
 auto due=static_cast<unsigned>(whole)+(carry?1U:0U);
 return due>limit_?limit_:due;
}
std::uint64_t ServerRuntime::run(const std::function<bool()>& stop) {
 using Clock=std::chrono::steady_clock;
 TickScheduler scheduler(std::chrono::milliseconds(config_.tick_ms), config_.max_catch_up);
 World world;
 if(!config_.world_file.empty() && std::filesystem::exists(config_.world_file))
  world=WorldStore::load(config_.world_file);
 auto last=Clock::now();
 std::uint64_t ticks=0;
 while(!stop() && (config_.ticks==0 || ticks<config_.ticks)) {
  auto now=Clock::now(); auto due=scheduler.advance(now-last); last=now;
  for(unsigned i=0;i<due && !stop() && (config_.ticks==0 || ticks<config_.ticks);++i) {
   world.tick();
   ++ticks;
  }
  if(config_.ticks!=0 && ticks==config_.ticks) break;
  std::this_thread::sleep_until(last+scheduler.remaining());
 }
 if(!config_.world_file.empty()) {
  WorldStore::save(world,config_.world_file);
  log("saved world age="+std::to_string(world.age()));
 }
 return ticks;
}
}
