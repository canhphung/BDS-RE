# Protocol-282 chunk streaming and spawn

Original ELF SHA256: `69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.
This is an independent implementation, not recovered original source.

## Evidence and wire format

- `FullChunkDataPacket::write` at `0x18c90a0`: ChunkPos then BinaryStream string.
  Coordinates are zigzag varints; string length is an unsigned varint.
- `SubChunk::serialize` at `0x1dff230`: format byte, layer-count byte, then storages.
  The captured format is 8; captured subchunks have one or two layers.
- `SubChunkBlockStorage::serialize` at `0x1e02250`, paletted 1-bit writer at
  `0x1e03db0`: storage header `(bits << 1) | 1` for network runtime IDs;
  little-endian packed uint32 words; zigzag palette count and runtime IDs.
  Supported widths are 1, 2, 3, 4, 5, 6, 8 and 16. A word contains
  floor(32 / bits) entries; entries never straddle word boundaries.
- `SubChunkBlockPos::index` at `0x1deb0c0`: `(x << 8) | (z << 4) | y`.
- `LevelChunk::serialize2DMaps` at `0x1de6280`: 512 raw height-map bytes,
  then 256 biome bytes. `ChunkBlockPos::index2D` at `0x1b3b2c0`, with its
  constructor at `0x1b43210`, establishes height-map order `(z << 4) | x`.
- After the maps: border count and optional block-entity network NBT. The
  codec retains this suffix verbatim when round-tripping trusted reference
  packets. It does not claim to parse NBT. The internal World emits a zero
  border count and no block entities; there is no extra zero varint appended.

The earlier 113-packet capture was local ignored input in another environment;
it was not available in this checkout. The same independent client and reference
workflow produced a new capture with **139** FullChunkData packets. Every packet
round-trips byte-for-byte, including two-layer storages and packets with additional
block-entity bytes. The C++ decoder's unpacked-cell digest also matches the
independent Python reader, not merely the original packed buffers.

`tests/reference/chunks_bds_1.6.0.15.zlib` contains unsigned-varint-length-prefixed
FullChunkData bodies compressed with zlib. Its provenance, SHA256 and independent
cell digest are in the neighboring JSON file. `tools/reference/export_chunk_corpus.py`
reproduces these files from a capture. No original executable or library is bundled.

## World and scheduling policy

`network_chunk` reads the simulation-owned internal World, maps its five named
block IDs to the captured vanilla runtime palette and encodes all sections up to
the highest occupied section. Missing sections are air. Supported internal blocks
are air, stone, grass, dirt and bedrock. Unsupported internal IDs disconnect that
session rather than silently substituting a block. Height maps use highest occupied
solid block + 1; biome is Plains. There is no original terrain generator, lighting
simulation, LevelDB import, liquid layer simulation or block-entity creation.

The initial chunk view is a bounded circle (radius 5..32), centered on the join
spawn and sorted nearest first. DiscoveryServer emits at most four chunks globally
per poll, rotating through connections, and pauses each connection at 64 pending
reliable datagrams. Each batch is compressed/encrypted once; RakNet retransmits the
identical encrypted bytes. Confirmed encryption handshakes lift the initial
30-second transport login deadline; the 15-second idle/retry limits remain.

## Spawn boundary

Additional startup packets encode AdventureSettings, game rules, the joining
player's UUID/name/signed skin profile, fourteen initial attributes, empty inventory,
armor/offhand/UI slots, hotbar, an empty creative catalog, empty recipes and an empty
command catalog. Empty catalogs reflect unimplemented gameplay systems; they are
not a reconstruction of all vanilla recipes or commands. Reference fixtures verify
AdventureSettings, rules, attributes, PlayerList and hotbar against original bytes.

`ServerPlayer::tickWorld` at `0x25f8b90` checks client-ticking offsets and respawn
readiness, then constructs PlayStatus 3 at `0x25f8cc0` / `0x25f8cc5`. Our capture
observed PlayerSpawn after 106 of 139 chunks. The independent implementation uses
an explicit readiness policy: send the nearest radius-five circle (81 chunks),
then PlayStatus 3 once; finish the requested outer view in the background. It does
not wait for a large view to finish before allowing initialization.

SetLocalPlayerAsInitialized (0x71, runtime ID as unsigned varint64) is accepted
only after PlayerSpawn and only for the joining runtime ID. It marks that connection
active; it does not enable unimplemented gameplay. This is stricter than the original
handler at `0x1827540`, which selects the player by network/sub-client identity.
Repeated matching initialization is idempotent; foreign or premature IDs close the
connection. Dynamic movement, changing the streamed view, entity replication,
multiplayer player-list broadcasts, inventory persistence and gameplay remain pending.

## Verification boundary

`network_chunks` checks all 139 original packet bodies, an independent digest of
all unpacked block cells, every palette width, negative coordinates, high sections,
invalid IDs, truncated packets and world mapping. `spawn_packets` compares startup
packets with the original. `auth` covers bounded streaming and initialization states.
`raknet_handshake` drives two encrypted joins over real UDP, checks persisted stone
and dirt blocks (including negative coordinates), receives 317 chunks per view,
checks PlayerSpawn and initialization, deliberately drops a chunk datagram, and
verifies retransmission preserves reliable ordering and CFB8 checksum state.

This establishes the protocol path with an independent client. No actual Minecraft
client has been run, so M4 real-client rendering/spawn remains UNVERIFIED.
