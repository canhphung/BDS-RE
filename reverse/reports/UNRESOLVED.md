# Unresolved

- Windows ZIP exceeds attachment transfer limit (32 MiB); matching EXE/PDB GUID+Age not inspected. A split archive or smaller individual artifacts would unblock inspection.
- Native Windows Debug/Release build and CTest now pass in CI run 43835644043 at ce383cd. Local Windows execution remains unavailable; real-client Windows verification is still pending.
- Clang and Ghidra absent locally; GCC and binutils available. Linux Clang Debug/Release build and CTest now pass in CI run 43835644043; local Clang tests remain unrun.
- Complete bootstrap and timer time-synchronization semantics; update-to-level dispatch is now traced through GameSession and ServerLevel primary vtable.
- Complete callers, remaining indirect dispatch targets, RTTI and inheritance validation, full class layouts.
- Worker timing observed statically; original runtime/differential tests not executed.
- Protocol constant verified as 282; discovery/login wire formats and RakNet behavior, login, chunk serialization, original LevelDB persistence, entities and gameplay. Independent internal chunk storage and snapshots are implemented; they do not establish original format compatibility.
- No real client or captures supplied; no compatibility evidence.
