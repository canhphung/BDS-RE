# Reference runs of the original BDS 1.6.0.15

## Draw-by-draw world-tick oracle

The random-tick/mob handoff is on `claude/clever-newton-bboqgm`. For a bounded
trace, use the safe child-process runner (GDB must support Python):

```sh
python3 tools/re/capture_level_random.py \
  --runtime input/reference-runtime/bds \
  --library-path input/reference-runtime/openssl-local/lib \
  --preload input/reference-runtime/ipv6_shim.so \
  --output input/reference/world-random-new \
  --players A,B --ticks 200 --block-samples
```

`--output` must be new. `--gdb` can select a local GDB installation. The default
port is 19220. For a high-subchunk reference run, add
`--command 'setblock 0 255 0 stone'`; commands execute after all clients spawn.
The original ignores `level-type=FLAT`, so this oracle explicitly selects flat
LevelSettings in GDB and records that override; `--generator original` retains
the stock setting. Algorithm instructions and RNG results are not patched.

Export bounded fixtures from a full run and a high-subchunk run:

```sh
python3 tools/re/export_world_tick_reference.py \
  tests/reference/world_ticks_bds_1.6.0.15.json \
  input/reference/world-random-new input/reference/world-random-high
```

See [trace evidence and current implementation boundary](../../reverse/reports/RANDOM_TICKS.md).
Startup/first-empty-tick RNG, block coordinate selection and the GrassBlock
function below have bounded regression comparisons; full random block ticks
and mobs remain in progress.

### GrassBlock function oracle

The paused-world oracle records live light/material/block queries and mutation
requests from the original GrassBlock::tick, using a separate seeded original
Random for every case:

```sh
python3 tools/re/capture_level_random.py \
  --runtime input/reference-runtime/bds \
  --library-path input/reference-runtime/openssl-local/lib \
  --preload input/reference-runtime/ipv6_shim.so \
  --output input/reference/grass-day-new --ticks 20 --grass-oracle \
  --command 'fill -2 4 -2 14 10 10 air' \
  --command 'fill 7 4 -1 9 4 1 stone' \
  --command 'fill 7 3 7 9 3 9 dirt 1' \
  --command 'setblock 12 4 0 grass_path' \
  --command 'setblock 0 7 8 grass'
```

Repeat into a different `--output` directory with the same scene commands and
an additional `--command 'time set 18000'` for night. Then:

```sh
python3 tools/re/export_grass_tick_reference.py \
  tests/reference/grass_ticks_bds_1.6.0.15.json \
  input/reference/grass-day-new input/reference/grass-night-new
```

`grass_tick_reference` compares all 160 cases, including the exact dependency
call order, query positions, mutation flags and RNG words. This is a function
comparison with original live dependency inputs; host lighting and tick scheduling
are still pending.

These tools run the original Linux server locally and talk to it with an independent protocol-282 client, for reference captures and differential checks. The original server, its libraries, worlds and full captures stay under the ignored `input/` directory.

## Running the original server

1. Extract the supplied `BDS-Linux.zip` (SHA256 `aafdef8e…8e07`) into `input/linux/bds/`. `bedrock_server` must hash to `69142612…c4ad`.
2. `libCrypto.so` links OpenSSL 1.0 (`libssl.so.1.0.0`, `libcrypto.so.1.0.0`). On hosts without it, build OpenSSL 1.0.2u (SHA256 `ecd0c6ffb493dd06707d38b14bb4d8c2288bb7033735606569d8f90f89669d16`) outside the repository with `./config shared no-ssl2 no-ssl3 --prefix=<dir> && make && make install_sw`. It is used only for this local loopback reference run.
3. Set `online-mode=false` in `input/linux/bds/server.properties` (the reference client logs in self-signed) and pick free ports.
4. On hosts without kernel IPv6, preload `ipv6_shim.c` (build: `cc -shared -fPIC -O2 -o ipv6_shim.so ipv6_shim.c -ldl`). Without it the original opens no sockets at all; see below.
5. Start it from `input/linux/bds`: `LD_PRELOAD=<shim> LD_LIBRARY_PATH=<openssl>/lib:. ./bedrock_server`. Type `stop` to quit.

