# BDS-RE

Independent C++20 server research targeting Bedrock Dedicated Server 1.6.0.15.
Currently a tested runtime bootstrap and chunked internal world, **not a Minecraft-compatible server**. No networking, client login, network chunk streaming or gameplay yet. The original `server.properties` is included as reference configuration; its parsing, world-directory selection and console commands are not implemented yet. See the [original BDS compatibility contract](docs/COMPATIBILITY.md).

## Build

Requires CMake >=3.20, Ninja and a C++20 compiler. GCC 14.2 is tested locally.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/bds_server --ticks 20
```

On this cloud machine, first `export PATH=/workspace/bds-tools/bin:$PATH`.
`--world build/world.snapshot` enables internal snapshot load/save across restarts (see [format and limits](docs/PERSISTENCE.md)). `--ticks` counts ticks per invocation.

`--ticks 0` (default) runs until SIGINT/SIGTERM. `--tick-ms` accepts 1..1000 (default 50); `--max-catch-up` accepts 1..100 (default 5).

Windows: install Visual Studio 2022 with Desktop development with C++, then:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
.\build\windows-msvc\Debug\bds_server.exe --ticks 20
```

Use `windows-release` for Release. Windows instructions and CI are configured, not yet executed. Linux Clang can use `-DCMAKE_CXX_COMPILER=clang++` in a separate build directory. Sanitizers: `-DBDS_SANITIZE=ON` on GCC/Clang.

## Research

Place the original ELF at `input/linux/bedrock_server` (ignored). Original executables, PDBs, raw disassemblies and generated symbol databases are excluded from Git. At the user's request, the four original data directories from the supplied Linux server ZIP are included; see [bundled data provenance](docs/BUNDLED_DATA.md). The current engine does not load these packs, definitions or structures yet.

```sh
python3 reverse/scripts/index_elf.py input/linux/bedrock_server
python3 -c "import sqlite3; c=sqlite3.connect('reverse/symbols/generated/linux.sqlite'); print(c.execute("select printf('0x%x',address),size,name from symbols where name like 'ServerInstance::%' limit 20").fetchall())"
```

The script verifies the expected hash, creates a searchable local SQLite index and regenerates the bounded call-site graph. See [progress](docs/PROGRESS.md), [build status](docs/BUILD_STATUS.md) and [function map](reverse/reports/FUNCTION_MAP.md).
