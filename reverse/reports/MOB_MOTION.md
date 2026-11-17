# Mob motion and random destinations — 2026-11-17

Navigation/water/riding follow-up: see [MOB_NAVIGATION.md](MOB_NAVIGATION.md).
The limitations below describe the earlier `2593094` checkpoint.

Builds on the user's `4e2f630` stationary AI checkpoint. Original Linux binary
SHA256 `69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.
This checkpoint adds land/air motion and non-riding mob push to the runtime,
plus a reference-tested RandomPos destination function. It does **not** enable
RandomStroll or complete navigation/locomotion.

## Functions translated / shared

| Original | Implementation / scope |
|---|---|
| Actor::moveRelative 0x21b0880 | `move_relative`: normalize only above the native 1e-4 squared threshold, clamp normalization length to 1; yaw uses double libm sin/cos rounded to float, not mce's table. |
| Mob::calcMoveRelativeSpeed 0x22b5886..0x22b5a4f | Ordinary land friction normalization; explicit evaluated ground-speed input; air speed input from native +0x1278. |
| Mob::travel 0x22b6f64..0x22b7430 | Ordinary land/air acceleration, friction sampled before movement, Actor::move, gravity -0.08, vertical damping 0.98 and final horizontal friction. No liquid, ladder, glide, rider or effect branches. |
| Mob::applyFinalFriction 0x22b7560 | Horizontal multiplication by sampled friction times native +0x1274 modifier. |
| Actor::move 0x21a6b50 | Existing item collision implementation now accepts shared `ActorMotion`; adds the cube-shape auto-step branch 0x21a7bb6..0x21a8675 and slide-offset subtraction/0.4 decay. |
| Actor::push(Actor&,bool) 0x21b1280 | Non-riding pushable pair: sqrt(max(abs(dx),abs(dz))), capped inverse, 0.05 impulse, self pushthrough factor and one-way flag. Does not draw RNG. |
| Mob::pushEntities 0x22bfa60 / BlockSource::fetchEntities 0x1c5bc60 | Runtime mob-only candidates: 0.2 horizontal AABB growth, server chunk bounds padded by 2, X/Z/chunk SmallSet iteration; invoke the other mob's push(this,false). No player/item/rider/vehicle branches. |
| RandomPos::getPos / generateRandomPos 0x231b5e0 / 0x231b620 | `random_stroll_position`: navigation present, no restriction/direction, land animals; ten candidates, own-Random order Z/Y/X with exclusive upper bounds. Not yet wired into the goal. |
| RandomPos::snapToGround 0x231bc40 | Initial solid/air transition succeeds at the original Y; downward/upward scans retain the binary's distinct Y+1 behavior and truncation of mob Y for scan bounds. |
| Animal::_getWalkTargetValue 0x22d2960 | Grass below scores 10, otherwise actual brightness ramp minus 0.5; strict score improvement retains the first equal candidate. No magma in the host palette. |

Constants were read from ELF load segments: air friction `0x3f68f5c3`,
ground fallback `0x3f0bc6a9`, gravity `0xbda3d70a`, damping `0x3f7ae148`,
ground sampling offset **0.1**, and best-score initial value -99999 at
0x2855c50. Native mob snapshots contain `maxAutoStep=0.5625`, heightOffset 0,
friction modifier 1 and air control speed 0.02. Item defaults remain unchanged.

The step matrix caught a precision/ordering distinction: slide offset adds the
**net rise after clipping back down**, not the attempted 0.5625 ascent. A 0.5
step produces slide 0.5, position Y still at the previous height while the box
has risen, then stored slide 0.2 after the native 0.4 decay. All are compared.

## Original-server evidence

The extra GDB probe records natural `Mob::travel`, its nested `Actor::move`,
moveRelative input/speed, actor position/motion/AABB/collision state, and both
sides of `Actor::push`. It observes original functions without modifying AI,
movement, RNG or collision outputs. Native bool storage may contain upper bits
(e.g. stuck-in-collider byte 116); consumers compare the logical low bit, just
as the binary's `test $1` does.

`mob_motion_bds_1.6.0.15.json` retains **647 native function cases**:

- 448 natural travel calls (and their nested move outputs) from a settled flat
  scene with a stone wall and summoned overlapping cows.
- 111 original pair-push calls, including both actors' before/after state.
- 48 explicit Actor::move inputs with six heights, four horizontal deltas and
  both on-ground values. Four successfully auto-step onto the real stone cube.
- 40 consecutive native Mob::travel calls retaining motion through a free fall
  from Y=12, landing and standing.

The scene wall is created by console after client initialization. Earlier
natural travel calls therefore did not see the same geometry: the fixture uses
travel calls after 25 settling ticks, rather than pretending the wall existed
from process startup. Push function inputs do not depend on that geometry, so
all observed push calls are retained. Raw startup calls remain in ignored input.

The explicit matrix calls real Actor::setPos and BlockSource::setBlock, and sets
explicit actor motion/ground/slide/collision inputs in the paused original.
Collision flag 46 is enabled via Actor::setStatusFlag. No decision or result is
overridden. Tests compare float values exactly for position, motion, box,
penetration and slide, plus all relevant collision flags.

`random_pos_bds_1.6.0.15.json` contains **96 native RandomPos::getPos calls**:
three original actor positions, each with own-Random seeds 0..31. The original
actor Random is saved/restored; inputs are seeded, outputs are never replaced.
Every case consumes 30 original RNG words: **2,880 words** total. The 64 successful
destinations and 32 failures match, including negative coordinates and a search
from Y=12 that cannot reach the floor. The fixture supplies the real scene's
dirt edits and stone cube; the clear first-day world uses darkening 0.

Raw runs: `input/reference/mob-motion-v3` (natural/push) and `mob-motion-v6`
(explicit matrix/fall/RandomPos). The exporter validates complete level trace
hashes and retains hashes of the function oracle files. Expected results are
native observations, not outputs calculated with BDS-RE. Early exploratory runs
with an overlapping probe-output variable / missing post-oracle actor registry
are not used for references.

## Runtime and remaining boundaries

`MobActor` and ItemActor share ActorMotion collision state. Spawned mobs receive
the definition-sized box, zero height offset and observed 0.5625 auto-step value.
`Server::tick_mob` runs the existing goals/look, land/air travel with zero control
input until navigation exists, then mob push and body control. Standing vertical
motion becomes the original **-0.07840000092983246**. Existing replication reads
actual position/motion; this is not a claim of new native packet-timing coverage.
Mob owner changes now use the deferred dimension queue, like items; unloaded
actors do not run physics.

Unfinished:

- RandomStroll remains disabled after its gate. The verified RandomPos helper
  is not enough to enable it: PathFinder, PathNavigation, MoveControl and
  JumpControl still need translation and integrated original comparisons.
- The natural travel test supplies the original evaluated control speed. It
  verifies travel/collision with those inputs, not how AI produced them.
- Species-specific aiStep/fall modifiers, liquids, ladders, gliding, mounts,
  effects, hard collidable actor shapes and mob fall damage/death are absent.
- Push actor enumeration follows the server's static chunk/SmallSet algorithm;
  the fixture checks the pair function, not a whole runtime pair-order trace.
  Player/item/rider/vehicle push branches remain absent.
- Definition dimensions/step defaults for every baby/species combination are
  not an exhaustive native registry dump. The host still uses the existing
  definition/scale handling from the previous checkpoint.
- Native actor NBT transfer/save/reload and the full level RNG trace remain
  unfinished. No RNG count padding or recorded-runtime RNG playback is added.

## Validation

GCC 14.2 Debug, GCC ASan+UBSan, GCC Release and Clang 19.1 Release each passed
**35/35 CTest** locally, including the existing 783-step stationary AI reference,
spawning, item collision and all encrypted network tests. The native step
matrix contains four successful auto-steps; the original item fixtures are
unchanged. No native Windows or remote CI result is claimed here.
