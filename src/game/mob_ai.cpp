#include "bds/game/mob_ai.hpp"
#include "bds/game/spawner.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <initializer_list>

namespace bds::game {
namespace {
constexpr float kRadDeg = std::bit_cast<float>(0x42652ee0u); // mce::Math::RADDEG 0x28784d4
constexpr float kDegRad = std::bit_cast<float>(0x3c8efa35u); // mce::Math::DEGRAD 0x28784d0
constexpr float kPi = std::bit_cast<float>(0x40490fdbu); // mce::Math::PI 0x28784c8

struct Entry {
    GoalKind kind;
    int priority;
    float speed = 1.0f;
};
// Control flags per goal constructor (Goal::setRequiredControlFlags; the
// oracle dump agrees): float 4, look 2, mount 3, panic 1, look around 3,
// stroll 3, tempt 3, breed 3, follow parent 1, eat block 7, run around 1.
int control_flags(GoalKind kind) {
    switch (kind) {
    case GoalKind::Float: return 4;
    case GoalKind::LookAtPlayer: return 2;
    case GoalKind::Panic: return 1;
    case GoalKind::FollowParent: return 1;
    case GoalKind::RunAroundLikeCrazy: return 1;
    case GoalKind::EatBlock: return 7;
    default: return 3;
    }
}
} // namespace

float wrap_degrees(float degrees) {
    // fmod(d + 180, 360), +360 when negative, -180.
    float r = std::fmod(degrees + 180.0f, 360.0f);
    if (r < 0.0f)
        r += 360.0f;
    return r - 180.0f;
}
float clamp_rotate(float from, float to, float max) {
    // mce::Math::clampRotate(Degree, Degree, Degree) 0x26de010.
    float delta = wrap_degrees(to - from);
    if (delta > max)
        delta = max;
    if (delta < -max)
        delta = -max;
    return from + delta;
}

MobBrain make_brain(std::uint32_t type, bool baby, float width, float height) {
    // Vector order observed in the original: the definition's behavior
    // components in alphabetical order, then those of the added groups; a
    // group component already present replaces it in place (cow adult breed).
    // INFERRED for baby cows/sheep: the group follow_parent (priority 6)
    // replaces the base one in place.
    std::vector<Entry> list;
    using K = GoalKind;
    switch (type) {
    case actor_type::Chicken:
        list = {{K::Float, 0}, {K::LookAtPlayer, 7}, {K::MountPathing, 2, 1.5f}, {K::Panic, 1, 1.5f},
                {K::RandomLookAround, 8}, {K::RandomStroll, 6, 1.0f}, {K::Tempt, 4, 1.0f}};
        list.push_back(baby ? Entry{K::FollowParent, 5, 1.1f} : Entry{K::Breed, 3});
        break;
    case actor_type::Pig:
        list = {{K::Float, 2}, {K::LookAtPlayer, 8}, {K::MountPathing, 1, 1.25f}, {K::Panic, 3, 1.25f},
                {K::RandomLookAround, 9}, {K::RandomStroll, 7, 1.0f}, {K::Tempt, 5, 1.2f}};
        list.push_back(baby ? Entry{K::FollowParent, 6, 1.1f} : Entry{K::Breed, 4});
        break;
    case actor_type::Cow:
        list = {{K::Breed, 3}, {K::Float, 0}, {K::FollowParent, baby ? 6 : 5, 1.1f}, {K::LookAtPlayer, 7},
                {K::MountPathing, 2, 1.5f}, {K::Panic, 1, 1.25f}, {K::RandomLookAround, 9},
                {K::RandomStroll, 6, 0.8f}, {K::Tempt, 4, 1.25f}};
        break;
    case actor_type::Sheep:
        list = {{K::EatBlock, 6}, {K::Float, 0}, {K::FollowParent, baby ? 6 : 5, 1.1f}, {K::LookAtPlayer, 8},
                {K::MountPathing, 2, 1.5f}, {K::Panic, 1, 1.25f}, {K::RandomLookAround, 9},
                {K::RandomStroll, 7, 0.8f}, {K::Tempt, 4, 1.25f}};
        if (!baby)
            list.push_back({K::Breed, 3});
        break;
    case actor_type::Horse:
    case actor_type::Donkey: {
        const int panic = type == actor_type::Horse ? 3 : 1;
        list = {{K::Float, 0}, {K::LookAtPlayer, 7}, {K::Panic, panic, 1.2f}, {K::RandomLookAround, 8},
                {K::RandomStroll, 6, 0.7f}};
        if (baby)
            list.push_back({K::FollowParent, 4, 1.0f});
        else
            list.insert(list.end(), {Entry{K::Breed, 2}, Entry{K::RunAroundLikeCrazy, 1, 1.2f},
                                     Entry{K::MountPathing, 2, 1.5f}});
        break;
    }
    default:
        break;
    }
    MobBrain brain;
    brain.baby = baby;
    for (const auto &e : list) {
        Goal g{e.kind, e.priority, control_flags(e.kind)};
        g.speed = e.speed;
        brain.goals.push_back(g);
    }
    // Actor::getAttachPos offsets read from the original's mobs (look oracle:
    // sheep 0.9x1.3 -> (0, 1.105, 0.765)/(0, 1.105, 0); horse 1.4x1.6 ->
    // (0, 1.36, 1.19)/(0, 1.36, 0)). INFERRED: 0.85 x height and width; the
    // function that fills them is not located yet. The behavior pack box is
    // used for babies too.
    brain.attach_head = {0.0f, height * 0.85f, width * 0.85f};
    brain.attach_eyes = {0.0f, height * 0.85f, 0.0f};
    // minecraft:spawn_entity in the chicken_adult group: 300..600 s, scaled
    // by 20 in SpawnActorComponent::initFromDefinition (0x2439390).
    if (type == actor_type::Chicken && !baby) {
        brain.lays_eggs = true;
        brain.spawn_min = 300 * 20;
        brain.spawn_max = 600 * 20;
    }
    return brain;
}

namespace {
Vec3 attach(const MobBrain &b, Vec3 pos, Vec3 offset) {
    // Actor::getAttachPos 0x21aaaa0 for locations 3/7: the offset turned by
    // Vec3::yRot(-headYaw * DEGRAD) (double libm cos/sin rounded to float),
    // plus the position (interpolation alpha 0).
    const float angle = -b.head_yaw * kDegRad;
    const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
    const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
    return {pos.x + (offset.x * c + offset.z * s), pos.y + offset.y, pos.z + (offset.z * c - offset.x * s)};
}
void set_look_at(MobBrain &b, Vec3 at, float y_max, float x_max) {
    // LookControl::setLookAt(Vec3, float, float) 0x2335e20.
    b.look.has_wanted = true;
    b.look.wanted = at;
    b.look.y_max = y_max;
    b.look.x_max = x_max;
}
constexpr float kMaxHeadXRot = 40.0f; // Mob::getMaxHeadXRot (0x2833128)

bool can_use(Goal &g, MobBrain &b, Vec3 pos, MobSenses &senses) {
    switch (g.kind) {
    case GoalKind::Float:
        return senses.in_liquid(); // FloatGoal::canUse 0x233af70
    case GoalKind::LookAtPlayer: {
        // LookAtActorGoal::canUse 0x233b300.
        if (senses.level_random().next_float() >= g.probability)
            return false;
        // _withinFieldOfView: the vanilla 360-degree view (+0x3c/+0x40 = 180)
        // accepts every direction; riders are never players here.
        for (const auto &p : senses.players_in(g.look_distance)) {
            g.target = p.id;
            return true;
        }
        return false;
    }
    case GoalKind::RandomLookAround:
        // RandomLookAroundGoal::canUse 0x23aa880: nextFloat() < 0.02.
        return senses.level_random().next_float() < 0.02f;
    case GoalKind::RandomStroll:
        // RandomStrollGoal::canUse 0x23c46f0: nextInt(interval) == 0, then
        // _setWantedPosition (RandomPos::getPos with the mob's own Random).
        if (g.interval != 0 && senses.level_random().next_int(g.interval) != 0)
            return false;
        if (auto wanted = senses.stroll_position(g.xz_dist, g.y_dist)) {
            g.stroll_wanted = *wanted;
            return true;
        }
        return false;
    case GoalKind::EatBlock: {
        // EatBlockGoal::canUse 0x23bec40: nextInt(baby ? 50 : 1000) == 0,
        // then tall grass at the feet (none here) or grass below.
        if (senses.level_random().next_int(b.baby ? 50 : 1000) != 0)
            return false;
        const int x = static_cast<int>(std::floor(pos.x)), y = static_cast<int>(std::floor(pos.y)),
                  z = static_cast<int>(std::floor(pos.z));
        return senses.block({x, y - 1, z}) == 2;
    }
    // FloatGoal (in water or lava), PanicGoal (recently hurt or burning),
    // MountPathingGoal / RunAroundLikeCrazyGoal (a rider), BreedGoal (love
    // mode), FollowParentGoal (an adult of its kind nearby, every 10 ticks of
    // its own timer), TemptGoal (a player holding the tempt item): their
    // preconditions do not occur for BDS-RE's mobs yet. NOT TRANSLATED:
    // TemptGoal and FollowParentGoal for when they do.
    default:
        return false;
    }
}
bool can_continue(Goal &g, MobSenses &senses, Vec3 pos) {
    switch (g.kind) {
    case GoalKind::Float:
        return senses.in_liquid(); // FloatGoal::canUse 0x233af70
    case GoalKind::LookAtPlayer: {
        // LookAtActorGoal::canContinueToUse 0x233b910: alive target within the
        // look distance (Actor::distanceToSqr), still in view, time left.
        if (!g.target)
            return false;
        const auto p = senses.player(*g.target);
        if (!p || !p->alive)
            return false;
        const float dx = p->pos.x - pos.x, dy = p->pos.y - pos.y, dz = p->pos.z - pos.z;
        if (dx * dx + dy * dy + dz * dz > g.look_distance * g.look_distance)
            return false;
        return g.look_time > 0;
    }
    case GoalKind::RandomStroll:
        return !senses.navigation_done(); // 0x23c4790
    case GoalKind::RandomLookAround:
        return g.look_around_time >= 0; // 0x23aa8d0
    case GoalKind::EatBlock:
        return g.eat_tick > 0; // 0x23bee70
    default:
        return false;
    }
}
void start(Goal &g, MobSenses &senses) {
    switch (g.kind) {
    case GoalKind::LookAtPlayer:
        // LookAtActorGoal::start 0x233ba10: nextInt(min, max).
        g.look_time = senses.level_random().next_int(g.min_look, g.max_look);
        break;
    case GoalKind::RandomLookAround: {
        // RandomLookAroundGoal::start 0x23aa8f0: angle 2*PI*nextFloat() with
        // libm cos/sin, then 20 + nextInt(20) ticks.
        const float angle = 2.0f * kPi * senses.level_random().next_float();
        g.rel_x = static_cast<float>(std::cos(static_cast<double>(angle)));
        g.rel_z = static_cast<float>(std::sin(static_cast<double>(angle)));
        g.look_around_time = 20 + senses.level_random().next_int(20);
        break;
    }
    case GoalKind::RandomStroll:
        senses.navigate(g.stroll_wanted, g.speed);
        g.stroll_last_end.reset();
        break;
    case GoalKind::EatBlock:
        senses.stop_navigation();
        // EatBlockGoal::start 0x23bedf0: 40 ticks, ActorEvent 10, stop the
        // navigation.
        g.eat_tick = 40;
        senses.eat_start();
        break;
    default:
        break;
    }
}
void stop(Goal &g, MobSenses &senses) {
    if (g.kind == GoalKind::RandomStroll)
        senses.stop_navigation();
    if (g.kind == GoalKind::LookAtPlayer)
        g.target.reset(); // 0x233ba60
    if (g.kind == GoalKind::EatBlock)
        g.eat_tick = 0; // 0x23bee50
}
void tick(Goal &g, MobBrain &b, Vec3 pos, MobSenses &senses) {
    switch (g.kind) {
    case GoalKind::Float:
        if (senses.level_random().next_float() < 0.8f)
            senses.float_jump(); // 0x233b050
        break;
    case GoalKind::RandomStroll:
        // RandomStrollGoal::tick 0x23c4840: retry a partial path only once
        // per distinct end node, when within two blocks of that end.
        if (senses.navigation_done() || senses.navigation_close(2.0f)) {
            if (auto end = senses.navigation_end()) {
                const World::Position wanted{int(std::floor(g.stroll_wanted.x)),
                                             int(std::floor(g.stroll_wanted.y)),
                                             int(std::floor(g.stroll_wanted.z))};
                if (*end != wanted && (!g.stroll_last_end || *g.stroll_last_end != *end)) {
                    g.stroll_last_end = end;
                    senses.navigate(g.stroll_wanted, g.speed);
                }
            }
        }
        break;
    case GoalKind::LookAtPlayer: {
        // LookAtActorGoal::tick 0x233ba80: look at the target's
        // getAttachPos(3) at 10 / getMaxHeadXRot. INFERRED: a player's
        // attach position 3 is taken as its (eye) position.
        if (const auto p = g.target ? senses.player(*g.target) : std::nullopt)
            set_look_at(b, p->pos, 10.0f, kMaxHeadXRot);
        --g.look_time;
        break;
    }
    case GoalKind::RandomLookAround: {
        // RandomLookAroundGoal::tick 0x23aa9b0.
        --g.look_around_time;
        const Vec3 head = attach(b, pos, b.attach_head);
        set_look_at(b, {head.x + g.rel_x, head.y, head.z + g.rel_z}, 10.0f, kMaxHeadXRot);
        break;
    }
    case GoalKind::EatBlock: {
        // EatBlockGoal::tick 0x23beeb0: at tick 4 the grass below becomes
        // dirt (LevelEvent 2001, setBlock flags 3) and Mob::ate runs.
        g.eat_tick = std::max(0, g.eat_tick - 1);
        if (g.eat_tick == 4) {
            const int x = static_cast<int>(std::floor(pos.x)), y = static_cast<int>(std::floor(pos.y)),
                      z = static_cast<int>(std::floor(pos.z));
            if (senses.block({x, y - 1, z}) == 2)
                senses.eat_grass({x, y - 1, z});
        }
        break;
    }
    default:
        break;
    }
}
bool can_coexist(const Goal &a, const Goal &b) { return (a.flags & b.flags) == 0; } // 0x23b3e60
bool can_use_in_system(const MobBrain &brain, const Goal &g) {
    // GoalSelector::_canUseInSystem 0x23b3c80: a running goal of equal or
    // higher priority (lower or equal number) must coexist; a running goal of
    // lower priority must be interruptible (Goal::canBeInterrupted: true).
    for (const auto &o : brain.goals) {
        if (&o == &g || !o.used)
            continue;
        if (g.priority >= o.priority) {
            if (!can_coexist(g, o))
                return false;
        }
        // else: o->canBeInterrupted() (vtable +0x20) is Goal's true for every
        // goal used here (none overrides it).
    }
    return true;
}
} // namespace

void tick_goals(MobBrain &brain, Vec3 pos, MobSenses &senses) {
    bool started = false;
    for (auto &g : brain.goals) {
        if (g.used) {
            if (can_use_in_system(brain, g) && can_continue(g, senses, pos))
                continue;
            stop(g, senses);
            g.used = false;
        }
        if (can_use_in_system(brain, g) && can_use(g, brain, pos, senses)) {
            g.used = true;
            g.to_start = true;
            started = true;
        }
    }
    if (started)
        for (auto &g : brain.goals)
            if (g.used && !can_use_in_system(brain, g)) {
                stop(g, senses);
                g.used = false;
                g.to_start = false;
            }
    for (auto &g : brain.goals) {
        if (g.to_start) {
            start(g, senses);
            g.to_start = false;
        }
        if (g.used)
            tick(g, brain, pos, senses);
    }
}

void tick_look(MobBrain &b, Vec3 pos) {
    auto &look = b.look;
    if (look.has_wanted) {
        look.has_wanted = false;
        const Vec3 eyes = attach(b, pos, b.attach_eyes);
        const float dx = look.wanted.x - eyes.x, dy = look.wanted.y - eyes.y, dz = look.wanted.z - eyes.z;
        const float horizontal = static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dz * dz)));
        // mce::Math::atan2 (libm atan2 on doubles, rounded to float) to degrees.
        const float y_rot =
            static_cast<float>(std::atan2(static_cast<double>(dz), static_cast<double>(dx))) * kRadDeg - 90.0f;
        const float x_rot =
            -(static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(horizontal))) * kRadDeg);
        b.pitch = clamp_rotate(b.pitch, x_rot, look.x_max);
        b.head_yaw = clamp_rotate(b.head_yaw, y_rot, look.y_max);
    } else if (look.has_rotation) {
        look.has_rotation = false;
        b.pitch = clamp_rotate(b.pitch, look.rotation_x, look.x_max);
        b.head_yaw = clamp_rotate(b.head_yaw, look.rotation_y, look.y_max);
    } else {
        b.pitch = clamp_rotate(b.pitch, 0.0f, 10.0f);
        b.head_yaw = clamp_rotate(b.head_yaw, b.body_yaw, 10.0f);
    }
    // The head stays within 75 degrees of the body (0x2854334 / 0x2854338).
    const float d = wrap_degrees(b.head_yaw - b.body_yaw);
    if (d < -75.0f)
        b.head_yaw = b.body_yaw - 75.0f;
    if (d > 75.0f)
        b.head_yaw = b.body_yaw + 75.0f;
}

