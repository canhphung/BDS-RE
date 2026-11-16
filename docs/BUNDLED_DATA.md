# Bundled original server data

At the user's explicit request, these directories were extracted without content changes from the supplied BDS-Linux.zip and added at repository root:

| Directory | Files |
|---|---:|
| behavior_packs | 229 |
| definitions | 361 |
| resource_packs | 327 |
| structures | 195 |
| Total | 1,112 |

Source archive SHA256: `aafdef8e5d711abb56814273108cc00febcd54cac1dca4d942a2c169dd158e07`.

Extraction checked archive paths and ZIP CRCs; all extracted file contents were compared against their archive entries by SHA256. The Git index contains all 1,112 selected files. Git does not retain empty directories. These files come from the original distribution, not from the independent C++ implementation. Their presence does not establish pack loading, recipe handling, structure generation or client compatibility in the new engine.

The server executable, shared libraries, ZIP archive, PDBs and generated analysis databases are not included by this import. The Windows ZIP was not used.

The root `server.properties` is also included byte-for-byte from the same archive as reference configuration. Its differences from the bundled guide are tracked in `docs/COMPATIBILITY.md`; the file is not included in the four-directory file counts above.
