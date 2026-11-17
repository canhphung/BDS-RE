# Progress — 2026-11-16

2026-11-17 random-tick/mob handoff: **IN PROGRESS** on
`claude/clever-newton-bboqgm`. A 200-initialized-tick original-server trace now
records every level RNG word and caller. The first checkpoint adds a draw
observer and reference-tested LevelChunk random coordinate selection (32
groups, 672 positions). The next checkpoint translates GrassBlock::tick against
160 original day/night dependency/RNG cases. A third checkpoint moves item
ticking into the per-player owner-chunk scheduler, including the deferred owner
transfer and nearby scan order (five original frames). Full block ticks, spawn/despawn, mobs and
strict combat packet order are not implemented by this checkpoint. See
[evidence and next integration requirements](../reverse/reports/RANDOM_TICKS.md).

| Slice | Status | Scope |
|---|---|---|
| M0 bootstrap | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | CMake, CLI, logger, lifecycle, native executable; Linux GCC/Clang and Windows MSVC Debug/Release build and CTest pass in native CI at 43d46b6 |
| M1 runtime | RESEARCHED, IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | Twenty-five focused function maps indexed; monotonic independent runtime with bounded catch-up; update-to-level dispatch traced; full startup still incomplete |
| M2 world core | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | Lazy chunk columns and sections, negative coordinates, block placement/removal and age; versioned internal snapshot persistence with restart/corruption checks; no Bedrock world-format or client compatibility |
| M3 transport foundation | RESEARCHED, IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | IPv4/IPv6 discovery (single-family fallback), RakNet 9 open/connected handshake, ACK/NACK, bounded retries, duplicate filtering, ordering/sequencing and fragment reassembly; 0xfe zlib game-packet batches, packet headers, evidence-generated packet id table, LoginPacket version gate (PlayStatus + Disconnect reasons), ES384 certificate-chain authentication (Mojang root, self-signed when online-mode is off, nbf/exp validation, client-data token). ServerToClientHandshake with ECDH P-384 and AES-256-CFB8 session encryption with checksums, through PlayStatus LoginSuccess; whitelist admission (UUID/XUID, XUID fill-in) the empty resource pack exchange through Completed; StartGame, SetSpawnPosition, SetTime, SetDifficulty, SetCommandsEnabled and ChunkRadiusUpdated (encoders matched byte-for-byte to an independent encoder). Verified with independently signed fixtures and an independent Python client; no genuine Xbox Live chain tested; the user later confirmed the self-signed join path on Minecraft 1.6.0.14. StartGame byte-identical to the original given the same values; full 4,493-state palette from a reference run. Initial chunk streaming and spawn initialization are implemented in M4 below |
| M4 protocol path | IMPLEMENTED, reference fixtures and independent-client integration tested on Linux and Windows | FullChunkData v8 with runtime palettes; 139 original chunk packets match byte-for-byte; startup attributes, permissions, profile and empty gameplay catalogs; bounded streaming, PlayerSpawn and matching runtime-ID initialization. User-confirmed basic join/spawn on Minecraft 1.6.0.14 after native timestamp correction; broader client/rendering scenarios remain unverified |
| M5 basic gameplay | IMPLEMENTED, unit and two-client UDP tested on Linux/Windows | Seeded flat/rolling terrain, dynamic views, movement and player visibility, simple block break/place, 36 inventory slots/cursor and atomic persistent edits/player state; see GAMEPLAY.md. Actual-client gameplay validation pending |
| M6–M8 / broader gameplay | Pending | Full vanilla generation, physics, tools, containers, recipes, mobs and broader differential checks |

The user confirmed the basic M4 self-signed join/spawn path with Minecraft 1.6.0.14. This does not verify the new gameplay or broader interoperability. Since 2026-11-16 the original server is executed locally for reference runs (tools/reference); RakNet, batching, self-signed login, the encryption handshake, the resource pack exchange and StartGame are DIFFERENTIAL_VERIFIED against it with an independent client (details in tools/reference/README.md). Static findings and reference observations are documented separately; no claim of recovered original source code.

Operational slice implemented: server.properties parser, worlds/<level-name> selection, polled console stop, whitelist/operator JSON and commands, runtime-only changesetting and asynchronous save hold/query/resume for internal snapshots. See OPERATIONS.md for exact boundaries. Whitelist admission is enforced before the encrypted handshake; trusted operators override default gameplay permissions. Storage is not Bedrock LevelDB; backup compatibility is limited to the documented lifecycle, not original database files.

Next: actual Minecraft validation of the basic gameplay slice, then full terrain/physics, tool/item support, containers and recipes. Local GCC Debug/Release and ASan+UBSan pass 21/21 tests, including two encrypted clients and restart persistence. Native [CI run 43893499034](https://github.com/canhphung/BDS-RE/actions/runs/43893499034) at `b13a0e4` passed all seven jobs, with 21/21 tests in both Windows configurations; details in BUILD_STATUS.md.

User-supplied source paths are indexed locally as heuristic navigation hints (docs/SOURCE_MAPPING.md), not recovered source or independently verified Windows PDB types. The transport slice now passes all seven jobs in native CI run 43843720476 at 4cdd95e, with 13/13 CTest explicitly confirmed in both Windows Debug and Release logs.
