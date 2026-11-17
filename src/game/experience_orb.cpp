#include "bds/game/experience_orb.hpp"
#include <bit>
#include <cmath>

namespace bds::game {
namespace {
int floor_int(float v) { return static_cast<int>(std::floor(v)); } // mce::Math::floor 0x26dda50
BlockId block_at(const World &world, int x, int y, int z) {
    if (y < 0 || y > 255)
        return 0;
    return world.block({x, y, z});
}
} // namespace

void ExperienceOrb::set_pos(Vec3 p) {
    // Actor::setPos with the 0.25 x 0.25 size and heightOffset 0 (the orb
    // constructor never writes Actor +0x194).
    pos = p;
    bb.min = {p.x - 0.125f, p.y - height_offset, p.z - 0.125f};
    bb.max = {bb.min.x + 0.25f, bb.min.y + 0.25f, bb.min.z + 0.25f};
}

int truncate_experience_amount(int amount) {
    // 0x2405750: the largest orb value not above the amount.
    for (int v : {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3})
        if (amount >= v)
            return v;
    return 1;
}

ExperienceOrb make_orb(Vec3 pos, int value, Random &level_random) {
    ExperienceOrb orb;
    // ExperienceOrb::ExperienceOrb 0x24042f0: data 15 (value) defined as 1,
    // category |= 0x40000, status flag 46 (HAS_COLLISION).
    orb.set_pos(pos);
    orb.pos_prev = pos;
    // reloadHardcoded 0x24043b0, InitializationMethod 2 (spawned): the level
    // Random gives the yaw and the motion; then flag 48 off, +0x11c8 = 5,
    // setSize(0.25, 0.25), follow time and age 0, no owner (the parameter
    // list carries no actor for ExperienceOrb::spawnOrbs).
    orb.yaw = level_random.next_float(0.0f, 1.0f) * 360.0f;
    orb.motion.x = (level_random.next_float(0.0f, 1.0f) * 0.2f - 0.1f) * 2.0f;
    orb.motion.y = level_random.next_float(0.0f, 1.0f) * 0.2f * 2.0f;
    orb.motion.z = (level_random.next_float(0.0f, 1.0f) * 0.2f - 0.1f) * 2.0f;
    orb.follow_time = 0;
    orb.age = 0;
    orb.value = value; // ExperienceOrb::setValue 0x2405110
    return orb;
}

bool orb_physics(ExperienceOrb &orb, const World &world, Random &level_random) {
    // Actor::normalTick 0x21ac2a0 -> baseTick: previous position, below the
    // world the actor is removed (as for items).
    orb.pos_prev = orb.pos;
    if (0.0f > orb.pos.y) {
        orb.removed = true;
        return false;
    }
    // ++age; at LIFETIME (0x285f8d0: 6000) Actor::remove. The tick goes on.
    if (++orb.age >= 6000)
        orb.removed = true;
    orb.motion.y -= 0.04f; // 0x2827d1c
    bool lava = false;
    // BlockSource::getMaterial(floor(pos)).isType(Lava): bounce with
    // nextGaussianFloat (nextFloat - nextFloat) of the level Random, then
    // playSynchronizedSound(FIZZ) at the attach position (the caller's).
    if (block_at(world, floor_int(orb.pos.x), floor_int(orb.pos.y), floor_int(orb.pos.z)) == kStillLava) {
        orb.motion.y = std::bit_cast<float>(0x3e4ccccdU); // 0.2f
        const float gx = level_random.next_float() - level_random.next_float();
        orb.motion.x = gx * 0.2f;
        const float gz = level_random.next_float() - level_random.next_float();
        orb.motion.z = gz * 0.2f;
        lava = true;
    }
    // Actor::pushOutOfBlocks(getPos()) (vtable +0x6f8), Actor::move(motion) (+0xa0).
    push_out_of_blocks(orb, world, orb.pos);
    move(orb, world, orb.motion);
    // Friction 0.98; on the ground 0.6 * 0.98 (0x285f738), or the friction of
    // the block under the box times 0.98 when it is not air.
    float friction = 0.98f;
    if (orb.on_ground) {
        friction = std::bit_cast<float>(0x3f16872cU);
        if (block_at(world, floor_int(orb.pos.x), floor_int(orb.bb.min.y) - 1, floor_int(orb.pos.z)) != 0)
            friction = 0.6f * 0.98f; // Block::getFriction 0x1e68d90 (0.6 for every block here)
    }
    orb.motion.x *= friction;
    orb.motion.y *= 0.98f;
    orb.motion.z *= friction;
    if (orb.on_ground)
        orb.motion.y *= -0.5f; // 0x282867c
    return lava;
}

bool orb_attract(ExperienceOrb &orb, Vec3 delta) {
    // 0x2404c04: the length uses the x and z components only.
    const float length = static_cast<float>(std::sqrt(static_cast<double>(delta.x * delta.x + delta.z * delta.z)));
    if (!(length > std::bit_cast<float>(0x3e666666U))) // 0x285f8cc: 0.225f
        return false;
    float pull = 1.0f - length;
    if (pull > 0.0f) {
        pull *= pull;
        // (delta / length) * pull * 0.1 added to the motion.
        Vec3 d{delta.x / length, delta.y / length, delta.z / length};
        d = {d.x * pull, d.y * pull, d.z * pull};
        d = {d.x * 0.1f, d.y * 0.1f, d.z * 0.1f};
        orb.motion.x = d.x + orb.motion.x;
        orb.motion.y = d.y + orb.motion.y;
        orb.motion.z = d.z + orb.motion.z;
    }
    return true;
}

int level_xp(int level) {
    // _computeLevelXp 0x22f69d0: switch on level / 15.
    switch (level / 15) {
    case 0: return level * 2 + 7;
    case 1: return (level - 15) * 5 + 37;
    default: return (level - 30) * 9 + 112;
    }
}
} // namespace bds::game
