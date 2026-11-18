"""Native OreFeature cases. Run after console fill of the documented arena."""
import json
import os
import random
import struct
import subprocess
from pathlib import Path
import gdb

def _ore_cases(probe):
    for bp in gdb.breakpoints(): bp.enabled=False
    inf=gdb.selected_inferior()
    def mem(a,n):return bytes(inf.read_memory(a,n))
    def q(a):return struct.unpack('<Q',mem(a,8))[0]
    def call(a,t,args):return gdb.parse_and_eval(f'(({t}){a})({args})')
    def alloc(n):return int(gdb.parse_and_eval(f'(void*)malloc({n})'))
    player=q(q(probe['level']+0x50))
    region=int(call(0x21a6660,'void*(*)(void*)',str(player)))
    pos,rng,feature=alloc(32),alloc(0x9d4),alloc(64)
    pointers={'stone:0':q(0x34a8598),'coal_ore:0':q(0x34a8610),'iron_ore:0':q(0x34a8608),
              'dirt:0':q(0x34a85a8),'air:0':q(0x34a84f0),'bedrock:0':q(0x34a85c8),
              'netherrack:0':q(0x34a8878)}
    pointers['stone:1']=int(call(0x1b4c580,'void*(*)(void*,void*,int)',f'{pointers["stone:0"]},0x349a790,1'))
    pointers['stone:3']=int(call(0x1b4c580,'void*(*)(void*,void*,int)',f'{pointers["stone:0"]},0x349a790,3'))
    names={p:n for n,p in pointers.items()}
    # Scene resets can touch tens of thousands of blocks. A setup-only C loop
    # calls the original setter for every point, avoiding one GDB round trip
    # per point. No feature, RNG function or observed output is replaced.
    helper=Path(os.environ['ORACLE_OUT_DIR'])/'ore_setup.c'
    helper.write_text('''#include <stddef.h>
void reset_ore_arena(void *region,const int *xyz,size_t count,const void *block) {
    typedef unsigned char (*setter)(void*,const int*,const void*,int,void*);
    setter set=(setter)0x1c59b20;
    for(size_t i=0;i<count;++i) set(region,xyz+3*i,block,0,0);
}
''')
    library=helper.with_suffix('.so')
    subprocess.run(['cc','-O2','-fPIC','-shared',str(helper),'-o',str(library)],check=True,capture_output=True)
    text=alloc(4096);inf.write_memory(text,str(library).encode()+b'\0')
    handle=int(gdb.parse_and_eval(f'((void*(*)(const char*,int))dlopen)((char*){text},2)'))
    if not handle:raise RuntimeError('native setup helper could not be loaded')
    inf.write_memory(text,b'reset_ore_arena\0')
    reset=int(gdb.parse_and_eval(f'((void*(*)(void*,const char*))dlsym)((void*){handle},(char*){text})'))
    points=alloc(1_000_000*12)
    def restore(positions):
        xyz=[n for p in positions for n in p]
        if xyz:
            inf.write_memory(points,struct.pack('<%di'%len(xyz),*xyz))
            call(reset,'void(*)(void*,void*,unsigned long,void*)',f'{region},{points},{len(xyz)//3},{pointers["stone:0"]}')
    def block_name(block):
        if block not in names:
            legacy=q(block+0x10);ptr,length=struct.unpack('<QQ',mem(legacy+0x28,16))
            names[block]=mem(ptr,length).decode()+':'+str(struct.unpack('<H',mem(block+8,2))[0])
        return names[block]
    def put(p,name):
        inf.write_memory(pos,struct.pack('<iii',*p))
        call(0x1c59b20,'unsigned char(*)(void*,void*,void*,int,void*)',f'{region},{pos},{pointers[name]},0,0')
    # Fail rather than produce a corpus of empty veins if the setup command
    # ran before these columns loaded.
    for p in ((-8,16,-8),(0,16,0),(8,16,8)):
        inf.write_memory(pos,struct.pack('<iii',*p))
        actual=int(call(0x1c58ad0,'void*(*)(void*,void*)',f'{region},{pos}'))
        if actual!=pointers['stone:0']:raise RuntimeError('native ore arena was not filled')
    words=[];writes=[]
    class Draw(gdb.Breakpoint):
        def stop(self):
            if q(int(gdb.parse_and_eval('$rbp'))-8)==rng:
                words.append(int(gdb.parse_and_eval('$eax'))&0xffffffff)
            return False
    class Write(gdb.Breakpoint):
        def stop(self):
            p=int(gdb.parse_and_eval('$rsi'));block=int(gdb.parse_and_eval('$rdx'))
            xyz=struct.unpack('<iii',mem(p,12))
            writes.append([*xyz,block_name(block),int(gdb.parse_and_eval('$ecx'))])
            return False
    draw=Draw('*0x187e704',internal=True);write=Write('*0x1c59b20',internal=True)
    draw.enabled=write.enabled=False
    rand=random.Random(16015);previous=set();cases=[]
    for n in range(64):
        restore(previous)
        at=(-16,16,-16) if n%2 else (-8,16,-8)
        size=(1,3,8,9,10,17,24,33)[n%8]
        target=('coal_ore:0','dirt:0','iron_ore:0','stone:1')[n//8%4]
        edits=[]
        if n>=32:
            for _ in range(40):
                p=(at[0]+8+rand.randrange(-4,5),rand.randrange(12,19),at[2]+8+rand.randrange(-4,5))
                name=rand.choice(('air:0','dirt:0','stone:3','netherrack:0','bedrock:0',target))
                put(p,name);edits.append([*p,name])
        seed=rand.randrange(2**32)
        call(0x187e350,'void(*)(void*,unsigned)',f'{rng},{seed}')
        call(0x1b58e40,'void(*)(void*,void*,int)',f'{feature},{pointers[target]},{size}')
        inf.write_memory(pos,struct.pack('<iii',*at))
        words.clear();writes.clear();draw.enabled=write.enabled=True
        result=bool(int(call(0x1b58eb0,'unsigned char(*)(void*,void*,void*,void*)',f'{feature},{region},{pos},{rng}'))&1)
        draw.enabled=write.enabled=False
        tail=[int(call(0x187d470,'int(*)(void*)',str(rng))) for _ in range(8)]
        previous={tuple(p[:3]) for p in edits+writes}
        cases.append({'at':at,'size':size,'target':target,'seed':seed,'edits':edits,'result':result,
                      'words':list(words),'writes':list(writes),'tail':tail})
    # Now execute the real default/Mesa ore decorator on the same known
    # geometry, including its rare extra-gravel branch and lapis distribution.
    calls=[]
    class Place(gdb.Breakpoint):
        def stop(self):
            obj=int(gdb.parse_and_eval('$rdi'));p=int(gdb.parse_and_eval('$rdx'))
            calls.append([*struct.unpack('<iii',mem(p,12)),block_name(q(obj+0x18)),
                          struct.unpack('<i',mem(obj+0x20,4))[0]])
            return False
    place=Place('*0x1b58eb0',internal=True);place.enabled=False
    batches=[];registry=[]
    for biome_id in (1,37):
        decorator=q(q(q(0x348b670)+8*biome_id)+0xc0)
        slots=[]
        for offset in range(0x20,0x78,8):
            f=q(decorator+offset)
            slots.append({'offset':offset,'target':block_name(q(f+0x18)),
                          'size':struct.unpack('<i',mem(f+0x20,4))[0]})
        registry.append({'biome':biome_id,'features':slots})
        for seed in (0,1,2,123,456,7,0x12345678,0x89abcdef):
            restore(previous)
            call(0x187e350,'void(*)(void*,unsigned)',f'{rng},{seed}')
            inf.write_memory(pos,struct.pack('<iii',-16,0,-16))
            calls.clear();words.clear();writes.clear()
            draw.enabled=write.enabled=place.enabled=True
            call(0x1da6de0 if biome_id==1 else 0x1dc9ad0,
                 'void(*)(void*,void*,void*,void*)',f'{decorator},{region},{rng},{pos}')
            draw.enabled=write.enabled=place.enabled=False
            tail=[int(call(0x187d470,'int(*)(void*)',str(rng))) for _ in range(8)]
            previous={tuple(p[:3]) for p in writes}
            batches.append({'biome':biome_id,'at':[-16,0,-16],'seed':seed,'calls':list(calls),
                            'words':list(words),'writes':list(writes),'tail':tail})
    draw.delete();write.delete();place.delete()
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'ore.json'),'w') as out:
        json.dump({'bounds':[-16,8,-16,15,80,15],'fill':'stone:0','cases':cases,
                   'registry':registry,'batches':batches},out,separators=(',',':'))
    print('Native ore cases:',len(cases))

_ore_cases(state)
