"""Execute unchanged LakeFeature in a paused original server; no branch/RNG patches.

Setup-only helper calls the native BlockSource setter/getter in a loop. All
feature calls, Random draws, brightness queries and placement attempts remain
original. Use with capture_level_random.py --gdb-post-script.
"""
import json
import os
import struct
import subprocess
from pathlib import Path
import gdb


def lake_cases(probe):
    for bp in gdb.breakpoints():
        bp.enabled = False
    inf = gdb.selected_inferior()
    def mem(a, n): return bytes(inf.read_memory(a, n))
    def q(a): return struct.unpack('<Q', mem(a, 8))[0]
    def call(a, t, args): return gdb.parse_and_eval(f'(({t}){a})({args})')
    def alloc(n): return int(gdb.parse_and_eval(f'(void*)malloc({n})'))
    player = q(q(probe['level'] + 0x50))
    region = int(call(0x21a6660, 'void*(*)(void*)', str(player)))
    # Use the original BlockSource's supported event-suppression mode. These
    # fixture chunks are already ready; real postProcess uses false,true over
    # chunks whose state is not Ready, which suppresses events via the other
    # arm of shouldFireEvents (0x1c593f0). No branch or chunk state is patched.
    # This component oracle stops before neighbor events or pending tick jobs.
    live_region = region
    region, queue, tick = alloc(0xc0), alloc(0x48), alloc(8)
    call(0x1c584f0, 'void(*)(void*,void*,void*,void*,int,int)',
         f'{region},{probe["level"]},{q(live_region+0x28)},{q(live_region+0x20)},1,1')
    call(0x1b3b5b0, 'void(*)(void*,unsigned long)', f'{tick},1')
    call(0x1b3b5d0, 'void(*)(void*,void*)', f'{queue},{tick}')
    call(0x1c59360, 'void(*)(void*,void*)', f'{region},{queue}')
    pos, rng, feature = alloc(32), alloc(0x9d4), alloc(64)
    pointers = {}
    names = {}
    def block_name(block):
        if block not in names:
            legacy = q(block + 0x10)
            ptr, length = struct.unpack('<QQ', mem(legacy + 0x28, 16))
            names[block] = mem(ptr, length).decode() + ':' + str(struct.unpack('<H', mem(block + 8, 2))[0])
        return names[block]
    for a in (0x34a84f0, 0x34a8598, 0x34a85a0, 0x34a85a8, 0x34a85d0, 0x34a85d8,
              0x34a85e0, 0x34a85e8, 0x34a8620, 0x34a8648, 0x34a8698, 0x34a8820, 0x34a8878):
        b = q(a)
        pointers[block_name(b)] = b
    for name, data in (('dirt:0', 1), ('water:0', 3)):
        b = int(call(0x1b4c580, 'void*(*)(void*,void*,int)', f'{pointers[name]},{0x349a700 if name.startswith("water") else 0x349a790},{data}'))
        pointers[block_name(b)] = b
    materials = {}
    for name, b in pointers.items():
        m = int(call(0x1e68cf0, 'void*(*)(void*)', str(b)))
        materials[name] = {
            'solid': bool(int(call(0x1c56d70, 'int(*)(void*)', str(m))) & 1),
            'liquid': bool(int(call(0x1c56d10, 'int(*)(void*)', str(m))) & 1),
            'lava': bool(int(call(0x1c56be0, 'int(*)(void*,int)', f'{m},6')) & 1)}
    root = Path(os.environ['ORACLE_OUT_DIR'])
    helper = root / 'lake_setup.c'
    helper.write_text('''#include <stddef.h>
struct Edit { int x,y,z,padding; const void *block; };
void setup_lake(void *region,const struct Edit *edits,size_t count) {
    typedef unsigned char (*Setter)(void*,const int*,const void*,int,void*);
    Setter set=(Setter)0x1c59b20;
    typedef unsigned char (*Extra)(void*,const int*,const void*,int);
    const void *air=*(const void**)0x34a84f0;
    for(size_t i=0;i<count;++i) {
        set(region,&edits[i].x,air,0,0);
        ((Extra)0x1c5a190)(region,&edits[i].x,air,0);
        set(region,&edits[i].x,edits[i].block,0,0);
        ((Extra)0x1c5a190)(region,&edits[i].x,air,0);
    }
}
void sky_lake(void *region,const int *xyz,size_t count) {
    typedef void *(*Chunk)(void*,const int*);
    typedef void (*Local)(void*,const int*);
    typedef void (*Light)(void*,const void*,const unsigned char*);
    for(size_t i=0;i<count;++i) {
        if(xyz[3*i+1]<0 || xyz[3*i+1]>=256) continue;
        void *chunk=((Chunk)0x1c594c0)(region,xyz+3*i);
        if(chunk) {
            int local[4]; unsigned char sky=0;
            ((Local)0x1dd0790)(local,xyz+3*i);
            ((Light)0x1de1700)(chunk,local,&sky);
        }
    }
}
void snapshot_lake(void *region,const int *xyz,size_t count,const void **out,int extra) {
    typedef const void *(*Getter)(void*,const int*);
    Getter get=(Getter)(extra ? 0x1c59a10 : 0x1c58ad0);
    for(size_t i=0;i<count;++i) out[i]=get(region,xyz+3*i);
}
''')
    library = helper.with_suffix('.so')
    subprocess.run(['cc', '-O2', '-fPIC', '-shared', str(helper), '-o', str(library)], check=True, capture_output=True)
    text = alloc(4096)
    inf.write_memory(text, str(library).encode() + b'\0')
    handle = int(gdb.parse_and_eval(f'((void*(*)(const char*,int))dlopen)((char*){text},2)'))
    if not handle: raise RuntimeError('could not load setup helper')
    def symbol(name):
        inf.write_memory(text, name.encode() + b'\0')
        return int(gdb.parse_and_eval(f'((void*(*)(void*,const char*))dlsym)((void*){handle},(char*){text})'))
    setup, snapshot, sky = symbol('setup_lake'), symbol('snapshot_lake'), symbol('sky_lake')
    edits_ptr, coords_ptr, blocks_ptr = alloc(2000000), alloc(1000000), alloc(1000000)
    def put(edits):
        if not edits: return
        inf.write_memory(edits_ptr, b''.join(struct.pack('<iiiiQ', *p[:3], 0, pointers[p[3]]) for p in edits))
        call(setup, 'void(*)(void*,void*,unsigned long)', f'{region},{edits_ptr},{len(edits)}')
    def scan(coords, extra=False):
        inf.write_memory(coords_ptr, b''.join(struct.pack('<iii', *p) for p in coords))
        call(snapshot, 'void(*)(void*,void*,unsigned long,void*,int)', f'{region},{coords_ptr},{len(coords)},{blocks_ptr},{int(extra)}')
        return [block_name(b) for b in struct.unpack('<%dQ' % len(coords), mem(blocks_ptr, 8 * len(coords)))]
    words, writes, light, masks = [], [], [], []
    class Draw(gdb.Breakpoint):
        def stop(self):
            if q(int(gdb.parse_and_eval('$rbp')) - 8) == rng:
                words.append(int(gdb.parse_and_eval('$eax')) & 0xffffffff)
            return False
    class Write(gdb.Breakpoint):
        def stop(self):
            p = int(gdb.parse_and_eval('$rsi'))
            writes.append([*struct.unpack('<iii', mem(p, 12)), block_name(int(gdb.parse_and_eval('$rdx'))),
                           int(gdb.parse_and_eval('$ecx'))])
            return False
    class LightReturn(gdb.FinishBreakpoint):
        def __init__(self, out, p):
            super().__init__(internal=True)
            self.out, self.p = out, p
        def stop(self):
            light.append([*self.p, mem(self.out, 1)[0]])
            return False
    class Light(gdb.Breakpoint):
        def stop(self):
            LightReturn(int(gdb.parse_and_eval('$rdi')),
                        struct.unpack('<iii', mem(int(gdb.parse_and_eval('$rdx')), 12)))
            return False
    class Shape(gdb.Breakpoint):
        def stop(self):
            rbp = int(gdb.parse_and_eval('$rbp'))
            masks.append({'origin': list(struct.unpack('<iii', mem(rbp - 0x38, 12))),
                          'inside': mem(rbp - 0x840, 2048).hex()})
            return False
    draw, write = Draw('*0x187e704', internal=True), Write('*0x1c59b20', internal=True)
    brightness, shape = Light('*0x1c5a560', internal=True), Shape('*0x1b5e207', internal=True)
    observers = (draw, write, brightness, shape)
    for bp in observers: bp.enabled = False
    # Each input is an explicitly initialized 16x8x16 box; outside that box
    # the feature only examines the descent column (also initialized here).
    cases = []
    scenes = ('buried', 'surface', 'flooded', 'air_floor', 'wrong_liquid', 'upper_liquid',
              'mixed_solid', 'custom_upper', 'low_y', 'below_world', 'high_y', 'descending', 'dark_dirt')
    for scene in scenes:
        for seed in (0, 1, 7, 123, 0x12345678, 0x89abcdef):
            liquid = 'water:0' if seed in (0, 7, 0x12345678) else 'lava:0'
            upper = 'ice:0' if scene == 'custom_upper' else 'air:0'
            bx = bz = -16 if seed & 1 else -8
            by = 1 if scene == 'low_y' else -2 if scene == 'below_world' else 251 if scene == 'high_y' else 16
            initial = []
            coords = [(bx+x, by+y, bz+z) for x in range(16) for z in range(16) for y in range(8)]
            for x in range(16):
                for z in range(16):
                    for y in range(8):
                        name = 'stone:0'
                        if scene in ('surface', 'descending'):
                            name = 'air:0' if y >= 4 else 'dirt:0' if y == 3 else 'stone:0'
                        elif scene == 'flooded' and y < 4:
                            name = 'water:3' if liquid == 'water:0' else liquid
                        elif scene == 'air_floor' and y == 1: name = 'air:0'
                        elif scene == 'wrong_liquid' and y < 4:
                            name = 'lava:0' if liquid == 'water:0' else 'water:0'
                        elif scene == 'upper_liquid' and y == 5: name = 'water:0'
                        elif scene == 'dark_dirt': name = 'dirt:0'
                        elif scene == 'mixed_solid':
                            name = ('netherrack:0', 'dirt:1', 'glass:0', 'leaves:0')[(x+z+y)%4]
                        if x == 0 and z == 0 and y == 4: name = 'stone:0'
                        initial.append([bx+x, by+y, bz+z, name])
            # Descending starts 5 air blocks above the anchor; low-Y starts at
            # Y=5 and high-Y at255. No native return condition is patched.
            at = [bx+8, by+4+(5 if scene == 'descending' else 0), bz+8]
            if scene == 'descending':
                initial += [[bx, by+y, bz, 'air:0'] for y in range(5, 10)]
                coords += [(bx, by+y, bz) for y in range(5, 10)]
            put(initial)
            before = scan(coords)
            if any(b != 'air:0' for b in scan(coords, True)):
                raise RuntimeError('lake primary-layer fixture contains a secondary layer')
            if scene == 'dark_dirt':
                # A lighting input snapshot, initialized via the original sky
                # setter. getBrightnessPair itself is never overridden.
                call(sky, 'void(*)(void*,void*,unsigned long)', f'{region},{coords_ptr},{len(coords)}')
            # Read actual post-setup values (including native height limits),
            # so the test does not assume setters succeeded outside the world.
            initial = [[*p, b] for p, b in zip(coords, before)]
            call(0x187e350, 'void(*)(void*,unsigned)', f'{rng},{seed}')
            call(0x1b5dc10, 'void(*)(void*,void*,void*)', f'{feature},{pointers[liquid]},{pointers[upper]}')
            inf.write_memory(pos, struct.pack('<iii', *at))
            words.clear(); writes.clear(); light.clear(); masks.clear()
            for bp in observers: bp.enabled = True
            result = bool(int(call(0x1b5dc70, 'unsigned char(*)(void*,void*,void*,void*)',
                                   f'{feature},{region},{pos},{rng}')) & 1)
            for bp in observers: bp.enabled = False
            after = scan(coords)
            tail = [int(call(0x187d470, 'int(*)(void*)', str(rng))) for _ in range(8)]
            if len(masks) != 1: raise RuntimeError('missing native lake shape')
            cases.append({'scene': scene, 'at': at, 'seed': seed, 'liquid': liquid, 'upper': upper,
                          'initial': initial, 'sky_setup': 0 if scene == 'dark_dirt' else None, 'result': result, 'shape': masks[0], 'words': list(words),
                          'writes': list(writes), 'light': list(light), 'final': after, 'tail': tail})
            print('Native lake:', scene, seed, result, len(writes), 'writes', len(light), 'light queries', flush=True)
    for bp in observers: bp.delete()
    (root / 'lakes.json').write_text(json.dumps({'source_context': {'constructor_flags': [True, True], 'ticking_queue_tick': 1, 'events': False, 'pending_ticks_run': False, 'input_extra_layer': 'air:0'}, 'materials': materials, 'cases': cases}, separators=(',', ':')))
    call(0x1b3b7c0, 'void(*)(void*)', str(queue))
    call(0x1c58760, 'void(*)(void*)', str(region))
    print('Native lake cases complete:', len(cases))


lake_cases(state)
