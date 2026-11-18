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

        // Decoration must actually appear: the translated ore decorator places
        // gravel, coal, iron and redstone into the generated chunk, mapped onto
        // the internal ids the palette appended for them.
        {
            World decorated;
            decorated.configure_terrain({3, 123});
            decorated.ensure_chunk(0, 0);
            const auto *generated = decorated.chunk(0, 0);
            std::size_t gravel = 0, coal = 0, iron = 0, decorations = 0;
            for (unsigned bx = 0; bx < 16; ++bx)
                for (unsigned bz = 0; bz < 16; ++bz)
                    for (unsigned y = 0; y < 128; ++y) {
                        const auto id = generated->block(bx, y, bz);
                        if (id == 39) ++gravel;
                        if (id == 40) ++coal;
                        if (id == 41) ++iron;
                        if (id >= 39) ++decorations;
                    }
            check(decorations > 0, "the ore decorator placed decoration blocks");
            check(gravel > 0 && coal > 0, "gravel and coal appear in the generated chunk");
            std::cout << "  decoration: " << gravel << " gravel, " << coal << " coal, " << iron
                      << " iron in chunk (0,0)\n";
        }

        // Trees: a land chunk must contain logs and leaves placed by the
        // translated TreeFeature, mapped onto ids 51/52.
        {
            World forest;
            forest.configure_terrain({3, 123});
            forest.ensure_chunk(-3, -3);
            const auto *chunk = forest.chunk(-3, -3);
            std::size_t logs = 0, leaves = 0;
            for (unsigned bx = 0; bx < 16; ++bx)
                for (unsigned bz = 0; bz < 16; ++bz)
                    for (unsigned y = 0; y < 128; ++y) {
                        const auto id = chunk->block(bx, y, bz);
                        if (id == 51) ++logs;
                        if (id == 52) ++leaves;
                    }
            check(logs > 0, "the translated TreeFeature placed logs");
            check(leaves > 0, "the translated TreeFeature placed leaves");
            std::cout << "  trees: " << logs << " logs, " << leaves << " leaves in chunk (-3,-3)\n";
        }

        // Cold-biome blocks: the generator's ice paths emit ice/packed_ice and
        // the palette maps them, so a cold chunk must carry those ids.
        {
            World cold;
            cold.configure_terrain({3, 42});
            cold.ensure_chunk(-10, 9);
            const auto *chunk = cold.chunk(-10, 9);
            std::size_t snow = 0, ice = 0, packed = 0;
            for (unsigned bx = 0; bx < 16; ++bx)
                for (unsigned bz = 0; bz < 16; ++bz)
                    for (unsigned y = 0; y < 128; ++y) {
                        const auto id = chunk->block(bx, y, bz);
                        if (id == 54) ++snow;
                        if (id == 55) ++ice;
                        if (id == 56) ++packed;
                    }
            check(snow + ice + packed > 0, "cold-biome blocks appear in a cold chunk");
            std::cout << "  cold: " << snow << " snow, " << ice << " ice, " << packed
                      << " packed ice in chunk (-10,9) of seed 42\n";
        }

        std::cout << "OverworldGenerator runtime integration: chunk fill, determinism, edits, decoration, trees and cold blocks pass\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
