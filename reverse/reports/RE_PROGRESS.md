# Reverse engineering progress

Target SHA256: `69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

Verified ELF64 little-endian x86-64 ET_EXEC, entry `0x17f30b0`, dynamic linkage, `.symtab`/`.strtab` present; no `.debug_info` section. `nm -S -C --defined-only` indexed 207,639 sized defined symbols (functions and data, not a function count). Script output and database remain local; forty-five bounded functions have outgoing call-site metadata in `reverse/callgraphs/initial.json`.

Used GNU readelf/nm/objdump; no Ghidra decompilation or PDB analysis performed. No Windows archive available locally: attachment transfer rejected its size above 32 MiB. EXE/PDB match therefore UNKNOWN. No layouts taken from unverified PDBs.

Operational properties/discovery observations and login-body layout are recorded in OPERATIONS.md and LOGIN.md. These are focused static slices, not a claim of complete analysis of every mapped function or a successful authenticated client login.

RakNet 9 protocol byte, 1400 MTU cap, 20 internal addresses and frame/receipt layout have focused evidence in RAKNET.md. Source-path hints from the user are independently correlated against Linux symbols but have no independently verified Windows EXE/PDB provenance in this environment.
