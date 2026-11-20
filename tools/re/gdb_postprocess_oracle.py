"""Record the original's chunk post-processing (Linux 1.6.0.15, overworld).

Sourced by capture_terrain.py --postprocess next to gdb_terrain_oracle.py.
Every block write of the level goes through LevelChunk::setBlock 0x1de2560
or LevelChunk::setExtraBlock 0x1de2980 (BlockSource::setBlock and
setExtraBlock call them; loadChunk fills chunks with setAllBlocks instead).
The timeline written to POSTPROCESS_ORACLE_OUT (JSON lines) holds, in the
order the server ran them:

- {"e": "load", "x", "z"}: loadChunk 0x1b388c7, after the carvers (the
  chunk then holds the carved BlockVolume, see gdb_terrain_oracle.py);
- {"e": "begin", "t", "x", "z", "area"}: postProcess 0x1b38a1f, after
  PostprocessingManager::tryLock succeeded for the 3 x 3 area around the
  chunk (ChunkViewSource bounds min + 1), with the state (LevelChunk +0x68)
  and heightmap (+0xd18, 256 shorts, index z * 16 + x) of each loaded chunk
  of the area;
- {"e": "decorate", "t", "biome", "plains_flowers", "plains_grass"}: the
  Biome::decorate vcall 0x1b3955e (rdi = the Biome at LevelChunk::getMax),
  with the plains decorator's current flower and grass counts;
- {"e": "end", "t"}: postProcess 0x1b39804, after tickAllPendingTicks;
- {"e": "set", "t", "x", "y", "z", "b", "l"}: a write of block "name:data"
  at world position x, y, z on layer l (0 setBlock, 1 setExtraBlock), with
  the gdb thread number t (writes outside a postProcess belong to whatever
  else that thread runs).

LevelChunk +0x28 is its ChunkPos (LevelChunk::getPosition 0x1de4b70);
ChunkBlockPos is {u8 x, u8 z, i16 y}.

At the first postProcess the block registry is dumped once to
POSTPROCESS_ORACLE_OUT with "_blocks.json" in place of ".jsonl": every
BlockLegacy of BlockTypeRegistry::mBlockLookupMap 0x34a9340 (unordered_map
of name -> WeakPtr<BlockLegacy>) with its class (vtable), raw bytes 0..0x1c0 (the reference keeps 0..0xc0) and
its Material (BlockLegacy +0x78) raw bytes 0..0x18. Field offsets used by
the translation: BlockLegacy +0x58 BlockProperty bits, +0x78 Material*,
+0xaa solid, +0xb4 light block, +0xb5 light emission; Material +0 type,
+7 replaceable, +8 liquid, +0xc translucency, +0x10 blocks motion, +0x12 solid.
"""
import json
import os
import struct
import gdb

OUT = os.environ["POSTPROCESS_ORACLE_OUT"]
names = {}
out = open(OUT, "a", buffering=1 << 20)