## Capturing and analysing

```bash
python3 tools/reference/capture_reference.py 127.0.0.1 19140 input/reference/bds_join.json 4
```

```bash
python3 tools/reference/analyze_capture.py input/reference/bds_join.json input/reference/palette_282.json
```

```bash
python3 tools/reference/make_palette_source.py input/reference/palette_282.json
```

`bedrock_client.py` is the same client the `raknet_handshake` integration test uses against BDS-RE, so one implementation is exercised against both servers.

## Findings from the first reference run (2026-11-16)

DIFFERENTIAL_VERIFIED against the original (independent client accepted, or bytes identical):

- RakNet 9 open/connected handshake, split reassembly in both directions, reliable ordering and ACKs.
- 0xfe zlib batches and packet headers; self-signed ES384 login; the encryption handshake (token with only a `salt` claim, ECDH P-384, SHA-256(salt‖secret), AES-256-CFB8, 8-byte SHA-256 checksums with per-direction counters) — every server packet's checksum verified.
- PlayStatus 0, empty ResourcePacksInfo (`0000000000`) and ResourcePackStack (`000000`), byte-identical to BDS-RE's.
- StartGame: parsed to its last byte with the reconstructed layout, and BDS-RE's encoder reproduces the original's 111,701 bytes exactly given the same values (`tests/reference/start_game_bds_1.6.0.15.bin`).
- Game rules: 21 defaults; integer rules use zigzag varints. ChunkRadiusUpdated 5 for a request of 4 (minimum clamp).
- `Player connected: %s` logs the XUID (empty for a self-signed player).
- Discovery advertisement layout, ending in `;1;` on Linux (BDS-RE now matches).

Behaviour of the original worth recording:

- Without kernel IPv6 the original's IPv4-only retry still opens an IPv6 socket, fails again and leaves no sockets, while logging "Listening on IPv4 port: 19132" regardless of the configured port (strace: it bound the configured 19140 before failing).
- The log reports `Error opening ops file: ops.json`, while the supplied guide documents `opsfile.json`; BDS-RE still uses `opsfile.json` (see reverse/reports/UNRESOLVED.md).
- After StartGame the original also sends PlayerList, AdventureSettings, GameRulesChanged, UpdateAttributes, InventoryContent, PlayerHotbar, CraftingData, AvailableCommands, then FullChunkData (0x3a), MoveActorDelta and entity packets, and PlayStatus 3 (PlayerSpawn) once chunks are sent.

## Chunk and spawn regression corpus

The original ignored 113-chunk capture was absent in the next environment. A new
run of this workflow produced 139 chunks; every body, both layers where present,
and block-entity suffixes are preserved in the committed compressed corpus.
To export a fresh capture:

```sh
python3 tools/reference/export_chunk_corpus.py input/reference/m4_join.json tests/reference/chunks_bds_1.6.0.15
```

The generated metadata records the capture hash, corpus hash, packet count and
independent unpacked-cell digest. A deliberate corpus refresh also requires updating
the count/digest constants in `tests/protocol/chunk_tests.cpp`. See
[chunk format, reference evidence and spawn policies](../../reverse/reports/CHUNKS.md).
The reference client's receive path now honors reliable ordering before decrypting
batches, so deliberate chunk loss/retransmission is testable without corrupting
its continuous CFB8 stream.

## ItemUse validation captures

Run the pinned original binary in a fresh isolated world and export held-item/
selected-slot rejection cases:

```sh
python3 tools/reference/capture_item_use_errors.py \
  --runtime input/reference-runtime/bds \
  --library-path input/reference-runtime/openssl-local/lib \
  --preload input/reference-runtime/ipv6_shim.so \
  --output input/reference/item-use-new
```

