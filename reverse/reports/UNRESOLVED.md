# Unresolved

- Windows ZIP exceeds attachment transfer limit (32 MiB); matching EXE/PDB GUID+Age not inspected. A split archive or smaller individual artifacts would unblock inspection.
- Native Windows execution unavailable; CI configured but not dispatched. Do not mark Windows build passed.
- Clang and Ghidra absent locally; GCC and binutils available. Clang CI configured; local Clang tests unrun.
- Complete bootstrap and timer time-synchronization semantics; update-to-level dispatch is now traced through GameSession and ServerLevel primary vtable.
- Complete callers, remaining indirect dispatch targets, RTTI and inheritance validation, full class layouts.
- Worker timing observed statically; original runtime/differential tests not executed.
- Protocol constant verified as 282; discovery/login wire formats and RakNet behavior, login, chunk serialization, original LevelDB persistence, entities and gameplay. Independent internal chunk storage and snapshots are implemented; they do not establish original format compatibility.
- No real client or captures supplied; no compatibility evidence.
