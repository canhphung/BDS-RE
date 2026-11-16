#pragma once
#include <cstdint>
#include <map>
#include <tuple>
namespace bds {
// Independent sparse test world; IDs are internal, not Bedrock wire IDs.
// Owned exclusively by the simulation thread. Missing blocks are air (0).
class World {
public:
 using Position=std::tuple<int,int,int>;
 using Block=std::uint16_t;
 Block block(Position p) const {auto i=blocks_.find(p); return i==blocks_.end()?0:i->second;}
 void set_block(Position p,Block b) {if(b==0) blocks_.erase(p); else blocks_.insert_or_assign(p,b);}
 void tick() {++age_;}
 std::uint64_t age() const {return age_;}
 std::size_t stored_blocks() const {return blocks_.size();}
private: std::map<Position,Block> blocks_; std::uint64_t age_=0;
};
}
