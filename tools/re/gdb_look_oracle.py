"""LookControl::tick (0x2335ef0) inputs and outputs for every mob.

Sourced with capture_level_random.py --gdb-script. At entry: the LookControl
(+0x8 mob, +0x10 yMax, +0x14 xMax, +0x18 hasWanted, +0x1c wanted Vec3,
+0x28 hasWantedRotation, +0x2c/+0x30 rotation) and the mob's rotations
(+0xac pitch, +0xb0 yaw, +0x11b8 body yaw, +0x11c0 head yaw), position
(StateVectorComponent +0xe7c), height offsets (+0x194, +0x198) and the attach
offsets read by Actor::getAttachPos for locations 3 (+0x1c8) and 7 (+0x1e0).
At the return: the new pitch and head yaw.
"""
import json
import os
import struct
import gdb

probe = state  # noqa: F821
OUT = os.path.join(os.environ["ORACLE_OUT_DIR"], "look.json")
records = []
pending = {}


def r(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xffffffffffffffff


def mem(address, count):
    return bytes(gdb.selected_inferior().read_memory(address, count))


def q(address):
    return struct.unpack("<Q", mem(address, 8))[0]


def f(address, n=1):
    v = struct.unpack("<%df" % n, mem(address, 4 * n))
    return list(v) if n > 1 else v[0]


class Entry(gdb.Breakpoint):
    def stop(self):
        control = r("rdi")
        mob = q(control + 8)
        rec = {"tick": probe.get("tick"), "mob": hex(mob),
               "y_max": f(control + 0x10), "x_max": f(control + 0x14),
               "has_wanted": mem(control + 0x18, 1)[0], "wanted": f(control + 0x1c, 3),
               "has_rotation": mem(control + 0x28, 1)[0], "rotation": f(control + 0x2c, 2),
               "pitch": f(mob + 0xac), "yaw": f(mob + 0xb0), "body": f(mob + 0x11b8), "head": f(mob + 0x11c0),
               "pos": f(mob + 0xe7c, 3), "offsets": [f(mob + 0x194), f(mob + 0x198)],
               "attach3": f(mob + 0x1c8, 3), "attach7": f(mob + 0x1e0, 3)}
        records.append(rec)
        pending[mob] = rec
        return False


class Exit(gdb.Breakpoint):
    # LookControl::tick epilogue (0x2336203): rbp-? holds this; read via r12-free
    # path: the control pointer was saved at -0x8(%rbp).
    def stop(self):
        control = q(r("rbp") - 8)
        mob = q(control + 8)
        rec = pending.pop(mob, None)
        if rec is not None:
            rec["pitch_after"], rec["head_after"] = f(mob + 0xac), f(mob + 0x11c0)
        if len(records) % 50 == 0:
            with open(OUT, "w") as out:
                json.dump(records, out)
        return False


Entry("*0x2335ef0", internal=True)
Exit("*0x2336203", internal=True)
