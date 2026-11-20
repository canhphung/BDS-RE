# LakeFeature — Phase 3 component

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

## Native functions and translation

| Original | Reconstruction |
| --- | --- |
| Constructor `0x1b5dbb0` | Liquid target and air as the upper block |
| Two-block constructor `0x1b5dc10` | Liquid target and explicit upper block |
| `place` `0x1b5dc70` | `place_lake` in `src/world/decoration.cpp` |
| `_check` `0x1b5e980` | Unmarked mask cells adjacent to a marked cell along one of six axes |
| `Feature::_placeBlock` `0x1b6ebd0` | Primary-layer placement attempts, flags 3 |
| `BlockSource::getBrightnessPair` `0x1c5a560` | Caller-supplied sky-brightness reader |

The feature subtracts 8 from X/Z, descends through air in that column while
Y>0, then subtracts 4 from Y. It does **not** have a low-Y early-return guard
before drawing its shape. It draws `nextInt(4)+4` ellipsoids. Each draws three
float diameters `(nextFloat()*6+3, nextFloat()*4+2, nextFloat()*6+3)` followed
by three center coordinates. Preserve the actual Vec3 operations:

```
center.x = nextFloat() * (((16 - diameter.x) - 2) + 1 + diameter.x * 0.5)
center.y = nextFloat() * ((( 8 - diameter.y) - 4) + 2 + diameter.y * 0.5)
center.z = nextFloat() * (((16 - diameter.z) - 2) + 1 + diameter.z * 0.5)
```

The final offset is inside the random multiplier. Using a familiar lake
algorithm's different center formula would change the shape. All operations
are single precision; normalized squared distance is `(x*x+y*y)+z*z < 1`.
The 16x8x16 byte mask uses `(x*16+z)*8+y`; ellipsoids mark only X/Z=1..14,
Y=1..6. Every pass traverses X, then Z, then Y.

Before writing anything, all outside cells adjoining the mask are checked:
upper-half liquids reject the lake; lower-half nonsolid materials reject it
unless their **legacy name** matches the target (data does not matter).
Material solidity is not collision solidity or opacity: it must come from the
block registry. On success, marked lower-half cells become the liquid and
upper-half cells become the chosen upper block.

A second pass examines marked cells at local Y=4..7. Dirt directly below
becomes grass only when the current cell's sky component of the native
brightness pair is positive. This uses dirt's legacy identity, including
coarse dirt; there is no biome-specific mycelium choice here. The test's
brightness reader returns observed native lighting inputs, never an invented
constant or a recomputation from the expected result.

If the target's material is Lava (`MaterialType=6`), a final boundary pass
places stone into solid materials. Below local Y=4 there is no extra draw;
at Y>=4 it draws `nextInt(2)` **before** checking solidity and places stone
only for a nonzero draw. Even an already-stone cell is a placement attempt.
There is no water-freezing pass in this function.

## API boundary

`LakeEnvironment` supplies block materials, sky brightness, and an optional
observer of every placement attempt. Both readers are required: the component
does not silently guess missing registry or lighting information.
`DecorationRegion` retains changed cells; the observer also sees no-op writes
and failed writes beyond the 256-high region. This distinction matters for
flooded lakes, stone lava rims, and lakes above Y=255.

This component stops at feature placement. It does not execute neighbor
notifications, pending liquid ticks, or lighting jobs. `generate()` still ends
after carving; the full postProcess orchestration and runtime integration
remain separate work.

## Original-server oracle

`tools/re/gdb_lake_cases.py` calls the original two-block constructor and `place` in a
paused original server. A setup-only C helper loops over the original setter
and getter to initialize and snapshot inputs efficiently. No feature code, RNG,
condition, return value, shape, brightness-query implementation, or expected
output is patched.

An initial live-BlockSource capture demonstrated immediate still-to-flowing
liquid changes from neighbor events in **already Ready** chunks. That is not
the event-free feature stage being tested here. The component corpus therefore
constructs a separate original BlockSource with its supported constructor
flags `(true,true)` and attaches an original `BlockTickingQueue(Tick(1))`.
`shouldFireEvents` (`0x1c593f0`) suppresses events through the first flag.
Real `postProcess` constructs `(false,true)` over chunks not yet Ready, which
suppresses events through the chunk-state arm of the same native predicate.
The source flags and unexecuted pending queue are recorded in the fixture;
this is component verification, not proof of complete postProcess integration.

The setup clears both block layers through native setters before loading each
scene and verifies the extra layer is air, preventing waterlogging left by a
previous scene from contaminating the primary-layer oracle. The dark-dirt scene
initializes a zero sky-light snapshot through the original
`LevelChunk::_setLight` (`0x1de1700`); the feature still reads the actual native
`getBrightnessPair` result. Other scenes use the existing native lighting.

Input geometry includes buried and surface basins, existing liquid with
nonzero data, wrong-liquid/air floors, an upper-liquid leak, mixed material
solidity, the second constructor, descent through air, low Y, negative Y, and
a box spanning Y=251..258. Inputs and final snapshots are read from the native
BlockSource, including actual rejected out-of-height writes. Breakpoints record
the complete supplied Random stream, the generated mask, ordered setter calls
with flags, and the actual brightness queries/returned sky values. Eight
following native `nextInt()` results verify Random continuation.

To reproduce:

```sh
LD_LIBRARY_PATH=/workspace/bds-tools/debian/root/usr/lib/x86_64-linux-gnu \
 /workspace/bds-tools/bin/python tools/re/capture_level_random.py \
 --runtime input/reference-runtime/bds \
 --library-path input/reference-runtime/openssl-local/lib \
 --preload input/reference-runtime/ipv6_shim.so \
 --gdb /workspace/bds-tools/debian/root/usr/bin/gdb \
 --output input/reference/decoration-lake-new --port 19246 --ticks 10 \
 --timeout 600 --players A --gdb-post-script tools/re/gdb_lake_cases.py
python3 tools/re/export_lake_reference.py input/reference/decoration-lake-new \
 tests/reference/decoration_lake_bds_1.6.0.15.json
```

A host C compiler is needed for the setup helper. Captures and compiled helper
remain under ignored `input/`. The exporter rejects missing branches, incomplete
traces and a wrong binary hash. `lake_reference` checks the return, complete
Random word sequence, ordered placements, brightness-query order, every final
input-box cell and the eight continuation outputs.

## Verification result

The corpus contains **78 cases**: **54 accepted** and **24 rejected**. The
reconstruction matches **5,427 Random words**, **22,317 ordered placement
attempts**, **95 sky-brightness queries** (65 positive and 30 zero), all
**159,774 final snapshot cells**, and the eight subsequent Random outputs per
case. The attempt stream includes **125 out-of-height writes**. Every rejected
lake leaves its scene unchanged.

**48/48 tests pass** in GCC Debug, GCC Release, Clang and GCC ASan+UBSan,
including existing terrain, ore, water, gameplay, mob and protocol tests.
