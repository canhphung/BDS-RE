# Progress — 2026-11-16

| Slice | Status | Scope |
|---|---|---|
| M0 bootstrap | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | CMake, CLI, logger, lifecycle, native executable; Linux GCC/Clang and Windows MSVC Debug/Release build and CTest pass in native CI at ce383cd |
| M1 runtime | RESEARCHED, IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | Eighteen focused function maps indexed; monotonic independent runtime with bounded catch-up; update-to-level dispatch traced; full startup still incomplete |
| M2 world core | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | Lazy chunk columns and sections, negative coordinates, block placement/removal and age; versioned internal snapshot persistence with restart/corruption checks; no Bedrock world-format or client compatibility |
| M3 offline discovery | RESEARCHED, IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux | Configured IPv4/IPv6 UDP ping/pong and advertisement; full RakNet reliability/connection/login pending |
| M4–M8 | Not implemented | Real-client spawn, gameplay and differential verification pending |

No feature is REAL_CLIENT_VERIFIED or DIFFERENTIAL_VERIFIED. The proprietary server was not executed. Findings concern static binary evidence only; no claim of recovered original source code.

Operational slice implemented: server.properties parser, worlds/<level-name> selection, polled console stop, whitelist/operator JSON and commands, runtime-only changesetting and asynchronous save hold/query/resume for internal snapshots. See OPERATIONS.md for exact boundaries. Access lists do not yet enforce connected sessions. Storage is not Bedrock LevelDB; backup compatibility is limited to the documented lifecycle, not original database files.

Next: full RakNet connection/reliability, protocol-282 login/authentication and world streaming, then networked gameplay. Client connection/spawn and gameplay remain unimplemented and unverified. Current native CI evidence for the earlier commit does not validate these new changes until a new CI run passes.
