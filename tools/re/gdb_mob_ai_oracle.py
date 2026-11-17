"""Per-mob AI oracle (Linux BDS 1.6.0.15), sourced with
capture_level_random.py --gdb-script; writes mob_ai.json in the output.

- Mob::newServerAiStep entry (0x22bad00): {tick, draw index, mob}; the
  level-Random draws until the next marker belong to that mob's AI step.
- LookAtActorGoal::canUse return (0x233b64a): the result and the target's
  position (TempEPtr<Actor> at goal +0x10 -> Actor; StateVectorComponent +0xe7c).
- LookControl::tick entry/exit (0x2335ef0 / 0x2336203): the inputs and the
  resulting pitch (Actor +0xac) and head yaw (Mob +0x11c0), body yaw +0x11b8,
  attach offsets +0x1c8 / +0x1e0, position (+0xe7c).
"""
import json
import os
import struct
import gdb


def _install(probe):
    out_path = os.path.join(os.environ["ORACLE_OUT_DIR"], "mob_ai.json")
    events = []

    def r(name):
        return int(gdb.parse_and_eval("$" + name)) & 0xffffffffffffffff

    def mem(address, count):
        return bytes(gdb.selected_inferior().read_memory(address, count))

    def q(address):
        return struct.unpack("<Q", mem(address, 8))[0]

    def f(address, n=1):
        v = struct.unpack("<%df" % n, mem(address, 4 * n))
        return list(v) if n > 1 else v[0]

    def save(force=False):
        if force or len(events) % 100 == 0:
            with open(out_path, "w") as out:
                json.dump(events, out)

    class AiStep(gdb.Breakpoint):
        def stop(self):
            mob = r("rdi")
            events.append({"kind": "ai", "tick": probe.get("tick"), "draw": probe.get("draw"), "mob": hex(mob),
                           "pos": f(mob + 0xe7c, 3)})
            save()
            return False

    class LookAtResult(gdb.Breakpoint):
        def stop(self):
            goal = q(r("rbp") - 0x10) if False else None
            events.append({"kind": "look_at_can_use", "draw": probe.get("draw"), "result": r("eax") & 1})
            save()
            return False

    pending = {}

    class LookEntry(gdb.Breakpoint):
        def stop(self):
            control = r("rdi")
            mob = q(control + 8)
            rec = {"kind": "look", "tick": probe.get("tick"), "mob": hex(mob),
                   "has_wanted": mem(control + 0x18, 1)[0], "wanted": f(control + 0x1c, 3),
                   "y_max": f(control + 0x10), "x_max": f(control + 0x14),
                   "pitch": f(mob + 0xac), "body": f(mob + 0x11b8), "head": f(mob + 0x11c0),
                   "pos": f(mob + 0xe7c, 3), "attach3": f(mob + 0x1c8, 3), "attach7": f(mob + 0x1e0, 3)}
            events.append(rec)
            pending[mob] = rec
            return False

    class LookExit(gdb.Breakpoint):
        def stop(self):
            control = q(r("rbp") - 8)
            mob = q(control + 8)
            rec = pending.pop(mob, None)
            if rec is not None:
                rec["pitch_after"], rec["head_after"] = f(mob + 0xac), f(mob + 0x11c0)
            save()
            return False

    AiStep("*0x22bad00", internal=True)
    LookAtResult("*0x233b64a", internal=True)
    LookEntry("*0x2335ef0", internal=True)
    LookExit("*0x2336203", internal=True)


_install(state)  # noqa: F821
