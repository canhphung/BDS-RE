#pragma once
// Experience orb entity (ExperienceOrb, ActorType 69) translated from BDS
// 1.6.0.15 (Linux). Addresses are cited in experience_orb.cpp.
#include "bds/game/item_actor.hpp"
#include "bds/world/random.hpp"
#include "bds/world/world.hpp"
#include <cstdint>
#include <optional>
#include <utility>

namespace bds::game {
struct ExperienceOrb : ActorMotion {
    ExperienceOrb() { height_offset = 0.0f; } // Actor default; setSize(0.25, 0.25) only
    std::uint64_t runtime_id = 0;
    std::int64_t unique_id = 0;
    int value = 1;          // SynchedActorData 15 (defined as 1 in the constructor)
    float yaw = 0;          // Actor +0xb0, nextFloat * 360 in reloadHardcoded
    int follow_time = 0;    // +0x11c0: tick count of the last follow-target search
    int age = 0;            // +0x11c4: removed at LIFETIME (6000)
    int tick_count = 0;     // Actor +0x208
    std::optional<std::int64_t> following; // +0x11b8 ActorUniqueID of the followed player
    std::pair<int, int> owner_chunk{};
    bool added = false, removed = false;
    bool gone = false; // left its chunk at the end of the tick it was removed in

    void set_pos(Vec3 p);
};

// ExperienceOrb::truncateExperienceAmount 0x2405750.
int truncate_experience_amount(int amount);
// ExperienceOrb constructor 0x24042f0 and reloadHardcoded 0x24043b0 for a
// spawned orb (four level-Random draws: yaw, motion x, y, z), then setValue.
ExperienceOrb make_orb(Vec3 pos, int value, Random &level_random);
// ExperienceOrb::normalTick 0x24045d0 up to the follow-target step: lifetime,
// gravity, lava bounce, pushOutOfBlocks, move and friction. Returns true when
// the orb was in lava (the caller plays LevelSoundEvent FIZZ).
bool orb_physics(ExperienceOrb &orb, const World &world, Random &level_random);
// The follow step's attraction: `delta` is (target attach position - orb
// position) / 8. Returns false when the target is reached (horizontal length
// at most 0.225): the caller moves the orb by `delta` and touches the player.
bool orb_attract(ExperienceOrb &orb, Vec3 delta);
// _computeLevelXp 0x22f69d0: experience needed to leave `level`.
int level_xp(int level);
} // namespace bds::game
