# Actual build status — 2026-11-16

| Platform/toolchain | Debug | Release | Sanitizers | Notes |
|---|---|---|---|---|
| Linux x86-64 GCC 14.2.0 | PASS, 11/11 CTest | PASS, 11/11 CTest | ASan+UBSan PASS, 11/11 CTest | CMake 4.4.5, Ninja 1.13.2 |
| Linux Clang | PASS (native CI) | PASS (native CI) | UNRUN | CI run 43839417910 at 43d46b6; compiler absent locally |
| Windows MSVC / VS 2022 | PASS (native CI) | PASS (native CI) | N/A | windows-2022; CI run 43839417910 at 43d46b6 |
| Windows Clang | UNRUN | UNRUN | UNRUN | Not configured |

CTest cases: server (properties, access lists, commands, backup), discovery (offline wire codec), login_codec (bounded unverified packet body), server_console (live native process, console, backup and IPv4/IPv6 UDP); persistence (independent golden fixture, malformed/truncated inputs, duplicate positions, resource limits, deterministic round trips and filesystem replacement), world_lifecycle (two native executable runs restore age 2 then 5; corrupt file is preserved); chunks (full-column placement/read/removal, extreme coordinates, 4,000 fixed-seed updates against independent sparse map); core (scheduler/config/runtime), world (block ownership and age), lifecycle (three ticks and expected shutdown output), invalid_config (expected nonzero exit). Checks remain active in Release and do not use disabled `assert` macros. All 11 cases executed in each final local build.

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

## Current operational slice

Linux GCC Debug/Release and ASan+UBSan executed all 11 CTest cases successfully after adding properties, access-list/command tests, discovery codec and real-process console/backup/IPv4/IPv6 integration. The new process test checks world path with spaces, whitelist persistence, runtime-only setting changes, immutable backup file and length while ticks advance, release on resume, console stop and restart. This is independent implementation validation; no original runtime, real client, login or gameplay validation occurred. Native Windows/Clang results are now verified in run 43839417910 at 43d46b6.

The initial operational CI at 250e810 passed all Linux GCC/Clang/sanitizer jobs but failed Windows core/lifecycle tests because the root confinement check compared path spellings when root was ".". Native console/backup/discovery tests passed on Windows. The final fix canonicalizes the existing root before joining worlds and then uses filesystem equivalence for confinement. Relative-root regression tests now pass in native Windows CI. The added eleventh test covers the bounded unverified login body decoder; it is not an authenticated login test.

### Verified current source

[Run 43839417910](https://github.com/canhphung/BDS-RE/actions/runs/43839417910), source commit `43d46b64df09d15a7e5db73ebba99a934256b95b`, completed with all seven jobs successful. Windows MSVC Debug and Release logs each explicitly report 100% tests passed, 0 tests failed out of 11. Linux GCC/Clang Debug/Release and the sanitizer job also passed. This replaces the earlier operational Windows failures; no tests were skipped or disabled to make them pass. Login/JWT verification, real-client spawn, world streaming and networked gameplay remain unimplemented.

## Current RakNet foundation (CI pending)

Linux GCC Debug, Release and ASan+UBSan each execute 13/13 CTest cases successfully. New tests are raknet (wire fixtures, sequence wrap arithmetic, padding, ACK/NACK, loss/retry, duplicates, ordered/sequenced delivery, bounded split reassembly, peer limits/expiry) and raknet_handshake (independent Python client over real IPv4/IPv6 UDP, protocol 9 negotiation, 20-address acceptance, forced retransmission, ping/pong and unauthenticated-application rejection). These do not count as Minecraft client or differential verification. The accompanying local source-map correlator processed 2,500 supplied module rows and produced 1,429 INFERRED navigation candidates; its raw input/output remain ignored.
