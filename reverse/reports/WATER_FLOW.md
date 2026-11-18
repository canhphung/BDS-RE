# Water propagation and currents — Linux BDS 1.6.0.15

Reference binary SHA-256:
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

## Reconstructed behavior

| Native function | Implementation / observed behavior |
| --- | --- |
| `LiquidBlockDynamic::_tick` `0x1e9dd10` | Ordinary Overworld water branch. Samples W/E/S/N, recomputes depth, takes water above into account, changes to static when unchanged, then prefers downward spread. Water consumes no RNG in this branch. |
| `_getHighest` `0x1e9eab0` | Counts depth-zero neighbors; falling depths 8–15 contribute rendered depth zero. |
| `_getSpread` `0x1e9ee70`, `_getSlopeDistance` `0x1e9f180` | W/E/N/S, all equal-cost directions selected, maximum search distance 4. Sentinel 1000 ties are retained; `_trySpreadTo` still rejects blocked destinations. |
| `_canSpreadTo` `0x1e9dbe0`, `_spread` `0x1e9d950` | Rejects unloaded/out-of-height targets, existing water, lava and blocking cells. New cells preserve the four-bit depth. Downward water gets depth+8 (falling depth is retained); horizontal flow from falling water starts at depth 1. |
| `_setStatic` `0x1e9eb60` | Changes flowing to still water without changing depth; flag 2. |
| `LiquidBlockStatic::_setDynamic` `0x1e3d330`, `BlockSource::updateNeighborsAt` `0x1c5b400` | W/E/S/N/U/D notifications wake still neighbors with flag 4, preserving depth and scheduling future ticks. |
| `LiquidBlock::getTickDelay` `0x1e8dcd0`, dynamic constructor `0x1e9d93a` | Scheduled delay 5 ticks; dynamic water also participates in random block ticking. Random ticks can therefore update water before its scheduled deadline. |
| `BlockTickingQueue::add` `0x1c51370`, `tickPendingTicks` `0x1c517b0`, `TickNextTickData::operator>` `0x1b2a300` | Duplicate entries are allowed. Deadlines use the column queue's last clock. At most 100 existing entries are moved into a second heap before execution; new entries wait. Equal deadlines/priority use the original libstdc++ heap behavior, with no insertion-order tie-break. A missing 8-block neighborhood postpones execution with delay zero. |
| `LiquidBlock::_getFlow` `0x1e8d8d0` | Samples native facing order N/E/S/W, sums rendered-depth gradients including water below adjacent air, then normalizes. This binary returns a horizontal vector even for falling water. |
| `handleEntityInside` `0x1e8d830`, `Level::checkAndHandleMaterial` `0x1c84fe0` | Adds half each cell's normal, normalizes their sum, then adds `0.014f` to actor motion along that direction. All comparisons in the golden fixture use exact float equality. |

The source-creation rule is not restricted to two horizontal sources. At
`0x1e9e2d5`, exactly one horizontal source plus a source above increments the
source count to two. A solid support or source below then permits depth zero.
The final downward spread can still occur after the current cell disappears;
the native `-1 + 8` result is preserved rather than replaced by an early return.

## Runtime integration

- Stable internal IDs 0–6 are unchanged. IDs 7–22 are flowing water depths
  0–15; IDs 23–37 are still water depths 1–15. These are block indices, not item
  indices. Both names/data are encoded through the original protocol-282 palette.
- `World::set_block` wakes neighbors synchronously, including edits caused by
  mining, placement and block ticks. `Server::tick` runs pending water after
  random block ticks and before actors in each active column. Water updates
  retain their native flags and order; flag-4 wakes are not sent as extra packets.
- Mob and item Actor phases apply the current before normal movement. Path
  collision, dismount water checks, spawn-ground scanning and skylight recognize
  every water depth. Cold-weather freezing and aquatic spawning are not supplied.
- State changes update the authoritative chunks and terrain edits. Opacity
  changes invalidate lighting; depth-only changes do not. FullChunkData on
  rejoin includes the current liquid state.
- Optional version-2 snapshot metadata stores queue clocks, duplicate entries
  and exact heap layouts. Decode validates positions, serials and heap ordering;
  loading blocks does not wake their neighbors. Earlier snapshots without the
  optional metadata remain readable; dynamic cells are scheduled on loading.
  This snapshot metadata is BDS-RE storage, not native BDS LevelDB/NBT.

## Evidence and reproduction

`tools/re/gdb_water_cases.py` calls the original functions in a paused, disposable
flat world. No original result, RNG draw or branch is replaced. The first part
records 78 independent depth/feed/falling/slope/obstacle scenes, native flow
vectors and `checkAndHandleMaterial` motion changes. The second part creates a
fresh walled arena in another column and calls its **real** native tick queue
for 120 consecutive ticks, removing the source before tick 61. It does not run
random block ticks during that controlled queue sequence.

```sh
LD_LIBRARY_PATH=/workspace/bds-tools/debian/root/usr/lib/x86_64-linux-gnu \
 /workspace/bds-tools/bin/python tools/re/capture_level_random.py \
 --runtime input/reference-runtime/bds \
 --library-path input/reference-runtime/openssl-local/lib \
 --preload input/reference-runtime/ipv6_shim.so \
 --gdb /workspace/bds-tools/debian/root/usr/bin/gdb \
 --output input/reference/water-flow-new --port 19230 --ticks 5 --players A \
 --gdb-script tools/re/gdb_mob_motion_oracle.py \
 --gdb-post-script tools/re/gdb_water_cases.py \
 --command 'summon cow 3.5 4 3.5'
python3 tools/re/export_water_reference.py input/reference/water-flow-new \
 tests/reference/water_bds_1.6.0.15.json
```

The exporter rejects incomplete traces and GDB Python exceptions and records
binary, trace, oracle and script hashes. `water_reference` compares every native
case and all 120 queue snapshots, including a save/reload at tick 37. Additional
tests exercise random ticking, source withdrawal, cross-column propagation,
lighting invalidation, packet flags/depths and FullChunkData after rejoin.

40/40 CTest tests pass in GCC Debug, GCC Release, GCC ASan+UBSan and Clang.
The age-exhaustion integration fixture now uses inert stone: its previous
unassigned index 7 is newly registered as flowing water and requires a future
tick deadline. The test still verifies three successful ticks, an in-loop
exception and an exact emergency snapshot.

## Boundaries

This translation covers the host's primary-layer water and full-cube palette in
a warm, clear Overworld. Partial shapes, waterlogging/extra layers, lava spread
and water/lava reactions, evaporation/freezing, bubble columns and waterfall
specific item-gravity behavior need their own native implementations. Bucket
items/transactions and a `setblock` command are not added by this change. Tests
create water through `World::set_block(position, water_block(depth))`; still
water remains still until a neighbor change wakes it, as in the original.

The existing host broadcasts block updates to active sessions rather than
reconstructing native per-viewer dimension packet filtering. No manual Minecraft
client play session is claimed by the automated packet/chunk checks.
