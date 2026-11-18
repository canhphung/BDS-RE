#pragma once
// Mob AI translated from BDS 1.6.0.15 (Linux): GoalSelector, the goals the
// vanilla animals use, LookControl and the chicken's SpawnActorComponent timer.
// Addresses are cited in mob_ai.cpp. Goal lists, priorities, control flags and
// parameters were read from the original's GoalSelector objects
// (tools/re/gdb_goal_oracle.py); the vector order is the alphabetical order of
// the definition's minecraft:behavior.* components, then the components of the
// groups added by minecraft:entity_spawned.
#include "bds/game/item_actor.hpp"
#include "bds/world/random.hpp"
#include "bds/world/world.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace bds::game {
enum class GoalKind {
    Float,              // FloatGoal: canUse needs water/lava
    LookAtPlayer,       // LookAtActorGoal
    MountPathing,       // MountPathingGoal: needs a rider target
    Panic,              // PanicGoal: needs a recent hurt/fire
    RandomLookAround,   // RandomLookAroundGoal
    RandomStroll,       // RandomStrollGoal
    Tempt,              // TemptGoal
    Breed,              // BreedGoal: needs love mode
    FollowParent,       // FollowParentGoal
    EatBlock,           // EatBlockGoal
    RunAroundLikeCrazy, // RunAroundLikeCrazyGoal: needs a rider
};
struct Goal {
    GoalKind kind;
    int priority;      // InternalGoal +0x8
    int flags;         // Goal +0x8 (1 move, 2 look, 4 jump)
    bool used = false, to_start = false; // InternalGoal +0xc / +0xd
    // LookAtActorGoal (+0x38 distance, +0x44 look time, +0x48/+0x4c range, +0x50 probability).
    float look_distance = 6.0f, probability = 0.02f;
    int min_look = 40, max_look = 80, look_time = 0;
    std::optional<std::uint64_t> target;
    // RandomLookAroundGoal (+0xc/+0x10 relative x/z, +0x14 time).
    float rel_x = 0, rel_z = 0;
    int look_around_time = 0;
    // RandomStrollGoal (+0x18 speed, +0x1c xz, +0x20 y, +0x24 interval).
    float speed = 1.0f;
    int xz_dist = 10, y_dist = 7, interval = 120;
    Vec3 stroll_wanted;
    std::optional<World::Position> stroll_last_end;
    // PanicGoal (+0xc speed, +0x10 wanted position).
    Vec3 panic_wanted;
    // EatBlockGoal (+0xc eat animation tick).
    int eat_tick = 0;
};
// LookControl (ctor 0x2335c60): +0x10 yMax, +0x14 xMax, +0x18 hasWanted,
// +0x1c wanted, +0x28 hasWantedRotation, +0x2c/+0x30 rotation.
struct LookControl {
    float y_max = 0, x_max = 0;
    bool has_wanted = false;
    Vec3 wanted;
    bool has_rotation = false;
    float rotation_x = 0, rotation_y = 0;
};
struct MobBrain {
    bool baby = false;
    std::vector<Goal> goals; // Mob goal selector (the target selector is empty for animals)
    LookControl look;
    float pitch = 0;     // Actor +0xac
    float yaw = 0;       // Actor +0xb0
    float body_yaw = 0;  // Mob +0x11b8
    float head_yaw = 0;  // Mob +0x11c0
    // BodyControl (+0x10 timer, +0x14 last head yaw).
    int body_timer = 0;
    float body_last_head = 0;
    // Actor::getAttachPos offsets: location 3 (+0x1c8) and 7 (+0x1e0).
    Vec3 attach_head, attach_eyes;
    // SpawnActorComponent (+0xc min, +0x10 max, +0x14 timer) for egg laying.
    bool lays_eggs = false;
    int spawn_min = 0, spawn_max = 0, spawn_timer = 0;
};

// The services the goals use.
struct NearbyPlayer {
    std::uint64_t id;
    Vec3 pos; // Actor position (eye height for players)
    float distance_sqr;
    bool alive = true;
};
class MobSenses {
  public:
    virtual ~MobSenses() = default;
    virtual Random &level_random() = 0;
    // BlockSource::fetchEntities(mob, mob AABB grown by `grow`) restricted to
    // players (the only entities the vanilla look_at_player filter accepts),
    // sorted by distance as LookAtActorGoal::canUse's std::sort.
    virtual std::vector<NearbyPlayer> players_in(float grow) = 0;
    virtual std::optional<NearbyPlayer> player(std::uint64_t id) = 0;
    virtual BlockId block(World::Position at) = 0;
    // EatBlockGoal: ActorEvent 10, then the grass under the mob turns to dirt
    // with LevelEvent 2001 (mobGriefing is the default true).
    virtual void eat_start() = 0;
    virtual std::optional<Vec3> stroll_position(int, int) { return std::nullopt; }
    // Mob::getLastHurtByMob (vtable +0x898): the attacker's position
    // (Actor::getPos), if the mob remembers one.
    virtual std::optional<Vec3> last_attacker() const { return std::nullopt; }
    virtual bool on_fire() const { return false; } // Actor::isOnFire
    // RandomPos::getPosAvoid 0x231bbc0: generateRandomPos with the direction
    // mob position minus `avoid`.
    virtual std::optional<Vec3> avoid_position(int, int, Vec3) { return std::nullopt; }
    virtual bool navigation_done() const { return true; }
    virtual void navigate(Vec3, float) {}
    virtual void stop_navigation() {}
    virtual bool navigation_close(float) const { return false; }
    virtual std::optional<World::Position> navigation_end() const { return std::nullopt; }
    virtual bool in_liquid() const { return false; }
    virtual void float_jump() {}
    virtual void eat_grass(World::Position at) = 0;
};

// The goal list of a newly spawned animal (adult/baby groups applied).
MobBrain make_brain(std::uint32_t type, bool baby, float width, float height);
// GoalSelector::tick 0x23b3950.
void tick_goals(MobBrain &brain, Vec3 pos, MobSenses &senses);
// LookControl::tick 0x2335ef0.
void tick_look(MobBrain &brain, Vec3 pos);
// Mob::normalTick's body turn for useNewAi mobs: BodyControl::clientTick
// 0x2331a00 with the motion (Actor +0xa0 / +0xa8).
void tick_body(MobBrain &brain, float motion_x, float motion_z);
// SpawnActorComponent::tick 0x2439460 (no rider). Returns true when the egg
// is due (spawnEntity, not translated yet).
bool tick_spawn_actor(MobBrain &brain, Random &level_random);
// mce::Math helpers.
float wrap_degrees(float degrees);                      // 0x26ddf10
float clamp_rotate(float from, float to, float max);    // 0x26de0f0
} // namespace bds::game
