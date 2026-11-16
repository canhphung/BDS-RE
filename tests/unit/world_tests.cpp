#include "bds/world/world.hpp"
#include <iostream>
int main() {
 bds::World world;
 auto check=[](bool ok){if(!ok) throw "world invariant failed";};
 try {
 check(world.age()==0); check(world.block({0,0,0})==0);
 world.set_block({-16,255,17},42); world.set_block({0,0,0},7);
 check(world.block({-16,255,17})==42); check(world.stored_blocks()==2);
 world.set_block({-16,255,17},9); check(world.stored_blocks()==2);
 check(world.block({-16,255,17})==9);
 world.set_block({-16,255,17},0); check(world.block({-16,255,17})==0);
 check(world.block({0,0,0})==7 && world.stored_blocks()==1);
 for(int i=0;i<100;++i) world.tick(); check(world.age()==100);
 check(world.block({0,0,0})==7); return 0;
 } catch(...) {std::cerr<<"world invariant failed\n";return 1;}
}
