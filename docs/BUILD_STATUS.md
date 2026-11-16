# Actual build status — 2026-11-16

| Platform/toolchain | Debug | Release | Sanitizers | Notes |
|---|---|---|---|---|
| Linux x86-64 GCC 14.2.0 | PASS, 13/13 CTest | PASS, 13/13 CTest | ASan+UBSan PASS, 13/13 CTest | CMake 4.4.5, Ninja 1.13.2 |
| Linux Clang | PASS (native CI) | PASS (native CI) | UNRUN | CI run 43843720476 at 4cdd95e; compiler absent locally |
| Windows MSVC / VS 2022 | PASS (native CI) | PASS (native CI) | N/A | windows-2022; CI run 43843720476 at 4cdd95e |
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

## Current RakNet foundation (native CI verified)

Linux GCC Debug, Release and ASan+UBSan each execute 13/13 CTest cases successfully. New tests are raknet (wire fixtures, sequence wrap arithmetic, padding, ACK/NACK, loss/retry, duplicates, ordered/sequenced delivery, bounded split reassembly, peer limits/expiry) and raknet_handshake (independent Python client over real IPv4/IPv6 UDP, protocol 9 negotiation, 20-address acceptance, forced retransmission, ping/pong and unauthenticated-application rejection). These do not count as Minecraft client or differential verification. The accompanying local source-map correlator processed 2,500 supplied module rows and produced 1,429 INFERRED navigation candidates; its raw input/output remain ignored.

