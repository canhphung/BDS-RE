"""Native liquid travel and real effect instances, post motion oracle."""
import json,os,struct,gdb

def _run(probe):
    inf=gdb.selected_inferior()
    def mem(a,n):return bytes(inf.read_memory(a,n))
    def q(a):return struct.unpack('<Q',mem(a,8))[0]
    def put(a,*v):inf.write_memory(a,struct.pack('<%df'%len(v),*v))
    def call(a,t,args):return gdb.parse_and_eval(f'(({t}){a})({args})')
    a=min(probe['motion_actors']);region=q(a+0xc98)
    b=int(gdb.parse_and_eval('(char*)malloc(256)'))
    cases=[];edits=[];registry=[]
    for value,ptr in ((5,0x34a85d8),(6,0x34a85e8)):
        block=q(ptr);legacy=q(block+0x10)
        material=int(call(0x1e68cf0,'void*(*)(void*)',str(block)))
        entry={'id':value,'opacity':mem(legacy+0xb4,1)[0],
               'solid':bool(mem(legacy+0xaa,1)[0]&1),
               'translucency':struct.unpack('<f',mem(material+12,4))[0],
               'liquid':bool(mem(material+8,1)[0]&1),
               'propagate':bool(int(call(0x1c639e0,'unsigned char(*)(void*)',str(legacy)))&1),
               'tickable':bool(int(call(0x1e68b30,'unsigned char(*)(void*)',str(block)))&1)}
        call(0x1e68b80,'void(*)(void*,void*)',f'{b+128},{block}')
        entry['emission']=mem(b+128,1)[0];registry.append(entry)
    for kind,ptr,value in [('water',0x34a85d8,5),('lava',0x34a85e8,6),('land',0x34a84f0,0)]:
        for x in range(-2,3):
            for z in range(-2,3):
                for y in range(4,7):
                    inf.write_memory(b,struct.pack('<iii',x,y,z))
                    call(0x1c59b20,'unsigned char(*)(void*,void*,void*,int,void*)',f'{region},{b},{q(ptr)},3,0')
                    edits.append([x,y,z,value])
        for effect in ((0,0),(24,0),(24,2),(27,0)) if kind=='land' else ((0,0),):
            call(0x22b1580,'void(*)(void*)',str(a))
            if effect[0]:
                call(0x1b0c240,'void(*)(void*,int,int,int)',f'{b+64},{effect[0]},200,{effect[1]}')
                call(0x22b0630,'void(*)(void*,void*)',f'{a},{b+64}')
            for motion in ((0.,0.,0.),(.2,-.3,.1),(-.1,.2,.3)):
                for control in ((0.,0.,0.),(.2,0.,.7)):
                    put(b,.5,4.5,.5);call(0x21aa6d0,'void(*)(void*,void*)',f'{a},{b}')
                    put(a+0xa0,*motion);put(a+0xb0,35.)
                    inf.write_memory(a+0x124,b'\0')
                    call(0x21bc0e0,'void(*)(void*)',str(a))
                    travel=int(call(0x22b55d0,'int(*)(void*)',str(a)))
                    before=probe['motion_snapshot'](a)
                    call(0x22b5ad0,'void(*)(void*,float,float,float)',f'{a},{control[0]:.9g}f,{control[1]:.9g}f,{control[2]:.9g}f'.replace('0f','0.0f'))
                    cases.append({'kind':kind,'edits':list(edits),'effect':effect,'travel_type':travel,'input':control,
                                  'before':before,'after':probe['motion_snapshot'](a)})
    call(0x22b1580,'void(*)(void*)',str(a))
    for x in range(-2,3):
        for z in range(-2,3):
            for y in range(4,7):
                inf.write_memory(b,struct.pack('<iii',x,y,z))
                call(0x1c59b20,'unsigned char(*)(void*,void*,void*,int,void*)',f'{region},{b},{q(0x34a85d8)},3,0')
                edits.append([x,y,z,5])
    for y in range(4,9):
        for z in range(-2,3):
            inf.write_memory(b,struct.pack('<iii',1,y,z))
            call(0x1c59b20,'unsigned char(*)(void*,void*,void*,int,void*)',f'{region},{b},{q(0x34a8598)},3,0')
            edits.append([1,y,z,1])
    for y in (4.5,6.5,7.0):
        for motion in ((.3,-.1,0.),(.3,.2,.1)):
            put(b,.5,y,.5);call(0x21aa6d0,'void(*)(void*,void*)',f'{a},{b}')
            put(a+0xa0,*motion);put(a+0xb0,35.)
            inf.write_memory(a+0x124,b'\0')
            call(0x21bc0e0,'void(*)(void*)',str(a))
            travel=int(call(0x22b55d0,'int(*)(void*)',str(a)))
            before=probe['motion_snapshot'](a)
            call(0x22b5ad0,'void(*)(void*,float,float,float)',f'{a},0.0f,0.0f,0.0f')
            cases.append({'kind':'water_wall','edits':list(edits),'effect':[0,0],'travel_type':travel,'input':[0.,0.,0.],
                          'before':before,'after':probe['motion_snapshot'](a)})
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'liquids_effects.json'),'w') as out:json.dump({'registry':registry,'cases':cases},out)
    print('Native liquid/effect cases:',len(cases))
_run(state)
