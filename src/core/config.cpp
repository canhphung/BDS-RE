#include "bds/core/config.hpp"
#include <charconv>
#include <stdexcept>
namespace bds {
Config parse_config(std::span<const std::string_view> args) {
 Config c;
 for (std::size_t i=0; i<args.size(); i+=2) {
  if(i+1==args.size()) throw std::invalid_argument("option needs a value");
  std::uint64_t v=0; auto s=args[i+1];
  auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),v);
  if(e!=std::errc{} || p!=s.data()+s.size()) throw std::invalid_argument("invalid unsigned integer");
  if(args[i]=="--ticks") c.ticks=v;
  else if(args[i]=="--tick-ms" && v>=1 && v<=1000) c.tick_ms=static_cast<unsigned>(v);
  else if(args[i]=="--max-catch-up" && v>=1 && v<=100) c.max_catch_up=static_cast<unsigned>(v);
  else throw std::invalid_argument("unknown option or out-of-range value");
 }
 return c;
}
}
