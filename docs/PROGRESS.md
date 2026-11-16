# Progress — 2026-11-16

| Slice | Status | Scope |
|---|---|---|
| M0 bootstrap | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux | CMake, CLI, logger, lifecycle, native executable; Windows CI configured but unrun, so M0 cross-platform acceptance is pending |
| M1 runtime | RESEARCHED, IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux | Thirteen original functions inspected; monotonic independent runtime with bounded catch-up; update-to-level dispatch traced; full startup still incomplete |
| M2 world core | IMPLEMENTED, UNIT_TESTED, INTEGRATION_TESTED on Linux | Lazy chunk columns and sections, negative coordinates, block placement/removal and age; versioned internal snapshot persistence with restart/corruption checks; no Bedrock world-format or client compatibility |
| M3 network research | RESEARCHED | Static protocol constant 282 and default ports 19132/19133 verified from ELF; no networking implementation |
| M4–M8 | Not implemented | Real-client spawn, gameplay and differential verification pending |

No feature is REAL_CLIENT_VERIFIED or DIFFERENTIAL_VERIFIED. The proprietary server was not executed. Findings concern static binary evidence only; no claim of recovered original source code.

Next vertical slice: trace offline discovery wire format using verified protocol constant 282 before discovery/login implementation. Original LevelDB and network chunk formats remain unimplemented.
