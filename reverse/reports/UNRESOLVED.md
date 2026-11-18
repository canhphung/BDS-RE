# Unresolved

- ~~Windows ZIP exceeds attachment transfer limit (32 MiB); matching EXE/PDB GUID+Age not inspected.~~ Resolved 2026-11-18: the 42,216,524-byte `BDS-windows.zip` was supplied and unpacked. EXE RSDS GUID `093aad07-19a7-2d45-afa7-9fe3abd628fe`/Age 1 equals the PDB MSF info-stream GUID/Age, and the PDB public for `SharedConstants::NetworkProtocolVersion` resolves to EXE bytes `1a01` = 282. See [INPUT.md](../../docs/INPUT.md) and `reverse/evidence/windows_pdb_match.json`. PDB *type* layouts are still not used as ground truth: only public symbol records and compiland file lists have been read.
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

Windows artifacts: a BDS-windows.rar (31,278,032 bytes, SHA256 968d889368e40369afe188b34013bcf5d36bd43ecd667856acb115bc4083f420) was supplied first and could not be unpacked here. The later `BDS-windows.zip` (42,216,524 bytes, SHA256 182902f542756389fb7c849c7d1768680cf3c5d9824dc09d3976acdfa95eeb1e) was unpacked; the EXE/PDB GUID+Age match is verified and recorded in `reverse/evidence/windows_pdb_match.json`. Still open: Windows-side class layouts/vtable layouts are not validated, no Windows execution happened, and no Linux↔Windows function identity is claimed beyond demangled public names.

Address-family fallback: RakPeerHelper::peerStartup handling of result 5 (port in use) and the meaning of IPSupportInterface slots +0x10/+0x18 are not reconstructed; the independent server treats a port conflict as fatal.

Batching: whether the original disconnects after a PlayStatus login failure, the exact Util::decompress failure handling, and any original decompressed-size bound are not traced. Sub-client (split-screen) packets are rejected rather than reconstructed. Login certificate-chain verification (ConnectionRequest::verify/verifySelfSigned, Certificate::isValid) and the encryption handshake remain the next binary targets.

Authentication: the handler flag at +0xc0 is INFERRED to be online-mode from control flow; its initialisation from PropertiesSettings is not traced. The original algorithm table also lists RS256/RS384/RS512/ES256/ES512; the reconstruction accepts ES384 only. Behaviour on a genuine Xbox Live chain is untested (no capture). The batching note above is partly resolved: version-mismatch PlayStatus is followed by disconnectClient with outdatedClient/outdatedServer.

Encryption: the `signedToken` claim condition (virtual at handler+0x170, value at +0x1a8) and whether getEncryptedPeerForUser can return null for remote RakNet peers are not traced; the reconstruction always encrypts and never sends signedToken. The meaning of Util::base64_encode's boolean argument (passed true for the salt) is not traced; standard padded base64 is used.

Admission/packs: the +0x68 whitelist flag and the "realms" comparison operand are inferred from control flow; tryUpdateEntries' second branch (gamertag update) and whether it also fills the UUID are inferred. Whether the original's ResourcePacksInfo/ResourcePackStack include vanilla packs depends on ResourcePackManager stack contents and pack category/origin filters (0x182b1b2, 0x182b1cd) that are not traced; the reconstruction sends empty lists. sendLoginMessageLocal and player creation (StartGame) are the next targets.

StartGame: the full block palette (runtime-built), the dimension-derived chunk radius limit (virtual +0x120), the levelId value LevelStorage provides for a dedicated server, the enchantment seed source, the multiplayer correlation id, and the actual LAN/XBL/platform broadcast settings of a dedicated server are not recovered; the reconstruction uses documented defaults. Remaining _sendLevelData/_sendAdditionalLevelData packets and chunk serialization (FullChunkData 0x3a) are the next targets.

Reference run (2026-11-16): the original logs "Error opening ops file: ops.json" although the supplied guide names opsfile.json; BDS-RE still reads opsfile.json until the original's file format and precedence (ops.json vs permissions.json) are traced. The IPv4-only startup retry still opening an IPv6 socket is original behaviour on IPv6-less hosts. The palette order was captured once; whether it changes between runs is untested and does not matter on the wire.
