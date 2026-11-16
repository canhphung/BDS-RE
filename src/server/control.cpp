#include "bds/server/control.hpp"
#include "bds/core/console.hpp"
#include <sstream>
#include <iomanip>
#include <stdexcept>
namespace bds {
std::string ServerControl::execute(const std::string& line,const World& world) {
 try {
  const auto words=command_words(line);
  if(words.empty())return {};
  if(words[0]=="stop" && words.size()==1){stopping_=true;return "stopping server";}
  if(words[0]=="changesetting" && words.size()==3) {
   if(words[1]=="allow-cheats")properties_.cheats=parse_boolean(words[2]);
   else if(words[1]=="difficulty")properties_.difficulty=parse_difficulty(words[2]);
   else throw std::invalid_argument("setting cannot be changed at runtime");
   return "setting changed: "+words[1]+" (runtime only)";
  }
  if(words[0]=="whitelist") {
   if(words.size()==3 && (words[1]=="add" || words[1]=="remove")) {
    const bool changed=words[1]=="add"?access_.add(words[2]):access_.remove(words[2]);
    return changed?"whitelist updated":"whitelist unchanged";
   }
   if(words.size()!=2)throw std::invalid_argument("whitelist requires on/off/list/reload or add/remove <gamertag>");
   if(words[1]=="on" || words[1]=="off") {properties_.whitelist=words[1]=="on";return "whitelist "+words[1]+" (runtime only)";}
   if(words[1]=="reload"){access_.reload_whitelist();return "whitelist reloaded";}
   if(words[1]=="list") {
    std::ostringstream output;output<<"whitelist entries="<<access_.whitelist().size();
    for(const auto& entry:access_.whitelist())output<<"\n"<<std::quoted(entry.name)<<" xuid="<<std::quoted(entry.xuid)<<" ignoresPlayerLimit="<<entry.ignores_limit;
    return output.str();
   }
  }
  if(words[0]=="ops" && words.size()==2) {
   if(words[1]=="reload"){access_.reload_operators();return "operators reloaded";}
   if(words[1]=="list") {std::string output="operators entries="+std::to_string(access_.operators().size());for(const auto& id:access_.operators())output+="\n"+id;return output;}
  }
  if(words[0]=="save" && words.size()==2) {
   if(words[1]=="hold"){backup_.hold(world);return "backup preparing";}
   if(words[1]=="resume"){backup_.resume();return "backup resumed";}
   if(words[1]=="query") {
    backup_.poll();
    if(backup_.state()==Backup::State::preparing)return "backup preparing";
    if(backup_.state()==Backup::State::failed)return "error: backup failed: "+backup_.error();
    if(backup_.state()==Backup::State::idle)return "error: no held backup";
    std::ostringstream output;output<<"backup ready: "<<std::quoted(backup_.file().string())<<" length="<<backup_.length();return output.str();
   }
  }
  if(words[0]=="kick")return "error: player sessions are not implemented";
  throw std::invalid_argument("unknown command or invalid arguments");
 } catch(const std::exception& e){return std::string("error: ")+e.what();}
}
}
