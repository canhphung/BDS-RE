# Progress — 2026-11-16

| Slice | Status | Scope |
|---|---|---|
| M0 bootstrap | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | CMake, CLI, logger, lifecycle, native executable; Linux GCC/Clang and Windows MSVC Debug/Release build and CTest pass in native CI at ce383cd |
| M1 runtime | RESEARCHED, IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | Thirteen original functions inspected; monotonic independent runtime with bounded catch-up; update-to-level dispatch traced; full startup still incomplete |
| M2 world core | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux and Windows | Lazy chunk columns and sections, negative coordinates, block placement/removal and age; versioned internal snapshot persistence with restart/corruption checks; no Bedrock world-format or client compatibility |
| M3 network research | RESEARCHED | Static protocol constant 282 and default ports 19132/19133 verified from ELF; no networking implementation |
| M4–M8 | Not implemented | Real-client spawn, gameplay and differential verification pending |

No feature is REAL_CLIENT_VERIFIED or DIFFERENTIAL_VERIFIED. The proprietary server was not executed. Findings concern static binary evidence only; no claim of recovered original source code.

Next vertical slice (revised after reading the original dedicated-server guide): implement server.properties settings and original world-directory selection, then console stop. The operational compatibility contract and remaining command/storage gaps are tracked in COMPATIBILITY.md. Protocol 282 discovery follows these interfaces; original storage and network chunk formats remain unimplemented.

Current CLI settings and internal snapshots are development harness features, not BDS-compatible configuration or world saves. Their passing tests do not establish operational compatibility.
