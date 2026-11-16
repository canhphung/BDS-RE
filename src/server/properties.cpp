#include "bds/server/properties.hpp"
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace bds {
namespace {
std::string trim(std::string_view s) {
 const auto first=s.find_first_not_of(" \t\r\n");
 if(first==s.npos) return {};
 return std::string(s.substr(first,s.find_last_not_of(" \t\r\n")-first+1));
}
unsigned integer(std::string_view s,unsigned low,unsigned high) {
 unsigned result=0;const auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),result);
 if(e!=std::errc{} || p!=s.data()+s.size() || result<low || result>high)
  throw std::invalid_argument("integer outside allowed range");
 return result;
}
unsigned mode(std::string_view v) {
 if(v=="survival" || v=="0")return 0;
 if(v=="creative" || v=="1")return 1;
 if(v=="adventure" || v=="2")return 2;
 throw std::invalid_argument("invalid gamemode");
}
}
bool parse_boolean(std::string_view v) {
 if(v=="true" || v=="on")return true;
 if(v=="false" || v=="off")return false;
 throw std::invalid_argument("invalid boolean");
}
unsigned parse_difficulty(std::string_view v) {
 if(v=="peaceful" || v=="0")return 0;
 if(v=="easy" || v=="1")return 1;
 if(v=="normal" || v=="2")return 2;
 if(v=="hard" || v=="3")return 3;
 throw std::invalid_argument("invalid difficulty");
}
Properties Properties::parse(std::istream& input) {
 Properties p; std::string line;unsigned number=0;std::size_t bytes=0;
 while(std::getline(input,line)) {
  ++number;bytes+=line.size();
  if(line.size()>4095 || bytes>1024*1024) throw std::invalid_argument("properties file limit exceeded");
  // Original skips lines whose first character is # or space.
  if(line.empty() || line.front()=='#' || line.front()==' ')continue;
  const auto split=line.find('=');
  if(split==line.npos){p.diagnostics.push_back("ignored line "+std::to_string(number)+": missing =");continue;}
  auto k=trim(std::string_view(line).substr(0,split)), v=trim(std::string_view(line).substr(split+1));
  try {
   constexpr auto max=std::numeric_limits<unsigned>::max();
   if(k=="server-name")p.server_name=v;
   else if(k=="level-name")p.level_name=v;
   else if(k=="level-seed")p.seed=v;
   else if(k=="level-type") {
    if(v!="DEFAULT" && v!="FLAT" && v!="LEGACY")throw std::invalid_argument("invalid level-type");
    p.level_type=v;
   } else if(k=="gamemode")p.gamemode=mode(v);
   else if(k=="difficulty")p.difficulty=parse_difficulty(v);
   else if(k=="max-players")p.max_players=integer(v,1,max);
   else if(k=="server-port")p.port=integer(v,1,65535);
   else if(k=="server-portv6")p.port_v6=integer(v,1,65535);
   else if(k=="view-distance")p.view_distance=integer(v,1,max);
   else if(k=="tick-distance")p.tick_distance=integer(v,4,12);
   else if(k=="player-idle-timeout")p.idle_minutes=integer(v,1,max);
   else if(k=="max-threads")p.max_threads=integer(v,0,max);
   else if(k=="online-mode")p.online_mode=parse_boolean(v);
   else if(k=="white-list")p.whitelist=parse_boolean(v);
   else if(k=="allow-cheats")p.cheats=parse_boolean(v);
   else if(k=="texturepack-required")p.texturepack_required=parse_boolean(v);
   else if(k=="default-player-permission-level") {
    if(v!="visitor" && v!="member" && v!="operator")throw std::invalid_argument("invalid permission");
    p.permission=v;
   } else p.diagnostics.push_back("unknown property: "+k);
  } catch(const std::invalid_argument& e) {
   throw std::invalid_argument("server.properties line "+std::to_string(number)+" ("+k+"): "+e.what());
  }
 }
 if(input.bad())throw std::runtime_error("properties read failed");
 return p;
}
Properties Properties::load(const std::filesystem::path& file) {
 std::ifstream input(file);
 if(!input.is_open()) {
  if(std::filesystem::exists(file))throw std::runtime_error("cannot read server.properties");
  return {};
 }
 return parse(input);
}
std::filesystem::path Properties::world_directory(const std::filesystem::path& root) const {
 // Original accepts arbitrary strings; confinement is an intentional path policy.
 if(level_name.empty() || level_name=="." || level_name==".." || level_name.find_first_of("/\\:")!=level_name.npos || level_name.find('\0')!=level_name.npos)
  throw std::invalid_argument("level-name must be a single directory name");
 const auto server_root=std::filesystem::canonical(root);
 const auto base=std::filesystem::weakly_canonical(server_root/"worlds");
 if(!std::filesystem::equivalent(base.parent_path(),server_root))throw std::invalid_argument("worlds directory escapes server root");
 const auto selected=std::filesystem::weakly_canonical(base/level_name);
 if(selected.parent_path()!=base)throw std::invalid_argument("world path escapes worlds directory");
 return selected;
}
}
