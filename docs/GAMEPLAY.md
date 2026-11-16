# Experimental gameplay (protocol 282)

The server now supports a small playable block world: terrain, authenticated
player movement and visibility, block breaking/placement, and a 36-slot player
inventory plus cursor. This is an independent implementation. It does not reproduce
all Bedrock gameplay or the original terrain generator. The existing terrain,
movement, mining and inventory algorithms are experimental, not a completed
reconstruction of BDS. Movement and block breaking (timing, reach, crack events)
and dropped items (ItemActor physics, replication and pickup) are now translated
from the binary; a known remaining mismatch is the full inventory response to
successful predicted transactions.

The ItemUse held-item/selected-slot rejection branch has now been traced in the
original binary and compared with 37 original-server cases. It validates all
three ItemUse actions and resends authoritative blocks, inventory and hotbar on
mismatch without resetting movement. See the [bounded audit](../reverse/reports/ITEM_USE.md)
for exact coverage and outstanding differences.

## Try locally

Update and rebuild, then use a new level name to see a clean generated world:

```properties
level-name=Gameplay Test
level-type=FLAT
level-seed=123
gamemode=creative
online-mode=false
white-list=false
view-distance=8
```

`FLAT` creates bedrock, two dirt layers and grass; player feet spawn at y=4.
`DEFAULT` creates deterministic rolling stone/dirt/grass terrain from the seed.
Numeric seeds use their unsigned value; other strings use the documented FNV-1a
hash in runtime.cpp. The seed and generator are persisted; changing properties
later does not regenerate the existing world. `LEGACY` currently keeps explicit
snapshot columns without procedural generation; it is not the original finite
world generator. Original BDS LevelDB directories are still refused.

In Creative, the creative inventory contains stone, grass, dirt and bedrock.
Select one, place it, break it, and check the changes from a second client.
In Survival, break grass/dirt: a dirt item entity drops, falls, and is picked up
when you walk over it; place it; placement consumes one item. Bare-hand stone breaking yields no item. Tools,
normal item drops/entities, ores, liquids, biomes, trees/caves, recipes/crafting,
chests, armor, damage, hunger and mobs are not implemented.

## Block breaking

Translated from the original (ItemUseInventoryTransaction::handle 0x1ae6290,
GameMode::destroyBlock 0x1ab4a80, PlayerAction handler 0x182d140) and checked
against the original server with tools/reference/capture_break_oracle.py
(tests/reference/break_bds_1.6.0.15.json):

- A block is broken by InventoryTransaction ItemUse action 2. There is no
  destroy-time gate: StartBreak is not required and elapsed time is never checked.
  PlayerAction 13 does not break blocks (it belongs to dimension changes).
