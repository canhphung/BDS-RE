# BDS 1.6.0.15 operational compatibility contract

Source: the user-supplied “How to use the dedicated server” document, read on 2026-11-16. These are documented requirements, not VERIFIED_RUNTIME findings. Binary analysis and differential tests must resolve details the document does not specify. Independent implementation means independently authored code; it does not justify changing the external behavior being reconstructed.

## Current gaps

The current executable is a development harness. Its --ticks, --tick-ms, --max-catch-up and --world flags, snapshot format and configurable catch-up policy are project-specific mechanisms. Their tests validate internal behavior only. They do not establish compatibility with original configuration, saves, console commands, authentication or clients. The custom snapshot must remain identified as a development format, not the production BDS world format.

| Original documented interface | Current implementation | Acceptance requirement |
|---|---|---|
| Read server.properties at startup, one key=value per line | Missing; CLI configuration only | Implement documented names, defaults, enum aliases and application lifecycle; distinguish unknown details from observed behavior |
| World selected by level-name under worlds/; create worlds/ at startup | Missing; --world selects a single custom snapshot | Default runtime must follow original directory selection; retain custom snapshots only as an explicit development format until original storage support is implemented |
| behavior_packs and resource_packs locations | Missing | Preserve documented directory roles; do not claim loading or activation before implementation |
| Console stop | Missing; signals/finite tick count only | Parse console input, schedule shutdown on simulation owner, save and exit cleanly |
| whitelist.json and whitelist commands | Missing | Persist entries; reload explicitly; preserve name, optional xuid and ignoresPlayerLimit semantics |
| opsfile.json and ops list/reload | Missing | Load XUID entries and reload on command; enforce roles once sessions exist |
| changesetting allow-cheats/difficulty | Missing | Apply changes in memory without rewriting server.properties |
| save hold/query/resume | Missing; atomic replacement of one snapshot only | Implement asynchronous backup preparation and retained-file lifecycle; query reports file paths and copy lengths while simulation continues; resume releases retention |
| Networking, Xbox Live authentication and session limits | Missing | Implement and test actual protocol behavior; parsing configuration alone does not enforce any of these settings |

## Guide defaults

The following table records defaults stated in the guide. It is not a claim about the shipped file or built-in parser defaults; differences from the supplied archive are recorded below.

## Documented configuration

| Key | Default | Values or meaning |
|---|---|---|
| gamemode | survival | survival/0, creative/1, adventure/2; always or only for new players (exact condition needs verification) |
| difficulty | easy | peaceful/0, easy/1, normal/2, hard/3; always |
| level-type | DEFAULT | FLAT, LEGACY, DEFAULT; world creation |
| server-name | Dedicated Server | String; advertised name |
| max-players | 10 | Integer; player admission limit |
| server-port | 19132 | Integer; IPv4 port |
| server-portv6 | 19133 | Integer; IPv6 port |
| level-name | level | World directory selection under worlds/ |
| level-seed | empty | String; used on world creation, empty selects a random seed |
| online-mode | true | Xbox Live authentication required when enabled |
| white-list | false | Restrict admission to whitelist when enabled |
| allow-cheats | false | Boolean |
| view-distance | 10 | Integer; maximum allowed view distance |
| player-idle-timeout | 30 | Idle minutes before kick |
| max-threads | 8 | Maximum thread count the server tries to use |
| tick-distance | 4 | Integer 4..12; simulation radius in chunks, not a timing interval |
| default-player-permission-level | member | visitor, member, operator |
| texturepack-required | false | Require world-specific texture packs |

The document does not fully define malformed values, duplicate keys, whitespace, comments, bounds for all integer options, path normalization or error handling. Investigate these instead of inventing historical behavior. Runtime-only mutations must not silently persist into server.properties.

Whitelist command arguments support quoted gamertags. Missing XUID may be resolved on first matching connection. ignoresPlayerLimit is not unlimited capacity: the document describes an additional soft limit whose exact behavior requires verification. opsfile.json is the documented operator filename for this version; do not substitute a newer version's filename without evidence.

## Revised implementation sequence

1. Implement original server.properties parsing and settings model, documenting unsupported enforcement and experimentally unresolved parser behavior. Keep harness flags distinct from original settings.
2. Implement original world directory selection and console stop with lifecycle integration tests. Do not disguise the custom snapshot as an original Bedrock save.
3. Add whitelist/operator file models, command parsing and runtime-only changesetting semantics. Enforce admission/permissions when real sessions exist; report unavailable operations honestly in the meantime.
4. Recover original storage and backup lifecycle from binary/reference behavior. A single-file rename is not equivalent to save hold/query/resume; do not add commands that falsely report success.
5. Continue protocol 282 discovery, connection/login, authoritative world streaming and gameplay slices. Track real-client verification separately.

The guide's launch and example commands are reference behavior to reconstruct, not instructions to execute the proprietary server during this document review. No new original-binary execution or runtime compatibility verification was performed here.

## Shipped server.properties versus the guide

The original root server.properties was extracted byte-for-byte from the user-supplied BDS-Linux.zip. It matches the user's pasted configuration after removing message-formatting backslashes. Preserve the actual archive bytes rather than copying chat escaping.

| Setting | Guide | Shipped file / comments |
|---|---|---|
| view-distance | Default 10 | Configured value 32; positive integer |
| level-name | Default level | Configured value Bedrock level; preserve embedded space |
| Permission key | default-player-permission-level | default-player-permission=member |
| max-threads | Default 8 | Configured value 8; comment says 0 or an omitted key selects as many threads as possible |
| level-seed | Empty default | Example line is commented out; do not treat the sample seed as active |
| level-type | DEFAULT, creation-time | Not present in shipped configuration |

The shipped comments additionally specify positive max-players, view-distance and player-idle-timeout; ports 1..65535; tick-distance 4..12. max-threads=0 is an explicit exception to the following positive-integer comment.

Neither the file nor the guide alone proves the binary's behavior for absent keys, invalid inputs or the two permission-key spellings. Trace the original property lookup before choosing alias behavior; do not silently replace the shipped spelling with the guide spelling. Distinguish configured values from actual built-in defaults. Numeric gamemode/difficulty aliases appear in the guide but not in the shipped comments and still require parser verification.

The file is now included as reference configuration. The current executable still lacks server.properties parsing; adding the file does not enforce the listed gameplay, network, authentication or permission settings.
