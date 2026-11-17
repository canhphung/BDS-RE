# Log tick phases and packet sends (vtable names) after the first Mob::addEffect.
import gdb, os, struct
OUT=open(os.environ["TRACE_OUT"],"w",buffering=1)
def reg(r): return int(gdb.parse_and_eval("$"+r))&0xffffffffffffffff
def mem(a,n): return bytes(gdb.selected_inferior().read_memory(a,n))
def q(a): return struct.unpack("<Q",mem(a,8))[0]
def sym(a):
    try: return gdb.execute("info symbol 0x%x"%a,to_string=True).split(" in section")[0].strip()
    except Exception: return hex(a)
st={'on':False}
class Add(gdb.Breakpoint):
    def stop(s): st['on']=True; OUT.write("addEffect\n"); return False
class T(gdb.Breakpoint):
    def __init__(s,a,n): super().__init__("*"+hex(a),internal=True); s.n=n
    def stop(s):
        if st['on']: OUT.write(s.n+"\n")
        return False
class Send(gdb.Breakpoint):
    def __init__(s,a,n,r): super().__init__("*"+hex(a),internal=True); s.n=n; s.r=r
    def stop(s):
        if st['on']:
            p=reg(s.r); OUT.write("  %s %s\n"%(s.n,sym(q(p))))
        return False
Add("*0x22b0630",internal=True)
T(0x1c86000,"LevelTick"); T(0x25fac60,"playerTick"); T(0x21bd970,"sendDirty"); T(0x22b2050,"tickEffects"); T(0x22b1fc0,"tickAttributes")
Send(0x1d69020,"sendForEntity","rdx")
Send(0x18813f0,"NetworkHandler::send","rdx")
class BT(gdb.Breakpoint):
    def stop(s):
        if st['on'] and 'SetActorData' in sym(q(reg("rdx"))):
            fr=gdb.newest_frame(); names=[]
            for i in range(7):
                if fr is None: break
                names.append(str(fr.name())); fr=fr.older()
            OUT.write("  BT "+" < ".join(names)+"\n")
        return False
BT("*0x1d69020",internal=True)
