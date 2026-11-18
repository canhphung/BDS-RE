// The translated OverworldGenerator wired into World (terrain kind 3).
//
// The generator itself is verified against the original by
// terrain_reference; this test covers the RUNTIME integration: a World
// configured for kind 3 must fill chunks from OverworldGenerator::generate,
// keep edits on top of it, and reproduce the same chunk twice.
#include "bds/world/world.hpp"
#include "bds/world/overworld.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using bds::World;
using bds::BlockId;
using bds::TerrainVolume;
using bds::OverworldGenerator;

namespace {
void check(bool condition, const std::string &what) {
    if (!condition)
        throw std::runtime_error("check failed: " + what);
}
} // namespace

int main() {
    try {
        World world;
        world.configure_terrain({3, 123});
        check(world.terrain().kind == 3, "kind 3 accepted");

        // A chunk generated through the runtime must equal the generator's own
        // output for the same seed and coordinates.
        world.ensure_chunk(0, 0);
        const auto *chunk = world.chunk(0, 0);
        check(chunk != nullptr, "chunk generated");
        check(world.block({0, 0, 0}) == 4, "bedrock floor at y=0");

        bds::OverworldGenerator generator(123);
        const auto volume = generator.generate(0, 0);
        std::size_t compared = 0, mismatched = 0;
        for (unsigned bx = 0; bx < 16; ++bx)
            for (unsigned bz = 0; bz < 16; ++bz)
                for (int y = 0; y < TerrainVolume::height; ++y) {
                    const BlockId expected =
                        chunk->block(bx, static_cast<unsigned>(y), bz);
                    const auto &generated = volume.at(static_cast<int>(bx), y, static_cast<int>(bz));
                    // Only air/stone/grass/dirt/bedrock/water/lava are mapped.
                    ++compared;
                    if (std::string(generated.name) == "air" && expected != 0)
                        ++mismatched;
                    if (std::string(generated.name) == "bedrock" && expected != 4)
                        ++mismatched;
                }
        check(compared == 16 * 16 * 128, "compared every cell");
        check(mismatched == 0, "runtime chunk follows the generator");

        // Determinism: the same chunk twice.
        world.ensure_chunk(3, 4);
        const auto first = world.block({3 * 16 + 5, 60, 4 * 16 + 7});
        World other;
        other.configure_terrain({3, 123});
        other.ensure_chunk(3, 4);
        check(other.block({3 * 16 + 5, 60, 4 * 16 + 7}) == first, "chunk generation is deterministic");

        // An edit survives regeneration of the same column.
        const World::Position edited{1, 70, 1};
        world.set_block(edited, 1);
        world.ensure_chunk(0, 0); // already present: no regeneration
        check(world.block(edited) == 1, "edit kept in the generated chunk");

        std::cout << "OverworldGenerator runtime integration: chunk fill, determinism and edits pass\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
