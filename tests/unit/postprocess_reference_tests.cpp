// OverworldGenerator::postProcess against the original: for each recorded
// call, rebuild the 3 x 3 area (regenerated chunks, the original's heightmaps
// and states, the earlier writes replayed), run post_process and compare its
// LevelChunk::setBlock / setExtraBlock calls with the original's.
//
// usage: postprocess_reference_tests <reference.json> [--report]
// Without --report every call must match up to the stages translated so far
// (the writes before Biome::decorate, "decorate_at").
#include "bds/world/decoration.hpp"
#include "nlohmann/json.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

using namespace bds;

namespace {
TerrainBlock block_of(const std::string &text) { return parse_terrain_block(text.c_str()); }

std::string name(const TerrainBlock &b) { return std::string(b.name) + ":" + std::to_string(b.data); }

// Replays the earlier writes: layer 0 through set_block (which makes the
// extra-layer calls LevelChunk::setBlock makes), layer 1 directly unless the
// preceding set_block produced it.
void replay(GenRegion &region, const nlohmann::json &prior) {
    std::size_t pending = 0; // extra-layer writes produced by the last set_block, not yet matched
    std::vector<GenWrite> produced;
    for (const auto &w : prior) {
        const int x = w[0], y = w[1], z = w[2], layer = w[4];
        const auto b = block_of(w[3].get<std::string>());
        if (layer == 0) {
            region.writes.clear();
            region.set_block(x, y, z, b, 3);
            produced.assign(region.writes.begin() + 1, region.writes.end());
            pending = 0;
        } else {
            const GenWrite expected{x, y, z, b, 1};
            if (pending < produced.size() && produced[pending] == expected)
                ++pending;
            else
                region.set_extra_block(x, y, z, b);
        }
    }
    region.writes.clear();
}
} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "expected post-process reference path\n";
        return 1;
    }
    const bool report_only = argc > 2 && std::strcmp(argv[2], "--report") == 0;
    std::ifstream input(argv[1]);
    const auto reference = nlohmann::json::parse(input);
    const auto seed = reference.at("seed").get<std::uint32_t>();
    const OverworldGenerator generator(seed);
    std::map<std::pair<int, int>, GenChunk> cache;
    int failures = 0, heightmap_failures = 0;
    std::size_t matched_total = 0, total = 0;
    for (const auto &call : reference.at("calls")) {
        const int cx = call.at("x"), cz = call.at("z");
        GenRegion region;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                const auto key = std::make_pair(cx + dx, cz + dz);
                const std::string area_key = std::to_string(key.first) + "," + std::to_string(key.second);
                if (!call.at("area").contains(area_key))
                    continue;
                auto it = cache.find(key);
                if (it == cache.end())
                    it = cache.emplace(key, GenRegion::loaded_chunk(generator, key.first, key.second)).first;
                GenChunk chunk = it->second;
                const auto &area = call.at("area").at(area_key);
                chunk.state = area.at("state");
                for (int i = 0; i < 256; ++i) {
                    const int h = area.at("heightmap")[static_cast<std::size_t>(i)];
                    if (h != chunk.heightmap[static_cast<std::size_t>(i)] && chunk.state != 8 &&
                        ++heightmap_failures < 5)
                        std::cerr << "heightmap " << area_key << " column " << i << ": "
                                  << chunk.heightmap[static_cast<std::size_t>(i)] << " != " << h << '\n';
                    chunk.heightmap[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(h);
                }
                region.add(std::move(chunk));
            }
        replay(region, call.at("prior"));
        const auto report = post_process(region, seed, cx, cz);
        const auto &expected = call.at("writes");
        std::size_t matched = 0;
        while (matched < expected.size() && matched < region.writes.size()) {
            const auto &e = expected[matched];
            const auto &w = region.writes[matched];
            if (w.x != e[0] || w.y != e[1] || w.z != e[2] || name(w.block) != e[3].get<std::string>() ||
                w.layer != e[4])
                break;
            ++matched;
        }
        matched_total += matched;
        total += expected.size();
        // Every write before Biome::decorate is translated (lakes, the second
        // reseed, dungeons); decoration comes next.
        const std::size_t want = call.at("decorate_at").get<std::size_t>();
        const bool ok = matched >= want && (want < expected.size() || region.writes.size() == expected.size());
        if (report_only || !ok) {
            std::cout << "chunk " << cx << "," << cz << " stage " << report.stage << ": " << matched << " / "
                      << expected.size() << " writes match (ours " << region.writes.size() << ")";
            if (matched < expected.size()) {
                const auto &e = expected[matched];
                std::cout << "; original next " << e.dump();
                if (matched < region.writes.size()) {
                    const auto &w = region.writes[matched];
                    std::cout << ", ours [" << w.x << "," << w.y << "," << w.z << ",\"" << name(w.block) << "\","
                              << w.layer << "]";
                }
            }
            std::cout << '\n';
        }
        if (!ok)
            ++failures;
    }
    std::cout << matched_total << " / " << total << " post-process writes match in order\n";
    if (heightmap_failures)
        std::cerr << heightmap_failures << " heightmap columns differ from recomputeHeightMap\n";
    return failures || heightmap_failures ? 1 : 0;
}
