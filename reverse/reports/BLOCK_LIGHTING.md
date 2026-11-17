# Block tick / lighting integration — 2026-11-17

Original Linux BDS 1.6.0.15 SHA256:
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

This checkpoint connects the translated GrassBlock algorithm to world lighting,
chunk scheduling, authoritative block mutation and the existing UpdateBlock path.
It is bounded to the host palette (air, stone, grass, ordinary dirt, bedrock),
clear weather and a non-freezing Overworld. It does not complete the mob/RNG
handoff or reproduce the original async relighting scheduler.

## Function evidence and translation

| Original function/site | Recorded/translated behavior |
|---|---|
| Block::getLight 0x1e68a90; getLightEmission 0x1e68b80; shouldTick 0x1e68b30 | Live registry values for the five supported blocks. Air opacity 0, other four 15; emission 0; only grass ticks. |
| BlockLegacy::canPropagateBrightness 0x1c639e0; material query 0x1e68cf0 | All five have neighbor-propagation flag false and no liquid material; air translucency 1, other four 0. |
| LevelChunk::recomputeHeightMap 0x1de9520 | Direct-sky initialization scans down until a primary/extra opacity blocker. The current palette has no extra-layer state. |
| LevelChunk::_recalcHeight 0x1de1bf0 | Old height is masked to 8 bits (0x1de1c7a); stored height is clamped (0x1de1eb4); direct-sky writes use a half-open interval (0x1de1f46..0x1de2037). |
| SubChunkRelighter::_propagateLight 0x1e208c0 | Six neighbor updates from independent sky/block channels. |
| _checkAndAddToQueue 0x1e20a10 | Attenuation is max(1, primary opacity, extra opacity), clamp to zero, only raise/enqueue channels that increase. |
| runPropagation 0x1e20820 | Iterate a growing queue to exhaustion. |
| LevelChunk::getBrightness 0x1de2f80; getBrightness(...,darken) 0x1de3060; getRawBrightness 0x1de3100 | Read sky/block pair; saturating subtraction on sky; combined result is max(sky,block). |
| BlockSource::getRawBrightness 0x1c5a670 | Opacity 15 returns zero before bounds checks; neighbor-propagating blocks query above/west/east/south/north with recursion disabled; negative Y returns zero. Missing chunk returns 15, even at night. Y >= 256 subtracts sky darkening even with the darken argument false. |
| BlockSource::_getSkyDarken 0x1c5aae0 | Server source uses the dimension's cached old sky-darkening byte. |
| Dimension::getTimeOfDay(int,float) 0x1d688e0; getOldSkyDarken 0x1d64f30 | Clear-sky time calculation uses single-precision stages and the mce cosine table, not the newer float-returning getSkyDarken's cosf. Night darkening is 11. |
| LevelChunk::tickBlocks 0x1de5040 | Clear-weather nextInt(100000), weather-column LCG step, nextInt(16), then trunc(2.5 * allocated subchunk span) random positions. Signed negative Y remains negative. Dispatch primary tick then extra-layer tick. |
| GrassBlock::tick 0x1e716a0 | Existing translation now queries live host light/material/block state and mutates world data. |
| LevelChunk::tick 0x1de4501 / 0x1de451d / 0x1de452d | Original spawner -> despawn -> block ticks -> actors. Runtime now inserts block ticks before actors; spawner/despawn remain missing. |

Float constants were read from ELF load segments, including 0.25, 24000, 3,
11, 0.5 and PI (0x40490fdb), rather than inferred from decimal descriptions.

### Highest-block removal

A fresh native scene places stone at (0,255,0), then removes it through console
commands. The measured pair at that now-air position is **(14,0)**, not (15,0).
The old height is 255; the direct-sky update ends before that old height, so
Y=255 is not reseeded. Lateral propagation then supplies 14. The translated chunk
heightmap/direct-sky seeds preserve this history. Rebuilding light solely from
the final geometry would fail this original case. The allocated block span also
remains 256, yielding 40 samples per tick after the high stone is removed.

## Original-server observations

`capture_level_random.py --lighting-oracle` runs console scene commands normally,
waits 25 initialized ticks, then invokes native functions in the paused original.
The existing explicit flat-generator input is recorded. Lighting, materials,
branch outputs and mutation results are not patched. The five fresh runs are:

- `input/reference/lighting-day-v2`: fixed time 6000, roof, sealed stone room,
  and a stone tunnel across chunk boundaries.
