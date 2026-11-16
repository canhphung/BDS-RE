#pragma once
// Dropped-item entity (ItemActor) translated from BDS 1.6.0.15 (Linux).
// Original addresses are cited next to each function in item_actor.cpp; the
// per-tick position/motion was compared with the original under gdb
// (tests/reference/item_actor_bds_1.6.0.15.json).
#include "bds/game/player.hpp"
#include "bds/world/random.hpp"
#include "bds/world/world.hpp"
#include <cstdint>
#include <vector>

namespace bds::game {
struct Vec3 {
    float x = 0, y = 0, z = 0;
    bool operator==(const Vec3 &) const = default;
};
struct AABB {
    Vec3 min, max;
    // AABB::intersects 0x2187ff0: strict overlap on all axes.
    bool intersects(const AABB &o) const;
    // AABB::clipCollide 0x21877e0 (this = block shape, other = moving box).
    Vec3 clip_collide(const AABB &other, Vec3 delta, bool keep_overlap, float *penetration) const;
    void move(Vec3 d);
};

struct ItemActor {
    std::uint64_t runtime_id = 0;
    std::int64_t unique_id = 0;
    ItemStack item;
    Vec3 pos, pos_prev, motion; // StateVectorComponent +0xe7c / +0xe88, Actor +0xa0
    AABB bb;                    // AABBShapeComponent +0xe58
    float height_offset = 0.125f; // +0x194: getAABBDim().y / 2 for the 0.25 x 0.25 item box
    float y_rot = 0, bob = 0;     // +0xb0 and ItemActor +0x1234 (bobbing offset, client only)
    bool on_ground = true;        // +0x124 (observed set before the first tick)
    bool horizontal_collision = false, vertical_collision = false, collision = false; // +0x126..+0x128
    bool stuck_in_collider = false; // +0x1190
    bool no_exit = false;           // +0xf18, written by pushOutOfBlocks
    float last_penetration = 0;     // +0x1194
    int age = 0, throw_time = 0, pickup_delay = 0, lifetime = 6000; // +0x1228 +0x122c +0x1230 +0x123c
    bool removed = false;
    // Replication state (SpatialActorNetworkData / Actor +0x110).
    bool added = false;
    Vec3 sent_pos, sent_motion;
    bool sent_on_ground = false;

    void set_pos(Vec3 p);
};

// Spawner::spawnItem (0x1c6a820) for BlockLegacy::popResource (0x1e545b0):
// level_random places the item inside the block, actor_random is the
// thread-local Random used by the ItemActor constructor (0x23fe150).
ItemActor pop_resource(World::Position block, ItemStack item, Random &level_random, Random &actor_random);

// Actor::drop (0x21bfbc0) through Spawner::spawnItem: an item at `at` with
// pickup delay 40 and throw time 10 (setDefaultPickUpDelay); the caller sets
// the throw motion.
ItemActor drop_item(Vec3 at, ItemStack item, Random &actor_random);

// ItemActor::_merge (0x23ff7f0): returns the absorbing item (the larger
// stack, `b` on a tie) or nullptr; the absorbed one is marked removed.
ItemActor *merge(ItemActor &a, ItemActor &b);
// ItemActor::_mergeWithNeighbours (0x23feee0) area: the box grown by 0.5.
AABB merge_area(const ItemActor &a);

// Actor::move (0x21a6b50) for an item: collision on, not sneaking, no web,
// no step-up (maxAutoStep 0), no collidable mobs.
void move(ItemActor &a, const World &world, Vec3 delta);
// Actor::pushOutOfBlocks (0x21bb8a0).
void push_out_of_blocks(ItemActor &a, const World &world, Vec3 feet);
// ItemActor::normalTick (0x23fe8a0). `merge` is ItemActor::_mergeWithNeighbours,
// called every 25 ticks of age.
template <class Merge> void normal_tick(ItemActor &a, const World &world, Merge merge);
void normal_tick_before_merge(ItemActor &a);
void normal_tick_after_merge(ItemActor &a, const World &world);
template <class Merge> void normal_tick(ItemActor &a, const World &world, Merge merge) {
    normal_tick_before_merge(a);
    if (a.removed)
        return;
    if (a.age % 25 == 0)
        merge(a);
    if (a.removed)
        return;
    normal_tick_after_merge(a, world);
}
} // namespace bds::game
