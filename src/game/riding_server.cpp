#include "bds/game/server.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace bds::game {
namespace {
Bytes actor_link(std::int64_t vehicle, std::int64_t rider, int type) {
    // serialize<ActorLink>::write 0x1863600.
    ByteWriter out;
    out.signed_varint64(vehicle);
    out.signed_varint64(rider);
    out.byte(static_cast<std::uint8_t>(type));
    out.boolean(false);
    return out.take();
}
Vec3 seat(const Server::MobActor &m) {
    if (m.data->type == actor_type::Horse)
        return {0, 1.1f, -0.2f};
    if (m.data->type == actor_type::Donkey)
        return {0, 0.925f, -0.2f};
    return {0, 0.63f, 0};
}
std::uint64_t mob_flags(const Server::MobActor &m) {
    return 0xC00400400000ULL | (m.baby ? 1ULL << 11 : 0) | (m.tamed ? 1ULL << 28 : 0) |
           (m.saddled ? 1ULL << 8 : 0) | (m.tamed && m.saddled ? (1ULL << 43) | (1ULL << 44) : 0);
}
Bytes rider_seat(std::uint64_t id, Vec3 offset) {
    ByteWriter out;
    out.varint64(id);
    out.varint(4);
    out.varint(56);
    out.varint(8);
    out.float32(offset.x);
    out.float32(offset.y);
    out.float32(offset.z);
    out.varint(57);
    out.varint(0);
    out.byte(0);
    out.varint(58);
    out.varint(3);
    out.float32(0);
    out.varint(59);
    out.varint(3);
    out.float32(0);
    return out.take();
}
} // namespace
bool Server::configure_mount(std::uint64_t vehicle, bool tamed, bool saddled) {
    auto m = std::find_if(mobs_.begin(), mobs_.end(),
                          [&](const MobActor &m) { return m.runtime_id == vehicle && !m.removed; });
    if (m == mobs_.end() || (m->data->type != actor_type::Horse && m->data->type != actor_type::Donkey) ||
        m->baby)
        return false;
    m->tamed = tamed;
    m->saddled = saddled;
    send_for_entity(vehicle, packet(PacketId::SetActorData, actor_flags(vehicle, mob_flags(*m))));
    return true;
}
bool Server::mount(std::uint64_t rider, std::uint64_t vehicle) {
    auto s = sessions_.find(rider);
    auto m = std::find_if(mobs_.begin(), mobs_.end(),
                          [&](const MobActor &m) { return m.runtime_id == vehicle && !m.removed; });
    if (s == sessions_.end() || !s->second.active || s->second.dead || s->second.vehicle ||
        m == mobs_.end() || m->baby || m->rider ||
        (m->data->type != actor_type::Horse && m->data->type != actor_type::Donkey))
        return false;
    const auto &p = world_.players().at(s->second.key);
    const float dx = p.x - m->pos.x, dy = p.y - m->pos.y, dz = p.z - m->pos.z;
    if (dx * dx + dy * dy + dz * dz > 144.0f)
        return false;
    s->second.rider_input.yaw = std::isfinite(p.yaw) ? wrap_degrees(p.yaw) : 0.0f;
    s->second.rider_input.pitch = std::isfinite(p.pitch) ? std::clamp(p.pitch, -90.0f, 90.0f) : 0.0f;
    m->rider = rider;
    s->second.vehicle = vehicle;
    s->second.lerp.steps = 0;
    s->second.flags_dirty = true;
    m->navigation.stop();
    m->move_control.has_wanted = false;
    auto offset = seat(*m);
    offset.x *= m->scale;
    offset.y = offset.y * m->scale + std::bit_cast<float>(0x3fcf5c7dU) - 0.4f;
    offset.z *= m->scale;
    send_for_entity(rider, packet(PacketId::SetActorData, rider_seat(rider, offset)));
    send_for_entity(vehicle,
                    packet(PacketId::SetActorLink, actor_link(m->unique_id, s->second.game.unique_id, 1)));
    position_rider(*m);
    return true;
}
void Server::dismount(std::uint64_t rider) {
    const auto s = sessions_.find(rider);
    if (s == sessions_.end() || !s->second.vehicle)
        return;
    const auto vehicle = *s->second.vehicle;
    const auto m =
        std::find_if(mobs_.begin(), mobs_.end(), [&](const MobActor &m) { return m.runtime_id == vehicle; });
    if (m != mobs_.end()) {
        auto &p = world_.players().at(s->second.key);
        ActorMotion rider;
        rider.pos = {p.x, p.y, p.z};
        rider.height_offset = std::bit_cast<float>(0x3fcf5c7dU);
        rider.bb = {{p.x - 0.3f, p.y - rider.height_offset, p.z - 0.3f},
                    {p.x + 0.3f, p.y - rider.height_offset + 1.8f, p.z + 0.3f}};
        const auto at = dismount_position(
            rider, world_, m->pos, view_vector(s->second.rider_input.pitch, s->second.rider_input.yaw));
        p.x = at.x;
        p.y = at.y;
        p.z = at.z;
        Motion reset;
        reset.runtime = s->second.game.runtime_id;
        reset.data = p;
        reset.mode = 2;
        send_for_entity(reset.runtime, packet(PacketId::MovePlayer, motion(reset)));
    }
    s->second.vehicle.reset();
    s->second.flags_dirty = true;
    s->second.rider_input = {};
    if (m != mobs_.end()) {
        m->rider.reset();
        m->max_step = 0.5625f;
        m->air_speed = 0.02f;
        m->jump_power = 0;
        send_for_entity(
            vehicle, packet(PacketId::SetActorLink, actor_link(m->unique_id, s->second.game.unique_id, 0)));
    }
}
void Server::position_rider(MobActor &m) {
    if (!m.rider)
        return;
    const auto s = sessions_.find(*m.rider);
    if (s == sessions_.end()) {
        m.rider.reset();
        return;
    }
    auto &p = world_.players().at(s->second.key);
    const auto position =
        rider_position(m.pos, m.brain.yaw, seat(m), m.scale, std::bit_cast<float>(0x3fcf5c7dU) - 0.4f);
    p.x = position.x;
    p.y = position.y;
    p.z = position.z;
    s->second.lerp.steps = 0;
    Motion update;
    update.runtime = *m.rider;
    update.data = p;
    update.riding = m.runtime_id;
    send_for_entity(*m.rider, packet(PacketId::MovePlayer, motion(update)), *m.rider);
}
} // namespace bds::game
