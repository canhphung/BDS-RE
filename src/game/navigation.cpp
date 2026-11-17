#include "bds/game/navigation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
namespace bds::game {
namespace {
int floor_int(float v) { return static_cast<int>(std::floor(v)); }
float distance(PathNode a, PathNode b) {
    const float x = float(a.x) - float(b.x), y = float(a.y) - float(b.y), z = float(a.z) - float(b.z);
    return static_cast<float>(std::sqrt(double(x * x + y * y + z * z)));
}
float square_distance(Vec3 a, Vec3 b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}
bool solid(const World &w, int x, int y, int z) { return block_has_collision(w.block({x, y, z})); }
struct Node {
    PathNode pos;
    int index = -1;
    float g = 0, h = 0, f = 0;
    Node *prev = nullptr;
    bool closed = false;
};
// BinaryHeap 0x1c6fda0/0x1c6ff70: equal children choose RIGHT. std heap's
// unspecified tie order changes paths, so preserve the native sift operations.
struct Heap {
    std::vector<Node *> data;
    void up(int i) {
        auto *n = data[i];
        while (i > 0) {
            const int p = (i - 1) >> 1;
            if (data[p]->f <= n->f)
                break;
            data[i] = data[p];
            data[i]->index = i;
            i = p;
        }
        data[i] = n;
        n->index = i;
    }
    void down(int i) {
        auto *n = data[i];
        for (;;) {
            const int l = 2 * i + 1, r = l + 1;
            if (l >= int(data.size()))
                break;
            const int c = r < int(data.size()) && !(data[l]->f < data[r]->f) ? r : l;
            if (!(data[c]->f < n->f))
                break;
            data[i] = data[c];
            data[i]->index = i;
            i = c;
        }
        data[i] = n;
        n->index = i;
    }
    void insert(Node *n) {
        n->index = int(data.size());
        data.push_back(n);
        up(n->index);
    }
    Node *pop() {
        Node *n = data.front();
        auto *last = data.back();
        data.pop_back();
        if (!data.empty()) {
            data[0] = last;
            last->index = 0;
            down(0);
        }
        n->index = -1;
        return n;
    }
};
bool free(const World &w, PathNode p, PathNode size) {
    for (int x = p.x; x < p.x + size.x; ++x)
        for (int y = p.y; y < p.y + size.y; ++y)
            for (int z = p.z; z < p.z + size.z; ++z)
                if (solid(w, x, y, z) || w.block({x, y, z}) == kStillWater ||
                    w.block({x, y, z}) == kStillLava)
                    return false;
    return true;
}
// PathNavigation::_canWalkOn / _canWalkDirectly 0x231e390/0x231e740.
bool walk_on(const World &w, int x, int y, int z, int sx, int sy, int sz, Vec3 from, float dx, float dz) {
    const int x0 = x - sx / 2, z0 = z - sz / 2;
    for (int xx = x0; xx < x0 + sx; ++xx)
        for (int zz = z0; zz < z0 + sz; ++zz) {
            const float rx = float(xx) + 0.5f - from.x, rz = float(zz) + 0.5f - from.z;
            if (rx * dx + rz * dz < 0)
                continue;
            for (int yy = y; yy < y + sy; ++yy)
                if (solid(w, xx, yy, zz))
                    return false;
            if (!solid(w, xx, y - 1, zz))
                return false;
        }
    return true;
}
bool directly(const World &w, Vec3 from, Vec3 to, int sx, int sy, int sz) {
    int x = floor_int(from.x), z = floor_int(from.z);
    float dx = to.x - from.x, dz = to.z - from.z;
    const float len = dx * dx + dz * dz;
    if (len < 1.0e-6f)
        return false;
    const float inv = static_cast<float>(1.0 / std::sqrt(double(len)));
    dx *= inv;
    dz *= inv;
    if (!walk_on(w, x, int(from.y), z, sx + 2, sy, sz + 2, from, dx, dz))
        return false;
    const float delta_x = 1.0f / std::abs(dx), delta_z = 1.0f / std::abs(dz);
    float tx = (float(x) - from.x + (dx >= 0 ? 1.0f : 0.0f)) / dx;
    float tz = (float(z) - from.z + (dz >= 0 ? 1.0f : 0.0f)) / dz;
    const int step_x = dx < 0 ? -1 : 1, step_z = dz < 0 ? -1 : 1;
    const int end_x = floor_int(to.x), end_z = floor_int(to.z);
    while ((end_x - x) * step_x > 0 || (end_z - z) * step_z > 0) {
        if (tx < tz) {
            tx += delta_x;
            x += step_x;
        } else {
            tz += delta_z;
            z += step_z;
        }
        if (!walk_on(w, x, int(from.y), z, sx, sy, sz, from, dx, dz))
            return false;
    }
    return true;
}
} // namespace
Vec3 Path::position(float width, std::size_t i) const {
    const auto n = nodes.at(i);
    const float offset = float(int(width + 1.0f)) * 0.5f; // Path::getPos 0x1c6e650
    return {float(n.x) + offset, float(n.y), float(n.z) + offset};
}
std::optional<Path> PathFinder::find(const World &w, PathNode start, PathNode target, PathNode size,
                                     float max_distance, Observer observe) {
    std::map<std::tuple<int, int, int>, Node> nodes;
    auto node = [&](PathNode p) -> Node * {
        auto [it, inserted] = nodes.try_emplace({p.x, p.y, p.z});
        if (inserted)
            it->second.pos = p;
        return &it->second;
    };
    auto candidate = [&](PathNode p, int step) -> Node * {
        if (!free(w, p, size)) {
            if (!step)
                return nullptr;
            p.y += step;
            if (!free(w, p, size))
                return nullptr;
        }
        auto *n = node(p);
        int fallen = 0;
        while (p.y > 0) {
            auto below = p;
            --below.y;
            if (!free(w, below, size))
                break;
            if (++fallen >= 4)
                return nullptr; // 0x1c73aec
            --p.y;
            if (p.y > 0)
                n = node(p);
        }
        return n;
    };
    Heap heap;
    auto *first = node(start), *goal = node(target), *best = first;
    first->h = distance(start, target);
    first->f = first->h;
    heap.insert(first);
    static constexpr PathNode directions[] = {{0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}};
    Node *result = nullptr;
    for (int count = 0; !heap.data.empty() && count < 200; ++count) {
        Node *cur = heap.pop();
        if (cur->pos == goal->pos) {
            result = cur;
            break;
        }
        if (distance(cur->pos, target) < distance(best->pos, target))
            best = cur;
        cur->closed = true;
        PathExpansion expansion{cur->pos, {}};
        const PathNode above{cur->pos.x, cur->pos.y + 1, cur->pos.z};
        const int step = free(w, above, size) ? 1 : 0;
        for (auto d : directions) {
            auto *n = candidate({cur->pos.x + d.x, cur->pos.y, cur->pos.z + d.z}, step);
            if (!n || n->closed || !(distance(n->pos, target) < max_distance))
                continue;
            expansion.neighbors.push_back(n->pos);
        }
        if (observe)
            observe(expansion);
        for (auto p : expansion.neighbors) {
            auto *n = node(p);
            const float g = cur->g + distance(cur->pos, n->pos);
            if (n->index < 0 || g < n->g) {
                n->prev = cur;
                n->g = g;
                n->h = distance(n->pos, target);
                const float cost = g + n->h, old = n->f;
                n->f = cost;
                if (n->index < 0)
                    heap.insert(n);
                else if (cost < old)
                    heap.up(n->index);
                else
                    heap.down(n->index);
            }
        }
    }
    if (!result && best != first)
        result = best;
    if (!result)
        return std::nullopt;
    Path path;
    for (auto *n = result; n; n = n->prev)
        path.nodes.push_back(n->pos);
    std::reverse(path.nodes.begin(), path.nodes.end());
    return path;
}
std::optional<Path> PathFinder::create(const World &w, const ActorMotion &a, Vec3 t, float width,
                                       float height, float max_distance) {
    // Level::findPath centers the integer request; PathFinder subtracts half
    // the actor's width and searches down when the target has air beneath it.
    PathNode start{floor_int(a.bb.min.x), int(a.bb.min.y), floor_int(a.bb.min.z)};
    PathNode target{floor_int(float(floor_int(t.x)) + 0.5f - width / 2.0f), int(t.y),
                    floor_int(float(floor_int(t.z)) + 0.5f - width / 2.0f)};
    while (target.y > 0 && !solid(w, target.x, target.y - 1, target.z))
        --target.y;
    const PathNode size{int(width + 1.0f), int(height + 1.0f), int(width + 1.0f)};
    return find(w, start, target, size, max_distance);
}
void MoveControl::tick(ActorMotion &a, MobBrain &b, float movement, JumpControl &jump) {
    // Gravity-bearing vanilla animal branch, 0x23315bb..0x23318e3.
    forward = 0;
    if (!has_wanted)
        return;
    const Vec3 d{wanted.x - a.pos.x, wanted.y - a.bb.min.y, wanted.z - a.pos.z};
    if (d.x * d.x + d.y * d.y + d.z * d.z < 0.1f) {
        has_wanted = false;
        return;
    }
    const float horizontal = d.x * d.x + d.z * d.z;
    const float radius =
        std::clamp(0.25f * ((a.bb.max.x - a.bb.min.x + a.bb.max.z - a.bb.min.z) * 0.5f), 0.05f, 0.25f);
    if (horizontal > radius * radius) {
        const float angle = static_cast<float>(std::atan2(double(d.z), double(d.x)));
        b.yaw = clamp_rotate(b.yaw, angle * 57.2957763671875f - 90.0f, max_turn);
    }
    speed = modifier * movement;
    forward = speed; // Mob::setSpeed also writes zza
    if (d.y > 0.009f && horizontal < std::max(1.0f, a.bb.max.x - a.bb.min.x))
        jump.jump();
}
bool PathNavigation::move_to(const World &w, const ActorMotion &a, Vec3 target, float modifier, float width,
                             float height) {
    auto p = PathFinder::create(w, a, target, width, height);
    if (!p)
        return false; // native preserves existing path on failure
    if (!path || path->nodes != p->nodes)
        path = std::move(p);
    if (path->nodes.empty())
        return false;
    speed = modifier;
    last_check = ticks;
    last_pos = {a.pos.x, float(int(a.bb.min.y + 0.1f)), a.pos.z};
    return true;
}
bool PathNavigation::close_to_done(Vec3 pos, float distance) const {
    if (!path)
        return false;
    if (path->done())
        return true;
    const auto end = path->nodes.back();
    return square_distance(pos, {float(end.x) + 0.5f, float(end.y) + 0.5f, float(end.z) + 0.5f}) <
           distance * distance;
}
void PathNavigation::tick(const World &w, const ActorMotion &a, float width, float height,
                          MoveControl &move) {
    ++ticks;
    if (done())
        return;
    if (a.on_ground) {
        const Vec3 pos{a.pos.x, float(int(a.bb.min.y + 0.1f)), a.pos.z};
        auto end = path->nodes.size();
        for (auto i = path->index; i < end; ++i)
            if (path->nodes[i].y != int(pos.y)) {
                end = i;
                break;
            }
        const float radius = std::max(0.6f, width);
        for (auto i = path->index; i < end; ++i)
            if (square_distance(pos, path->position(width, i)) < radius * radius)
                path->index = i + 1;
        for (auto i = end; i > path->index;) {
            --i;
            if (directly(w, pos, path->position(width, i), int(std::ceil(width)), int(height) + 1,
                         int(std::ceil(width)))) {
                path->index = i;
                break;
            }
        }
        if (ticks - last_check > 100) {
            if (square_distance(pos, last_pos) < 2.25f)
                stop();
            last_check = ticks;
            last_pos = pos;
        }
    }
    if (done())
        return;
    auto wanted = path->position(width, path->index);
    const int x = floor_int(wanted.x), y = floor_int(wanted.y), z = floor_int(wanted.z);
    if (!solid(w, x, y, z)) {
        wanted.y -= 0.5f;
        if (solid(w, x, y - 1, z))
            wanted.y = float(y);
    }
    move.set_wanted(wanted, speed);
}
} // namespace bds::game
