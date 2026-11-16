# Unresolved

- Windows ZIP exceeds attachment transfer limit (32 MiB); matching EXE/PDB GUID+Age not inspected. A split archive or smaller individual artifacts would unblock inspection.
- Native Windows Debug/Release build and CTest now pass in CI run 43835644043 at ce383cd. Local Windows execution remains unavailable; real-client Windows verification is still pending.
- Clang and Ghidra absent locally; GCC and binutils available. Linux Clang Debug/Release build and CTest now pass in CI run 43835644043; local Clang tests remain unrun.
- Complete bootstrap and timer time-synchronization semantics; update-to-level dispatch is now traced through GameSession and ServerLevel primary vtable.
- Complete callers, remaining indirect dispatch targets, RTTI and inheritance validation, full class layouts.
- Worker timing observed statically; original runtime/differential tests not executed.
- Protocol constant verified as 282; discovery/login wire formats and RakNet behavior, login, chunk serialization, original LevelDB persistence, entities and gameplay. Independent internal chunk storage and snapshots are implemented; they do not establish original format compatibility.
- No real client or captures supplied; no compatibility evidence.

Operational follow-up: canonical permission key and initial thread/view defaults now have binary evidence in OPERATIONS.md. Strict malformed-value handling, confined level paths and JSON matching policies are intentional implementation choices rather than verified original behavior. save hold/query/resume exists only for immutable internal snapshots; original LevelDB retention is pending. Offline discovery is implemented and loopback-tested, while full RakNet connection, authenticated login, world streaming and gameplay remain missing.

Current implementation verification: operational controls, backup, discovery and the bounded unverified login decoder pass CI run 43839417910 at 43d46b6 on native Windows/Linux. This does not resolve original storage formats, authenticated login, world streaming, gameplay or real-client verification.

RakNet follow-up: open/connected handshake and a bounded reliability implementation now exist. Original congestion control, security extensions, arbitrary bit-length payload support, application batching, trusted login/certificate validation and real Minecraft interoperability remain unresolved. Supplied module source paths support navigation only; raw metadata is kept locally and no Windows GUID/Age match was independently verified.

Native CI run 43843720476 at 4cdd95e verifies the RakNet foundation with all seven jobs passed, including 13/13 tests on both Windows configurations. It remains an independent implementation/synthetic-client result, not Minecraft client or differential verification.

Windows artifacts: a BDS-windows.rar (31,278,032 bytes, SHA256 968d889368e40369afe188b34013bcf5d36bd43ecd667856acb115bc4083f420) was supplied, but this environment has no RAR extractor (package install failed), so EXE/PDB GUID+Age remain uninspected. A ZIP or 7z re-pack would unblock it.

Address-family fallback: RakPeerHelper::peerStartup handling of result 5 (port in use) and the meaning of IPSupportInterface slots +0x10/+0x18 are not reconstructed; the independent server treats a port conflict as fatal.

Batching: whether the original disconnects after a PlayStatus login failure, the exact Util::decompress failure handling, and any original decompressed-size bound are not traced. Sub-client (split-screen) packets are rejected rather than reconstructed. Login certificate-chain verification (ConnectionRequest::verify/verifySelfSigned, Certificate::isValid) and the encryption handshake remain the next binary targets.
