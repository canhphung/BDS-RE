# Actual build status — 2026-11-16

| Platform/toolchain | Debug | Release | Sanitizers | Notes |
|---|---|---|---|---|
| Linux x86-64 GCC 14.2.0 | PASS, 5/5 CTest | PASS, 5/5 CTest | ASan+UBSan PASS, 5/5 CTest | CMake 4.4.5, Ninja 1.13.2 |
| Linux Clang | UNRUN | UNRUN | UNRUN | Compiler absent; CI configured |
| Windows MSVC | UNRUN | UNRUN | N/A | Native CI configured, not dispatched |
| Windows Clang | UNRUN | UNRUN | UNRUN | Not configured |

CTest cases: chunks (full-column placement/read/removal, extreme coordinates, 4,000 fixed-seed updates against independent sparse map); core (scheduler/config/runtime), world (block ownership and age), lifecycle (three ticks and expected shutdown output), invalid_config (expected nonzero exit). Checks remain active in Release and do not use disabled `assert` macros. All 5 cases executed in each final local build.

Additional Linux integration: launched unlimited runtime, sent SIGTERM after 150 ms, observed `stopped ticks=2` and exit 0 within three seconds. No original executable was run. No real Minecraft client, protocol or differential tests have run.

Commands executed from repository root, with `/workspace/bds-tools/bin` on PATH:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake -S . -B build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBDS_SANITIZE=ON
# For each of the three directories:
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Local symbol indexing succeeded and binary SHA256 matched. `git diff --check` passed. Proprietary inputs, symbol database and raw disassembly are ignored.
