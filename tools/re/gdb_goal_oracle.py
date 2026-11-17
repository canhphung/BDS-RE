"""Dump every Mob's GoalSelector contents from the original (Linux 1.6.0.15).

Sourced with capture_level_random.py --gdb-script. At GoalSelector::tick
(0x23b3950) the selector (rdi) is a std::vector<InternalGoal>: InternalGoal is
{unique_ptr<Goal> +0, int priority +8, bool used +0xc, bool toStart +0xd}
(InternalGoal::InternalGoal 0x23b3620); Goal keeps its control flags at +8
(Goal::setRequiredControlFlags 0x239f450). Each selector is dumped once, with
the raw bytes of its goals for the parameters.
"""
import json
import os
import struct
import gdb

OUT = os.path.join(os.environ["ORACLE_OUT_DIR"], "goals.json")
seen, selectors = set(), []


def r(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xffffffffffffffff


def mem(address, count):
    return bytes(gdb.selected_inferior().read_memory(address, count))


def q(address):
    return struct.unpack("<Q", mem(address, 8))[0]


def symbol(address):
    try:
        return str(gdb.execute("info symbol %#x" % address, to_string=True)).strip()
    except gdb.error:
        return hex(address)


class Tick(gdb.Breakpoint):
    def stop(self):
        selector = r("rdi")
        if selector in seen:
            return False
        seen.add(selector)
        begin, end = q(selector), q(selector + 8)
        goals = []
        for internal in range(begin, end, 0x10):
            goal = q(internal)
            priority, = struct.unpack("<i", mem(internal + 8, 4))
            goals.append({"vtable": symbol(q(goal)), "priority": priority,
                          "flags": struct.unpack("<i", mem(goal + 8, 4))[0], "raw": mem(goal, 0xa0).hex()})
        selectors.append({"selector": hex(selector), "return": symbol(q(r("rsp"))), "goals": goals})
        with open(OUT, "w") as f:
            json.dump(selectors, f, indent=1)
        return False


Tick("*0x23b3950", internal=True)
