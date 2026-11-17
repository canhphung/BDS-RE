"""Native PathNavigation::createPath cases, with actual expansion/neighbor order.
Post-script after gdb_mob_motion_oracle.py; no path/search results are patched.
"""
import json
import os
import struct
import gdb

def _run(probe):
    inf=gdb.selected_inferior()
    def mem(a,n):return bytes(inf.read_memory(a,n))
    def q(a):return struct.unpack('<Q',mem(a,8))[0]
    def r(s):return int(gdb.parse_and_eval('$'+s))
    def call(a,t,args):return gdb.parse_and_eval(f'(({t}){a})({args})')
    def node(a):
        if not a:return None
        return {'pos':list(struct.unpack('<iii',mem(a,12))),
                'malus':struct.unpack('<f',mem(a+0x1c,4))[0],
                'type':struct.unpack('<i',mem(a+0x20,4))[0]}
    def typ(a):return int(call(q(q(a)+0x4a0),'int(*)(void*)',str(a)))
    actor=next(a for a in sorted(probe['motion_actors'])
               if typ(a)==0x130b and not int(call(0x21a5fd0,'unsigned char(*)(void*)',str(a)))&1)
    nav=int(call(0x22b7450,'void*(*)(void*)',str(actor)))
    region=q(actor+0xc98)
    buffer=int(gdb.parse_and_eval('(char*)malloc(4096)'))
    point,slot=buffer,buffer+64
    current=None
    class NeighborEnd(gdb.FinishBreakpoint):
        def __init__(self,finder,record):
            super().__init__(gdb.newest_frame(),internal=True)
            self.finder,self.record=finder,record
        def stop(self):
            self.record['neighbors']=[node(q(self.finder+0x60+i*8)) for i in range(r('eax'))]
            return False
    class Neighbors(gdb.Breakpoint):
        def stop(self):
            if current is None:return False
            entry={'node':node(r('rdx'))}
            current['expanded'].append(entry)
            NeighborEnd(r('rdi'),entry)
            return False
    Neighbors('*0x1c72cb0',internal=True)
    class Core(gdb.Breakpoint):
        def stop(self):
            if current is not None:
                current['search']={'start':node(r('rcx')), 'target':node(r('r8')), 'size':node(r('r9'))['pos'],
                                   'max_distance':float(gdb.parse_and_eval('$xmm0.v4_float[0]')),
                                   'flags':list(mem(r('rsi')+0x162,12))}
            return False
    Core('*0x1c72780',internal=True)
    blocks={0:q(0x34a84f0),1:q(0x34a8598),2:q(0x34a85a0)}
    geometry=[]
    def set_block(x,y,z,value):
        inf.write_memory(point,struct.pack('<iii',x,y,z))
        call(0x1c59b20,'unsigned char(*)(void*,void*,void*,int,void*)',f'{region},{point},{blocks[value]},3,0')
        geometry.append([x,y,z,value])
    scenes=[]
    for name in ('flat','wall','gap','step','enclosed'):
        if name=='wall':
            for y in range(4,7):
                for z in range(-3,4):set_block(3,y,z,1)
        if name=='gap':
            for y in range(4,7):
                for z in range(-3,4):set_block(3,y,z,0)
            for x in range(3,6):
                for z in range(-3,4):set_block(x,3,z,0)
        if name=='step':
            for x in range(3,6):
                for z in range(-3,4):set_block(x,3,z,2)
            for z in range(-4,5):set_block(3,4,z,1)
        if name=='enclosed':
            for y in range(4,8):
                for x,z in ((0,1),(-1,0),(1,0),(0,-1)):set_block(x,y,z,1)
        scene={'name':name,'edits':list(geometry),'cases':[]}
        for at in ((8.0,4.0,0.0),(5.0,4.0,5.0),(-8.0,4.0,-6.0),(0.0,4.0,0.0),(0.0,8.0,0.0)):
            inf.write_memory(point,struct.pack('<fff',0.5,4.0,0.5))
            call(0x21aa6d0,'void(*)(void*,void*)',f'{actor},{point}')
            inf.write_memory(actor+0x124,b'\1')
            current={'start':[0.5,4.0,0.5],'target':at,'expanded':[]}
            inf.write_memory(point,struct.pack('<fff',*at))
            call(0x231c240,'void(*)(void*,void*,void*)',f'{slot},{nav},{point}')
            path=q(slot)
            if path:
                begin,end=q(path),q(path+8)
                if (end-begin)%0x38:raise RuntimeError('unexpected native Node span')
                current['path']=[node(a) for a in range(begin,end,0x38)]
            else:current['path']=None
            call(0x1c74a90,'void(*)(void*)',str(slot))
            scene['cases'].append(current)
            current=None
        scenes.append(scene)
    snapshot=probe['motion_snapshot'](actor)
    move=int(call(0x22befd0,'void*(*)(void*)',str(actor)))
    jump=int(call(0x22bf000,'void*(*)(void*)',str(actor)))
    data={'actor_state':snapshot,'navigation_vtable':hex(q(nav)),
          'move_vtable':hex(q(move)),'jump_vtable':hex(q(jump)),
          'navigation_flags':list(mem(nav+0x30,16)), 'scenes':scenes}
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'paths.json'),'w') as out:json.dump(data,out)
    print('Native navigation paths:',sum(len(s['cases']) for s in scenes))
_run(state)
