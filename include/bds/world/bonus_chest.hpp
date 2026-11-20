#pragma once
#include "bds/world/decoration.hpp"
#include "bds/world/overworld.hpp"
#include <cstdint>
#include <cstring>
#include <functional>

namespace bds {
// BonusChestFeature translated from BDS 1.6.0.15 (Linux).
//
//   BonusChestFeature::place          0x1b526b0
//   BonusChestFeature::$_0            0x1b528d0
//   BonusChestFeature::_place         0x1b529a0
//   BonusChestFeature::_getFacingDataId 0x1b52d70
//   BonusChestFeature::BonusChestFeature 0x1b3b760
//
// Verified against tests/reference/bonus_chest_bds_1.6.0.15.json.
//
// The feature seeds its OWN Random copy from the caller's seed, so it consumes
// nothing from the caller's stream. The corpus asserts that explicitly.

// A block identity as the original's `Block const&` carries it.
struct BonusChestBlock {
    const char *name = "air";
    std::uint16_t data = 0;
    bool operator==(const BonusChestBlock &other) const {
        return data == other.data && std::strcmp(name, other.name) == 0;
    }
};

struct BonusChestEnvironment {
    // BlockSource::findNextTopSolidBlockUnder 0x1c61410. The native signature
    // takes the position by reference and mutates it to the found cell; the
    // host answers with the found position and whether one was found.
    std::function<bool(DecorationRegion::Position &)> find_top_solid_under;
    // BlockSource::isEmptyBlock 0x1c60cb0.
    std::function<bool(DecorationRegion::Position)> is_empty;
    // BlockSource::isConsideredSolidBlock 0x1c5af40.
    std::function<bool(DecorationRegion::Position)> is_considered_solid;
    // Feature::_placeBlock 0x1b6ebd0.
    std::function<void(DecorationRegion::Position, BonusChestBlock)> place_block;
    // BlockSource::getBlockEntity 0x1c5b8e0 plus
    // RandomizableBlockActorContainerBase::setLootTable 0x1f015e0: the host
    // reports whether a container exists and receives the loot seed.
    std::function<void(DecorationRegion::Position, std::uint32_t loot_seed)> set_loot_table;
};

// The loot table literal the original passes, from rodata 0x283346c.
inline constexpr const char *bonus_chest_loot_table = "loot_tables/chests/spawn_bonus_chest.json";

// LevelData::setBonusChestSpawned 0x1cd1fe0, called when a chest was placed.
// A test installs an observer here; the runtime will point it at the level data.
void set_bonus_chest_spawned_observer(std::function<void()> observer);

// BonusChestFeature::place 0x1b526b0. Returns true when a chest was placed.
bool place_bonus_chest(DecorationRegion &, DecorationRegion::Position, Random &,
                       const BonusChestEnvironment &, BonusChestBlock chest = {"chest", 0},
                       BonusChestBlock torch = {"torch", 0});

// BonusChestFeature::_getFacingDataId 0x1b52d70, exposed for a direct test.
std::uint8_t bonus_chest_facing_data(const std::function<bool(DecorationRegion::Position)> &is_empty,
                                     DecorationRegion::Position, int x_term, int z_term);
} // namespace bds
