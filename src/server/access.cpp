#include "bds/server/access.hpp"
#include "bds/core/atomic_file.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
namespace bds {
namespace {
using Json=nlohmann::json;
Json load(const std::filesystem::path& file) {
 std::ifstream stream(file,std::ios::binary|std::ios::ate);
 if(!stream) {
  if(std::filesystem::exists(file))throw std::runtime_error("cannot read access list");
  return Json::array();
 }
 auto size=stream.tellg();
 if(size<0 || size>1024*1024)throw std::runtime_error("access list exceeds file limit");
 stream.seekg(0);std::string text(static_cast<std::size_t>(size),'\0');stream.read(text.data(),size);
 if(!stream || stream.peek()!=std::char_traits<char>::eof())throw std::runtime_error("access list read failed");
 auto result=Json::parse(text);
 if(!result.is_array() || result.size()>10000)throw std::runtime_error("access list must be a bounded JSON array");
 return result;
}
std::string string(const Json& entry,const char* key,bool optional=false) {
 if(optional && !entry.contains(key))return {};
 if(!entry.contains(key) || !entry[key].is_string())throw std::runtime_error(std::string("invalid access field: ")+key);
 auto value=entry[key].get<std::string>();
 if((value.empty() && !optional) || value.size()>1024 || value.find('\0')!=value.npos)
  throw std::runtime_error("invalid access string");
 return value;
}
bool decimal(const std::string& s){return !s.empty() && std::all_of(s.begin(),s.end(),[](char c){return c>='0' && c<='9';});}
}
void AccessLists::reload_whitelist() {
 std::vector<WhitelistEntry> entries;std::set<std::string> names,xuids;
 for(const auto& entry:load(root_/"whitelist.json")) {
  if(!entry.is_object())throw std::runtime_error("invalid whitelist entry");
  auto name=string(entry,"name"),xuid=string(entry,"xuid",true);
  if(!names.insert(name).second || (!xuid.empty() && (!decimal(xuid) || !xuids.insert(xuid).second)))throw std::runtime_error("duplicate or invalid whitelist identity");
  if(entry.contains("ignoresPlayerLimit") && !entry["ignoresPlayerLimit"].is_boolean())throw std::runtime_error("invalid ignoresPlayerLimit");
  entries.push_back({name,xuid,entry.value("ignoresPlayerLimit",false)});
 }
 whitelist_=std::move(entries);
}
void AccessLists::reload_operators() {
 std::vector<std::string> entries;std::set<std::string> seen;
 for(const auto& entry:load(root_/"opsfile.json")) {
  if(!entry.is_object())throw std::runtime_error("invalid operator entry");
  auto xuid=string(entry,"xuid");
  if(!decimal(xuid) || !seen.insert(xuid).second)throw std::runtime_error("duplicate or invalid operator XUID");
  entries.push_back(xuid);
 }
 operators_=std::move(entries);
}
void AccessLists::save(const std::vector<WhitelistEntry>& entries) const {
 Json output=Json::array();
 for(const auto& entry:entries) {
  Json value={{"name",entry.name},{"ignoresPlayerLimit",entry.ignores_limit}};
  if(!entry.xuid.empty())value["xuid"]=entry.xuid;
  output.push_back(value);
 }
 const auto text=output.dump(4)+"\n";
 if(text.size()>1024*1024)throw std::runtime_error("whitelist exceeds file limit");
 write_file_atomically(root_/"whitelist.json",std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
}
bool AccessLists::add(const std::string& name) {
 if(name.empty() || name.size()>1024 || name.find('\0')!=name.npos)throw std::invalid_argument("invalid gamertag");
 if(std::any_of(whitelist_.begin(),whitelist_.end(),[&](const auto& e){return e.name==name;}))return false;
 if(whitelist_.size()>=10000)throw std::runtime_error("whitelist entry limit exceeded");
 auto next=whitelist_;next.push_back({name,{},false});save(next);whitelist_=std::move(next);return true;
}
bool AccessLists::remove(const std::string& name) {
 auto next=whitelist_;std::erase_if(next,[&](const auto& e){return e.name==name;});
 if(next.size()==whitelist_.size())return false;
 save(next);whitelist_=std::move(next);return true;
}
bool AccessLists::is_operator(const std::string& xuid) const {
 return !xuid.empty() && std::find(operators_.begin(),operators_.end(),xuid)!=operators_.end();
}
const WhitelistEntry* AccessLists::find(const std::string& name,const std::string& xuid) const {
 for(const auto& entry:whitelist_) {
  if(!entry.xuid.empty()) {if(!xuid.empty() && xuid==entry.xuid)return &entry;}
  else if(entry.name==name)return &entry;
 }
 return nullptr;
}
}