- After the held-item check (error 3), Actor::distanceTo(Vec3(BlockPos)) > 12.5
  (player position to the block's minimum corner) is error 7; both errors send
  resendBlocksAroundArea plus the inventory and hotbar.
- Success sets air, broadcasts UpdateBlock and LevelEvent 2001 (block runtime id at
  the block centre) and sends PlayerAction StopBreak to the breaker. Like the
  original, bedrock breaks through this path in Survival.
- StartBreak broadcasts LevelEvent 3600 with floor(65535 × rate) to every player,
  where rate = speed / hardness / (always destroyable ? 30 : 100), speed is 1 for
  block items and an empty hand, divided by 5 when not on ground, and 0 for
  negative hardness. Abort/StopBreak broadcast LevelEvent 3601. Creative players
  send no crack event.
- Adventure and Visitor players are refused with resendBlocksAroundArea (permission
  check reduced to these two settings, INFERRED).
- Drops are item entities (next section).

## Dropped items

Translated from ItemActor and Actor in the original and replayed bit for bit
against gdb traces of the original (tests/reference/item_actor*_bds_1.6.0.15.json,
test `item_actor_reference`; trace tool tools/reference/trace_item_actor.py):

- Block::playerDestroy -> spawnResources -> BlockLegacy::popResource: the item
  appears at block + 0.15 + 0.7 x nextFloat per axis (Level random), with motion
  (±0.1, 0.2, ±0.1) from the actor constructor, pickup delay 10 and lifetime 6000.
  Both random sources are seeded from std::random_device, like the original.
- Every server tick, ItemActor::normalTick: pushOutOfBlocks, gravity 0.04,
  Actor::move with the original collision order and AABB::clipCollide (including
  overlap push-out and the stuck-in-collider flag), air friction 0.98 and ground
  friction 0.6 x 0.98; merging with matching items within 0.5 every 25 ticks;
  removal below y = 0 or at the end of the lifetime.
- Replication: SetActorData then AddItemActor after the first tick (metadata as
  captured), MoveActorDelta with float-bit deltas when isDifferenceSignificant
  holds (the onGround flag comes from the previously sent state, as in the
  original), SetActorMotion when the motion changed by more than 0.2, ActorEvent 69
  on merge, RemoveActor on removal. Packets go to every active player (the
  original sends only to players viewing the chunk).
- Pickup: Player::aiStep touches items within the player box grown by (1, 0.5, 1);
  with no pickup delay and a slot that can take it, Player::take fills the first
  unfinished stack or first empty slot, sends the InventoryTransaction (world
  action, then container actions) to the player and broadcasts TakeItemActor.
- Not modelled: saving item entities with the world, fall distance, lava/water/fire, unique-id high bits from the level's
  world start count (BDS-RE uses one counter for runtime and unique ids).

Move/split/merge the supported block stacks between inventory and cursor. Closing
the inventory returns cursor items to available inventory space. If full, remaining
cursor items are retained, including across restart. Unsupported NBT/items,
crafting/container and drop transactions are rejected with authoritative inventory
state; they are not treated as successful actions. Adventure and Visitor players
cannot change blocks. Trusted operator membership overrides the default player
permission; self-signed clients cannot claim an operator XUID.

## Movement and synchronization

MovePlayer is translated from the original handler (ServerNetworkHandler::handle
0x182bcf0): a packet for another runtime ID is ignored, Y is clamped to [-128, 512]
with mce::Math::clamp (NaN becomes -128), position and rotation are taken as sent in
every mode, and the received packet is relayed unchanged to other players. Like the
original, there is no speed, distance or collision validation and no correction is
sent (ServerPlayer::checkMovementStats only feeds telemetry). One documented safety
deviation: packets with non-finite or |coordinate| > 1e9 X/Z are ignored, because
BDS-RE converts them to chunk coordinates. The earlier invented motion budgets,
collision rejection and coordinate limits were removed. Riding and dimension changes
do not exist yet, so those branches of the original are not reachable. Verified with
tools/reference/movement_oracle.py against the original (test `movement_reference`). Other initialized players receive AddPlayer/PlayerList,
movement, held-item updates and RemoveActor/PlayerList removal on disconnect.

The streamed chunk circle follows the accepted player position and radius, bounded
to 5..32 and view-distance. Pending view coordinates are replaced when the center
changes; leaving/re-entering a view resends needed chunks. Existing reliable-window
and send-budget bounds apply. Movement broadcasts coalesce by actor. Per-player
pending gameplay queues are bounded; a slow peer that overflows must reconnect to
resynchronize. Capacity and duplicate persistent player identities are checked
before StartGame.

## Persistence

Version-2 snapshots atomically include terrain configuration, explicit legacy
columns, block edits (including air tombstones), player positions, selected slots,
inventories and cursor stacks. Generated terrain is reproducible and is not stored
as millions of block records. A 256-column procedural cache is disposable. Legacy
non-air columns loaded from old snapshots remain complete explicit columns, so
existing structures are preserved. Empty legacy-column markers and removed terrain
survive cache eviction and restart. There are at most 100,000 terrain edits, 1,024
stored players and 16 MiB of metadata per snapshot.

The runtime autosaves every five seconds while networking runs, and saves on
`stop`, handled signals and the existing emergency-save path. Backup hold/query
captures the same version-2 state, including inventories, in one snapshot. See
[PERSISTENCE.md](PERSISTENCE.md) for framing and compatibility. Older builds cannot
read version 2; this build continues to read version 1.

## Verification

`item_use_reference` replays the 37 original-server cases for held-item/selected-slot
validation. Responses match after translating UpdateBlock runtime IDs through the
captured and advertised StartGame palettes; other response bytes/order must match.
This is original-server evidence for that branch only. The other tests below
check the independent implementation's own policies.

`gameplay` tests terrain determinism, sparse generation/restart, tombstones and
legacy preservation, Creative and Survival changes, Visitor denial, duplicate
identity refusal, cursor transfer/close/persistence, atomic inventory rejection,
movement ownership/collision/NaN/speed checks, visibility and disconnects.

`gameplay_udp` uses two independent encrypted protocol clients against the real
server process. It checks mutual AddPlayer, movement broadcasts, new chunks after
crossing a boundary, Survival break/place, inventory/cursor updates, rejection of a
stack-creation attempt, stop/restart, restored position/inventory and edited chunk
contents. These are independent-client integration checks, not a claim that the
new gameplay has been verified with the actual Minecraft client yet.

The earlier M4 join path was confirmed by the user with Minecraft 1.6.0.14 after
the native timestamp fix (Login, StartGame, PlayerSpawn and initialization). That
observation does not establish verification of this new gameplay code.
