"""Native horse mount/seat cases using startRiding and the pack's seat."""
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
    actors=sorted(probe['motion_actors']);horses=[a for a in actors if typ(a)==0x205317 and not int(call(0x21a5fd0,'unsigned char(*)(void*)',str(a)))&1]
    if not horses:raise RuntimeError('summon horse required')
    mount=horses[0];player=q(q(probe['level']+0x50))
    buf=int(gdb.parse_and_eval('(char*)malloc(256)'))
    put(buf,1.5,4.,1.5);call(0x21aa6d0,'void(*)(void*,void*)',f'{mount},{buf}')
    put(buf,1.5,5.62,1.5);call(0x21aa6d0,'void(*)(void*,void*)',f'{player},{buf}')
    # Apply actual definition groups as explicit actor inputs, then let the
    # original normalTick reload the description/components.
    for group in ('minecraft:horse_tamed','minecraft:horse_saddled'):
        data=group.encode();inf.write_memory(buf+64,data+b'\0')
        inf.write_memory(buf+128,struct.pack('<QQQ',buf+64,len(data),len(data)))
        call(0x21a51d0,'void(*)(void*,void*)',f'{mount},{buf+128}')
    call(0x21adc30,'unsigned char(*)(void*,void*)',f'{mount},{q(mount+0xc98)}')
    mounted=bool(int(call(0x21afc20,'unsigned char(*)(void*,void*)',f'{player},{mount}'))&1)
    if not mounted:raise RuntimeError('native horse rejected rider')
    flags=0
    for bit in range(64):
        if int(call(0x21a4fc0,'unsigned char(*)(void*,int)',f'{mount},{bit}'))&1:flags|=1<<bit
    cases=[]
    for yaw in (0.,30.,90.,180.,-90.,359.):
        put(mount+0xb0,yaw)
        call(0x21aea60,'void(*)(void*)',str(mount))
        cases.append({'yaw':yaw,'scale':float(call(0x2261e10,'float(*)(void*,int)',f'{mount+0xe8},38')),'mount':probe['motion_snapshot'](mount),
                      'rider':probe['motion_snapshot'](player),'seat_offset':f(player+0x1c8,3)})
    if not int(call(0x21b2b70,'unsigned char(*)(void*)',str(mount)))&1:raise RuntimeError('native definition reload did not enable WASD')
    travel_cases=[]
    movement=int(call(q(q(mount)+0x5a8),'void*(*)(void*,void*)',f'{mount},0x34f2e28'))
    movement=float(call(0x24a7230,'float(*)(void*)',str(movement)))
    requests=[(yaw,rider_yaw,forward,0) for yaw in (0.,45.,179.) for rider_yaw in (0.,90.,-170.) for forward in (-1.,0.,1.)]
    requests += [(0.,yaw,forward,charge) for yaw in (0.,90.) for forward in (0.,1.) for charge in (45,90)]
    strength=int(call(q(q(mount)+0x5a8),'void*(*)(void*,void*)',f'{mount},0x34f2f18'))
    strength=float(call(0x24a7230,'float(*)(void*)',str(strength)))
    for yaw,rider_yaw,forward,charge in requests:
        put(buf,1.5,4.,1.5);call(0x21aa6d0,'void(*)(void*,void*)',f'{mount},{buf}')
        put(mount+0xa0,0.,-0.0784,0.);put(mount+0xb0,yaw)
        inf.write_memory(mount+0x124,b'\1')
        put(player+0xac,20.,rider_yaw);put(player+0x1230,0.5,0.,forward)
        put(mount+0x12d4,0.)
        if charge:call(0x22c0c10,'void(*)(void*,int)',f'{mount},{charge}')
        power_before=f(mount+0x12d4)
        speed_before=f(mount+0x12e8)
        before=probe['motion_snapshot'](mount)
        call(0x22b5ad0,'void(*)(void*,float,float,float)',f'{mount},0.0f,0.0f,0.0f')
        travel_cases.append({'before':before,'rider_yaw':rider_yaw,'rider_pitch':20.,'strafe':0.5,'forward':forward,
                             'charge':charge,'power_before':power_before,'jump_strength':strength,'movement':movement,'speed_before':speed_before,'after':probe['motion_snapshot'](mount),'pitch':f(mount+0xac),
                             'speed':f(mount+0x12e8)})
    wasd=bool(int(call(0x21b2b70,'unsigned char(*)(void*)',str(mount)))&1)
    exit_case={'before':probe['motion_snapshot'](player),'mount':probe['motion_snapshot'](mount),
               'distance':f(player+0xf1c),'candidates':[]}
    class ViewEnd(gdb.FinishBreakpoint):
        def stop(self):
            exit_case['view']=[float(gdb.parse_and_eval('$xmm0.v4_float[0]')),
                               float(gdb.parse_and_eval('$xmm0.v4_float[1]')),
                               float(gdb.parse_and_eval('$xmm1.v4_float[0]'))]
            return False
    class View(gdb.Breakpoint):
        def stop(self):ViewEnd(gdb.newest_frame(),internal=True);return False
    class Place(gdb.Breakpoint):
        def stop(self):
            exit_case['candidates'].append(f(int(gdb.parse_and_eval('$rsi')),3));return False
    view_bp=View('*0x21ab490',internal=True);place_bp=Place('*0x21af0a0',internal=True)
    call(0x21afd10,'void(*)(void*,int,int,int)',f'{player},1,0,0')
    view_bp.delete();place_bp.delete()
    exit_case['after']=probe['motion_snapshot'](player)
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'riding.json'),'w') as out:json.dump({'cases':cases,'wasd':wasd,'flags':flags,'travel_cases':travel_cases,'dismounted':probe['motion_snapshot'](player),'exit_case':exit_case},out)
    print('Native riding cases:',len(cases)+len(travel_cases)+1)
_run(state)
