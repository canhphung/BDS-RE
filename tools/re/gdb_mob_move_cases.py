"""Native Actor::move input matrix after the normal motion trace completes.

Original functions and real blocks do all collision/step decisions. The oracle
sets explicit actor position/motion/ground/slide inputs, never their results.
"""
import json
import os
import struct
import gdb

def _run(probe):
    inf=gdb.selected_inferior()
    def q(a):return struct.unpack('<Q',bytes(inf.read_memory(a,8)))[0]
    def call(a,t,args):return gdb.parse_and_eval(f'(({t}){a})({args})')
    actor=min(probe['motion_actors'])
    snapshot=probe['motion_snapshot']
    region=q(actor+0xc98)
    buffer=int(gdb.parse_and_eval('(char*)malloc(4096)'))
    position,delta=buffer,buffer+64
    # A real step block on flat grass; native setBlock handles its callbacks.
    inf.write_memory(position,struct.pack('<iii',0,4,0))
    stone=q(0x34a8598)
    call(0x1c59b20,'unsigned char(*)(void*,void*,void*,int,void*)',f'{region},{position},{stone},3,0')
    call(0x21a4d40,'void(*)(void*,int,int)',f'{actor},46,1')
    cases=[]
    for y in (4.0,4.4,4.5,4.6,5.0,6.0):
        for dx,dz in ((0.4,0.0),(0.4,0.2),(0.0,0.4),(-0.4,0.0)):
            for grounded in (False,True):
                p=(-0.6,y,0.5)
                d=(dx,-0.0784,dz)
                inf.write_memory(position,struct.pack('<fff',*p))
                call(0x21aa6d0,'void(*)(void*,void*)',f'{actor},{position}')
                inf.write_memory(actor+0xa0,struct.pack('<fff',*d))
                inf.write_memory(actor+0x124,bytes([int(grounded)]))
                for offset in (0x126,0x127,0x128,0x1190,0xf18):inf.write_memory(actor+offset,b'\0')
                inf.write_memory(actor+0x1c4,struct.pack('<f',0.0))
                inf.write_memory(actor+0x1194,struct.pack('<f',0.0))
                before=snapshot(actor)
                inf.write_memory(delta,struct.pack('<fff',*d))
                call(0x21a6b50,'void(*)(void*,void*)',f'{actor},{delta}')
                cases.append({'kind':'move','before':before,'delta':d,'after':snapshot(actor)})
    # A native free fall followed by landing, retaining motion across calls.
    inf.write_memory(position,struct.pack('<fff',3.5,12.0,3.5))
    call(0x21aa6d0,'void(*)(void*,void*)',f'{actor},{position}')
    inf.write_memory(actor+0xa0,struct.pack('<fff',0.0,0.0,0.0))
    inf.write_memory(actor+0x124,b'\0')
    inf.write_memory(actor+0x1c4,struct.pack('<f',0.0))
    for frame in range(40):
        before=snapshot(actor)
        travel_type=int(call(0x22b55d0,'int(*)(void*)',str(actor)))
        speed=float(call(0x22b5700,'float(*)(void*,int)',f'{actor},{travel_type}'))
        call(0x22b5ad0,'void(*)(void*,float,float,float)',f'{actor},0.0f,0.0f,0.0f')
        cases.append({'kind':'travel','tick':frame,'actor':0,'input':[0.0,0.0,0.0],
                      'relative':[0.0,0.0,0.0,speed],'before':before,'after':snapshot(actor),'moves':[]})
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'mob_move_cases.json'),'w') as out:
        json.dump({'geometry':[[0,4,0,1]],'cases':cases},out)
    print('Native Actor::move matrix:',len(cases),'cases')
_run(state)
