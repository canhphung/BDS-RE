# Experimental gameplay (protocol 282)

The server now supports a small playable block world: terrain, authenticated
player movement and visibility, block breaking/placement, and a 36-slot player
inventory plus cursor. This is an independent implementation. It does not reproduce
all Bedrock gameplay or the original terrain generator. The existing terrain,
movement, mining and inventory algorithms are experimental, not a completed
reconstruction of BDS. Known mismatches include mining delays, direct inventory
drops and full inventory responses to successful predicted transactions.

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
In Survival, break grass/dirt to obtain dirt directly in inventory and place it;
placement consumes one item. Bare-hand stone breaking yields no item. Bedrock
cannot be broken in Survival. Tools, normal item drops/entities, ores, liquids,
biomes, trees/caves, recipes/crafting, chests, armor, damage, hunger and mobs are
not implemented. Mining has basic independent timing/reach checks, not a complete
reconstruction of original hardness/tool rules.

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
