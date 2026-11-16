# Internal world snapshots, version 1

This is an independently designed checkpoint format for BDS-RE development. It is not Bedrock LevelDB, level.dat, chunk serialization or a Minecraft packet format. IDs are project-internal uint16 values; zero means air.

All integer fields are little-endian and have no native struct padding:

| Offset | Width | Field |
|---|---|---|
| 0 | 8 | Magic bytes `BDSREW\r\n` |
| 8 | 4 | Format version, currently 1 |
| 12 | 8 | World age, unsigned |
| 20 | 8 | Number of non-air block records |
| 28 | 12 × count | Block records |
| 28 + 12 × count | 4 | CRC32 of all preceding bytes |

Each record contains X (signed 32-bit two's-complement), Y (uint16), Z (signed 32-bit two's-complement), internal block ID (uint16). Y must be 0..255 and ID must be nonzero. Duplicate coordinates, trailing bytes, unknown versions and checksum mismatches are rejected. CRC32 is the standard reflected IEEE polynomial 0xEDB88320 with initial/final XOR 0xFFFFFFFF; it detects accidental corruption, not malicious changes.

Encoding order is chunk X, chunk Z, vertical section, local Y, local Z, local X, ascending. Loading permits other record orders but rejects duplicates. The golden fixture in persistence_tests.cpp was independently generated with Python struct.pack/zlib.crc32.

Default limits are 1,000,000 records and 4,096 allocated sections. File loading bounds allocation to at most 12,000,032 bytes of input; decoded sections use roughly 32 MiB of block arrays plus container overhead. Custom decode limits may tighten these caps. Loading builds a temporary World and returns only after complete validation. Save checks the same default limits so its output can be loaded again.

`WorldStore::save` creates a staging filename within a uniquely created sibling directory, writes/flushes/closes it, then replaces the destination with POSIX rename or Windows MoveFileExW. An I/O or rename exception is reported; staging files are cleaned on normal stack unwinding. An existing valid snapshot is retained if encoding, staging write or final rename fails. Parent directories must already exist. Only one process may write a destination at a time. There is no fsync-based power-loss guarantee; abrupt termination can leave a staging directory.

```sh
./build/bds_server --world build/world.snapshot --ticks 20
./build/bds_server --world build/world.snapshot --ticks 10
# Saved age is now 30; each invocation reports its own tick count.
```

Default execution now selects worlds/<level-name>/bdsre-world.snapshot using server.properties. --world explicitly overrides the checkpoint path for development. A missing snapshot starts an empty world; an invalid existing file fails startup and is not overwritten. Save occurs on clean finite completion or handled SIGINT/SIGTERM, not on crashes, forced termination or exceptions. World age exhaustion throws rather than wrapping. Gameplay commands and network-driven mutations are not implemented yet.