def r(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xffffffffffffffff


def mem(address, count):
    return bytes(gdb.selected_inferior().read_memory(address, count))


def q(address):
    return struct.unpack("<Q", mem(address, 8))[0]


def string(address):
    pointer, length = struct.unpack("<QQ", mem(address, 16))
    return mem(pointer, length).decode() if length else ""


def block_name(block):
    if block not in names:
        legacy = q(block + 0x10)
        data = struct.unpack("<H", mem(block + 8, 2))[0]
        names[block] = "%s:%d" % (string(legacy + 0x28), data)
    return names[block]


def emit(record):
    out.write(json.dumps(record, separators=(",", ":")) + "\n")


def thread():
    return gdb.selected_thread().num


chunks = {}


class Load(gdb.Breakpoint):
    # loadChunk -0x20 = &ChunkPos, -0x10 = LevelChunk*.
    def stop(self):
        frame = r("rbp")
        x, z = struct.unpack("<2i", mem(q(frame - 0x20), 8))
        chunks[(x, z)] = q(frame - 0x10)
        emit({"e": "load", "x": x, "z": z})
        return False


VTABLES = {}
dumped = []
# BlockLegacy virtuals with no argument but `this`: +0x160 canContainLiquid
# (Block::canContainLiquid 0x1abb340).
VIRTUALS = (0x160,)


def vtable_names():
    import subprocess
    binary = os.environ.get("POSTPROCESS_ORACLE_BIN", "./bedrock_server")
    for line in subprocess.run(["nm", "-C", binary], capture_output=True, text=True).stdout.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[2].startswith("vtable for "):
            VTABLES[int(parts[0], 16) + 0x10] = parts[2][len("vtable for "):]


def dump_blocks():
    # std::unordered_map<std::string, WeakPtr<BlockLegacy>>: +0x10 first node
    # (_M_before_begin._M_nxt); node = {next, std::string key, WeakPtr value,
    # cached hash}; the value's first word is the BlockLegacy*.
    vtable_names()
    node, rows = q(0x34a9340 + 0x10), []
    while node:
        try:
            name = string(node + 8)
            legacy = q(node + 0x28)
            if not legacy:
                rows.append({"name": name, "legacy": None})
            else:
                material = q(legacy + 0x78)
                row = {"name": name, "legacy_name": string(legacy + 0x28), "class": VTABLES.get(q(legacy)),
                       "raw": mem(legacy, 0x1c0).hex(),
                       "material": mem(material, 0x18).hex() if material else None, "virtuals": {}}
                # Virtuals that take only the BlockLegacy, evaluated by the
                # original: vtable offset -> result byte.
                for offset in VIRTUALS:
                    fn = q(q(legacy) + offset)
                    row["virtuals"][hex(offset)] = int(gdb.parse_and_eval(
                        "((unsigned char(*)(long))%d)(%d)" % (fn, legacy))) & 1
                rows.append(row)
        except (gdb.MemoryError, UnicodeDecodeError, OverflowError) as error:
            row = {"node": hex(node), "error": str(error), "raw_node": mem(node, 0x40).hex()}
            try:
                row["value"] = mem(q(node + 0x28), 0x20).hex()
            except gdb.MemoryError:
                pass
            rows.append(row)
        node = q(node)
    with open(OUT.replace(".jsonl", "_blocks.json"), "w") as f:
        json.dump(sorted(rows, key=lambda r: r.get("name", "")), f, indent=0)


def dump_decorators():
    # Biome::mBiomes 0x348b670 (vector<unique_ptr<Biome>>); OverworldBiome
    # +0xc0 = unique_ptr<OverworldDecorator> (OverworldBiome::decorate
    # 0x1db5a70), MutatedBiome +0xc0 = its parent Biome. Per decorator: class,
    # raw bytes 0..0x230 and, for every 8-byte slot that points at an object
    # with a known vtable, that object's class and raw bytes 0..0x60.
    begin, end = struct.unpack("<QQ", mem(0x348b670, 16))
    rows = []
    for slot in range(begin, end, 8):
        biome = q(slot)
        if not biome:
            continue
        entry = {"id": struct.unpack("<i", mem(biome + 0x88, 4))[0], "biome_class": VTABLES.get(q(biome))}
        try:
            decorator = q(biome + 0xc0)
            cls = VTABLES.get(q(decorator)) if decorator else None
            if cls and cls.endswith("Decorator"):
                raw = mem(decorator, 0x230)
                members = {}
                for offset in range(8, 0x230, 8):
                    pointer = struct.unpack_from("<Q", raw, offset)[0]
                    try:
                        member_class = VTABLES.get(q(pointer)) if pointer > 0x10000 else None
                    except gdb.MemoryError:
                        member_class = None
                    if member_class:
                        members[hex(offset)] = {"class": member_class, "raw": mem(pointer, 0x60).hex()}
                entry.update({"decorator_class": cls, "raw": raw.hex(), "members": members})
        except gdb.MemoryError as error:
            entry["error"] = str(error)
        rows.append(entry)
    with open(OUT.replace(".jsonl", "_decorators.json"), "w") as f:
        json.dump(rows, f, indent=0)


class Begin(gdb.Breakpoint):
    # postProcess: -0x48 = &ChunkPos of the centre chunk (LevelChunk::getPosition).
    def stop(self):
        if not dumped:
            dumped.append(1)
            dump_blocks()
            dump_decorators()
        x, z = struct.unpack("<2i", mem(q(r("rbp") - 0x48), 8))
        area = {}
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                chunk = chunks.get((x + dx, z + dz))
                if chunk:
                    # LevelChunk +0xd18 heightmap (256 shorts, index z * 16 + x), +0x68 ChunkState.
                    area["%d,%d" % (x + dx, z + dz)] = {
                        "state": mem(chunk + 0x68, 1)[0],
                        "heightmap": list(struct.unpack("<256h", mem(chunk + 0xd18, 512)))}
        emit({"e": "begin", "t": thread(), "x": x, "z": z, "area": area})
        return False


class Decorate(gdb.Breakpoint):
    # Also records the plains decorator's flower/grass counts (+0x1e8, +0x1ec):
    # PlainsBiome::Decorator::decorate rewrites them per chunk and
    # OverworldDecorator::decorate 0x1da5615 reads the plains grass count
    # for every biome (VanillaBiomes::mPlains 0x348a748, decorator +0xc0).
    def stop(self):
        plains = q(q(0x348a748) + 0xc0)
        flowers, grass = struct.unpack("<2i", mem(plains + 0x1e8, 8))
        emit({"e": "decorate", "t": thread(), "biome": struct.unpack("<i", mem(r("rdi") + 0x88, 4))[0],
              "plains_flowers": flowers, "plains_grass": grass})
        return False


class End(gdb.Breakpoint):
    def stop(self):
        emit({"e": "end", "t": thread()})
        out.flush()
        return False


class Set(gdb.Breakpoint):
    def __init__(self, address, layer):
        super().__init__("*" + hex(address), internal=True)
        self.layer = layer

    def stop(self):
        chunk = r("rdi")
        cx, cz = struct.unpack("<2i", mem(chunk + 0x28, 8))
        bx, bz, by = struct.unpack("<BBh", mem(r("rsi"), 4))
        emit({"e": "set", "t": thread(), "x": cx * 16 + bx, "y": by, "z": cz * 16 + bz,
              "b": block_name(r("rdx")), "l": self.layer})
        return False


class State(gdb.Breakpoint):
    # LevelChunk::changeState(ChunkState from, ChunkState to) 0x1de3540.
    def stop(self):
        cx, cz = struct.unpack("<2i", mem(r("rdi") + 0x28, 8))
        emit({"e": "state", "t": thread(), "x": cx, "z": cz, "from": r("rsi") & 0xff, "to": r("rdx") & 0xff})
        return False


Load("*0x1b388c7", internal=True)
State("*0x1de3540", internal=True)
Begin("*0x1b38a1f", internal=True)
Decorate("*0x1b3955e", internal=True)
End("*0x1b39804", internal=True)
Set(0x1de2560, 0)
Set(0x1de2980, 1)
