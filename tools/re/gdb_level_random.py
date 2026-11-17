# Count the level Random draws (Level +0x568) per caller chain; dumps every 200 ticks.
# Use: tools/re/run_gdb.sh tools/re/gdb_level_random.py OUT; python3 tools/re/join_players.py 19220 A,B 25
import gdb, os, struct, collections
OUT=open(os.environ["TRACE_OUT"],"w",buffering=1)
def reg(r): return int(gdb.parse_and_eval("$"+r))&0xffffffffffffffff
def mem(a,n): return bytes(gdb.selected_inferior().read_memory(a,n))
st={'level':None,'counts':collections.Counter(),'ticks':0}
class GetRandom(gdb.Breakpoint):
    def stop(s):
        if st['level'] is None:
            st['level']=reg("rdi")+0x568
            OUT.write("level random at %x\n"%st['level'])
            lv=reg("rdi")
            for off in (0x8,0x20,0x50):
                b,e=struct.unpack("<QQ",mem(lv+off,16)); OUT.write("vec +%x size bytes %d\n"%(off,e-b))
        return False
class Gen(gdb.Breakpoint):
    def stop(s):
        if st['level'] is not None and reg("rdi")==st['level']:
            fr=gdb.newest_frame().older(); names=[]
            for i in range(4):
                if fr is None: break
                names.append(str(fr.name())); fr=fr.older()
            st['counts'][' < '.join(names)]+=1
        return False
class Tick(gdb.Breakpoint):
    def stop(s):
        st['ticks']+=1
        if st['ticks']%200==0:
            OUT.write("after %d ticks:\n"%st['ticks'])
            for k,v in st['counts'].most_common(): OUT.write("  %6d %s\n"%(v,k[:300]))
            lv=st['level']-0x568
            for off in (0x8,0x20,0x50):
                b,e=struct.unpack("<QQ",mem(lv+off,16)); OUT.write("  vec +%x bytes %d\n"%(off,e-b))
        return False
GetRandom("*0x1c8c500",internal=True)
Gen("*0x187e460",internal=True)
Tick("*0x1c86000",internal=True)
