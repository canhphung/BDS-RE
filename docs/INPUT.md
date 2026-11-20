# Input tree reconstruction

The proprietary original binaries, libCrypto, PDBs, reference packs and every raw
capture live in the gitignored `input/` directory (`.gitignore` line 1). This
document records how that tree is rebuilt from the two supplied archives, what
each artefact is, and which claims are now independently verified. Nothing under
`input/` is committed, and nothing here is recovered source code.

Reproduce everything with:

```sh
BDS_LINUX_ZIP=/path/BDS-Linux.zip BDS_WINDOWS_ZIP=/path/BDS-windows.zip \
    tools/re/rebuild_input.sh
tools/re/rebuild_input.sh --verify      # checks the pinned hashes only
```

## Supplied archives

| Archive | SHA256 | Size |
|---|---|---:|
| `BDS-Linux.zip` | `aafdef8e5d711abb56814273108cc00febcd54cac1dca4d942a2c169dd158e07` | 21,928,697 |
| `BDS-windows.zip` | `182902f542756389fb7c849c7d1768680cf3c5d9824dc09d3976acdfa95eeb1e` | 42,216,524 |

Both unpack to 1,159 entries (1,117 files) with the same four data directories,
`server.properties`, `bedrock_server_how_to.html` and an empty
`release-notes.txt`. `rebuild_input.sh` verifies the ZIP CRC of every entry
before extraction and then checks the two hashes the tools depend on.

## Layout produced

| Path | Contents | Git |
|---|---|---|
| `input/linux/bds/` | Complete Linux 1.6.0.15 distribution | ignored |
| `input/windows/bds/` | Complete Windows 1.6.0.15 distribution (EXE + PDB) | ignored |
| `input/reference-runtime/bds/` | Symlinks into `input/linux/bds`; the `$BDS_RT` source of `tools/re/env.sh` | ignored |
| `input/reference-runtime/openssl-local/` | OpenSSL 1.0.2u built shared (`libssl.so.1.0.0`, `libcrypto.so.1.0.0`) | ignored |
| `input/reference-runtime/ipv6_shim.so` | Built from the tracked `tools/reference/ipv6_shim.c` | ignored |
| `input/reference/` | Raw oracle/capture output (joints, draws, chunk corpus) | ignored |
| `input/windows/source_mapping/` | PDB-derived module/source navigation metadata | ignored |
| `reverse/symbols/generated/linux.sqlite` | 207,639-symbol index from `reverse/scripts/index_elf.py` | ignored |

## Verified artefacts

| Artefact | SHA256 | Size |
|---|---|---:|
| `input/linux/bds/bedrock_server` | `69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad` | 68,868,344 |
| `input/linux/bds/libCrypto.so` | `a9fb4397dd236e34e583f9d5cff5a00d0bda3fbdfd1c12245072ba2f23b5d034` | 199,312 |
| `input/windows/bds/bedrock_server.exe` | `a36f050d622c520f795dc45764c43eb237ef5666eec641107d4c37ba33650461` | 13,427,200 |
| `input/windows/bds/bedrock_server.pdb` | `4ac304b50bbe76d9643793ecf73737432f56f3bc07afea5ccbdae170b405df19` | 137,097,216 |

The Linux server hash matches the target used by every RE report
(`index_elf.py` refuses any other file); `libCrypto.so` matches the hash recorded
in `reverse/evidence/network_constants.json` (`session_encryption.libCrypto_sha256`).
The tarball `openssl-1.0.2u.tar.gz` is pinned to
`ecd0c6ffb493dd06707d38b14bb4d8c2288bb7033735606569d8f90f89669d16`.

## Windows EXE/PDB match — now independently verified

Earlier notes could not inspect the Windows pair (the first attempt arrived as a
RAR this host cannot open). The ZIP version was supplied on 2026-11-18 and the
pair now resolves; `reverse/evidence/windows_pdb_match.json` records the parse:

| Item | Value |
|---|---|
| EXE machine / image | `0x8664` (x86-64), 7 sections, PE32+ |
| EXE debug directory (type 2) | RSDS GUID `093aad07-19a7-2d45-afa7-9fe3abd628fe`, Age 1 |
| PDB (MSF 7.00) | GUID `093aad07-19a7-2d45-afa7-9fe3abd628fe`, Age 1, Signature 1535457991 |
| PDB link name in the EXE | `...\Win32.Server.Publish\bedrock_server.pdb` |
| Match | **GUID + Age equal**, parsed independently from both files |

Cross-check that the pair belongs to the same build the Linux work models:
`?NetworkProtocolVersion@SharedConstants@@3HB` is a PDB public at section 2
(`.rdata`) offset 225,040; those two EXE bytes read `1a 01` = **282**, the
protocol number of the Linux 1.6.0.15 target. `windows_publics_curated.json`
also resolves 55 of 67 curated functions from the RE reports to MSVC public
records (e.g. `Level::tick`, `LevelChunk::tickBlocks`, `Spawner::tick`,
`Mob::travel`). Section/offset coordinates are PE image coordinates, **not**
equivalent to Linux ELF virtual addresses; presence or absence in this table is
navigation help, not proof of a Linux↔Windows function identity.

