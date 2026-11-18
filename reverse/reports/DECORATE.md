# OverworldDecorator::decorate — structure and blocker

Reference: unchanged Linux BDS 1.6.0.15, SHA-256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.
`OverworldDecorator::decorate(BlockSource*, Random&, Biome*, BlockPos const&,
bool, float)` `0x1da5250`, size `0x18f2`.

Status: **traced structure only; not translatable yet.** This documents why, so
the work is not re-traced next time.

## Calling convention

The register assignment at the prologue (`0x1da525b..0x1da5277`) is:

| Register | Argument |
|---|---|
| `rdi` | `this` (the `OverworldDecorator`) |
| `rsi` | `BlockSource*` |
| `rdx` | `Random&` |
| `rcx` | `Biome*` |
| `r8` | `BlockPos const&` |
| `r9b` | `bool` (stored at `-0x29`) |
| `xmm0` | `float` (stored at `-0x30`) |

## Observed structure

1. `0x1da5280..0x1da52bf`: a virtual call through `this->vtable[1]`
   (`mov (%rcx),%rdx; mov 0x8(%rdx),%rdx; call *%r8`), passing the
   `BlockSource`, the `Biome`, the origin and the `Random`. This is the
   biome-decorator dispatch that selects which feature set to use.
2. `0x1da52c2..0x1da53ae`: a loop bounded by `this->0x204`, one iteration per
   decoration attempt. Each iteration draws `nextInt(16)` twice against the
   origin's X and Z, calls `BlockSource::getAboveTopSolidBlock`
   `0x1c61a20` for the surface height, then places a feature.
3. `0x1da53b3..0x1da54a4`: a second loop bounded by `this->0x208`, structurally
   identical but reading its feature pointer from `this + 0x8` instead of
   `this + 0x10`.
4. `0x1da54a4` onward: a long sequence with `Random::nextFloat`,
   `_getRandomTreePosition` `0x1da50c0`, `BlockSource::getHeightmapPos`,
   `BlockSource::getMaterial`, `BlockPos::offset`, and
   `TallGrassFeature` (both the constructor `0x1b66370` and
   `place(...,int,int)` `0x1b663c0`), plus one `FlowerFeature::placeFlower`
   `0x1b5f230`.

`TallGrassFeature` is the only feature whose class the function names
directly (`VanillaBlocks::mTallgrass`, `BlockState::MappedType` and
`VanillaBiomes::mPlains` are the only other rodata references). Everything
else is reached through feature pointers held in the decorator/biome objects
and invoked as `Feature::place` virtuals.

## The blocker

Six virtual `call *reg` sites and roughly two dozen member offsets
(`0x118`, `0x11c`, `0x120`, `0x124`, `0x128`, `0x138`, `0x13c`, `0x140`,
`0x144`, `0x148`, `0x158`, `0x164`, `0x168`, `0x178`, `0x180`, `0x184`,
`0x188`, `0x18c`, `0x190`, `0x1a0`, ...) describe a large decorator object
graph: per-biome feature lists, tree/plant/marine/sub-feature slots, and the
`Biome::Decorator` vtable that decides which of them run.

Those features are exactly the ones **not yet translated**:

- trees: `TreeFeature` `0x1b5a230/0x1b5a290` and the 8 subclasses
  (`FancyTreeFeature` `0x1b4aff0`, `SwampTreeFeature` `0x1b6bcb0`,
  `SavannaTreeFeature` `0x1b511c0`, `JungleTreeFeature` `0x1b65db0`,
  `MegaTreeFeature` `0x1b55c70`, `MegaPineTreeFeature` `0x1b4d740`,
  `MegaJungleTreeFeature` `0x1b533e0`, `RoofTreeFeature` `0x1b5b7e0`), plus
  `getTreeFeature` per biome;
- marine: `KelpFeature` `0x1b53de0`, `SeagrassFeature` `0x1b6d090`,
  `CoralFeature` `0x1b676f0`, `IceSpikeFeature`;
- snow/ice covering. **Correction**: the names `SNOW_CUTOFF` and `SNOW_SCALE`
  that an earlier revision of this file attributed to `0x283bc00`/`0x283bc04`
  are **not** real symbols. Those addresses are `OverworldDecorator`'s
  constructor locals (`dirtSize` is at `0x283bc08`, `gravelSize` at
  `0x283bc0c`, and `0x283bc00`/`0x283bc04` are the floats 0.5 and 0.3 used by
  that same constructor). There is also **no `SnowFeature`**: `nm` shows
  `AddSnowLayer::fillArea 0x1d442b0` (a biome LAYER, already translated in
  `src/world/biome_layers.cpp` and present in the chain at salt 2) and
  `TopSnowBlock::buildSnowBlock 0x1e5d0d0`, plus `BlueIceFeature::place
  0x1b4d150` and `IcebergFeature::place 0x1b63830`. So surface snow comes from
  the biome layer and the cold biome's surface block (`ice_plains` carries
  `"snow:0"` as its top block in `biome_data.cpp`), not from a decorator
  feature. Any remaining work is making cold biomes appear and mapping the
  `snow`/`ice`/`packed_ice` names onto host ids — not translating a snow
  feature.
- `ClayFeature` `0x1b557f0`;
- the remaining biome `Decorator` overrides.

Translating `decorate` first would mean writing calls into features that do not
exist, or stubbing them — and a stub changes the Random stream, which is the
exact failure mode every corpus in this project is built to catch. So the order
has to stay: features first, then the dispatcher.

## Status of the cold-biome path

The translated generator already emits `snow` where a cold biome's surface
block is `"snow:0"`, but `internal_id` does not map `snow`, `ice` or
`packed_ice` yet, so those cells currently become air. Seed 123 places no cold
biome within 8 chunks of the origin (its biome ids are 1,3,4,6,7,18,27,132,134,
155,156 and none is `BiomeClass::Ice`), which is why a scan finds no snow: that
is correct behaviour, not a missing feature.

The concrete next step is therefore to map the three cold block names in
`internal_id` and its palette, and to pick a seed that does contain an ice biome
so a test can assert snow appears.

## What can be done next without the blocker

- Read the `OverworldDecorator` constructor `0x1da31e0` (0x1801 bytes) to
  enumerate member offsets to feature names. That is static work that turns the
  six virtual call sites into named targets and is a prerequisite for both the
  translation and its oracle.
- Translate `ClayFeature` `0x1b557f0` and `IceSpikeFeature`, which are
  self-contained and do not need the decorator graph.
- Translate the tree family, starting with `TreeFeature::place(...,int)`
  `0x1b5a290`, `_prepareSpawn` `0x1b59e30` and `_placeTrunk` `0x1b5ac20`.

## Correction to the plan

Step 3 of the working plan ("translate `OverworldDecorator::decorate`") cannot
precede step 4 ("translate trees, marine, snow/ice"). The dependency runs the
other way, because `decorate` is mostly a table of calls into those features.
The practical order is: feature components (step 4), `OverworldDecorator`
constructor member map, then `decorate`.
