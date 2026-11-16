"""gdb script: call the original BDS `Random` functions and record their outputs.

    LD_LIBRARY_PATH=<openssl>/lib:. gdb -q -batch \
        -ex "py OUTPUT='tests/reference/random_vectors.json'" \
        -x tools/reference/oracle_random.py input/linux/bds/bedrock_server

Run from input/linux/bds (libCrypto.so must resolve). The server stops at
main(); nothing else runs. Each case seeds a fresh 0x9d4-byte object with
Random::_setSeed and calls the original member functions by address.
"""
import json
import struct

import gdb

ADDRESS = {
    "set_seed": 0x187e380, "next_uint": 0x187e460, "next_int": 0x187d470, "next_int_n": 0x19c27f0,
    "next_int_range": 0x1b3b670, "next_float": 0x19323e0, "next_double": 0x1bc7740,
    "next_boolean": 0x1b5cff0, "next_gaussian": 0x1b02c70,
}
SHA = "69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad"

gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("break main")
gdb.execute("run")
buffer = int(gdb.parse_and_eval("(char*)malloc(0x1000)"))


def call(signature, name, *args):
    text = ", ".join([str(buffer)] + [repr(a) if not isinstance(a, float) else f"{a!r}f" for a in args])
    return gdb.parse_and_eval(f"(({signature}){ADDRESS[name]:#x})({text})")


def seed(value):
    call("void(*)(void*,unsigned)", "set_seed", value)


def as_float(value):
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


cases = []
for s in (0, 1, 12345, 0x7FFFFFFF, 0xFFFFFFFF):
    seed(s)
    words = [int(call("unsigned(*)(void*)", "next_uint")) & 0xFFFFFFFF for _ in range(1400)]
    cases.append({"seed": s, "kind": "next_uint", "values": words})
seed(42)
cases.append({"seed": 42, "kind": "next_int", "values": [int(call("int(*)(void*)", "next_int")) for _ in range(50)]})
for n in (1, 2, 3, 7, 16, 100, 1000, 2147483647, -5):
    seed(42)
    cases.append({"seed": 42, "kind": "next_int_n", "n": n,
                  "values": [int(call("int(*)(void*,int)", "next_int_n", n)) for _ in range(50)]})
for low, high in ((0, 10), (-8, 8), (5, 5), (10, 3)):
    seed(7)
    cases.append({"seed": 7, "kind": "next_int_range", "min": low, "max": high,
                  "values": [int(call("int(*)(void*,int,int)", "next_int_range", low, high)) for _ in range(50)]})
seed(99)
cases.append({"seed": 99, "kind": "next_float",
              "values": [as_float(call("float(*)(void*)", "next_float")) for _ in range(50)]})
seed(99)
cases.append({"seed": 99, "kind": "next_double",
              "values": [float(call("double(*)(void*)", "next_double")) for _ in range(50)]})
seed(3)
cases.append({"seed": 3, "kind": "next_boolean",
              "values": [bool(int(call("unsigned char(*)(void*)", "next_boolean")) & 1) for _ in range(64)]})
seed(2024)
cases.append({"seed": 2024, "kind": "next_gaussian",
              "values": [as_float(call("float(*)(void*)", "next_gaussian")) for _ in range(51)]})
with open(OUTPUT, "w") as out:  # noqa: F821 (set with -ex "py OUTPUT=...")
    json.dump({"binary_sha256": SHA, "generator": "tools/reference/oracle_random.py (original functions under gdb)",
               "cases": cases}, out)
    out.write("\n")
gdb.execute("kill")