- `input/reference/lighting-night-v1`: identical structures at fixed time 18000.
- `input/reference/lighting-opened-v1`: holes opened in roof, room wall and
  tunnel roof before the normal tick settling period.
- `input/reference/lighting-spread-v2`: daylight structures plus a dirt patch
  at (9,3,7)..(15,3,11), with 2,048 native function calls to exercise grass spread.
- `input/reference/lighting-tall-removed-v1`: original roof/room/tunnel, plus
  the Y=255 stone placement/removal.

Each records 2,029 actual sky/block pairs and raw brightness with darkening both
enabled and disabled. Inputs include negative coordinates, solid positions,
closed/open cavities, lateral gradients, Y=-1/255/256/300 and a missing chunk.
The five fixtures retain **10,145 pair samples and 20,290 raw values**. Each also
records 247 native clear-sky time samples, including dusk/dawn boundaries.

For native tickBlocks calls, the original level Random is saved and seeded with
0..127 in four runs and 0..2047 in the spread run, then restored. This supplies
explicit function inputs, not substituted RNG returns. The process-global LCG is observed and allowed to advance naturally
across calls. The original light/material/world providers are used, actual grass
mutations accumulate, and every raw Random word plus LCG boundary and mutation
request is retained. The host test supplies the same scene and actual initial
grass/dirt states, then executes real lighting + block sampling + GrassBlock
against the same seeds/initial LCG. It compares every word and mutation, not
just totals. **All 2,560 native function cases match**, including 2-, 3- and
5-word calls and 15 actual mutations: 12 dirt conversions and 3 grass spreads.

`export_lighting_reference.py` checks the raw trace SHA256/completion and oracle
provenance before exporting `tests/reference/lighting_*_bds_1.6.0.15.json`.
Expected results are native observations; the exporter does not run BDS-RE to
calculate them. Raw worlds, binary and GDB logs remain ignored under `input/`.

## Runtime integration and explicit boundaries

`Server::tick` calls `tick_clear_chunk` once for each scheduled loaded chunk,
before its actor snapshot. It passes the real level Random, a separate evolving
block-coordinate LCG and the translated clear-sky time result. Grass changes the
authoritative world through flags 3 and sends the existing UpdateBlock encoding.
The runtime test runs 512 ticks with covered grass and checks dirt mutations and
corresponding authoritative update packets. This is a wiring regression check;
new native client packet timing/recipient equivalence is not claimed here.

`WorldLighting`'s cache/job adapter is **INFERRED**, explicitly marked in code:
it computes the stable light field synchronously, using the translated seeds
and propagation rules. Each cached column depends on its 3x3 loaded-column
geometry revisions; a 16-block margin exceeds the 15-level attenuation range.
Only its central column is cached. This avoids rebuilding every distant column
while view streaming changes the world's cache. Grass/dirt changes have equal
opacity and do not invalidate lighting. Maximal direct-sky pairs return
immediately: the original lighting invariant preserves 15 and the supported
palette emits no block light. Propagation uses a dense integer-indexed queue
within the 48-wide footprint; opaque ground without cavities requires no
propagation work. Actual air/opaque edits and chunk cache changes do invalidate dependent fields. The opened-scene test warms the cache
before removals. This does not reproduce native pending-job/transient timing.

Remaining boundaries:

- No rain, lightning actors, freezing, snow, biome weather effects, liquid/extra
  layers, emissive/translucent blocks, grass path or coarse dirt in the host.
  The native neighbor-propagation branch is translated but the current registry
  contains no block that exercises it.
- No nether/end or boss attenuation. Runtime clock still follows its existing
  world age; the separate original time/daylight-cycle command/state machinery
  is not reconstructed by this change.
- Original relight job order, subchunk allocation side effects, missing-neighbor
  transition states, brightness-change listeners and light persistence across
  the host snapshot reload remain unverified. The internal snapshot does not
  retain allocated-empty spans or lighting update history; procedural cache
  eviction/re-generation can also lose that history.
- No spawner/despawn/AI/season/save consumers are padded or replayed. The whole
  level RNG sequence and later player shuffle still diverge; combat sorting
  remains in place until that larger integration is verified.

## Validation

GCC 14.2 Debug, GCC ASan+UBSan, GCC Release and Clang 19.1 Release each
passed **31/31 CTest** locally after the final integration. This includes
the five native lighting/tick fixtures, the runtime mutation/update check and
all earlier movement, item, combat and encrypted network references.
No native Windows or remote CI result is claimed for this checkpoint.