The EXE's version resource reports `FileVersion 1.2.15.0` / `ProductName
Minecraft` — the internal Windows build stamp, unrelated to the advertised
`1.6.0.15` (the Linux binary logs `Version 1.6.0.15` at startup).

## PDB-derived navigation metadata

`tools/re/rebuild_input.sh` regenerates `input/windows/source_mapping/` from the
PDB using `llvm-pdbutil dump --modules --files`:

- `ORIGINAL_SOURCE_TREE.txt` — 3,711 distinct source files referenced by the
  compilands (element counts, not fixed seeds).
- `MODULE_TO_PRIMARY_SOURCE.csv` — 2,500 compilands: 1,867
  `UNIQUE_BASENAME_MATCH`, 633 `NOT_MAPPED`, 0 ambiguous in this derivation. The
  label compares the compiled object's basename with the source files the same
  module lists; it is a heuristic, not proof that a Windows module equals a
  given Linux translation unit.

`python3 reverse/scripts/correlate_sources.py input/windows/source_mapping/MODULE_TO_PRIMARY_SOURCE.csv`
then yields 1,430 INFERRED class-prefix correlations against the Linux symbol
index, written to the ignored `reverse/evidence/local/source_correlations.json`.
See [source mapping](SOURCE_MAPPING.md).

## Reference-run prerequisites

`libCrypto.so` needs OpenSSL 1.0 shared libraries. On this host there is no
packaged OpenSSL 1.0, so `rebuild_input.sh` builds OpenSSL 1.0.2u from the pinned
tarball into `input/reference-runtime/openssl-local` (a few minutes on 4 cores). The
`ipv6_shim.so` preload is required only on hosts without kernel IPv6; this host
does have IPv6, and the original starts without the shim. Earlier reports note
the opposite on the previous host, where the unusual IPv4-only retry opened an
IPv6 socket too.

First reference runs on this reconstructed input, all with the original:

- Original starts from `input/reference-runtime/bds` and answers discovery,
  self-signed login, encryption and chunk streaming (`input/reference/bds_join.json`,
  553 packets) with `online-mode=false` and a fresh `worlds/O`.
- Palette capture from a fresh seed-318,168,905 world reproduces the committed
  registry order: unshuffling `input/reference/palette_282.json` with the
  captured seed gives exactly the 4,493 committed
  `src/network/block_palette_282.inc` entries (`minecraft:air:0` first).
- `tools/reference/oracle_random.py` regenerated
  `tests/reference/random_vectors.json` byte-identically (same `binary_sha256`,
  same 7,915 outputs).
- `tools/re/capture_terrain.py` + `make_terrain_reference.py` regenerate the
  terrain corpus: the fresh 113-chunk capture reproduces all 113 committed
  chunk-biome records byte-for-byte, its 407 raw biome grids are a subset of the
  committed 418 and every shared grid is identical, and 22 of the committed 24
  sampled BlockVolumes were captured again with identical `heights`, `surface`
  and `carved` data. The two missing coordinates are simply outside the fresh
  run's chunk set, so the exporter samples others; no committed value differs.
- `capture_level_random.py --grass-oracle` day + night runs regenerate all 160
  `tests/reference/grass_ticks_bds_1.6.0.15.json` cases byte-identically, and
  the compiled `grass_tick_reference` and `terrain_reference` tests pass against
  the regenerated JSON.
- The five lighting scenes (`--lighting-oracle` with the fixtures' own
  `scene_setup_commands`) regenerate the light part of every
  `lighting_*_bds_1.6.0.15.json` exactly: 2,029 sky/block pairs and 247
  clear-sky time cases per scene, identical block-property registry, identical
  scene commands, and `lighting_reference_tests` passes against them. Their
  `initial_ground` and `block_ticks` LCG chain do **not** reproduce bit for bit:
  the process-global block-tick LCG advances through the world's real random
  block ticks before the oracle pauses, so the starting LCG word (and, downstream,
  which grass cells already changed) depends on how many real ticks elapsed. The
  committed fixtures remain the authoritative record of one such run; the
  regenerated ones are kept in `input/reference/light_*.json` as proof that the
  reproducible parts match.
- Level-Random startup words reproduce exactly: 4,492 draws at tick 0 and 112
  in the first empty tick equal `startup_words` / `first_empty_tick_words` in
  `tests/reference/world_ticks_bds_1.6.0.15.json`.

Not regenerated yet (no new claim): the lighting fixtures
(`--lighting-oracle` runs need their captured scene commands), the 139-chunk
`chunks_bds_1.6.0.15.zlib` corpus, the mob/navigation/riding/water traces and
the item-actor/combat packet oracles. `export_world_tick_reference.py` also needs
the 200-tick A/B runs plus a `setblock 0 255 0 stone` run; a block-sample trace
is roughly ten times slower than a plain draw trace, which is why
`capture_level_random.py` now scales its timeout for `--block_samples`.

## Where things are cited

| Topic | Document |
|---|---|
| Bundled data provenance | [BUNDLED_DATA.md](BUNDLED_DATA.md) |
| Reference tool invocation | [tools/reference/README.md](../tools/reference/README.md) |
| Symbol index and call graphs | [FUNCTION_MAP.md](../reverse/reports/FUNCTION_MAP.md), [RE_PROGRESS.md](../reverse/reports/RE_PROGRESS.md) |
| Unknowns after this step | [UNRESOLVED.md](../reverse/reports/UNRESOLVED.md) |
