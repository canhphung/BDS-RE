#!/usr/bin/env python3
"""vt.py CLASS OFFSET... - resolve vtable slots (byte offsets, hex) of CLASS in the original."""
import os, struct, sys
binary = os.environ.get("BDS_BIN", "input/linux/bds/bedrock_server")
syms_path = os.environ.get("BDS_SYMS")
d = open(binary, "rb").read()
# text/rodata map at vaddr - 0x400000; the data segment at 0x31ceb18 -> file 0x2bceb18
off = lambda a: a - 0x31ceb18 + 0x2bceb18 if a >= 0x31ceb18 else a - 0x400000
syms = {}
for line in open(syms_path):
    p = line.split(" ", 3)
    if len(p) == 4:
        syms.setdefault(int(p[0], 16), p[3].strip())
v = [a for a, n in syms.items() if n == "vtable for " + sys.argv[1]][0] + 0x10
for o in sys.argv[2:]:
    o = int(o, 16)
    a = struct.unpack("<Q", d[off(v + o):off(v + o) + 8])[0]
    print(hex(o), hex(a), syms.get(a, "?")[:110])
