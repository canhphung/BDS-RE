"""Draw-by-draw oracle for the original level Random (Linux BDS 1.6.0.15).

Loaded by capture_level_random.py after stopping at main. No game algorithm is
replaced. An explicitly requested flat generator is set in LevelSettings before
the Level constructor, because the stock server ignores level-type=FLAT.
"""
import json
import os
import struct

import gdb

out = open(os.environ["TRACE_OUT"], "w", buffering=1)
state = {"level": None, "tick": 0, "draw": 0, "joined_tick": None, "initialized": 0}
limit = int(os.environ.get("TRACE_TICKS", "200"))
players = int(os.environ.get("TRACE_PLAYERS", "1"))


def reg(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xffffffffffffffff


def memory(address, count):
    return bytes(gdb.selected_inferior().read_memory(address, count))


def record(event, **fields):
    out.write(json.dumps({"event": event, **fields}, separators=(",", ":")) + "\n")


class Level(gdb.Breakpoint):
    def stop(self):
        # Level::Level (0x1c7ffd0), SysV sixth argument is LevelSettings const&.
        state["level"] = reg("rdi")
        settings = reg("r9")
        original = struct.unpack("<i", memory(settings + 0x10, 4))[0]
        if os.environ.get("TRACE_GENERATOR") == "flat":
            gdb.selected_inferior().write_memory(settings + 0x10, struct.pack("<i", 2))
        record("level", generator_before=original,
               generator=2 if os.environ.get("TRACE_GENERATOR") == "flat" else original)
        draws.condition = f"*(unsigned long long*)($rbp-8) == {state['level'] + 0x568}"
        draws.enabled = True
        seeds.condition = f"$rdi == {state['level'] + 0x568}"
        seeds.enabled = True
        self.enabled = False
        return False


class Seed(gdb.Breakpoint):
    def stop(self):
        # GDB invokes Python stop() before testing Breakpoint.condition.
        if reg("rdi") != state["level"] + 0x568:
            return False
        record("seed", tick=state["tick"], seed=reg("esi") & 0xffffffff)
        return False


class Initialized(gdb.Breakpoint):
    def stop(self):
        state["initialized"] += 1
        record("initialized", tick=state["tick"], count=state["initialized"])
        return False


class Draw(gdb.Breakpoint):
    def stop(self):
        # _genRandInt32 +0x2a4, after mov eax,[rbp-0xc], before its epilogue.
        this = struct.unpack("<Q", memory(reg("rbp") - 8, 8))[0]
        if this != state["level"] + 0x568:
            return False
        frame = gdb.newest_frame().older()
        callers = []
        for _ in range(6):
            if frame is None:
                break
            callers.append(frame.pc())
            frame = frame.older()
        state["draw"] += 1
        record("draw", index=state["draw"], tick=state["tick"],
               value=reg("eax") & 0xffffffff, callers=callers)
        return False


class Tick(gdb.Breakpoint):
    def stop(self):
        level = reg("rdi")
        begin, end = struct.unpack("<QQ", memory(level + 0x50, 16))
        count = (end - begin) // 8
        if state["initialized"] >= players and state["joined_tick"] is None:
            state["joined_tick"] = state["tick"]
        if state["joined_tick"] is not None and state["tick"] - state["joined_tick"] >= limit:
            record("complete", ticks=state["tick"], draws=state["draw"],
                   joined_tick=state["joined_tick"])
            out.close()
            return True
        state["tick"] += 1
        ids = []
        for address in range(begin, end, 8):
            player = struct.unpack("<Q", memory(address, 8))[0]
            ids.append(struct.unpack("<Q", memory(player + 0xec0, 8))[0])
        record("tick", tick=state["tick"], players=ids)
        return False


class BlockSamples(gdb.Breakpoint):
    def __init__(self, address, event):
        super().__init__("*" + hex(address), internal=True)
        self.event = event

    def stop(self):
        frame = reg("rbp")
        chunk = struct.unpack("<Q", memory(frame - 0x2a0, 8))[0]
        cx, cz = struct.unpack("<ii", memory(chunk + 0x28, 8))
        lcg = struct.unpack("<I", memory(0x32d753c, 4))[0]
        fields = {"tick": state["tick"], "chunk": [cx, cz], "lcg": lcg}
        if self.event == "block_samples_begin":
            fields.update(height=reg("eax") & 0xffff,
                          count=struct.unpack("<i", memory(frame - 0x21c, 4))[0])
        elif self.event == "block_sample":
            x, z, y = struct.unpack("<BBh", memory(frame - 0x268, 4))
            fields["position"] = [x, y, z]
        elif self.event == "weather_column":
            x, y, z = struct.unpack("<iii", memory(frame - 0x1c0, 12))
            fields["column"] = [x, z]
        record(self.event, **fields)
        return False


class BlockEntry(gdb.Breakpoint):
    def stop(self):
        cx, cz = struct.unpack("<ii", memory(reg("rdi") + 0x28, 8))
        record("block_tick_begin", tick=state["tick"], chunk=[cx, cz],
               lcg=struct.unpack("<I", memory(0x32d753c, 4))[0])
        return False


draws = Draw("*0x187e704", internal=True)
draws.enabled = False
seeds = Seed("*0x187e380", internal=True)
seeds.enabled = False
Level("*0x1c7ffd0", internal=True)
Tick("*0x1c86000", internal=True)
Initialized("*0x1827540", internal=True)
if os.environ.get("TRACE_BLOCK_SAMPLES") == "1":
    BlockEntry("*0x1de5040", internal=True)
    BlockSamples(0x1de56de, "weather_column")
    BlockSamples(0x1de5a30, "block_samples_begin")
    BlockSamples(0x1de5b32, "block_sample")
    BlockSamples(0x1de5c49, "block_samples_end")
