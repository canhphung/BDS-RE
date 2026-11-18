"""Call native water functions in a paused flat BDS world (post motion oracle).

The fixture records actual initial states and mutations, including synchronous
neighbor notifications. It does not synthesize expected results or run RE code.
"""
import json
import os
import random
import struct
import gdb


def _run(probe):
    inf = gdb.selected_inferior()
    def q(a): return struct.unpack('<Q', bytes(inf.read_memory(a, 8)))[0]
    def call(a, t, args): return gdb.parse_and_eval(f'(({t}){a})({args})')
    actor = min(probe['motion_actors'])
    region = q(actor + 0xc98)
    buf = int(gdb.parse_and_eval('(char*)malloc(256)'))
    dynamic = q(q(0x34a85d0) + 0x10)
    blocks = {0: q(0x34a84f0), 1: q(0x34a8598), 5: q(0x34a85d8)}
    for d in range(16):
        blocks[7+d] = int(call(0x1b4c580, 'void*(*)(void*,void*,int)',
                             f'{q(0x34a85d0)},0x349a700,{d}'))
        if d:
            blocks[22+d] = int(call(0x1b4c580, 'void*(*)(void*,void*,int)',
                                   f'{q(0x34a85d8)},0x349a700,{d}'))
    ids = {p: i for i, p in blocks.items()}
    def pos(p): inf.write_memory(buf, struct.pack('<iii', *p))
    def get(p):
        pos(p)
        b = int(call(0x1c58ad0, 'void*(*)(void*,void*)', f'{region},{buf}'))
        if b not in ids:
            raise RuntimeError(f'unsupported native block at {p}: {b:#x}')
        return ids[b]
    def put(p, i, flags=0):
        pos(p)
        call(0x1c59b20, 'unsigned char(*)(void*,void*,void*,int,void*)',
             f'{region},{buf},{blocks[i]},{flags},0')
    touched = set()
    class Mutation(gdb.Breakpoint):
        def stop(self):
            p = int(gdb.parse_and_eval('$rsi'))
            touched.add(struct.unpack('<iii', bytes(inf.read_memory(p, 12))))
            return False
    # Every primary-layer set, including the native liquid setter's writes.
    watch = Mutation('*0x1c59b20', internal=True)
    watch.enabled = False
    rng = random.Random(7282)
    cases = []
    floor = {(x, 9, z): 1 for x in range(2, 15) for z in range(2, 15)}
    layouts = []
    for depth in range(16):
        for feed in ('none', 'west', 'above'):
            scene = dict(floor)
            scene[8, 10, 8] = 7+depth
            if feed == 'west': scene[7, 10, 8] = 5
            if feed == 'above': scene[8, 11, 8] = 5
            layouts.append((f'depth_{depth}_{feed}', scene, (8, 10, 8)))
    for name in ('fall', 'two_sources', 'source_above_and_side', 'slope_east', 'slope_tie', 'walls'):
        scene = dict(floor)
        scene[8, 10, 8] = 7 if name in ('fall', 'slope_east', 'slope_tie', 'walls') else 11
        if name == 'fall': scene.pop((8, 9, 8))
        if name == 'two_sources': scene[7, 10, 8] = scene[9, 10, 8] = 5
        if name == 'source_above_and_side': scene[7, 10, 8] = scene[8, 11, 8] = 5
        if name.startswith('slope'):
            scene.pop((10, 9, 8))
            if name == 'slope_tie': scene.pop((8, 9, 6))
        if name == 'walls':
            for p in ((7, 10, 8), (8, 10, 7), (8, 10, 9)): scene[p] = 1
        layouts.append((name, scene, (8, 10, 8)))
    for n in range(24):
        scene = dict(floor)
        scene[8, 10, 8] = 7+rng.randrange(16)
        for x in range(5, 12):
            for z in range(5, 12):
                if (x, z) == (8, 8): continue
                choice = rng.randrange(8)
                if choice == 0: scene.pop((x, 9, z))
                if choice == 1: scene[x, 10, z] = 1
                if choice == 2: scene[x, 10, z] = rng.choice([5, 7, 8, 11, 15, 22, 24])
        layouts.append((f'obstacles_{n}', scene, (8, 10, 8)))
    previous = set()
    for name, scene, at in layouts:
        for p in previous | set(scene): put(p, scene.get(p, 0))
        initial = [[*p, get(p)] for p in sorted(scene)]
        pos(at)
        spread = int(call(0x1e9ee70, 'unsigned int(*)(void*,void*,void*)', f'{dynamic},{region},{buf}'))
        inf.write_memory(buf+64, struct.pack('<fff', 0., 0., 0.))
        call(0x1e8d830, 'void(*)(void*,void*,void*,void*,void*)',
             f'{dynamic},{region},{buf},{actor},{buf+64}')
        flow = list(struct.unpack('<fff', bytes(inf.read_memory(buf+64, 12))))
        box = [7.9, 10.05, 7.9, 9.1, 10.8, 9.1]
        inf.write_memory(buf+96, struct.pack('<ffffff', *box))
        inf.write_memory(actor+0xa0, struct.pack('<fff', .123, -.0784, .234))
        motion_before = list(struct.unpack('<fff', bytes(inf.read_memory(actor+0xa0, 12))))
        in_water = bool(int(call(0x1c84fe0, 'unsigned char(*)(void*,void*,int,void*)',
                                f'{probe["level"]},{buf+96},5,{actor}')) & 1)
        motion_after = list(struct.unpack('<fff', bytes(inf.read_memory(actor+0xa0, 12))))
        touched.clear()
        watch.enabled = True
        pos(at)
        call(0x1e9dd10, 'unsigned char(*)(void*,void*,void*,void*,int)',
             f'{dynamic},{region},{buf},{probe["level"]+0x568},0')
        watch.enabled = False
        changes = [[*p, get(p)] for p in sorted(touched)]
        previous = set(scene) | touched
        cases.append({'name': name, 'initial': initial, 'at': at,
                      'spread': [bool((spread >> (8*i)) & 1) for i in range(4)],
                      'flow_half': flow, 'box': box, 'in_water': in_water,
                      'motion_before': motion_before, 'motion_after': motion_after,
                      'after': changes})
    # A fresh column isolates the real BlockTickingQueue from the function
    # cases above. Advance the native queue, not an imitation of its ordering.
    scene = {(x, 9, z): 1 for x in range(34, 47) for z in range(2, 15)}
    for x in range(34, 47):
        for z in range(2, 15):
            if x in (34, 46) or z in (2, 14): scene[x, 10, z] = 1
    for p, i in sorted(scene.items()): put(p, i)
    at = (40, 10, 8)
    pos(at)
    queue = int(call(0x1c592d0, 'void*(*)(void*,void*)', f'{region},{buf}'))
    base_tick = q(queue+8)
    put(at, 7, 3)
    scene[at] = get(at)
    timeline = []
    for tick in range(1, 121):
        edits = [[*at, 0]] if tick == 61 else []
        touched.clear()
        watch.enabled = True
        for edit in edits: put(tuple(edit[:3]), edit[3], 3)
        inf.write_memory(buf+96, struct.pack('<Q', base_tick+tick))
        call(0x1c517b0, 'unsigned char(*)(void*,void*,void*,int,int)',
             f'{queue},{region},{buf+96},100,0')
        watch.enabled = False
        timeline.append({'tick': tick, 'edits': edits, 'after': [[*p, get(p)] for p in sorted(touched)]})
    watch.delete()
    delay = int(call(0x1e8dcd0, 'int(*)(void*,void*)', f'{dynamic},{region}'))
    with open(os.path.join(os.environ['ORACLE_OUT_DIR'], 'water.json'), 'w') as out:
        horizontal = list(bytes(inf.read_memory(q(0x32f3cd8), q(0x32f3ce0)-q(0x32f3cd8))))
        json.dump({'tick_delay': delay, 'horizontal_faces': horizontal, 'cases': cases,
                   'queue': {'base_tick': base_tick, 'initial': [[*p, i] for p, i in sorted(scene.items())],
                             'steps': timeline}}, out)
    print('Native water cases:', len(cases))


_run(state)
