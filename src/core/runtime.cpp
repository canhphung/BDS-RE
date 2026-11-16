#include "bds/core/runtime.hpp"
#include <stdexcept>
#include "bds/world/world.hpp"
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
 auto last=Clock::now(); World world;
 while(!stop() && (config_.ticks==0 || world.age()<config_.ticks)) {
  auto now=Clock::now(); auto due=scheduler.advance(now-last); last=now;
  for(unsigned i=0;i<due && !stop() && (config_.ticks==0 || world.age()<config_.ticks);++i) world.tick();
  if(config_.ticks!=0 && world.age()==config_.ticks) break;
  std::this_thread::sleep_until(last+scheduler.remaining());
 }
 return world.age();
}
}
