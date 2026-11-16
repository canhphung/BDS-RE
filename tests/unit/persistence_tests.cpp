#include "bds/world/store.hpp"
#include <array>
#include <climits>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("persistence invariant failed"); }
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected);
}
// Refresh corrupted semantic fixtures so checksum failure does not hide validation bugs.
void checksum(std::vector<std::uint8_t>& bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i+4 < bytes.size(); ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    crc ^= 0xffffffffU;
    for (unsigned i = 0; i < 4; ++i) bytes[bytes.size()-4+i] = static_cast<std::uint8_t>(crc >> (i*8));
}
struct TestDirectory {
    std::filesystem::path path;
    TestDirectory() {
        std::random_device random;
        for (unsigned i=0; i<32; ++i) {
            auto candidate = std::filesystem::temp_directory_path() / ("bds-persistence-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error("cannot create test directory");
    }
    ~TestDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
}

int main() {
    try {
        const auto decode=[](const auto& bytes) { return bds::WorldStore::decode(bytes, {}); };
        bds::World world;
        world.set_block({-1,255,16},42); world.tick(); world.tick();
        auto bytes = bds::WorldStore::encode(world);
        // Generated independently using Python struct.pack and zlib.crc32.
        constexpr std::string_view fixture="4244535245570d0a0100000002000000000000000100000000000000ffffffffff00100000002a00ed84c4a9";
        std::string hex;
        for (auto byte : bytes) { hex += "0123456789abcdef"[byte>>4]; hex += "0123456789abcdef"[byte&15]; }
        check(hex == fixture);
        auto loaded = decode(bytes);
        check(loaded.age()==2 && loaded.block({-1,255,16})==42 && loaded.stored_blocks()==1);
        check(bds::WorldStore::encode(loaded)==bytes);
        check(decode(bds::WorldStore::encode(bds::World{})).loaded_chunks()==0);
        // Every truncated prefix and every single-byte corruption must fail.
        for (std::size_t size=0; size<bytes.size(); ++size)
            rejects([&] { decode(std::span(bytes).first(size)); });
        for (std::size_t i=0; i<bytes.size(); ++i) {
            auto broken=bytes; broken[i]^=1; rejects([&] { decode(broken); });
        }
        auto broken=bytes; broken.push_back(0); rejects([&] { decode(broken); });
        broken=bytes; broken[8]=2; checksum(broken); rejects([&] { decode(broken); });
        broken=bytes; broken[32]=0; broken[33]=1; checksum(broken); rejects([&] { decode(broken); }); // y=256
        broken=bytes; broken[38]=0; broken[39]=0; checksum(broken); rejects([&] { decode(broken); }); // air record
        broken=bytes; broken[20]=255; checksum(broken); rejects([&] { decode(broken); }); // size/count mismatch
        broken=bytes;
        broken.insert(broken.end()-4, bytes.begin()+28, bytes.begin()+40);
        broken[20]=2; checksum(broken); rejects([&] { decode(broken); }); // duplicate position
        rejects([&] { bds::WorldStore::decode(bytes, {0,4096}); });
        rejects([&] { bds::WorldStore::decode(bytes, {100,0}); });
        broken=bytes;
        for (unsigned i=0; i<8; ++i) broken[12+i]=255;
        checksum(broken); auto exhausted=decode(broken); rejects([&] { exhausted.tick(); });
        check(exhausted.age()==UINT64_MAX);

        bds::World large;
        for (int x : {INT_MIN,-17,-16,-1,0,15,16,INT_MAX})
            for (int z : {INT_MIN,-16,0,16,INT_MAX})
                for (int y : {0,15,16,255}) large.set_block({x,y,z},65535);
        auto restored=decode(bds::WorldStore::encode(large));
        check(restored.stored_blocks()==large.stored_blocks());
        large.for_each_block([&](auto p,auto block) { check(restored.block(p)==block); });
        // Insertion order does not affect snapshot bytes.
        bds::World reversed;
        std::vector<std::pair<bds::World::Position,bds::BlockId>> records;
        large.for_each_block([&](auto p,auto block) { records.emplace_back(p,block); });
        for (auto i=records.rbegin(); i!=records.rend(); ++i) reversed.set_block(i->first,i->second);
        check(bds::WorldStore::encode(reversed)==bds::WorldStore::encode(large));

        TestDirectory directory;
        auto path=directory.path/"world.snapshot";
        bds::WorldStore::save(world,path);
        check(bds::WorldStore::load(path).age()==2);
        world.set_block({0,0,0},7); world.tick();
        bds::WorldStore::save(world,path); // Replace existing snapshot.
        check(bds::WorldStore::load(path).block({0,0,0})==7);
        check(bds::WorldStore::load(path).age()==3);
        // Failed read cannot replace the live world.
        auto bad_path=directory.path/"corrupt.snapshot";
        { std::ofstream file(bad_path); file << "broken"; }
        rejects([&] { world=bds::WorldStore::load(bad_path); });
        check(world.age()==3 && world.block({0,0,0})==7);
        // Failed final rename preserves the destination and cleans staging files.
        auto destination=directory.path/"existing-directory";
        std::filesystem::create_directory(destination);
        { std::ofstream keep(destination/"keep"); keep << "preserved"; }
        rejects([&] { bds::WorldStore::save(world,destination); });
        check(std::filesystem::exists(destination/"keep"));
        for (const auto& entry : std::filesystem::directory_iterator(directory.path))
            check(!entry.path().filename().string().starts_with(".bds-world-"));
        std::cout << "snapshot fixtures, corruption, limits, round trips and file replacement passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
