#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace bds {
struct WhitelistEntry {std::string name,xuid;bool ignores_limit=false;};
class AccessLists {
public:
 explicit AccessLists(std::filesystem::path root):root_(std::move(root)){}
 void reload_whitelist();void reload_operators();
 bool add(const std::string& name);bool remove(const std::string& name);
 const std::vector<WhitelistEntry>& whitelist() const{return whitelist_;}
 const std::vector<std::string>& operators() const{return operators_;}
 bool is_operator(const std::string& xuid) const;
 const WhitelistEntry* find(const std::string& name,const std::string& xuid) const;
private:
 void save(const std::vector<WhitelistEntry>& entries) const;
 std::filesystem::path root_;
 std::vector<WhitelistEntry> whitelist_;
 std::vector<std::string> operators_;
};
}
