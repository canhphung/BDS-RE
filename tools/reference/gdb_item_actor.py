"""gdb script (sourced by trace_item_actor.py): logs ItemActor state and
related calls in the original bedrock_server 1.6.0.15 to $TRACE_OUT.

Offsets were recovered from the accessors: StateVectorComponent pos +0xe7c,
posPrev +0xe88, AABBShapeComponent box +0xe58, runtime id +0xec0, motion
+0xa0, onGround +0x124, ItemActor age +0x1228, throw time +0x122c, pickup
delay +0x1230, lifetime +0x123c.
"""
import gdb, os
OUT = open(os.environ["TRACE_OUT"], "w", buffering=1)
inf = None
def mem(addr, n):
    return bytes(gdb.selected_inferior().read_memory(addr, n))
import struct
def f3(a): return struct.unpack("<3f", mem(a, 12))
def f(a): return struct.unpack("<f", mem(a, 4))[0]
def i32(a): return struct.unpack("<i", mem(a, 4))[0]
def u8(a): return mem(a, 1)[0]
def u64(a): return struct.unpack("<Q", mem(a, 8))[0]
def reg(r): return int(gdb.parse_and_eval("$" + r)) & 0xffffffffffffffff
def actor(this):
    return dict(rid=u64(this + 0xec0), pos=f3(this + 0xe7c), prev=f3(this + 0xe88), motion=f3(this + 0xa0),
                aabb=struct.unpack("<6f", mem(this + 0xe58, 24)), ground=u8(this + 0x124), hcol=u8(this + 0x126),
                vcol=u8(this + 0x127), step=f(this + 0x1a0), hoff=f(this + 0x194), yslide=f(this + 0x1c4),
                f18=u8(this + 0xf18), a2a1=u8(this + 0x2a1), stuck=u8(this + 0x1190), pen=f(this + 0x1194),
                sub_empty=u64(this + 0x1178) == u64(this + 0x1180), age=i32(this + 0x1228), delay=i32(this + 0x1230),
                throw=i32(this + 0x122c), life=i32(this + 0x123c), web=u8(this + 0x1b8))
class Log(gdb.Breakpoint):
    def __init__(self, addr, name, fn):
        super().__init__("*" + hex(addr), internal=True)
        self.name, self.fn = name, fn
    def stop(self):
        try:
            OUT.write(repr((self.name, self.fn())) + "\n")
        except Exception as e:
            OUT.write(repr((self.name, "error", str(e))) + "\n")
        return False
Log(0x23fe8a0, "normalTick.enter", lambda: actor(reg("rdi")))
Log(0x23fe8bd + 5, "normalTick.afterBase", lambda: actor(reg("rax")))
Log(0x23ff130, "playerTouch", lambda: dict(item=actor(reg("rdi")), player=f3(reg("rsi") + 0xe7c)))
Log(0x22f0640, "Player::take", lambda: dict(actor=u64(reg("rsi") + 0xec0), a=reg("rdx") & 0xffffffff, b=reg("rcx") & 0xffffffff))
Log(0x21a6b50, "move", lambda: dict(rid=u64(reg("rdi") + 0xec0), d=f3(reg("rsi"))) if u64(reg("rdi") + 0xec0) != 1 else None)
Log(0x21bb8a0, "pushOutOfBlocks", lambda: dict(rid=u64(reg("rdi") + 0xec0), v=f3(reg("rsi"))))
Log(0x23ff7f0, "merge", lambda: dict(this=u64(reg("rdi") + 0xec0), other=u64(reg("rsi") + 0xec0)))
Log(0x21ab7b0, "remove", lambda: dict(rid=u64(reg("rdi") + 0xec0)))
Log(0x23ffca0, "AddItemActorPacket", lambda: actor(reg("rsi")))
Log(0x18b38d0, "MoveActorDelta.write", lambda: mem(reg("rdi"), 0x60).hex())
Log(0x18bfab0, "SetActorMotion.write", lambda: mem(reg("rdi"), 0x40).hex())
Log(0x18b0eb0, "TakeItemActor.write", lambda: mem(reg("rdi"), 0x40).hex())
