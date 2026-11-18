# Overworld decoration — Phase 3 progress

Reference: Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

## Implemented

`DecorationRegion` supplies primary-layer blocks over multiple **256-high**
columns. Surface generation produces a 128-high BlockVolume, but decoration
reads/writes the LevelChunks behind a BlockSource and can cross column edges
or extend above Y=127. Loading a surface volume copies it into the lower half.
Out-of-height/unloaded reads return air and writes fail. Mutations retain their
order, block data and flags for differential testing and later integration.

| Native code | Reconstruction |
| --- | --- |
| `OreFeature` constructor `0x1b58e40` | Target block, integer size, float size and reciprocal |
| `OreFeature::place` `0x1b58eb0` | Native sine-table endpoints, single-precision interpolation, varying spherical radius, strict distance comparisons, X/Z/Y traversal |
| `OreFeature::_isDiggable` `0x1b596f0` | All stone data variants and netherrack; an already-identical target is skipped |
| `Feature::_placeBlock` `0x1b6ebd0` | Ordered primary-layer writes with flags 3 |
| `decorateDepthSpan` `0x1da6b50` | Draw Z, Y in the half-open depth span, then X; invoke the feature before selecting the next position |
| `decorateDepthAverage` `0x1da6c90` | Draw Z, two Y terms, then X; used for lapis's triangular depth distribution |
| `OverworldDecorator::decorateOres` `0x1da6de0` | Exact feature order, counts, heights and the rare extra-gravel branch |
| `MesaBiome::Decorator::decorateOres` `0x1dc9ad0` | Default sequence followed by 20 gold attempts in Y `[32,80)` |

The original returns true from `OreFeature::place` even if no blocks changed.
The implemented API accepts feature sizes 1–64; the native registry used here
has sizes 7–33, and the individual feature corpus additionally covers 1 and 3.
The sine/cosine calls use `mce::Math`'s table, not libc trigonometry. Interpolation
multiplies by the stored float reciprocal after multiplying by the step index.

The default sequence is:

| Target | Vein size | Attempts | Depth |
| --- | ---: | ---: | --- |
| Dirt | 33 | 10 | `[0,128)` |
| Gravel | 33 | 8 | `[0,128)` |
| Extra gravel, only when `nextInt(16)==0` | 33 | 80 | `[0,50)` |
| Diorite, granite, andesite, in this order | 33 | 10 each | `[0,80)` |
| Coal | 17 | 20 | `[0,128)` |
| Iron | 9 | 20 | `[0,64)` |
| Gold | 9 | 2 | `[0,32)` |
| Redstone | 8 | 8 | `[0,16)` |
| Diamond | 8 | 1 | `[0,16)` |
| Lapis | 7 | 1 | `nextInt(16)+nextInt(16)` |

LakeFeature is also translated and independently verified; see [LAKES.md](LAKES.md).
Its water/lava carving, boundary checks, sky-light-dependent grass and lava
stone rims are available through the explicit `place_lake` API.

## Native verification

`gdb_ore_cases.py` executes real constructors, `OreFeature::place`, and the
Plains/Mesa `decorateOres` methods in a paused original server. A console-filled
stone arena spans negative coordinates, chunk boundaries and Y=8–80. Mixed
feature scenes add air, dirt, bedrock, stone variants, netherrack and previously
identical targets. Observer breakpoints record the entire supplied Random's
word stream, every attempted feature position/target/size and every ordered
native block write, including its flags. Eight following `nextInt` outputs
verify continuation rather than merely matching a final block count.

The corpus holds **64 individual features**, **16 complete decorator batches**,
**54,520 Random words**, and **81,540 ordered block writes**. Seed 7 enters the
rare extra-gravel branch, yielding 180 default attempts / 200 Mesa attempts.
The arena also contains actual stone in Mesa's extra-gold height range, so that
branch is checked by real writes, not just its calls or Random consumption.

Large scene resets use a small, separately compiled C helper that loops over
the **original `BlockSource::setBlock`** with flag 0. This only speeds up arena
input restoration. No native feature, random function, condition or expected
output is replaced. The helper source/library remain under ignored `input/`.
The capture therefore needs a host C compiler as well as GDB with Python.

```sh
LD_LIBRARY_PATH=/workspace/bds-tools/debian/root/usr/lib/x86_64-linux-gnu \
 /workspace/bds-tools/bin/python tools/re/capture_level_random.py \
 --runtime input/reference-runtime/bds \
 --library-path input/reference-runtime/openssl-local/lib \
 --preload input/reference-runtime/ipv6_shim.so \
 --gdb /workspace/bds-tools/debian/root/usr/bin/gdb \
 --output input/reference/decoration-ore-new --port 19244 --ticks 10 \
 --timeout 600 --players A --gdb-post-script tools/re/gdb_ore_cases.py \
 --command 'fill -16 8 -16 15 28 15 stone' \
 --command 'fill -16 29 -16 15 49 15 stone' \
 --command 'fill -16 50 -16 15 70 15 stone' \
 --command 'fill -16 71 -16 15 80 15 stone'
python3 tools/re/export_decoration_reference.py input/reference/decoration-ore-new \
 tests/reference/decoration_ore_bds_1.6.0.15.json
```

The exporter rejects an incomplete trace, a wrong binary/hash, GDB Python
exceptions and an empty/incomplete feature corpus. Expected values are copied
from native observations. `decoration_reference` compares all words, calls,
writes and continuation outputs exactly; there is no RNG padding.

48/48 CTest tests pass in GCC Debug, GCC Release, GCC ASan+UBSan and Clang.
The existing terrain, water, gameplay, mob and protocol corpora remain enabled.

## Still pending

The implemented ore and lake components are **not full `postProcess`**. The generator's
own post-processing lock/view ownership, per-stage reseeding, lake-selection calls, structure
calls, monster rooms, other biome decorator overrides, trees/plants, marine
features, snow/ice covering, and the final native pending-tick pass remain.
The latter can settle gravel after decoration; the fixture deliberately checks
the ore phase before that pass. Extra layers, block entities and runtime
lighting are not supplied by `DecorationRegion`.

The current `generate()` still ends after carving; calling `decorate_ores` is
explicit. No incomplete decorator is silently called as a complete world
generator, and no runtime integration or Minecraft play test is claimed here.