[Run 43843720476](https://github.com/canhphung/BDS-RE/actions/runs/43843720476) completed successfully for source commit `4cdd95e317bb311e52eb64478bdf6d225c509578`. All seven jobs passed: GCC/Clang Debug/Release, Linux sanitizers, native Windows MSVC Debug/Release. Both Windows logs explicitly report 100% tests passed, 0 tests failed out of 13. This includes actual IPv4/IPv6 UDP handshake tests with an independent protocol client, not a Minecraft client. No authenticated login, spawn or gameplay verification is claimed.

## Network robustness slice (local, IPv4-only host)

Verified in a cloud container whose kernel rejects AF_INET6 sockets (EAFNOSUPPORT). Before this change the server exited 1 with "cannot create UDP socket" and both Python integration tests failed creating IPv6 sockets (11/13). Now:

| Toolchain | Configuration | Result |
|---|---|---|
| GCC 13.3.0 | Debug + ASan/UBSan | PASS, 14/14 CTest |
| GCC 13.3.0 | Release | PASS, 14/14 CTest |
| Clang 18.1.3 | Release | PASS, 14/14 CTest |

CMake 3.28.3, Ninja 1.11.1. On this host the integration tests exercise the IPv4-only fallback path and assert the `IPv6 not supported` log; dual-stack hosts (CI) still exercise both families. New test runtime_failure forces an in-loop exception (world age exhaustion three ticks after load) and checks exit 1, the logged cause, and a persisted emergency snapshot with the advanced age. Windows receive-error classification (WSAECONNRESET/WSAEMSGSIZE skip one datagram) compiles only on Windows and awaits native CI.

## Game-packet batch slice (local, IPv4-only host)

| Toolchain | Configuration | Result |
|---|---|---|
| GCC 13.3.0 | Debug + ASan/UBSan | PASS, 15/15 CTest |
| GCC 13.3.0 | Release | PASS, 15/15 CTest |
| Clang 18.1.3 | Release | PASS, 15/15 CTest |

New test `batch` covers varint/BinaryStream codecs, header bit layout, zlib round trips with level-7 header bytes, truncated/corrupt/trailing streams, a 64 MiB decompression bomb stopped at 4 MiB, a Python-generated batch fixture, the generated packet id header against reverse/evidence/packet_ids.json, and the pre-login state machine. `raknet_handshake` now also sends Python-built zlib LoginPacket batches over real UDP and checks the DisconnectPacket (protocol 282) and PlayStatus 1/2 (281/283) replies followed by RakNet disconnect. Vendored zlib compiles on MSVC only once native CI runs; no real Minecraft client was used.

## Login authentication slice (local, IPv4-only host)

| Toolchain | Configuration | Result |
|---|---|---|
| GCC 13.3.0 | Debug + ASan/UBSan | PASS, 16/16 CTest |
| GCC 13.3.0 | Release | PASS, 16/16 CTest |
| Clang 18.1.3 | Release | PASS, 16/16 CTest |

New test `auth`: RFC 4648 base64 vectors and strictness, FIPS 180-2 SHA-256/384 vectors, ES384 verification of signatures produced by Python `cryptography` (tampered input/signature, off-curve and truncated keys), the reconstructed chain algorithm (trusted chain, Mojang-key rejection, inclusive nbf/exp bounds, forged leaf, non-CA intermediate, expired leaf, missing nbf, ES256 header, foreign client token, self-signed acceptance only with online mode off, chain length and malformed input) and the GameConnection outcomes. `raknet_handshake` now sends these signed logins over real UDP as split RakNet messages and checks the self-signed success, forged rejection and outdated client/server replies. Vendored Mbed TLS compiles on MSVC only once native CI runs. No genuine Xbox Live chain or Minecraft client has been tested.

## Encryption handshake slice (local, IPv4-only host)

| Toolchain | Configuration | Result |
|---|---|---|
| GCC 13.3.0 | Debug + ASan/UBSan | PASS, 16/16 CTest |
| GCC 13.3.0 | Release | PASS, 16/16 CTest |
| Clang 18.1.3 | Release | PASS, 16/16 CTest |

`auth` adds the NIST SP 800-38A CFB8-AES256 vector (with stream state across calls), ECDH/session-key/sealed-packet vectors from Python `cryptography`, tamper/replay/short-packet rejection, server token signature checks and a full in-process handshake playing the client. `raknet_handshake` now completes the encrypted handshake over UDP with an independent Python client (token signature, ECDH, AES-CFB8, checksums) up to PlayStatus LoginSuccess. CI installs `cryptography` through actions/setup-python and passes that interpreter to CMake; this workflow change has not run yet. No Minecraft client was used.

## Admission and resource pack slice (local, IPv4-only host)

| Toolchain | Configuration | Result |
|---|---|---|
| GCC 13.3.0 | Debug + ASan/UBSan | PASS, 16/16 CTest |
| GCC 13.3.0 | Release | PASS, 16/16 CTest |
| Clang 18.1.3 | Release | PASS, 16/16 CTest |

`server` adds whitelist UUID parsing/normalisation, UUID/XUID admission, XUID fill-in and gamertag update with persistence. `auth` adds the encrypted PlayStatus/ResourcePacksInfo/ResourcePackStack sequence, Completed, refusal, download request, malformed responses and admission refusal. `raknet_handshake` drives the pack exchange over UDP with the Python client and toggles `whitelist on` through the console to check a plaintext notAllowed refusal. During development ASan caught a use-after-free in new test code (a ByteReader over a temporary); it was fixed before commit. No Minecraft client was used.

## StartGame slice (local, IPv4-only host)

| Toolchain | Configuration | Result |
|---|---|---|
| GCC 13.3.0 | Debug + ASan/UBSan | PASS, 17/17 CTest |
| GCC 13.3.0 | Release | PASS, 17/17 CTest |
| Clang 18.1.3 | Release | PASS, 17/17 CTest |

New test `level_packets` compares StartGame (all LevelSettings fields, negative/extreme values, palette) and the small level packets with golden bytes from an independent Python encoder, plus zigzag varints and the chunk radius clamp. `auth` covers the Completed → level data sequence, ignored packets while spawning and the radius reply. `raknet_handshake` parses StartGame with an independent Python reader over UDP and checks ChunkRadiusUpdated. No Minecraft client was used.

## Reference-run slice (local, IPv4-only host)

| Toolchain | Configuration | Result |
|---|---|---|
| GCC 13.3.0 | Debug + ASan/UBSan | PASS, 17/17 CTest |
| GCC 13.3.0 | Release | PASS, 17/17 CTest |
| Clang 18.1.3 | Release | PASS, 17/17 CTest |

The original BDS 1.6.0.15 ran locally (OpenSSL 1.0.2u, IPv6 shim) and the independent client in tools/reference joined it through StartGame and 113 FullChunkData packets. `level_packets` now also checks byte equality with the captured StartGame; `raknet_handshake` runs encrypted joins against BDS-RE with that same client (StartGame now spans split RakNet messages). `discovery` checks the original advertisement format. No Minecraft client was used.

## Merge review of clever-newton-bboqgm

Reviewed source: `f8a6f27ce1aa462e577b1a5537aacb0f199327da` from
`claude/clever-newton-bboqgm`, merged against `819c225` in a separate worktree.
No merge conflicts occurred; the original workspace's uncommitted work was preserved.
The independent synthetic StartGame fixture now compares every byte instead of only
checking the encoded length; both this check and the captured reference fixture pass.

Local review ran GCC 14.2 Debug and ASan+UBSan: 17/17 CTest passed in each,
including live IPv4/IPv6 UDP, encrypted Login, resource pack negotiation and StartGame.
The integration client requires Python `cryptography`, installed in the local tool
virtual environment before running the full suite.

[Native CI run 43872363343](https://github.com/canhphung/BDS-RE/actions/runs/43872363343)
for `f8a6f27` passed all seven jobs (GCC/Clang Debug/Release, sanitizers,
Windows MSVC Debug/Release). Both Windows job logs explicitly report 17/17 tests
passed. These are branch results, not a claim that the final merge commit has
already run in CI. Chunk streaming, Minecraft client spawn and gameplay remain pending.
