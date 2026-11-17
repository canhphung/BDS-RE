#include "bds/game/item_actor.hpp"
#include "bds/game/items.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

namespace bds::game {
namespace {
constexpr float pi = std::numbers::pi_v<float>; // mce::Math::PI (0x28784c8)

float component(const Vec3 &v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
float &component(Vec3 &v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

int floor_int(float v) { return static_cast<int>(std::floor(v)); } // mce::Math::floor 0x26dda50

// Blocks of the internal palette (air, stone, grass, dirt, bedrock) are full
// cubes: BlockLegacy defaults give pushesOutItems, a unit collision shape,
// friction 0.6 and updateEntityAfterFallOn (0x1e56a10) setting motion.y = 0.
bool solid(BlockId id) { return block_has_collision(id); }
constexpr float default_friction = 0.6f; // Block::getFriction 0x1e68d90

bool has_chunk(const World &world, Vec3 p) {
    // BlockSource::hasBlock 0x1c59440: the chunk exists (no height check).
    const auto x = floor_int(p.x), z = floor_int(p.z);
    return world.chunk(x >> 4, z >> 4) != nullptr;
}

BlockId block_at(const World &world, int x, int y, int z) {
    if (y < 0 || y > 255)
        return 0;
    return world.block({x, y, z});
}

// AABB::expanded: grows only in the direction of the delta.
AABB expanded(const AABB &b, Vec3 d) {
    AABB out = b;
    for (int i = 0; i < 3; ++i) {
        const float v = component(d, i);
        if (v < 0)
            component(out.min, i) += v;
        else if (v > 0)
            component(out.max, i) += v;
    }
    return out;
}

// BlockSource::fetchCollisionShapes (0x1c5d990): x, then z, then y over
// floor(min - 1) .. floor(max + 1), each block adding its shape when it
// strictly intersects the box (BlockLegacy::addAABB 0x1e54990). Touching
// cubes are not returned, and the order matters to the sequential clipping.
// Unloaded-chunk boxes, the void floor and border blocks do not occur here.
std::vector<AABB> collision_shapes(const World &world, const AABB &box) {
    std::vector<AABB> out;
    for (int x = floor_int(box.min.x - 1.0f); x <= floor_int(box.max.x + 1.0f); ++x)
        for (int z = floor_int(box.min.z - 1.0f); z <= floor_int(box.max.z + 1.0f); ++z)
            for (int y = floor_int(box.min.y - 1.0f); y <= floor_int(box.max.y + 1.0f); ++y) {
                if (!solid(block_at(world, x, y, z)))
                    continue;
                const AABB cube{{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                                {static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)}};
                if (box.intersects(cube))
                    out.push_back(cube);
            }
    return out;
}
} // namespace

bool is_free(const ActorMotion &a, const World &world, Vec3 delta) {
    // Actor::isFree 0x21a6aa0: test the destination box, then liquid occupancy.
    auto box = a.bb;
    box.move(delta);
    if (!collision_shapes(world, box).empty())
        return false;
    for (int x = floor_int(box.min.x); x < int(std::floor(box.max.x + 1.0f)); ++x)
        for (int y = floor_int(box.min.y); y < int(std::floor(box.max.y + 1.0f)); ++y)
            for (int z = floor_int(box.min.z); z < int(std::floor(box.max.z + 1.0f)); ++z) {
                const auto id = block_at(world, x, y, z);
                if (id == kStillWater || id == kStillLava)
                    return false;
            }
    return true;
}

bool AABB::intersects(const AABB &o) const {
    return !(o.min.x >= max.x || min.x >= o.max.x || o.min.y >= max.y || min.y >= o.max.y ||
             o.min.z >= max.z || min.z >= o.max.z);
}

void AABB::move(Vec3 d) {
    min.x += d.x;
    min.y += d.y;
    min.z += d.z;
    max.x += d.x;
    max.y += d.y;
    max.z += d.z;
}

Vec3 AABB::clip_collide(const AABB &other, Vec3 delta, bool keep_overlap, float *penetration) const {
    if (penetration)
        *penetration = 0;
    Vec3 result = delta;
    int separated = 0, axis = 0;
    float min_penetration = std::numeric_limits<float>::max();
    float pen[3], dist[3], sign[3];
    for (int i = 0; i < 3; ++i) {
        const float a = component(other.max, i) - component(min, i);
        const float b = component(max, i) - component(other.min, i);
        const float pa = std::max(0.0f, a), pb = std::max(0.0f, b);
        if (pa == 0) {
            pen[i] = 0;
            dist[i] = a;
            sign[i] = -1.0f;
            ++separated;
            axis = i;
        } else if (pb == 0) {
            pen[i] = 0;
            dist[i] = b;
            sign[i] = 1.0f;
            ++separated;
            axis = i;
        } else if (pb > pa) {
            dist[i] = pa;
            pen[i] = pa;
            sign[i] = -1.0f;
        } else {
            dist[i] = pb;
            pen[i] = pb;
            sign[i] = 1.0f;
        }
        if (separated > 1)
            return result;
        min_penetration = std::min(min_penetration, pen[i]);
    }
    if (penetration)
        *penetration = min_penetration;
    if (separated == 0) {
        if (keep_overlap)
            return result;
        int k = 0;
        for (int j = 1; j < 3; ++j)
            if (pen[k] > pen[j])
                k = j;
        component(result, k) = pen[k] * sign[k];
        return result;
    }
    const float v = dist[axis] - sign[axis] * component(delta, axis);
    if (0 < v)
        component(result, axis) = dist[axis] * sign[axis];
    return result;
}

ItemActor *merge(ItemActor &a, ItemActor &b) {
    if (&a == &b || a.removed || b.removed || a.throw_time == 0xffff || b.throw_time == 0xffff ||
        a.item.block != b.item.block || a.item.aux != b.item.aux) // matchesItem: id and aux value
        return nullptr;
    if (a.item.count < b.item.count)
        return merge(b, a);
    if (a.item.count + b.item.count > item_info(b.item.block).max_stack) // getMaxStackSize
        return nullptr;
    b.item.count = static_cast<std::uint8_t>(b.item.count + a.item.count);
    b.throw_time = std::max(b.throw_time, a.throw_time);
    b.age = std::min(b.age, a.age);
    a.removed = true;
    return &b;
}

AABB merge_area(const ItemActor &a) {
    return {{a.bb.min.x - 0.5f, a.bb.min.y - 0.5f, a.bb.min.z - 0.5f},
            {a.bb.max.x + 0.5f, a.bb.max.y + 0.5f, a.bb.max.z + 0.5f}};
}

void ItemActor::set_pos(Vec3 p) {
    // Actor::setPos: the 0.25 x 0.25 box hangs heightOffset below the position.
    pos = p;
    bb.min = {p.x - 0.125f, p.y - height_offset, p.z - 0.125f};
    bb.max = {bb.min.x + 0.25f, bb.min.y + 0.25f, bb.min.z + 0.25f};
}

ItemActor pop_resource(World::Position block, ItemStack item, Random &level_random, Random &actor_random) {
    ItemActor a;
    a.item = item;
    // ItemActor(ActorDefinitionGroup*, ...) 0x23fe150 with the thread-local Random.
    a.bob = actor_random.next_float() * pi * 2.0f;
    a.y_rot = actor_random.next_float() * 360.0f;
    a.motion.x = actor_random.next_float() * 0.2f - 0.1f;
    a.motion.z = actor_random.next_float() * 0.2f - 0.1f;
    a.motion.y = 0.2f;
    // BlockLegacy::popResource 0x1e545b0: 0.15 + nextFloat * 0.7 per axis
    // (Level::getRandom), pickup delay 10.
    const float s = 0.7f;
    const float dx = level_random.next_float() * s + 0.15f;
    const float dy = level_random.next_float() * s + 0.15f;
    const float dz = level_random.next_float() * s + 0.15f;
    auto [x, y, z] = block;
    a.set_pos({static_cast<float>(x) + dx, static_cast<float>(y) + dy, static_cast<float>(z) + dz});
    a.pos_prev = a.pos;
    a.pickup_delay = 10;
    return a;
}

ItemActor drop_item(Vec3 at, ItemStack item, Random &actor_random) {
    ItemActor a;
    a.item = item;
    a.bob = actor_random.next_float() * pi * 2.0f;
    a.y_rot = actor_random.next_float() * 360.0f;
    a.motion.x = actor_random.next_float() * 0.2f - 0.1f;
    a.motion.z = actor_random.next_float() * 0.2f - 0.1f;
    a.motion.y = 0.2f;
    a.set_pos(at);
    a.pos_prev = a.pos;
    a.pickup_delay = 40;
    a.throw_time = 10;
    return a;
}

void move(ActorMotion &a, const World &world, Vec3 delta) {
    if (!has_chunk(world, a.pos))
        return;
    Vec3 d = delta;
    // Actor::isImmobile is false for a live item: deltas beyond 500 are dropped.
    if (std::abs(d.x) > 500.0f || std::abs(d.y) > 500.0f || std::abs(d.z) > 500.0f)
        d = {};
    const Vec3 d0 = d;
    const AABB initial_box = a.bb;
    const auto shapes = collision_shapes(world, expanded(a.bb, d));
    // The sub-collider list (+0x1178) is empty and +0x2a1 is false for items.
    const bool keep_overlap = a.stuck_in_collider && !a.no_exit;
    Vec3 axes[3] = {{0, d.y, 0}, {d.x, 0, 0}, {0, 0, d.z}};
    float pen[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        for (const auto &shape : shapes) {
            float p = 0;
            axes[i] = shape.clip_collide(a.bb, axes[i], keep_overlap, &p);
            pen[i] = std::max(pen[i], p);
        }
        a.bb.move(axes[i]);
    }
    const float total = pen[0] + pen[1] + pen[2];
    constexpr float threshold = std::numeric_limits<float>::epsilon(); // 0x284c46c
    if (a.stuck_in_collider && total <= threshold)
        a.stuck_in_collider = false;
    else if (!a.stuck_in_collider && a.last_penetration > threshold && total > threshold)
        a.stuck_in_collider = true;
    a.last_penetration = total;
    d = {axes[0].x + axes[1].x + axes[2].x, axes[0].y + axes[1].y + axes[2].y,
         axes[0].z + axes[1].z + axes[2].z};
    // Actor::move step branch, 0x21a7bb6..0x21a8675. Try up, original
    // horizontal X/Z, then down; prefer strictly larger horizontal progress
    // and reject an intersecting final box. The supported shapes are cubes.
    if (a.max_step > 0 && (a.on_ground || (d0.y != d.y && d0.y < 0)) &&
        (d0.x != d.x || d0.z != d.z)) {
        const auto ordinary_box = a.bb;
        a.bb = initial_box;
        const auto step_shapes = collision_shapes(world, expanded(a.bb, d0));
        Vec3 step[4]={{0,a.max_step,0},{d0.x,0,0},{0,0,d0.z},{}};
        for (int i=0;i<4;++i) {
            if (i==3) step[3]={0,-step[0].y,0};
            for (const auto &shape : step_shapes)
                step[i]=shape.clip_collide(a.bb,step[i],keep_overlap,nullptr);
            a.bb.move(step[i]);
        }
        const bool overlaps = !collision_shapes(world,a.bb).empty();
        if (overlaps || d.x*d.x+d.z*d.z >= step[1].x*step[1].x+step[2].z*step[2].z)
            a.bb=ordinary_box;
        else {
            d={step[1].x,step[0].y+step[3].y,step[2].z};
            a.slide_offset+=d.y; // 0x21a8656
        }
    }
    // Position from the collision box, Actor::move 0x21a86b5..0x21a8739.
    a.pos = {(a.bb.min.x + a.bb.max.x) / 2.0f, a.bb.min.y + a.height_offset - a.slide_offset,
             (a.bb.min.z + a.bb.max.z) / 2.0f};
    a.horizontal_collision = d0.x != d.x || d0.z != d.z;
    a.vertical_collision = d0.y != d.y;
    a.on_ground = (d0.y != d.y && d0.y < 0) || (a.on_ground && d0.y == d.y && d0.y == 0);
    a.collision = a.horizontal_collision || a.vertical_collision;
    // checkFallDamage only accumulates fall distance for items (no effect).
    if (d0.x != d.x)
        a.motion.x = 0;
    if (d0.y != d.y)
        a.motion.y = 0; // Block::updateEntityAfterFallOn
    if (d0.z != d.z)
        a.motion.z = 0;
    a.slide_offset *= 0.4f; // Actor::move epilogue, 0x21a8a97..0x21a8aae
}

void push_out_of_blocks(ItemActor &a, const World &world, Vec3 p) {
    const int bx = floor_int(p.x), by = floor_int(p.y), bz = floor_int(p.z);
    const Vec3 frac{p.x - static_cast<float>(bx), p.y - static_cast<float>(by), p.z - static_cast<float>(bz)};
    if (!solid(block_at(world, bx, by, bz)))
        return;
    const AABB cube{{static_cast<float>(bx), static_cast<float>(by), static_cast<float>(bz)},
                    {static_cast<float>(bx + 1), static_cast<float>(by + 1), static_cast<float>(bz + 1)}};
    if (!cube.intersects(a.bb))
        return;
    const bool west = !solid(block_at(world, bx - 1, by, bz)), east = !solid(block_at(world, bx + 1, by, bz)),
               below = !solid(block_at(world, bx, by - 1, bz)), above = !solid(block_at(world, bx, by + 1, bz)),
               north = !solid(block_at(world, bx, by, bz - 1)), south = !solid(block_at(world, bx, by, bz + 1));
    a.no_exit = !(west || east || below || above || north || south);
    float best = 9999.0f; // 0x284ed30
    int dir = -1;
    if (west && frac.x < best) {
        best = frac.x;
        dir = 0;
    }
    if (east && 1.0f - frac.x < best) {
        best = 1.0f - frac.x;
        dir = 1;
    }
    if (below && frac.y < best) { // stairs/slabs do not exist in the palette
        best = frac.y;
        dir = 2;
    }
    if (above && 1.0f - frac.y < best) {
        best = 1.0f - frac.y;
        dir = 3;
    }
    if (north && frac.z < best) {
        best = frac.z;
        dir = 4;
    }
    if (south && 1.0f - frac.z < best)
        dir = 5;
    float speed = 0.25f;
    if (dir == 3) {
        const float v = static_cast<float>(by) + a.height_offset + 1.0f - p.y;
        speed *= v > 0.0f ? v : 0.0f;
    }
    switch (dir) {
    case 0: a.motion.x = -speed; break;
    case 1: a.motion.x = speed; break;
    case 2: a.motion.y = -speed; break;
    case 3: a.motion.y = speed; break;
    case 4: a.motion.z = -speed; break;
    case 5: a.motion.z = speed; break;
    default: break;
    }
}

void normal_tick_before_merge(ItemActor &a) {
    // Actor::normalTick 0x21ac2a0 -> Actor::baseTick 0x21ad110:
    // setPreviousPosRot, then outOfWorld (remove) below y = 0. Fire, lava,
    // water and bubble columns cannot occur with the internal palette.
    a.pos_prev = a.pos;
    if (0.0f > a.pos.y) {
        a.removed = true;
        return;
    }
    if (a.throw_time > 0 && a.throw_time != 0xffff)
        --a.throw_time;
    if (a.pickup_delay > 0)
        --a.pickup_delay;
}

void normal_tick_after_merge(ItemActor &a, const World &world) {
    push_out_of_blocks(a, world, {a.pos.x, a.pos.y - a.height_offset, a.pos.z});
    const float speed2 = a.motion.x * a.motion.x + a.motion.y * a.motion.y + a.motion.z * a.motion.z;
    bool moved = !a.on_ground || speed2 > 0.01f; // isInWater() is always false here
    if (!moved && a.age % 4 == 0)
        moved = true;
    if (moved) {
        a.motion.y -= 0.04f; // gravity (status flag 47)
        move(a, world, a.motion);
        float friction = 0.98f;
        if ((a.vertical_collision && 0.0f >= a.motion.y) || a.on_ground) {
            friction = std::bit_cast<float>(0x3f16872cU); // 0x285f738: 0.6f * 0.98f, not the literal 0.588f
            const auto below = block_at(world, floor_int(a.pos.x), floor_int(a.bb.min.y) - 1, floor_int(a.pos.z));
            if (below != 0)
                friction = default_friction * 0.98f;
        }
        a.motion.x *= friction;
        a.motion.y *= 0.98f;
        a.motion.z *= friction;
    }
    ++a.age;
    if (a.age >= a.lifetime)
        a.removed = true; // Item::shouldDespawn is true for block items
}
} // namespace bds::game
