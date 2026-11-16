#include "bds/core/config.hpp"
#include <charconv>
#include <stdexcept>
#include <string>
namespace bds {
Config parse_config(std::span<const std::string_view> args) {
 Config c;
 for (std::size_t i=0; i<args.size(); i+=2) {
  if(i+1==args.size()) throw std::invalid_argument("option needs a value");
  if(args[i]=="--world" || args[i]=="--root") {
   if(args[i+1].empty()) throw std::invalid_argument("world path must not be empty");
   if(args[i]=="--world")c.world_file=std::string(args[i+1]);
   else c.root=std::string(args[i+1]);
   continue;
  }
  std::uint64_t v=0; auto s=args[i+1];
  auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),v);
  if(e!=std::errc{} || p!=s.data()+s.size()) throw std::invalid_argument("invalid unsigned integer");
  if(args[i]=="--network" && v<=1)c.network=v!=0;
  else if(args[i]=="--ticks") c.ticks=v;
  else if(args[i]=="--tick-ms" && v>=1 && v<=1000) c.tick_ms=static_cast<unsigned>(v);
  else if(args[i]=="--max-catch-up" && v>=1 && v<=100) c.max_catch_up=static_cast<unsigned>(v);
  else throw std::invalid_argument("unknown option or out-of-range value");
 }
 return c;
}
}
