#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace bds {
// WhitelistEntry(name, mce::UUID, xuid, ignoresPlayerLimit) (ctor 0x25e0e70);
// `uuid` is optional in whitelist.json and kept as lower-case text.
struct WhitelistEntry {std::string name,xuid;bool ignores_limit=false;std::string uuid;};
class AccessLists {
public:
 explicit AccessLists(std::filesystem::path root):root_(std::move(root)){}
 void reload_whitelist();void reload_operators();
 bool add(const std::string& name);bool remove(const std::string& name);
 const std::vector<WhitelistEntry>& whitelist() const{return whitelist_;}
 const std::vector<std::string>& operators() const{return operators_;}
 bool is_operator(const std::string& xuid) const;
 const WhitelistEntry* find(const std::string& name,const std::string& xuid) const;
 // Whitelist::tryUpdateEntries (0x25f5ac0): fill a name-matched entry's
 // missing XUID, or follow a gamertag change for an XUID-matched entry.
 // Persists and returns true when an entry changed.
 bool try_update_entries(const std::string& uuid,const std::string& xuid,const std::string& name);
 // Whitelist::isAllowed (0x25f5410): entry with this UUID, else with this
 // XUID. Empty identifiers never match (stricter than the original).
 bool is_allowed(const std::string& uuid,const std::string& xuid) const;
private:
 void save(const std::vector<WhitelistEntry>& entries) const;
 std::filesystem::path root_;
 std::vector<WhitelistEntry> whitelist_;
 std::vector<std::string> operators_;
};
}
