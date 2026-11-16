# Progress — 2026-11-16

| Slice | Status | Scope |
|---|---|---|
| M0 bootstrap | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | CMake, CLI, logger, lifecycle, native executable; Linux GCC/Clang and Windows MSVC Debug/Release build and CTest pass in native CI at 43d46b6 |
| M1 runtime | RESEARCHED, IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | Twenty-five focused function maps indexed; monotonic independent runtime with bounded catch-up; update-to-level dispatch traced; full startup still incomplete |
| M2 world core | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | Lazy chunk columns and sections, negative coordinates, block placement/removal and age; versioned internal snapshot persistence with restart/corruption checks; no Bedrock world-format or client compatibility |
| M3 transport foundation | RESEARCHED, IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | IPv4/IPv6 discovery (single-family fallback), RakNet 9 open/connected handshake, ACK/NACK, bounded retries, duplicate filtering, ordering/sequencing and fragment reassembly; 0xfe zlib game-packet batches, packet headers, evidence-generated packet id table, LoginPacket version gate (PlayStatus) and explicit Disconnect for unauthenticated logins. Batching slice verified locally on Linux; Windows awaits CI. Authentication pending |
| M4–M8 | Not implemented | Real-client spawn, gameplay and differential verification pending |

No feature is REAL_CLIENT_VERIFIED or DIFFERENTIAL_VERIFIED. The proprietary server was not executed. Findings concern static binary evidence only; no claim of recovered original source code.

Operational slice implemented: server.properties parser, worlds/<level-name> selection, polled console stop, whitelist/operator JSON and commands, runtime-only changesetting and asynchronous save hold/query/resume for internal snapshots. See OPERATIONS.md for exact boundaries. Access lists do not yet enforce connected sessions. Storage is not Bedrock LevelDB; backup compatibility is limited to the documented lifecycle, not original database files.

Next: protocol-282 login authentication (certificate chain verification, ServerToClientHandshake/encryption), then ResourcePacks, StartGame and world streaming, then networked gameplay; original congestion-control and broader client interoperability remain unverified. Client connection/spawn and gameplay remain unimplemented and unverified. The current slice passes all seven jobs in native CI run 43839417910 at 43d46b6, including Windows MSVC Debug/Release with 11/11 CTest each.

User-supplied source paths are indexed locally as heuristic navigation hints (docs/SOURCE_MAPPING.md), not recovered source or independently verified Windows PDB types. The transport slice now passes all seven jobs in native CI run 43843720476 at 4cdd95e, with 13/13 CTest explicitly confirmed in both Windows Debug and Release logs.
