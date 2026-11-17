"""Postscript: native MoveControl, JumpControl and retained navigation steps.
Explicit actor/control inputs; executes the real methods, no return patching.
"""
import json, os, struct, gdb

def _run(probe):
    inf=gdb.selected_inferior()
    def mem(a,n):return bytes(inf.read_memory(a,n))
    def q(a):return struct.unpack('<Q',mem(a,8))[0]
    def f(a,n=1):
        v=list(struct.unpack('<%df'%n,mem(a,4*n)));return v if n!=1 else v[0]
    def put(a,*v):inf.write_memory(a,struct.pack('<%df'%len(v),*v))
    def call(a,t,args):return gdb.parse_and_eval(f'(({t}){a})({args})')
    actor=min(probe['motion_actors'])
    nav=int(call(0x22b7450,'void*(*)(void*)',str(actor)))
    move=int(call(0x22befd0,'void*(*)(void*)',str(actor)))
    jump=int(call(0x22bf000,'void*(*)(void*)',str(actor)))
    buf=int(gdb.parse_and_eval('(char*)malloc(256)'))
    def snapshot():
        return {'actor':probe['motion_snapshot'](actor), 'width':f(actor+0xe74), 'height':f(actor+0xe78),
                'control':{'wanted':f(move+0x10,3),'modifier':f(move+0x1c),
                           'has_wanted':mem(move+0x20,1)[0]&1,'max_turn':f(move+0x24),
                           'speed':f(actor+0x12e8),'forward':f(actor+0x1238)},
                'jump_wanted':mem(jump+0x10,1)[0]&1,
                'jumping':mem(actor+0x12cc,1)[0]&1}
    cases=[]
    for yaw in (0.,45.,179.,-179.,270.):
        for target in ((8.,4.,0.),(0.5,4.,0.5),(0.8,4.,0.5),(0.5,5.,0.5),(-1.,5.,-1.),(0.5,4.31,0.5)):
            put(buf,0.5,4.,0.5);call(0x21aa6d0,'void(*)(void*,void*)',f'{actor},{buf}')
            put(actor+0xa0,0.,-0.0784,0.);put(actor+0xb0,yaw)
            put(actor+0x12e8,0.);put(actor+0x1238,0.)
            inf.write_memory(jump+0x10,b'\0')
            put(buf,*target);call(0x2331030,'void(*)(void*,void*,float)',f'{move},{buf},0.8f')
            before=snapshot();call(0x23310e0,'void(*)(void*)',str(move));after=snapshot()
            call(0x23364c0,'void(*)(void*)',str(jump));after_jump=snapshot()
            cases.append({'before':before,'after':after,'after_jump':after_jump})
    sequences=[]
    for target in ((8.,4.,0.),(5.,4.,5.),(-8.,4.,-6.)):
        call(0x231d3a0,'void(*)(void*)',str(nav))
        put(buf,0.5,4.,0.5);call(0x21aa6d0,'void(*)(void*,void*)',f'{actor},{buf}')
        put(actor+0xa0,0.,-0.0784,0.);put(actor+0xb0,0.)
        inf.write_memory(actor+0x124,b'\1')
        put(move+0x1c,0.);inf.write_memory(move+0x20,b'\0')
        put(actor+0x12e8,0.);put(actor+0x1238,0.)
        put(buf,*target);call(0x231c3d0,'unsigned char(*)(void*,void*,float)',f'{nav},{buf},0.8f')
        path=q(nav+0x10)
        sequence={'target':target,'initial':snapshot(),'path':[],'steps':[]}
        if path:
            sequence['path']=[list(struct.unpack('<iii',mem(a,12))) for a in range(q(path),q(path+8),0x38)]
        for i in range(100):
            before=snapshot()
            call(0x231c960,'void(*)(void*)',str(nav))
            call(0x23310e0,'void(*)(void*)',str(move))
            call(0x23364c0,'void(*)(void*)',str(jump))
            path=q(nav+0x10)
            index=struct.unpack('<i',mem(path+0x18,4))[0] if path else None
            controls=snapshot()
            # Explicit post-aiStep forward damping, then real Mob::travel.
            forward=struct.unpack('<f',struct.pack('<f',f(actor+0x1238)*struct.unpack('<f',bytes.fromhex('48e17a3f'))[0]))[0]
            call(0x22b5ad0,'void(*)(void*,float,float,float)',f'{actor},0.0f,0.0f,(float){forward!r}')
            sequence['steps'].append({'before':before,'controls':controls,'index':index,'after':snapshot()})
        sequences.append(sequence)
    data={'move_cases':cases,'navigation_cases':sequences}
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'controls.json'),'w') as output:json.dump(data,output)
    print('Native control cases:',len(cases))
_run(state)
