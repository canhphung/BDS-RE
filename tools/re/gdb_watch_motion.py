# Watch writes to the second player's motion (Actor +0xa0) with the writer's call chain.
import gdb, os, struct
OUT=open(os.environ["TRACE_OUT"],"w",buffering=1)
def mem(a,n): return bytes(gdb.selected_inferior().read_memory(a,n))
def reg(r): return int(gdb.parse_and_eval("$"+r))&0xffffffffffffffff
def rid(p): return struct.unpack("<Q",mem(p+0xec0,8))[0]
def vec(a): return struct.unpack("<3f",mem(a,12))
state={'b':None,'tick':0,'wp':[],'seen':[]}
class Tick(gdb.Breakpoint):
    def stop(s):
        p=reg("rdi")
        r=rid(p)
        if r not in state['seen']: state['seen'].append(r)
        if r==state['seen'][0]:
            state['tick']+=1
            OUT.write("tick %d motion %r pos %r\n"%(state['tick'],vec(p+0xa0),vec(p+0xe7c)))
            if state['b'] is None:
                state['b']=p
                for off in (0xa0,0xa4,0xa8):
                    state['wp'].append(Watch("*(float*)%d"%(p+off)))
        return False
class Watch(gdb.Breakpoint):
    def __init__(s,expr): super().__init__(expr,gdb.BP_WATCHPOINT,gdb.WP_WRITE,internal=True)
    def stop(s):
        fr=gdb.newest_frame(); names=[]
        for i in range(5):
            if fr is None: break
            names.append(str(fr.name())); fr=fr.older()
        OUT.write("  write %r -> %r  %s\n"%(s.expression,vec(state['b']+0xa0),' < '.join(names)))
        return False
Tick("*0x25fac60",internal=True)
