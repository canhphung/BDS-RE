#pragma once
#include "bds/server/properties.hpp"
#include "bds/server/access.hpp"
#include "bds/server/backup.hpp"
#include <string>
namespace bds {
class ServerControl {
public:
 ServerControl(Properties& properties,AccessLists& access,Backup& backup):properties_(properties),access_(access),backup_(backup){}
 std::string execute(const std::string& line,const World& world);
 bool stopping() const{return stopping_;}
private:
 Properties& properties_;AccessLists& access_;Backup& backup_;bool stopping_=false;
};
}