Omit `--preload` when native IPv6 works. `--output` must be a new directory. The
script creates its own world and server.properties; source runtime assets are
symlinked and existing runtime worlds are never opened. `--port` defaults to
19150 (IPv6 uses the following port). Dependencies are the same Python 3 and
`cryptography` used by the other reference tools.

The ignored output contains a full raw capture (console commands, client requests,
server game packets) and `item_use_errors.json`. The committed fixture was obtained
from two runs with identical exported cases. Response bodies stay unmodified;
UpdateBlock runtime IDs are compared using each session's advertised StartGame
palette. Unrelated tick packets are filtered as documented in the fixture.
Run `ctest --test-dir build -R item_use_reference --output-on-failure` for the
regression check; see [function evidence and coverage](../../reverse/reports/ITEM_USE.md).

## Block-break captures

`capture_break_oracle.py` takes the same arguments (default port 19170) and records,
for grass, dirt, bedrock and two stone blocks either side of the 12.5 reach limit,
the LevelEvents produced by StartBreak/AbortBreak and every packet answering an
ItemUse break request sent without StartBreak. The breaking-relevant packets of
one run are kept in tests/reference/break_bds_1.6.0.15.json.

```sh
python3 tools/reference/capture_break_oracle.py \
  --runtime input/reference-runtime/bds \
  --library-path input/reference-runtime/openssl-local/lib \
  --preload input/reference-runtime/ipv6_shim.so \
  --output input/reference/break-new
```

## Item entity traces (gdb)

`trace_item_actor.py` runs the original server under gdb with
`gdb_item_actor.py`, which logs the ItemActor state at every
`ItemActor::normalTick` entry and the related calls, while a reference client
breaks blocks (`--scenario pickup` or `merge`). The item replay references in
tests/reference/item_actor*_bds_1.6.0.15.json were extracted from these logs.

```sh
python3 tools/reference/trace_item_actor.py \
  --runtime input/reference-runtime/bds \
  --library-path input/reference-runtime/openssl-local/lib \
  --preload input/reference-runtime/ipv6_shim.so \
  --output input/reference/items-new --scenario merge
```

## PlayerAction and visibility oracle

`player_actions_oracle.py HOST PORT OUT.json` joins two clients; A sneaks,
sprints, jumps, walks out of B's ticking area and back. `about_actor` holds the
normalised packets concerning A per step; tests/reference/player_actions_bds_1.6.0.15.json
is the original's result and test `player_actions_reference` compares BDS-RE.

## Hunger and health oracle

`hunger_health_oracle.py HOST PORT OUT.json` joins one survival client
(difficulty normal) and steps through a 19.8-block ActorFall, natural
regeneration, jumping, walking drain, waiting, a 40-block fatal fall and the
respawn action, recording UpdateAttributes, ActorEvent, SetActorData, Text,
Respawn and SetHealth per step. tests/reference/hunger_health_bds_1.6.0.15.json
holds the original's packets (runtime id 1) that `gameplay_tests` compares byte
for byte. Run it against the original with the same runtime options as the
other oracles.

## Combat oracle

`combat_oracle.py HOST PORT OUT.json CONSOLE_FIFO` joins two survival clients
(difficulty normal, cheats on) next to each other; the server's standard input
is the FIFO, through which the script types `give`/`effect` console commands. A
falls, eats a golden apple (start, then consume with its predicted inventory
change), B wears an iron helmet, A hits B with a diamond sword, B sprint-hits A,
B gets poison, A drops a spider eye. tests/reference/combat_bds_1.6.0.15.json is
the original's result (packets of other actors and chunk traffic removed); the
`combat_reference` test replays the scenario on BDS-RE.

`effect_modifier_oracle.py` (two clients: speed, strength, health boost,
weakness, punches, clear) and `instant_effect_oracle.py` (one client: instant
health, instant damage II) use the same console FIFO setup; their results are in
tests/reference/effect_modifiers_bds_1.6.0.15.json, also replayed by
`combat_reference`.
