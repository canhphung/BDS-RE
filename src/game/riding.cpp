#include "bds/game/riding.hpp"
#include "bds/game/mce_math.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace bds::game {
Vec3 rider_position(Vec3 mount, float yaw, Vec3 seat, float scale, float rider_height) {
    seat.x *= scale;
    seat.y = seat.y * scale + rider_height;
    seat.z *= scale;
    const float angle = -yaw * std::bit_cast<float>(0x3c8efa35u);
    const float c = static_cast<float>(std::cos(double(angle))),
                s = static_cast<float>(std::sin(double(angle)));
    return {mount.x + (seat.x * c + seat.z * s), mount.y + seat.y, mount.z + (seat.z * c - seat.x * s)};
}
Vec3 view_vector(float pitch, float yaw) {
    const float rad = std::bit_cast<float>(0x3c8efa35u), pi = std::bit_cast<float>(0x40490fdbu);
    const float yy = -yaw * rad - pi, pp = -pitch * rad;
    const float c = -mce_cos(pp);
    return {mce_sin(yy) * c, mce_sin(pp), mce_cos(yy) * c};
}
Vec3 dismount_position(const ActorMotion &rider, const World &world, Vec3 mount, Vec3 view, float distance) {
    // Native location 1 is the AABB center; retain subtraction/multiply/add order.
    const Vec3 center{(rider.bb.max.x - rider.bb.min.x) * 0.5f + rider.bb.min.x,
                      (rider.bb.max.y - rider.bb.min.y) * 0.5f + rider.bb.min.y,
                      (rider.bb.max.z - rider.bb.min.z) * 0.5f + rider.bb.min.z};
    if (distance > 0) {
        view.y = center.y + 1.0f;
        const float length =
            static_cast<float>(std::sqrt(double(view.x * view.x + view.y * view.y + view.z * view.z)));
        if (length < 1.0e-4f)
            view = {};
        else {
            view.x /= length;
            view.y /= length;
            view.z /= length;
        }
        Vec3 target{mount.x + view.x * distance, mount.y + view.y * distance, mount.z + view.z * distance};
        for (int attempt = 0; attempt < 3; ++attempt) {
            const float half = (rider.bb.max.x - rider.bb.min.x) * 0.5f;
            const float height = rider.bb.max.y - rider.bb.min.y;
            const AABB box{{target.x - half, target.y - rider.height_offset, target.z - half},
                           {target.x + half, target.y - rider.height_offset + height, target.z + half}};
            bool blocked = false;
            for (int x = int(std::floor(box.min.x)); x <= int(std::floor(box.max.x)); ++x)
                for (int z = int(std::floor(box.min.z)); z <= int(std::floor(box.max.z)); ++z)
                    for (int y = int(std::floor(box.min.y)); y <= int(std::floor(box.max.y)); ++y)
                        if (block_has_collision(world.block({x, y, z})) &&
                            box.intersects(
                                {{float(x), float(y), float(z)}, {float(x + 1), float(y + 1), float(z + 1)}}))
                            blocked = true;
            if (!blocked)
                return target;
            target.y += 1.0f;
        }
    }
    return center;
}
float player_jump_power(int charge) {
    charge = std::max(0, charge);
    return charge >= 90 ? 1.0f : 0.4f + 0.4f * static_cast<float>(charge) / 90.0f;
}
Vec3 ridden_input(ActorMotion &a, MobBrain &b, const RiderControl &r, float movement, float &speed,
                  float &air_speed, float &power, float strength, int boost) {
    const float change = wrap_degrees(r.yaw - b.yaw);
    const float factor = 0.7f * std::max(0.18f, (45.0f - std::min(45.0f, std::abs(change))) / 90.0f);
    const float current = wrap_degrees(b.yaw);
    const float wanted = current + wrap_degrees(r.yaw - b.yaw);
    b.yaw = wrap_degrees(current + factor * wrap_degrees(wanted - current));
    b.pitch = r.pitch * 0.5f;
    b.body_yaw = b.head_yaw = b.yaw;
    Vec3 input{r.strafe * 0.5f, r.vertical, r.forward};
    if (input.z <= 0)
        input.z *= 0.25f;
    if (power > 0 && a.on_ground) {
        a.motion.y = strength * power;
        if (boost >= 0)
            a.motion.y += 0.1f * static_cast<float>(boost + 1);
        if (input.z > 0) {
            const float angle = b.yaw * std::bit_cast<float>(0x40490fdbu) / 180.0f;
            a.motion.x += -0.4f * mce_sin(angle) * power;
            a.motion.z += 0.4f * mce_cos(angle) * power;
        }
        power = 0;
    }
    a.max_step = 1.0625f;
    air_speed = speed * 0.1f;
    speed = movement;
    return input;
}
} // namespace bds::game
