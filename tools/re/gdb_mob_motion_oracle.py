"""Observe native mob travel/move calls without changing motion, AI or RNG.

Use capture_level_random.py --gdb-script tools/re/gdb_mob_motion_oracle.py.
Captures function inputs, collision state, relative acceleration and outputs.
"""
import json
import os
import struct
import gdb

def _install(probe):
    out = open(os.path.join(os.environ['ORACLE_OUT_DIR'],'mob_motion.jsonl'),'w',buffering=1)
    active = {}

    def reg(name): return int(gdb.parse_and_eval('$'+name))
    def mem(a,n): return bytes(gdb.selected_inferior().read_memory(a,n))
    def q(a): return struct.unpack('<Q',mem(a,8))[0]
    def f(a,n=1):
        values=list(struct.unpack('<%df'%n,mem(a,4*n)))
        return values if n!=1 else values[0]
    def xmm(i): return float(gdb.parse_and_eval(f'$xmm{i}.v4_float[0]'))
    def snapshot(a):
        return {'position':f(a+0xe7c,3),'motion':f(a+0xa0,3),'box':f(a+0xe58,6),
                'yaw':f(a+0xb0),'ground':mem(a+0x124,1)[0],
                'hcol':mem(a+0x126,1)[0],'vcol':mem(a+0x127,1)[0],
                'collision':mem(a+0x128,1)[0],'height_offset':f(a+0x194),'max_step':f(a+0x1a0),'slide':f(a+0x1c4),
                'friction_modifier':f(a+0x1274),'air_speed':f(a+0x1278),
                'stuck':mem(a+0x1190,1)[0],'no_exit':mem(a+0xf18,1)[0],
                'penetration':f(a+0x1194),'pushthrough':f(a+0x1a4)}

    class EndTravel(gdb.FinishBreakpoint):
        def __init__(self,a,record):
            super().__init__(gdb.newest_frame(),internal=True)
            self.actor,self.record=a,record
        def stop(self):
            self.record['after']=snapshot(self.actor)
            out.write(json.dumps(self.record,separators=(',',':'))+'\n')
            active.pop(self.actor,None)
            return False
    class Travel(gdb.Breakpoint):
        def stop(self):
            if not probe['recording']: return False
            actor=reg('rdi')
            probe.setdefault('motion_actors',set()).add(actor)
            record={'tick':probe['tick'],'actor':q(actor+0xec0),'vtable':hex(q(actor)),
                    'input':[xmm(i) for i in range(3)],'before':snapshot(actor),'moves':[]}
            active[actor]=record
            EndTravel(actor,record)
            return False
    class EndMove(gdb.FinishBreakpoint):
        def __init__(self,a,record):
            super().__init__(gdb.newest_frame(),internal=True)
            self.actor,self.record=a,record
        def stop(self):
            self.record['after']=snapshot(self.actor)
            return False
    class Move(gdb.Breakpoint):
        def stop(self):
            actor=reg('rdi')
            if actor in active:
                record={'delta':f(reg('rsi'),3),'before':snapshot(actor)}
                active[actor]['moves'].append(record)
                EndMove(actor,record)
            return False
    class Relative(gdb.Breakpoint):
        def stop(self):
            actor=reg('rdi')
            if actor in active:
                active[actor]['relative']=[xmm(i) for i in range(4)]
            return False
    class EndPush(gdb.FinishBreakpoint):
        def __init__(self,a,b,record):
            super().__init__(gdb.newest_frame(),internal=True)
            self.a,self.b,self.record=a,b,record
        def stop(self):
            self.record['after']=snapshot(self.a)
            self.record['other_after']=snapshot(self.b)
            out.write(json.dumps(self.record,separators=(',',':'))+'\n')
            return False
    class Push(gdb.Breakpoint):
        def stop(self):
            if not probe['recording']: return False
            a,b=reg('rdi'),reg('rsi')
            record={'kind':'push','tick':probe['tick'],'actor':q(a+0xec0),'other':q(b+0xec0),
                    'one_way':reg('edx')&1,'before':snapshot(a),'other_before':snapshot(b)}
            EndPush(a,b,record)
            return False
    Push('*0x21b1280',internal=True)
    probe['motion_snapshot']=snapshot
    Travel('*0x22b5ad0',internal=True)
    Move('*0x21a6b50',internal=True)
    Relative('*0x21b0880',internal=True)

_install(state)
