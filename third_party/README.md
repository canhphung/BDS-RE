# Vendored dependencies

nlohmann/json 3.11.3 (MIT) is used for whitelist/operator JSON and, in tests only, to check the generated packet id header against its evidence file. Files were downloaded over verified HTTPS from the upstream v3.11.3 tag; LICENSE.MIT is included. No build-time dependency download is required.

- json.hpp: https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp
  SHA256: 9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6
- LICENSE.MIT: https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT
  SHA256: 86b998c792894ccb911a1cb7994f7a9652894e7a094c0b5e45be2f553f45cf14

## zlib 1.3.1 (zlib license)

Used for 0xfe game-packet batches (deflate level 7, 15-bit window with zlib header, matching the original's leveldb::ZlibCompressor). Built as the static `bds_zlib` target from the deflate/inflate sources only; gz* file I/O, compress.c, uncompress.c, infback.c and build scripts are not vendored. Files are unmodified copies from the official release archive:

- https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
  SHA256: 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
- Vendored files: adler32.c crc32.c crc32.h deflate.c deflate.h gzguts.h inffast.c inffast.h inffixed.h inflate.c inflate.h inftrees.c inftrees.h trees.c trees.h zconf.h zlib.h zutil.c zutil.h LICENSE
