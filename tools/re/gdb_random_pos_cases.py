"""Execute original RandomPos::getPos with explicit own-Random seeds.
Run after gdb_mob_move_cases.py so geometry is known. Real navigation, snap,
light and animal scores are used; no return values/branches are overwritten.
"""
import json
import os
import struct
import gdb

def _run(probe):
    inf=gdb.selected_inferior()
    def q(a): return struct.unpack('<Q',bytes(inf.read_memory(a,8)))[0]
    def call(a,t,args):return gdb.parse_and_eval(f'(({t}){a})({args})')
    actor=next(a for a in sorted(probe['motion_actors'])
               if not int(call(0x21bf990,'unsigned char(*)(void*)',str(a)))&1)
    region=q(actor+0xc98)
    buffer=int(gdb.parse_and_eval('(char*)malloc(4096)'))
    own=actor+0x2b0
    saved=bytes(inf.read_memory(own,0x9d4))
    ground=[]
    for x in range(-20,21):
        for z in range(-20,21):
            inf.write_memory(buffer,struct.pack('<iii',x,3,z))
            block=int(call(0x1c58ad0,'void*(*)(void*,void*)',f'{region},{buffer}'))
            if q(block+0x10)==q(q(0x34a85a8)+0x10):ground.append([x,3,z,3])
    cases=[]
    probe['oracle_random']=own
    for at in ((0.5,4.0,0.5),(0.5,12.0,0.5),(-8.5,4.0,-5.5)):
        inf.write_memory(buffer,struct.pack('<fff',*at))
        call(0x21aa6d0,'void(*)(void*,void*)',f'{actor},{buffer}')
        for seed in range(32):
            call(0x187e380,'void(*)(void*,unsigned)',f'{own},{seed}')
            inf.write_memory(buffer+64,struct.pack('<fff',-777.0,-777.0,-777.0))
            record={'position':at,'seed':seed,'xz':10,'y':7,'words':[]}
            probe['oracle_words']=record['words']
            record['result']=bool(int(call(0x231b5e0,'unsigned char(*)(void*,void*,int,int)',f'{buffer+64},{actor},10,7'))&1)
            record['wanted']=list(struct.unpack('<fff',bytes(inf.read_memory(buffer+64,12))))
            cases.append(record)
    probe['oracle_words']=None
    probe['oracle_random']=None
    inf.write_memory(own,saved)
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'random_pos_cases.json'),'w') as out:
        json.dump({'extra_blocks':[[0,4,0,1]]+ground,'cases':cases},out)
    print('Native RandomPos::getPos:',len(cases),'seeded cases')
_run(state)
