"""Retained native GoalSelector -> navigation/control/look -> travel sequence.
Explicit initial goal/control state and seeds; all decisions execute original code.
"""
import json,os,struct,gdb

def _run(probe):
    inf=gdb.selected_inferior()
    def mem(a,n):return bytes(inf.read_memory(a,n))
    def q(a):return struct.unpack('<Q',mem(a,8))[0]
    def f(a,n=1):
        v=list(struct.unpack('<%df'%n,mem(a,4*n)));return v if n!=1 else v[0]
    def put(a,*v):inf.write_memory(a,struct.pack('<%df'%len(v),*v))
    def call(a,t,args):return gdb.parse_and_eval(f'(({t}){a})({args})')
    def typ(a):return int(call(q(q(a)+0x4a0),'int(*)(void*)',str(a)))
    actor=next(a for a in sorted(probe['motion_actors']) if typ(a)==0x130b and not int(call(0x21a5fd0,'unsigned char(*)(void*)',str(a)))&1)
    nav=int(call(0x22b7450,'void*(*)(void*)',str(actor)))
    move=int(call(0x22befd0,'void*(*)(void*)',str(actor)))
    look=q(actor+0x12f0);jump=int(call(0x22bf000,'void*(*)(void*)',str(actor)))
    buf=int(gdb.parse_and_eval('(char*)malloc(128)'))
    # Isolate wandering from look_at_player: an explicit distant player
    # position, with the original collision box updated by setPos.
    for ptr in range(q(probe['level']+0x50),q(probe['level']+0x58),8):
        player=q(ptr);put(buf,128.5,32.,128.5)
        call(0x21aa6d0,'void(*)(void*,void*)',f'{player},{buf}')
    put(buf,.5,4.,.5);call(0x21aa6d0,'void(*)(void*,void*)',f'{actor},{buf}')
    put(actor+0xa0,0.,-0.0784,0.);put(actor+0xac,0.,0.)
    put(actor+0x11b8,0.,0.,0.,0.)
    put(buf+32,0.,0.)
    call(0x21aaa20,'void(*)(void*,void*,void*)',f'{actor},{buf},{buf+32}')
    inf.write_memory(actor+0x124,b'\1');inf.write_memory(look+0x18,b'\0');inf.write_memory(look+0x28,b'\0')
    inf.write_memory(move+0x20,b'\0');put(actor+0x1230,0.,0.,0.);put(actor+0x12e8,0.)
    for selector in (actor+0x1308,actor+0x1320):
        for internal in range(q(selector),q(selector+8),0x10):inf.write_memory(internal+0xc,b'\0\0')
    call(0x231d3a0,'void(*)(void*)',str(nav))
    level_random=probe['level']+0x568;own=actor+0x2b0
    call(0x187e380,'void(*)(void*,unsigned)',f'{level_random},456')
    call(0x187e380,'void(*)(void*,unsigned)',f'{own},123')
    current=None
    class DrawEnd(gdb.FinishBreakpoint):
        def __init__(self,kind):super().__init__(gdb.newest_frame(),internal=True);self.kind=kind
        def stop(self):
            if current is not None:current[self.kind].append(int(gdb.parse_and_eval('$eax'))&0xffffffff)
            return False
    class Draw(gdb.Breakpoint):
        def stop(self):
            address=int(gdb.parse_and_eval('$rdi'))
            if current is not None and address in (level_random,own):DrawEnd('level_words' if address==level_random else 'own_words')
            return False
    Draw('*0x187e460',internal=True)
    def snapshot():
        path=q(nav+0x10)
        return {'actor':probe['motion_snapshot'](actor),'pitch':f(actor+0xac),'head':f(actor+0x11c0),
                'body':f(actor+0x11b8),'wanted':f(move+0x10,3),'has_wanted':mem(move+0x20,1)[0]&1,
                'look_wanted':f(look+0x1c,3),'speed':f(actor+0x12e8),'forward':f(actor+0x1238),
                'index':struct.unpack('<i',mem(path+0x18,4))[0] if path else None}
    vector=int(call(0x21a4c20,'void*(*)(void*)',str(actor)))
    data={'initial':snapshot(),'attach3':f(actor+0x1c8,3),'attach7':f(actor+0x1e0,3),'width':f(actor+0xe74),'height':f(actor+0xe78),'steps':[]}
    for i in range(200):
        # Actor::tick normally copies current position into posPrev2 before
        # normalTick/newServerAiStep. Reproduce that caller input explicitly.
        put(buf,*f(actor+0xe7c,3))
        call(0x21c4350,'void(*)(void*,void*)',f'{vector},{buf}')
        current={'level_words':[],'own_words':[]}
        call(0x22bad00,'void(*)(void*)',str(actor))
        current['controls']=snapshot()
        forward=struct.unpack('<f',struct.pack('<f',f(actor+0x1238)*struct.unpack('<f',bytes.fromhex('48e17a3f'))[0]))[0]
        call(0x22b5ad0,'void(*)(void*,float,float,float)',f'{actor},0.0f,0.0f,(float){forward!r}')
        current['after']=snapshot();data['steps'].append(current);current=None
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'walking_ai.json'),'w') as out:json.dump(data,out)
    print('Native retained walking AI:',len(data['steps']))
_run(state)
