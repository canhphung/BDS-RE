# Actual build status — 2026-11-16

| Platform/toolchain | Debug | Release | Sanitizers | Notes |
|---|---|---|---|---|
| Linux x86-64 GCC 14.2.0 | PASS, 7/7 CTest | PASS, 7/7 CTest | ASan+UBSan PASS, 7/7 CTest | CMake 4.4.5, Ninja 1.13.2 |
| Linux Clang | PASS (native CI) | PASS (native CI) | UNRUN | CI run 43835644043 at ce383cd; compiler absent locally |
| Windows MSVC / VS 2022 | PASS (native CI) | PASS (native CI) | N/A | windows-2022; CI run 43835644043 at ce383cd |
| Windows Clang | UNRUN | UNRUN | UNRUN | Not configured |

CTest cases: persistence (independent golden fixture, malformed/truncated inputs, duplicate positions, resource limits, deterministic round trips and filesystem replacement), world_lifecycle (two native executable runs restore age 2 then 5; corrupt file is preserved); chunks (full-column placement/read/removal, extreme coordinates, 4,000 fixed-seed updates against independent sparse map); core (scheduler/config/runtime), world (block ownership and age), lifecycle (three ticks and expected shutdown output), invalid_config (expected nonzero exit). Checks remain active in Release and do not use disabled `assert` macros. All 7 cases executed in each final local build.

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

Persistence signal integration: SIGTERM saved age 98; restarting for two ticks saved age 100. CRC32 independently checked with Python zlib. These exact ages are observations of that run, not timing assertions. The Windows replacement implementation now passes native Windows CI; no Windows execution took place in the local Linux machine.

## Native CI verification after runner repair

[Run 43835644043](https://github.com/canhphung/BDS-RE/actions/runs/43835644043) completed successfully for commit `ce383cd757869bf2082f7d8151b7e978c54fd679`. GitHub API reports success for all seven jobs: Linux GCC Debug/Release, Linux Clang Debug/Release, Linux sanitizers, and native Windows MSVC Debug/Release. Each platform job's configure, build and CTest steps completed successfully. This verifies the existing development harness and its tests, not Minecraft client compatibility.

The previous run at f3fa372 failed at Windows configuration because the moving windows-latest image did not provide the Visual Studio 2022 instance required by the preset. The workflow now pins windows-2022, whose image includes VS 2022, and disables Windows matrix fail-fast so Debug and Release report independently. The Visual Studio 2022 CMake preset remains available for local builds.

## Current unpushed operational slice

Linux GCC Debug/Release and ASan+UBSan executed all 10 CTest cases successfully after adding properties, access-list/command tests, discovery codec and real-process console/backup/IPv4/IPv6 integration. The new process test checks world path with spaces, whitelist persistence, runtime-only setting changes, immutable backup file and length while ticks advance, release on resume, console stop and restart. This is independent implementation validation; no original runtime, real client, login or gameplay validation occurred. New Windows/Clang results must be recorded against the new source commit after CI runs.
