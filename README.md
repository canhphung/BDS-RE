# BDS-RE

Independent C++20 server research targeting Bedrock Dedicated Server 1.6.0.15.
The runtime supports server.properties, internal snapshots/backups, RakNet 9,
ES384 Login, encrypted chunk streaming and basic gameplay: seeded flat/rolling
terrain, player movement/visibility, block placement/breaking and persistent
36-slot inventories with a cursor. The supported block set is stone, grass, dirt
and bedrock. **Gameplay is experimental and differs from original BDS behavior.**
Terrain, movement, mining and inventory execution remain independent algorithms;
tools, crafting, chests, mobs and full physics remain pending. The user confirmed the M4 join path
with Minecraft 1.6.0.14; the new gameplay is integration-tested with two independent
protocol clients. A [scoped ItemUse validation audit](reverse/reports/ITEM_USE.md)
now has 37 original-server cases; it does not verify the whole gameplay slice. See
[gameplay and local testing](docs/GAMEPLAY.md) and [operations](docs/OPERATIONS.md).

## Build

Requires CMake >=3.20, Ninja and a C++20 compiler; Python 3 with the `cryptography` package (`python3 -m pip install cryptography`) is needed for the process/socket integration tests. GCC 14.2 is tested locally.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/bds_server --ticks 20
```

On this cloud machine, first `export PATH=/workspace/bds-tools/bin:$PATH`.
Default startup reads `server.properties`, selects `worlds/<level-name>` and binds the configured UDP ports. Type `stop` for clean shutdown. `--root <directory>` isolates server state; `--network 0` disables discovery for development tests.

`--world build/world.snapshot` overrides internal snapshot selection for development (see [format and limits](docs/PERSISTENCE.md)). `--ticks` counts ticks per invocation.

`--ticks 0` (default) runs until SIGINT/SIGTERM. `--tick-ms` accepts 1..1000 (default 50); `--max-catch-up` accepts 1..100 (default 5).

Windows: install Visual Studio 2022 with Desktop development with C++, then:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
.\build\windows-msvc\Debug\bds_server.exe --ticks 20
```

Use `windows-release` for Release. Windows CI uses the `windows-2022` runner to match the Visual Studio 2022 preset. See [build status](docs/BUILD_STATUS.md) for recorded verification. Linux Clang can use `-DCMAKE_CXX_COMPILER=clang++` in a separate build directory. Sanitizers: `-DBDS_SANITIZE=ON` on GCC/Clang.

## Research

Place the original ELF at `input/linux/bedrock_server` (ignored). Original executables, PDBs, raw disassemblies and generated symbol databases are excluded from Git. At the user's request, the four original data directories from the supplied Linux server ZIP are included; see [bundled data provenance](docs/BUNDLED_DATA.md). The current engine does not load these packs, definitions or structures yet.

Implementation policy (user directive, 2026-11-16): game and server logic follows the original binary. Translating recovered functions (disassembly or decompiled logic) directly into C++ is the intended approach; there is no requirement to author algorithms independently. Each translated function cites its original address and is verified against the original (function oracle under gdb, chunk or packet comparison). Deliberate deviations (e.g. security hardening) must be documented as such.

```sh
python3 reverse/scripts/index_elf.py input/linux/bedrock_server
python3 -c "import sqlite3; c=sqlite3.connect('reverse/symbols/generated/linux.sqlite'); print(c.execute("select printf('0x%x',address),size,name from symbols where name like 'ServerInstance::%' limit 20").fetchall())"
```

The script verifies the expected hash, creates a searchable local SQLite index and regenerates the bounded call-site graph. See [progress](docs/PROGRESS.md), [build status](docs/BUILD_STATUS.md) and [function map](reverse/reports/FUNCTION_MAP.md).
