"""Read original live lighting and execute native tickBlocks in the paused scene.

Sourced by capture_level_random.py --lighting-oracle. Scene commands run normally
before the pause. No lighting/branch outputs are patched. tickBlocks cases use
explicit seeds on the original level Random (saved/restored); the native LCG is
observed and allowed to advance normally. No recorded RNG is a runtime input.
"""
import json
import os
import struct
import gdb

probe = state  # from gdb_level_random_sequence.py
inf = gdb.selected_inferior()

def mem(a, n):
    return bytes(inf.read_memory(a, n))

def q(a):
    return struct.unpack('<Q', mem(a, 8))[0]

def call(address, signature, arguments):
    return gdb.parse_and_eval(f'(({signature}){address})({arguments})')

level = probe['level']
region = q(q(q(level + 0x50)) + 0xc98)
dimension = int(call(0x1c59020, 'void*(*)(void*)', str(region)))
buffer = int(gdb.parse_and_eval('(char*)malloc(8192)'))
position = buffer + 4096
output = buffer + 4200
random = level + 0x568

def pos(p):
    inf.write_memory(position, struct.pack('<iii', *p))

def block_at(p):
    pos(p)
    return int(call(0x1c58ad0, 'void*(*)(void*,void*)', f'{region},{position}'))

blocks = [block_at((0, 20, 0)), q(0x34a8598), q(0x34a85a0), q(0x34a85a8), q(0x34a85c8)]
registry = []
for internal_id, block in enumerate(blocks):
    legacy = q(block + 0x10)
    material = int(call(0x1e68cf0, 'void*(*)(void*)', str(block)))
    registry.append({'id': internal_id, 'opacity': mem(legacy + 0xb4, 1)[0],
                     'solid': bool(mem(legacy + 0xaa, 1)[0] & 1),
                     'translucency': struct.unpack('<f', mem(material + 12, 4))[0],
                     'liquid': bool(mem(material + 8, 1)[0] & 1),
                     'propagate': bool(int(call(0x1c639e0, 'unsigned char(*)(void*)', str(legacy))) & 1),
                     'tickable': bool(int(call(0x1e68b30, 'unsigned char(*)(void*)', str(block))) & 1)})
    call(0x1e68b80, 'void(*)(void*,void*)', f'{output},{block}')
    registry[-1]['emission'] = mem(output, 1)[0]

# Read real pairs without a native function call per voxel. Layout is verified
# by getBrightnessPair calls for every selected point below.
chunk_cache = {}
def pair_at(p):
    x,y,z = p
    key = (x // 16, z // 16)
    if key not in chunk_cache:
        pos(p)
        chunk_cache[key] = int(call(0x1c594c0, 'void*(*)(void*,void*)', f'{region},{position}'))
    chunk = chunk_cache[key]
    pos(p)
    call(0x1c5a560, 'void(*)(void*,void*,void*)', f'{output},{region},{position}')
    return list(mem(output, 2)), bool(chunk)

points = set()
for x in range(-22, 23, 2):
    for z in (-10, -8, -4, 0, 4, 8, 10, 12, 14, 16, 18):
        for y in (3, 4, 5, 7, 8, 9, 10, 11):
            points.add((x,y,z))
points.update([(0,-1,0),(0,255,0),(0,256,0),(0,300,0),(100000,5,100000)])
queries = []
for p in sorted(points):
    pair, loaded = pair_at(p)
    pos(p)
    values = []
    for darken in (False, True):
        call(0x1c5a670, 'void(*)(void*,void*,void*,int,int)',
             f'{output},{region},{position},1,{int(darken)}')
        values.append(mem(output, 1)[0])
    queries.append({'position': p, 'loaded': loaded, 'pair': pair, 'raw': values})

# Record actual grass/dirt under the structures before the explicit native calls.
# The runtime's natural ticks may have decayed covered grass already.
ground = []
for x in range(-1,17):
    for z in range(-1,17):
        block = block_at((x,3,z))
        ground.append([x,3,z,blocks.index(block)])

# Native tickBlocks integration: live light/material/blocks, real LCG, seeded
# original Random. Cases accumulate any actual grass mutations in the scene.
current = None
class Mutation(gdb.Breakpoint):
    def stop(self):
        if current is not None:
            rsp = int(gdb.parse_and_eval('$rsp'))
            if q(rsp) in (0x1e717b8, 0x1e71ab5):
                p = int(gdb.parse_and_eval('$rsi'))
                block = int(gdb.parse_and_eval('$rdx'))
                current['mutations'].append({'position': list(struct.unpack('<iii', mem(p,12))),
                                              'id': blocks.index(block),
                                              'flags': int(gdb.parse_and_eval('$ecx'))})
        return False
Mutation('*0x1c59b20', internal=True)
saved_random = mem(random, 0x9d4)
probe['oracle_random'] = random
cases = []
for seed in range(int(os.environ.get("LIGHTING_CASES","128"))):
    call(0x187e380, 'void(*)(void*,unsigned)', f'{random},{seed}')
    pos((0,3,0))
    chunk = int(call(0x1c594c0, 'void*(*)(void*,void*)', f'{region},{position}'))
    current = {'seed': seed, 'lcg_before': struct.unpack('<I',mem(0x32d753c,4))[0],
               'words': [], 'mutations': []}
    probe['oracle_words'] = current['words']
    call(0x1de5040, 'void(*)(void*,void*)', f'{chunk},{region}')
    current['lcg_after'] = struct.unpack('<I',mem(0x32d753c,4))[0]
    cases.append(current)
    current = None
probe['oracle_words'] = None
probe['oracle_random'] = None
inf.write_memory(random, saved_random)
fixture = {'binary_sha256': '69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad',
           'generator': 'tools/re/gdb_lighting_oracle.py', 'case_count': len(cases),
           'time': int(call(0x1c86b00, 'int(*)(void*)', str(level))),
           'sky_darken': mem(dimension+0xec,1)[0], 'registry': registry,
           'queries': queries, 'initial_ground': ground, 'block_ticks': cases}
saved_time = fixture['time']
time_cases = []
for t in sorted(set(range(0,24000,100)) | {11999,12001,12999,13001,22999,23001,23999}):
    call(0x1c86ab0, 'void(*)(void*,int)', f'{level},{t}')
    call(0x1d64f30, 'void(*)(void*,void*,float)', f'{output},{dimension},0.0f')
    time_cases.append([t,mem(output,1)[0]])
call(0x1c86ab0, 'void(*)(void*,int)', f'{level},{saved_time}')
fixture['clear_sky_time_cases'] = time_cases
with open(os.environ['LIGHTING_ORACLE_OUT'],'w') as f:
    json.dump(fixture,f,indent=1)
    f.write('\n')
print('Original lighting oracle:', len(queries), 'points;', len(cases), 'native tickBlocks calls')
