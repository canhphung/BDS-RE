// Replays the original's per-mob AI steps (GoalSelector::tick then
// LookControl::tick) from a captured level-Random trace: for every step the
// level Random is re-seeded and advanced to the step's draw index; the goal
// selector's draws and the resulting pitch / head yaw must equal the original's.
#include "bds/game/mob_ai.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace bds;
using namespace bds::game;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
class Senses final : public MobSenses {
  public:
    explicit Senses(Random &random) : random_(random) {}
    Random &level_random() override { return random_; }
    // The oracle records every LookAtActorGoal::canUse result; this capture
    // has no successful one (the players stayed out of the 6-block box).
    std::vector<NearbyPlayer> players_in(float) override { return {}; }
    std::optional<NearbyPlayer> player(std::uint64_t) override { return std::nullopt; }
    BlockId block(World::Position at) override { return std::get<1>(at) == 3 ? 2 : std::get<1>(at) < 3 ? 3 : 0; }
    void eat_start() override {}
    void eat_grass(World::Position) override {}

  private:
    Random &random_;
};
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "expected mob AI reference path");
        std::ifstream input(argv[1]);
        const auto reference = nlohmann::json::parse(input);
        check(reference.at("binary_sha256") == "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad",
              "unexpected original binary");
        const auto seed = reference.at("seed").get<std::uint32_t>();
        std::size_t steps = 0, words_checked = 0, looks = 0;
        for (const auto &mob : reference.at("mobs")) {
            auto brain = make_brain(mob.at("type").get<std::uint32_t>(), false, 1.0f, 1.0f);
            const auto head = mob.at("attach_head").get<std::array<float, 3>>();
            const auto eyes = mob.at("attach_eyes").get<std::array<float, 3>>();
            brain.attach_head = {head[0], head[1], head[2]};
            brain.attach_eyes = {eyes[0], eyes[1], eyes[2]};
            for (const auto &step : mob.at("steps")) {
                // The original's position at this step: mobs push each other
                // (Actor::push), which is not translated yet.
                const auto p = step.at("position").get<std::array<float, 3>>();
                const Vec3 pos{p[0], p[1], p[2]};
                Random random(seed);
                for (std::size_t i = 0, n = step.at("draw").get<std::size_t>(); i < n; ++i)
                    random.next_uint();
                std::vector<std::uint32_t> words;
                random.observe_draws([&](std::uint32_t w) { words.push_back(w); });
                Senses senses(random);
                for (const auto result : step.at("look_at").get<std::vector<int>>())
                    check(result == 0, "fixture has a successful LookAtActorGoal::canUse");
                tick_goals(brain, pos, senses);
                const auto where = " (mob type " + std::to_string(mob.at("type").get<int>()) + ", tick " +
                                   std::to_string(step.at("tick").get<int>()) + ")";
                check(words == step.at("words").get<std::vector<std::uint32_t>>(),
                      "goal selector draws differ from the original" + where);
                tick_look(brain, pos);
                tick_body(brain, 0.0f, 0.0f); // Mob::normalTick after aiStep
                // In a stroll-start step the original stops RandomLookAroundGoal
                // (GoalSelector second pass); the stroll is not translated, so
                // only its draws are compared there.
                if (step.contains("look") && !step.contains("stroll_start")) {
                    const auto look = step.at("look").get<std::array<float, 2>>();
                    check(brain.pitch == look[0] && brain.head_yaw == look[1],
                          "LookControl result differs from the original" + where + ": " +
                              std::to_string(brain.pitch) + "/" + std::to_string(brain.head_yaw) + " vs " +
                              std::to_string(look[0]) + "/" + std::to_string(look[1]));
                    ++looks;
                }
                ++steps;
                words_checked += words.size();
            }
        }
        std::cout << steps << " original mob AI steps, " << words_checked << " level-Random words and " << looks
                  << " LookControl results match\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
