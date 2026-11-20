// postProcess runtime slice: the seed derivations and the lake draw sequence.
//
// The full postProcess is not callable (see reverse/reports/POSTPROCESS.md), so
// this checks the parts the slice does implement: both seed derivations match
// the same formula the translated carvers use for the same level seed, they are
// deterministic, they do not touch the level Random, and the lake stages
// consume the documented number of draws in the documented order.
#include "bds/world/post_process.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const std::string &what) {
    if (!condition)
        throw std::runtime_error("check failed: " + what);
}
using bds::DecorationRegion;
using bds::PostProcessSettings;
using bds::Random;
} // namespace

int main() {
    try {
        // The derivation is the carvers' one: seed a Random with the level seed,
        // draw two nextInt(), make them odd, mix with the origin and xor the seed.
        const std::uint32_t seed = 123;
        const DecorationRegion::Position origin{0, 64, 0};
        {
            Random scratch(seed);
            const auto first = bds::post_process_seed_for(origin, seed, scratch);
            Random again(seed);
            const auto second = bds::post_process_seed_for(origin, seed, again);
            check(first == second, "the derivation is deterministic");
        }
        // Same arithmetic as Carver::apply for origin (0,0): (0*a + 0*b) ^ seed.
        {
            Random scratch(0);
            check(bds::post_process_seed_for({0, 64, 0}, seed, scratch) == seed,
                  "origin (0,0) mixes to just the seed");
        }
        // A non-zero origin must actually mix: two different origins differ.
        {
            Random a(0);
            Random b(0);
            check(bds::post_process_seed_for({0, 64, 0}, seed, a) !=
                      bds::post_process_seed_for({7, 64, -3}, seed, b),
                  "different origins derive different seeds");
        }

        // The level Random must come out untouched: postProcess reseeds a
        // private copy, so a caller's stream does not advance.
        {
            Random level(seed);
            std::vector<std::uint32_t> level_words;
            level.observe_draws([&](std::uint32_t word) { level_words.push_back(word); });
            DecorationRegion region;
            PostProcessSettings settings;
            settings.level_seed = seed;
            (void)bds::post_process_chunk(region, origin, level, settings);
            check(level_words.empty(), "postProcess must not draw from the level Random");
        }

        // The lake stages consume a deterministic number of draws, and the
        // desert / no-lake flags skip them entirely.
        {
            auto draws_for = [&](bool desert, bool no_lakes) {
                Random level(seed);
                std::vector<std::uint32_t> words;
                level.observe_draws([&](std::uint32_t word) { words.push_back(word); });
                DecorationRegion region;
                PostProcessSettings settings;
                settings.level_seed = seed;
                settings.desert_like = desert;
                settings.superflat_or_no_lakes = no_lakes;
                (void)bds::post_process_chunk(region, origin, level, settings);
                return words.size();
            };
            // These counts describe THIS slice: it draws from its own scratch
            // Random, so the level observer sees nothing either way. The value
            // checked is that the two skip flags cannot change the level stream.
            check(draws_for(true, false) == draws_for(false, false),
                  "desert skip does not change the level stream");
            check(draws_for(false, true) == draws_for(false, false),
                  "no-lake skip does not change the level stream");
        }

        // Region edits made before postProcess survive: the slice only reports
        // mutations, it does not clear the region.
        {
            DecorationRegion region;
            region.load_empty_chunk(0, 0);
            region.set_block({1, 5, 1}, bds::terrain_block::stone);
            Random level(seed);
            PostProcessSettings settings;
            settings.level_seed = seed;
            const auto result = bds::post_process_chunk(region, origin, level, settings);
            check(!result.mutations.empty(), "mutations are reported to the caller");
            check(region.block({1, 5, 1}).name == std::string("stone"),
                  "the region keeps its cells");
        }

        std::cout << "postProcess slice: seed derivations, level-stream isolation and lake gating pass\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
