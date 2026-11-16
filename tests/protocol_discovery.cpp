#include "bds/network/discovery.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
int main() {
 try {
  auto check=[](bool ok){if(!ok)throw std::runtime_error("discovery invariant failed");};
  std::vector<std::uint8_t> ping={1,1,2,3,4,5,6,7,8,0,255,255,0,254,254,254,254,253,253,253,253,18,52,86,120};
  const auto pong=bds::discovery_pong(ping,0x0102030405060708ULL,"abc");
  check(pong.size()==38 && pong[0]==28 && pong[1]==1 && pong[8]==8 && pong[9]==1 && pong[16]==8 && pong[33]==0 && pong[34]==3 && pong[35]=='a');
  for(std::size_t n=0;n<ping.size();++n)check(bds::discovery_pong(std::span(ping).first(n),1,"abc").empty());
  for(std::size_t i=9;i<25;++i){auto bad=ping;bad[i]^=1;check(bds::discovery_pong(bad,1,"abc").empty());}
  ping[0]=2;check(!bds::discovery_pong(ping,1,"abc").empty());
  ping[0]=5;check(bds::discovery_pong(ping,1,"abc").empty());
  ping[0]=1;check(bds::discovery_pong(ping,1,std::string(1025,'x')).empty());
  bds::Properties p;p.server_name="Test;Injected\nName";
  check(bds::server_advertisement(p,1).starts_with("MCPE;Test Injected Name;282;1.6.0;0;10;1;"));
  // Same field layout and trailing flag as the original BDS advertisement captured on Linux:
  // MCPE;Dedicated Server;282;1.6.0;0;10;<guid>;Reference;Survival;1;
  bds::Properties d;d.level_name="Reference";
  check(bds::server_advertisement(d,9968884998189047337ULL)=="MCPE;Dedicated Server;282;1.6.0;0;10;9968884998189047337;Reference;Survival;1;");
  return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
