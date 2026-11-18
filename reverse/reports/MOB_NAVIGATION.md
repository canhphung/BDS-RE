# Mob navigation, water, riding and movement effects

Continuation of `2593094` and `reverse/reports/MOB_MOTION.md`.
Original Linux BDS **1.6.0.15**, SHA256
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.
These are translations of the named functions, checked by executing that binary;
they are not Minecraft Java algorithms or prerecorded decision playback.

## Implemented

| Original function | Translation and integration |
|---|---|
| `PathFinder::findPath` `0x1c72780` | A* with the original Euclidean float costs, strict comparisons, 200-expansion limit and nearest partial-path result. Stable nodes and the original binary heap tie order (equal children choose the right child). |
| `getNeighbors` `0x1c72cb0`, `getNode` `0x1c73670`, `_isFree` `0x1c73b70` | Walker evaluation for the host's full-cube blocks, actor footprint/height, one-block ascent and fewer than four blocks of descent. South/west/east/north expansion; no invented diagonal edges. Water/lava cells are avoided by the walker evaluator. |
| `PathNavigation` `0x231c240`, `0x231c5f0`, `0x231c960`, `0x231cc80` | Path creation, replacement only when nodes differ, preserved old path on failure, width-dependent node centers, same-height advancement, direct-path shortcutting and the native 100-tick stuck check. `closeToDone` measures squared distance to the final block center. |
| `MoveControl::tick` `0x23310e0` | Gravity-bearing animal branch: 30-degree default turn limit, minimum distance/turn radius, speed modifier times movement attribute and conditional jump request. |
| `JumpControl::jump/tick` `0x2336480` / `0x23364c0` | One-tick request handoff and clearing; runtime grounded jump velocity, ten-tick delay, jump boost and liquid jump impulse. |
| `RandomStrollGoal` `0x23c46f0..0x23c4b02` | Enabled in the server: own-actor RNG destination search, navigation start/continue/stop, partial-end retry once per distinct end node. The level RNG remains separate. |
| `Mob::newServerAiStep` `0x22bad00` | Runtime order: goals, navigation, move control, look control, jump control; then jump handling, damped input, travel, pushing and body control. |
| `Mob::travel` `0x22b6320..0x22b66f6`, `Actor::isFree` `0x21a6aa0` | Water acceleration, drag, gravity and collision escape at the water edge. Liquid cells no longer behave as cube colliders. The registry's real water opacity is **2**, lava opacity **14** and lava emission **15**. |
| `FloatGoal` `0x233af70` / `0x233b050` | Liquid precondition, 0.8 probability jump request using level RNG, and submerged-rider removal. |
| `RideableComponent::positionRider` `0x248f470`, `Vec3::yRot` `0x1d768b0` | Pack seat positions, actor scale, libm rotation and the player's `heightOffset - 0.4` riding-height correction. |
| Ridden branch of `Mob::travel` `0x22b5d5d..0x22b62ef` | WASD turn interpolation, half strafe, quarter reverse, movement attribute, mounted step height, air control and charged jump impulse. |
| `Mob::onPlayerJump` `0x22c0c10` | Charge scaling, including the threshold at 90. |
| `Actor::_exitRide` `0x21af210` | Native view/box-center-based exit direction, three vertical placement attempts and box-center fallback; default distance 1.6 from `Actor::Actor` `0x21a08d2`. |
| Movement effects in `Mob::travel` and `jumpFromGround` | Speed/slowness use the existing attribute-modifier implementation; jump boost, levitation and slow falling use the original arithmetic. Effect durations, modifier removal and particle color state are shared with players. |

Server integration includes horse/donkey mounting through use-on-actor,
`PlayerInput`, `RiderJump`, sneak/Interact dismount, rider cleanup on leave/death,
`SetActorLink`, seat metadata and links for newly observing clients. Tamed and
saddled state is supplied through `Server::configure_mount`; wild adults may
be mounted but do not gain WASD control. `Server::give_effect` accepts the five
movement effects for mobs (IDs 1, 2, 8, 24, 27).

The actor now has its own RNG. Definition events and RandomPos no longer share
the host thread RNG. Collision dimensions are scaled for babies rather than
using the adult box. Baby growth over time remains outside this change.

## Native evidence

The four JSON fixtures contain only observed inputs/outputs plus hashes and
commands. `tools/re/make_locomotion_reference.py` checks the original binary
hash, trace hash, completion footer and GDB Python errors before exporting.
Raw binaries/worlds/captures remain ignored under `input/`.

- `navigation_reference`: 40 paths in flat, wall, gap, step and enclosed scenes;
  compares every returned node and every expansion/neighbour list. Also 30
  direct control cases and **300 retained navigation/travel ticks**.
- `walking_ai_reference`: **200 retained cow AI/travel ticks**, including an
  actual stroll start and completion; **513 level/actor RNG words**. Neither
  RNG is reseeded or padded between steps. Positions, motion and look angles
  are compared with exact float equality.
- `mob_environment_reference`: 36 native water/effect calls, including deep
  water, a wall, escape at the surface, transition back to air, levitation and
  slow falling; also compares the native liquid registry.
- `riding_reference`: 42 native cases covering seat rotations, forward/reverse/strafe, turn
  interpolation, charge scaling, powered jump and exit placement.

The retained AI oracle explicitly reproduces the caller's `posPrev2` write
before each AI step. The player is placed beyond look-at range so this scene
isolates wandering. Earlier exploratory captures that omitted those inputs are
not golden fixtures. No native method result, search branch or RNG output is
patched.

Re-export the committed corpus using the example in
`tools/re/make_locomotion_reference.py`. Run all tests with:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

## Scope and remaining parity work

This is not complete BDS gameplay parity. Navigation currently evaluates the
host's cube palette: door/fence/hazard evaluators, flying/swimming navigation,
restrictions and other navigation subclasses are not translated. Water physics
was initially implemented for placed still-liquid cells. The subsequent
[water-flow translation](WATER_FLOW.md) adds propagation and native currents.
Waterlogging, Depth Strider equipment integration and gliding/ladder travel
remain separate work. The optional lava travel branch is not certified by this
corpus: the sequential native lava scene did not enter that branch and was
explicitly excluded from physics evidence.

The taming/equipment UI and the other mount AI goals (including wild-horse
bucking and pig item control) are not supplied by this physics change. Mob
movement state is integrated with the existing replication path; a real client
play session after these changes has not been run. Footstep/particle movement
sounds are not part of the potion-effect translation. Existing inferred head/
eye attachment initialization remains marked in `mob_ai.cpp`; this change does
not claim to have located that initializer in the binary.

## Validation

39/39 CTest tests pass in GCC Debug, GCC ASan+UBSan, GCC Release and Clang.
The gameplay disconnect test now reads all bounded packet batches and identifies
exactly the departing player's unique ID. The lighting integration test drains
all batches and compares only its explicitly covered grass area; unrelated sheep
grazing is no longer counted as a mutation of that fixture. Native expected
values and comparison tolerances were not relaxed.