void tick_body(MobBrain &b, float motion_x, float motion_z) {
    // The server branch uses the motion's horizontal length squared.
    const float speed_sqr = motion_x * motion_x + motion_z * motion_z;
    if (speed_sqr > 2.500000277905201e-07f) { // MoveControl::MIN_SPEED_SQR
        b.body_yaw = clamp_rotate(b.body_yaw, b.yaw, 25.0f);
        b.body_last_head = b.head_yaw;
        b.body_timer = 0;
        return;
    }
    float max = 75.0f;
    if (std::fabs(b.head_yaw - b.body_last_head) > 15.0f) {
        b.body_timer = 0;
        b.body_last_head = b.head_yaw;
    } else if (++b.body_timer > 10)
        max = std::max(1.0f - static_cast<float>(b.body_timer - 10) / 10.0f, 0.0f) * 75.0f;
    // clampEnough(head, body, max) 0x2331c60.
    float d = wrap_degrees(b.head_yaw - b.body_yaw);
    if (-max > d)
        d = -max;
    if (d >= max)
        d = max;
    b.body_yaw = b.head_yaw - d;
}
bool tick_spawn_actor(MobBrain &b, Random &random) {
    if (!b.lays_eggs)
        return false;
    if (b.spawn_timer == 0) {
        const int range = b.spawn_max - b.spawn_min;
        b.spawn_timer = (range == 0 ? 0 : random.next_int(range)) + b.spawn_min;
    }
    --b.spawn_timer;
    return b.spawn_timer <= 0;
}
} // namespace bds::game
