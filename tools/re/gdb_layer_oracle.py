"""Call every biome layer of the original once (Linux 1.6.0.15).

Sourced by capture_terrain.py --layer-oracle. A one-shot breakpoint at
prepareHeights 0x1b36b47 stops the server thread; then `layer_dump()` walks
the layer graph from the BiomeSource (raw layer at BiomeSource +0x10, zoomed
at +0x20; Layer +0x30 parent, RegionHills/RiverMixer +0x40, OceanMixer +0x40
and +0x50) and calls each layer's fillArea (vtable +0x18) for a few areas on a
LayerData made of two 64K-int buffers. Output: LAYER_ORACLE_OUT (JSON).
"""
import json
import os
import struct
import subprocess
import gdb

OUT = os.environ["LAYER_ORACLE_OUT"]
BIN = os.environ.get("LAYER_ORACLE_BIN", "./bedrock_server")
AREAS = [(-2, -2, 10, 10), (37, -61, 10, 10), (-120, 85, 12, 12), (-8, -8, 16, 16)]
VTABLES = {}
for line in subprocess.run(["nm", "-C", BIN], capture_output=True, text=True).stdout.splitlines():
    parts = line.split(" ", 2)
    if len(parts) == 3 and parts[2].startswith("vtable for ") and parts[2].endswith(("Layer", "Zoom")):
        VTABLES[int(parts[0], 16) + 0x10] = parts[2][len("vtable for "):]


def mem(address, count):
    return bytes(gdb.selected_inferior().read_memory(address, count))


def q(address):
    return struct.unpack("<Q", mem(address, 8))[0]


class Stop(gdb.Breakpoint):
    def stop(self):
        self.enabled = False
        return True


Stop("*0x1b36b47", internal=True)


def layer_dump():
    # The BiomeSource the fillRawBiomeData vcall received (RawSource below).
    source = int(gdb.parse_and_eval("$biome_source")) & 0xffffffffffffffff
    roots = {"raw": q(source + 0x10), "zoomed": q(source + 0x20)}
    seen, order = {}, []

    def visit(layer):
        if not layer or layer in seen:
            return
        vt = q(layer)
        name = VTABLES.get(vt)
        if name is None:
            return
        entry = {"address": hex(layer), "class": name, "base_seed": struct.unpack("<q", mem(layer + 0x28, 8))[0],
                 "world_seed": struct.unpack("<q", mem(layer + 0x18, 8))[0], "children": []}
        seen[layer] = entry
        for offset in (0x30, 0x40, 0x50):
            child = q(layer + offset)
            try:
                valid = child > 0x10000 and q(child) in VTABLES
            except gdb.MemoryError:
                valid = False
            if valid:
                entry["children"].append({"offset": offset, "address": hex(child)})
                visit(child)
        order.append(layer)

    for root in roots.values():
        visit(root)
    buffers = int(gdb.parse_and_eval("(long)malloc(%d)" % (2 * 65536 * 4 + 64))) & 0xffffffffffffffff
    data = buffers + 2 * 65536 * 4
    inferior = gdb.selected_inferior()
    for layer in order:
        entry = seen[layer]
        fill = q(q(layer) + 0x18)
        entry["outputs"] = []
        for x, z, w, h in AREAS:
            inferior.write_memory(data, struct.pack("<QQ", buffers, buffers + 65536 * 4))
            gdb.parse_and_eval("((void(*)(long,long,int,int,int,int))%d)(%d,%d,%d,%d,%d,%d)" %
                               (fill, layer, data, x, z, w, h))
            result = q(data)
            values = list(struct.unpack("<%di" % (w * h), mem(result, 4 * w * h)))
            entry["outputs"].append({"x": x, "z": z, "w": w, "h": h, "values": values})
    with open(OUT, "w") as f:
        json.dump({"roots": {k: hex(v) for k, v in roots.items()}, "layers": [seen[l] for l in order]}, f)


class RawSource(gdb.Breakpoint):
    # 0x1b36b44: the fillRawBiomeData vcall; rdi = BiomeSource.
    def stop(self):
        gdb.set_convenience_variable("biome_source", int(gdb.parse_and_eval("$rdi")) & 0xffffffffffffffff)
        self.enabled = False
        return False


RawSource("*0x1b36b44", internal=True)
