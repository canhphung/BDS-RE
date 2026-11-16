#include "bds/world/world.hpp"
#include <climits>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>

void check(bool ok) { if (!ok) throw std::runtime_error("chunk invariant failed"); }

int main() {
    try {
        for (int coordinate : {INT_MIN, -33, -32, -17, -16, -15, -1, 0, 1, 15, 16, 17, INT_MAX}) {
            auto chunk = bds::chunk_coordinate(coordinate);
            auto local = bds::local_coordinate(coordinate);
            check(local < 16);
            check(static_cast<long long>(chunk) * 16 + local == coordinate);
        }
        bds::Chunk chunk;
        check(chunk.allocated_sections() == 0);
        for (unsigned y = 0; y < 256; ++y)
            for (unsigned z = 0; z < 16; ++z)
                for (unsigned x = 0; x < 16; ++x)
                    chunk.set_block(x, y, z, static_cast<bds::BlockId>(1 + x + 16*z + 256*(y%16)));
        check(chunk.stored_blocks() == 65536 && chunk.allocated_sections() == 16);
        for (unsigned y = 0; y < 256; ++y)
            for (unsigned z = 0; z < 16; ++z)
                for (unsigned x = 0; x < 16; ++x) {
                    check(chunk.block(x,y,z) == 1 + x + 16*z + 256*(y%16));
                    chunk.set_block(x,y,z,0);
                }
        check(chunk.stored_blocks() == 0 && chunk.allocated_sections() == 0);
        bool rejected = false;
        try { chunk.set_block(16,0,0,1); } catch (const std::out_of_range&) { rejected = true; }
        check(rejected);

        bds::World world;
        std::map<bds::World::Position, bds::BlockId> reference;
        std::mt19937 random(42);
        // Differential against an independent sparse map, across negative boundaries.
        for (int i = 0; i < 4000; ++i) {
            bds::World::Position p{static_cast<int>(random()%67)-33,
                                  static_cast<int>(random()%256),
                                  static_cast<int>(random()%67)-33};
            auto value = static_cast<bds::BlockId>(random()%5);
            world.set_block(p,value);
            if (value == 0) reference.erase(p); else reference[p] = value;
            check(world.block(p) == value && world.stored_blocks() == reference.size());
        }
        for (auto [position,value] : reference) check(world.block(position) == value);
        for (auto [position,value] : reference) world.set_block(position,0);
        check(world.stored_blocks() == 0 && world.loaded_chunks() == 0);
        world.set_block({INT_MIN,255,INT_MAX}, 77);
        check(world.block({INT_MIN,255,INT_MAX}) == 77);
        check(world.block({INT_MIN,254,INT_MAX}) == 0);
        check(world.block({0,-1,0}) == 0 && world.block({0,256,0}) == 0);
        rejected = false;
        try { world.set_block({0,256,0},1); } catch (const std::out_of_range&) { rejected = true; }
        check(rejected && world.stored_blocks() == 1);
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
