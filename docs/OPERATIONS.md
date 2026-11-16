# Current server operations

Run `bds_server` from the distribution root, or use the development override `--root <directory>`. It reads `server.properties`, `whitelist.json` and `opsfile.json` from that root, selects `worlds/<level-name>`, and opens the configured IPv4/IPv6 UDP ports for offline discovery. On hosts without one address family the server logs `IPv6 not supported` (or `IPv4 not supported`) and continues on the other, matching the original RakPeerHelper::LogIPSupport wording; it fails only if neither family is available or a port cannot be bound. Missing properties use reconstructed defaults. The unchanged supplied file contains `level-name=Bedrock level`, so that is the selected directory when using the repository root.

World state currently uses `bdsre-world.snapshot` inside the selected directory. This is still the internal BDS-RE format, not LevelDB. If `level.dat` or `db` is present, startup fails before modifying the world. `--world <file>` remains a development override; `--network 0` disables socket binding for isolated harness tests. `--ticks` remains a finite development run, not an original BDS setting.

## Configuration and known differences

The model parses the documented settings, textual/numeric gamemode and difficulty aliases, booleans, seeds and level types. Original binary evidence identifies the permission key as `default-player-permission-level`. The shorter spelling in the shipped file is reported as unknown and does not silently become an alias. The original file itself is preserved unchanged. Original constructor defaults include max-threads=8 and view-distance=10; configured file values override them. max-threads=0 is accepted but a worker pool is not implemented yet.

The original constructor skips lines beginning with # or a literal space. The new parser follows that rule, trims key/value whitespace, processes duplicate keys in order and bounds input size. It deliberately rejects invalid known values, out-of-range ports and tick distances rather than reproducing permissive strtol behavior. It splits on the first = to preserve strings; full original behavior for multiple separators is not yet matched. level-name must be a single directory name within worlds; traversal is rejected. These are explicit implementation policies, not claims of exact historical error handling.

Settings are not all enforced yet: server-name, level-name and ports affect discovery/runtime selection. World generation, new-player mode, gameplay difficulty/cheats, view/tick radii, authentication, admission limits, idle timeout, texture pack requirements and permissions need the remaining world/session/gameplay implementations. Parsing a value does not establish that feature's behavior. The guide's omitted-thread-count behavior conflicts with the constructor's default 8; the binary observation is recorded in reverse/reports/OPERATIONS.md.

## Console

Commands execute on the simulation owner; stdin is polled without a blocking reader-thread join. EOF alone does not stop a running server. Finite runs and SIGINT/SIGTERM still work. Redirected UTF-8 console input is supported; interactive Windows console editing currently handles ASCII key input and backspace only.

- `stop`: leave the loop, save and exit.
- `whitelist on|off`: update the in-memory setting only.
- `whitelist list|reload`: inspect or reload whitelist.json.
- `whitelist add|remove "Example Name"`: atomically persist an entry without rewriting server.properties. Existing xuid/ignoresPlayerLimit fields of retained entries are preserved.
- `ops list|reload`: inspect/reload XUIDs from opsfile.json.
- `changesetting allow-cheats true|false`, `changesetting difficulty peaceful|easy|normal|hard` (or 0..3): update in-memory settings only. Gameplay effects are pending; these commands do not rewrite properties.
- `save hold|query|resume`: internal storage backup lifecycle described below.

Malformed JSON reloads fail without replacing the current lists. Entry/file bounds, exact case-sensitive matching and duplicate rejection are implementation policies awaiting original-runtime comparison. XUID-bound entries do not grant access based on a different XUID with a matching name. Operator membership and whitelist lookup APIs exist and are tested, but no connected session admission exists yet. XUID authentication/resolution and ignoresPlayerLimit capacity rules are pending. `kick` reports unavailable sessions instead of success.

## Backup

`save hold` captures the current internal World and schedules a worker to write an immutable checkpoint. Capture copies world memory on the simulation thread; file encoding and I/O run asynchronously. `save query` reports preparing, failure, or a quoted filename and its byte length once ready. The file is held under worlds/<level-name>/.bdsre-backups/<id>/world.snapshot and remains unchanged while simulation continues. Copy precisely that many bytes to back it up. `save resume` releases the checkpoint. A second hold is rejected; resume while preparing is rejected and may be retried once ready. Shutdown can wait for an in-flight worker, and unresumed files remain on disk.

This implements the documented control flow for the independent snapshot backend. It does not reproduce original LevelDB file retention, asynchronous database flush semantics or the original query output wording. A backup can currently restore BDS-RE state only, not an original BDS world. Cross-version world loading remains unimplemented.

## Networking boundary

IPv4/IPv6 UDP offline discovery handles RakNet ping IDs 1 and 2 with verified magic bytes and returns pong ID 0x1c, echoed timestamp, server GUID and length-prefixed advertisement. Processing is bounded per poll; malformed datagrams are dropped. The advertisement reports protocol 282 and version 1.6.0 (as recovered from the original networking version formatter), with zero connected players because sessions are not implemented. Names have delimiter/control filtering and a bounded advertised size; stored configuration remains unchanged. The final announcement flag is currently 0; platform-dependent interpretation is not fully recovered.

A bounded protocol-282 LoginPacket body decoder is implemented separately and tested against a synthetic binary fixture. Its return type is explicitly unverified and it is not connected to UDP. It does not validate JWTs or authenticate clients. See reverse/reports/LOGIN.md.

RakNet 9 open/connected handshakes and bounded reliable/ordered/sequenced receive, ACK/NACK, retry and split-message reassembly are now implemented. See reverse/reports/RAKNET.md for limits and policies. A transport peer is not a Minecraft player.

Application messages with ID 0xfe are decoded as zlib-compressed game-packet batches (see reverse/reports/NETWORK.md) and passed to a per-peer protocol state machine. The first packet must be LoginPacket. A protocol other than 282 receives PlayStatus 1 (older client) or 2 (newer client) followed by Disconnect `disconnectionScreen.outdatedClient`/`outdatedServer`, as in the original handler. A protocol-282 login is authenticated: the ES384 certificate chain must be anchored in the Mojang root key (or, with `online-mode=false`, may be self-signed) and within its nbf/exp window, and the client-data token must be signed by the leaf identity key. Failures receive Disconnect `disconnectionScreen.notAuthenticated` (shown translated by the client). An authenticated login is logged as `login authenticated: "<name>" xuid=<xuid>` and receives ServerToClientHandshake (ES384 token with a 16-byte salt, signed by a P-384 key generated at server start); everything after it is AES-256-CFB8 encrypted with per-packet checksums. When the client answers with ClientToServerHandshake the server logs `Player connected: <name>`, sends PlayStatus LoginSuccess and then an encrypted Disconnect, because resource packs, StartGame and world streaming are not implemented yet. Whitelist and max-players admission are not enforced yet. The connection is then closed and the reason logged as `closing connection: ...`. Any other application message, malformed batch, sub-client packet or packet before Login closes the RakNet connection. Login/JWT verification, encryption, spawn, StartGame, chunk streaming and networked gameplay are not implemented; unknown offline packets are ignored. Loopback packet tests do not count as real-client verification. These remain the next required phases, not features claimed by the discovery listener.
