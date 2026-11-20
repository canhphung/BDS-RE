"""Native Mesa/Frozen Ocean column builders; capture_level_random post script."""
import json
import os
import random
import struct
import gdb


def _special_surfaces(probe):
    inf = gdb.selected_inferior()
    for breakpoint in gdb.breakpoints():
        breakpoint.enabled = False
    def mem(a,n): return bytes(inf.read_memory(a,n))
    def q(a): return struct.unpack('<Q',mem(a,8))[0]
    def call(a,t,args): return gdb.parse_and_eval(f'(({t}){a})({args})')
    def alloc(n): return int(gdb.parse_and_eval(f'(void*)malloc({n})'))
    names = {}
    def block_name(block):
        if block not in names:
            legacy=q(block+0x10)
            ptr,length=struct.unpack('<QQ',mem(legacy+0x28,16))
            names[block]=mem(ptr,length).decode()+':'+str(struct.unpack('<H',mem(block+8,2))[0])
        return names[block]
    volume, cells, pos, rng = alloc(64),alloc(32768*8),alloc(32),alloc(0x9d4)
    call(0x1b3b410,'void(*)(void*,void*,void*,int,int,int,void*)',
         f'{volume},{cells},{cells+32768*8},16,128,16,{q(0x34a84f0)}')
    words=[]
    class Draw(gdb.Breakpoint):
        def stop(self):
            frame=int(gdb.parse_and_eval('$rbp'))
            if q(frame-8)==rng: words.append(int(gdb.parse_and_eval('$eax'))&0xffffffff)
            return False
    observer=Draw('*0x187e704',internal=True)
    observer.enabled=False
    rand=random.Random(16015)
    cases=[]; bands=[]
    for seed in (0,123,0x89abcdef):
        for biome_id in (37,38,39,165,166,167,46,47):
            biome=q(q(0x348b670)+8*biome_id)
            mesa=biome_id not in (46,47)
            call(0x1dc87e0 if mesa else 0x1dcf520,'void(*)(void*,unsigned)',f'{biome},{seed}')
            if mesa:
                bands.append({'seed':seed,'biome':biome_id,'colors':list(mem(biome+0x1b2,64)),
                              'bryce':bool(mem(biome+0x1b0,1)[0]&1),
                              'forest':bool(mem(biome+0x1b1,1)[0]&1)})
            for n in range(24):
                x,z=(4608+n,5120+n) if n<4 else (rand.randrange(-20000,20000),rand.randrange(-20000,20000))
                noise=(-10.,-2.,0.,2.,8.,14.)[n%6]
                height=(45,62,63,75,98,120)[n//4]
                initial=[q(0x34a8598) if y<=height else q(0x34a85d8) if y<63 else q(0x34a84f0) for y in range(128)]
                # Include air gaps and detached ceilings, exercising the state
                # reset and Mesa's 15-original-stone-cell budget.
                if n in (13,19):
                    initial[68:73]=[q(0x34a84f0)]*5
                offset=((x&15)*16+(z&15))*128
                inf.write_memory(cells+offset*8,struct.pack('<128Q',*initial))
                inf.write_memory(pos,struct.pack('<iii',x,0,z))
                random_seed=(seed ^ (n*0x193+biome_id))&0xffffffff
                call(0x187e350,'void(*)(void*,unsigned)',f'{rng},{random_seed}')
                words.clear();observer.enabled=True
                call(0x1dc9020 if mesa else 0x1dcfa80,
                     'void(*)(void*,void*,void*,void*,float,short)',
                     f'{biome},{rng},{volume},{pos},(float){noise!r},63')
                observer.enabled=False
                actual=struct.unpack('<128Q',mem(cells+offset*8,128*8))
                tail=[int(call(0x187d470,'int(*)(void*)',str(rng))) for _ in range(8)]
                cases.append({'seed':seed,'biome':biome_id,'x':x,'z':z,'noise':noise,'random_seed':random_seed,
                              'initial':[block_name(b) for b in initial], 'after':[block_name(b) for b in actual],
                              'words':list(words),'tail':tail})
    observer.delete()
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'],'special_surfaces.json'),'w') as out:
        json.dump({'bands':bands,'cases':cases},out,separators=(',',':'))
    print('Native special surface cases:',len(cases))


_special_surfaces(state)
