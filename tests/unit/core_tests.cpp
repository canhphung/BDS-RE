#include "bds/core/runtime.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
void check(bool ok) { if(!ok) throw std::runtime_error("check failed"); }
template<class F> void rejects(F f) { bool caught=false; try { f(); } catch(const std::invalid_argument&) {caught=true;} check(caught); }
int main() { try {
 bds::TickScheduler s(50ms,5);
 check(s.advance(49ms)==0); check(s.advance(1ms)==1);
 check(s.advance(125ms)==2); check(s.remaining()==25ms);
 check(s.advance(1000ms)==5); check(s.remaining()==25ms);
 check(s.advance(25ms)==1); check(s.advance(0ns)==0);
 rejects([&]{s.advance(-1ns);}); rejects([]{bds::TickScheduler bad(0ns,1);});
 bds::TickScheduler a(50ms,5),b(50ms,5);
 unsigned total=0; for(int i=0;i<10;++i) total+=a.advance(10ms);
 check(total==b.advance(100ms)); check(a.remaining()==b.remaining());
 check(a.advance(bds::TickScheduler::Duration::max())==5);
 std::array<std::string_view,4> args={"--ticks","7","--tick-ms","2"};
 auto c=bds::parse_config(args); check(c.ticks==7 && c.tick_ms==2);
 std::array<std::string_view,4> persisted={"--world","world with spaces.snapshot","--ticks","3"};
 auto persistent=bds::parse_config(persisted);
 check(persistent.world_file=="world with spaces.snapshot" && persistent.ticks==3);
 std::array<std::string_view,2> empty_path={"--world",""};
 rejects([&]{bds::parse_config(empty_path);});
 for(auto bad:{"-1","0","1001","1x","18446744073709551616"}) {
  std::array<std::string_view,2> input={"--tick-ms",bad}; rejects([&]{bds::parse_config(input);});
 }
 std::array<std::string_view,1> missing={"--ticks"}; rejects([&]{bds::parse_config(missing);});
 bds::Config runtime_config{3,1,5};runtime_config.network=false;
 bds::ServerRuntime r(runtime_config); check(r.run([]{return false;})==3);
 check(r.run([]{return true;})==0);
 std::cout<<"scheduler, config and lifecycle checks passed\n"; return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
