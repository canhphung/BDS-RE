// BonusChestFeature, translated from BDS 1.6.0.15 (Linux).
//
//   BonusChestFeature::place            0x1b526b0
//   BonusChestFeature::place::$_0       0x1b528d0
//   BonusChestFeature::_place           0x1b529a0
//   BonusChestFeature::_getFacingDataId 0x1b52d70
//   BonusChestFeature::BonusChestFeature 0x1b3b760
//   LevelData::setBonusChestSpawned     0x1cd1fe0
//
// Verified against tests/reference/bonus_chest_bds_1.6.0.15.json.
//
// The feature seeds its own Random copy from the caller's seed (0x1b52da ->
// Random::Random 0x187e350), so the caller's stream is untouched. The corpus
// asserts that: every case records zero caller words.

#include "bds/world/bonus_chest.hpp"

namespace bds {
namespace {
std::function<void()> g_bonus_chest_spawned;  // LevelData::setBonusChestSpawned observer

// BonusChestFeature::_getFacingDataId 0x1b52d70. Arguments after the position
// are the X term (ecx) and the Z term (r8d). The native order of the tests is
// +Z, -Z, +X, -X, then a default of 2 (0x1b52fa4).
bool is_empty_at(const std::function<bool(DecorationRegion::Position)> &is_empty,
                 DecorationRegion::Position base, int dx, int dy, int dz) {
    return is_empty({std::get<0>(base) + dx, std::get<1>(base) + dy, std::get<2>(base) + dz});
}
} // namespace

void set_bonus_chest_spawned_observer(std::function<void()> observer) {
    g_bonus_chest_spawned = std::move(observer);
}

std::uint8_t bonus_chest_facing_data(const std::function<bool(DecorationRegion::Position)> &is_empty,
                                     DecorationRegion::Position at, int x_term, int z_term) {
    // 0x1b52d8e..0x1b52df2: z_term > 0 and the +Z neighbour is NOT empty -> 2.
    if (z_term > 0 && !is_empty_at(is_empty, at, 0, 0, 1))
        return 2;
    // 0x1b52df7..0x1b52e5b: z_term < 0 and the -Z neighbour is NOT empty -> 3.
    if (z_term < 0 && !is_empty_at(is_empty, at, 0, 0, -1))
        return 3;
    // 0x1b52e65..0x1b52ecf: x_term > 0 and the +X neighbour is NOT empty -> 4.
    if (x_term > 0 && !is_empty_at(is_empty, at, 1, 0, 0))
        return 4;
    // 0x1b52ed4..0x1b52f9a: x_term < 0 and the -X neighbour is NOT empty -> 5.
    if (x_term < 0 && !is_empty_at(is_empty, at, -1, 0, 0))
        return 5;
    // 0x1b52fa4: default.
    return 2;
}

namespace {
// BonusChestFeature::_place 0x1b529a0. `local` is the feature's own Random.
bool place_chest_and_torches(DecorationRegion &region, DecorationRegion::Position at,
                             Random &local, const BonusChestEnvironment &environment,
                             BonusChestBlock chest, BonusChestBlock torch) {
    (void)region;
    // 0x1b529ce..0x1b529db: the target must be empty.
    if (!environment.is_empty(at))
        return false;
    // 0x1b529fa: `place` always passes 0, 0, so the facing is the default 2.
    const auto facing = bonus_chest_facing_data(environment.is_empty, at, 0, 0);
    environment.place_block(at, BonusChestBlock{chest.name, facing});
    // 0x1b52a69..0x1b52abb: attach the loot table, seeded from the LOCAL Random's
    // nextInt() 0x187d470, which is `_genRandInt32() >> 1`. The corpus records
    // the RAW generator word (the breakpoint observes the generator epilogue),
    // so the seed handed to setLootTable is that word shifted right once.
    // The table name is the rodata literal at 0x283346c.
    if (environment.set_loot_table)
        environment.set_loot_table(at, static_cast<std::uint32_t>(local.next_int()));
    // 0x1b52ad2..0x1b52b2b builds east/west/north/south into slots -0x68/-0x80/
    // -0x98/-0xb0, but the checks then run in the order -0x80 (west), -0x68
    // (east), -0x98 (north), -0xb0 (south) at 0x1b52b42, 0x1b52bc1, 0x1b52c40
    // and 0x1b52cc8.
    const auto torch_at = [&](int dx, int dz) {
        const DecorationRegion::Position p{std::get<0>(at) + dx, std::get<1>(at),
                                           std::get<2>(at) + dz};
        if (!environment.is_empty(p))
            return;
        const DecorationRegion::Position below{std::get<0>(p), std::get<1>(p) - 1, std::get<2>(p)};
        if (!environment.is_considered_solid(below))
            return;
        environment.place_block(p, torch);
    };
    torch_at(-1, 0); // west
    torch_at(1, 0);  // east
    torch_at(0, -1); // north
    torch_at(0, 1);  // south
    return true;
}
} // namespace

bool place_bonus_chest(DecorationRegion &region, DecorationRegion::Position at, Random &random,
                       const BonusChestEnvironment &environment, BonusChestBlock chest,
                       BonusChestBlock torch) {
    // 0x1b526da..0x1b526e8: the feature seeds its OWN Random from the caller's
    // seed and uses only that copy afterwards. The caller's stream is therefore
    // untouched, which the corpus asserts.
    Random local(static_cast<std::uint32_t>(random.seed()));
    // 0x1b52714 / 0x1b5272b: two nested loops, each 3..10 inclusive, giving a
    // 7x7 grid of candidate columns.
    for (int dz = 3; dz <= 10; ++dz) {
        for (int dx = 3; dx <= 10; ++dx) {
            // 0x1b52742..0x1b52867 calls the $_0 lambda for the four corner
            // combinations in this native order: (+dx, +dz), (-dx, -dz),
            // (+dx, -dz), (-dx, +dz).
            for (const auto &[sx, sz] : {std::pair{1, 1}, {-1, -1}, {1, -1}, {-1, 1}}) {
                // 0x1b528ed..0x1b52915: BlockPos(origin.x + sx*dx, origin.y,
                // origin.z + sz*dz) fed to findNextTopSolidBlockUnder, which
                // mutates the position.
                DecorationRegion::Position target{std::get<0>(at) + sx * dx, std::get<1>(at),
                                                  std::get<2>(at) + sz * dz};
                if (!environment.find_top_solid_under(target))
                    continue;
                // 0x1b5292b..0x1b52931: the found cell's Y is incremented.
                ++std::get<1>(target);
                if (!place_chest_and_torches(region, target, local, environment, chest, torch))
                    continue;
                // 0x1b5295a..0x1b52973: LevelData::setBonusChestSpawned(true).
                if (g_bonus_chest_spawned)
                    g_bonus_chest_spawned();
                return true;
            }
        }
    }
    return false;
}
} // namespace bds
