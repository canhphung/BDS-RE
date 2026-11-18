"""Native TreeFeature::_placeLeaf cases, called directly as a function oracle.

_placeLeaf 0x1b59d60 is invoked per cell from GDB with a real BlockSource, so
the three Material::isType answers it asks can be recorded without stopping
inside a call that GDB owns. This is the GrassBlock-oracle pattern.

No feature code, RNG draw or expected result is patched.
"""
import json
import os
import random
import struct
import subprocess
from pathlib import Path

import gdb


def leaf_cases(probe):
    for bp in gdb.breakpoints():
        bp.enabled = False
    inf = gdb.selected_inferior()
    def mem(a, n): return bytes(inf.read_memory(a, n))
    def q(a): return struct.unpack('<Q', mem(a, 8))[0]
    def call(a, t, args): return gdb.parse_and_eval(f'(({t}){a})({args})')
    def alloc(n): return int(gdb.parse_and_eval(f'(void*)malloc({n})'))

    player = q(q(probe['level'] + 0x50))
    live_region = int(call(0x21a6660, 'void*(*)(void*)', str(player)))
    region, queue, tick = alloc(0xc0), alloc(0x48), alloc(8)
    call(0x1c584f0, 'void(*)(void*,void*,void*,void*,int,int)',
         f'{region},{probe["level"]},{q(live_region+0x28)},{q(live_region+0x20)},1,1')
    call(0x1b3b5b0, 'void(*)(void*,unsigned long)', f'{tick},1')
    call(0x1b3b5d0, 'void(*)(void*,void*)', f'{queue},{tick}')
    call(0x1c59360, 'void(*)(void*,void*)', f'{region},{queue}')

    pos, feature = alloc(32), alloc(0x80)
    pointers, names = {}, {}

    def block_name(block):
        if block not in names:
            legacy = q(block + 0x10)
            ptr, length = struct.unpack('<QQ', mem(legacy + 0x28, 16))
            names[block] = mem(ptr, length).decode() + ':' + str(struct.unpack('<H', mem(block + 8, 2))[0])
        return names[block]

    def global_block(symbol):
        return q(int(gdb.parse_and_eval("&'" + symbol + "'")))

    for symbol, alias in (
            ('BedrockBlocks::mAir', 'air:0'), ('VanillaBlocks::mStone', 'stone:0'),
            ('VanillaBlocks::mDirt', 'dirt:0'), ('VanillaBlocks::mGrass', 'grass:0'),
            ('VanillaBlocks::mSand', 'sand:0'), ('VanillaBlocks::mGravel', 'gravel:0'),
            ('VanillaBlocks::mStillWater', 'water:0'), ('VanillaBlocks::mLeaves', 'leaves:0'),
            ('VanillaBlocks::mLog', 'log:0'), ('VanillaBlocks::mVine', 'vine:0'),
            ('VanillaBlocks::mBedrock', 'bedrock:0'), ('VanillaBlocks::mClay', 'clay:0')):
        block = global_block(symbol)
        pointers[alias] = block
        names[block] = alias

    root = Path(os.environ['ORACLE_OUT_DIR'])
    helper = root / 'leaf_setup.c'
    helper.write_text('''#include <stddef.h>
struct Edit { int x, y, z, padding; const void *block; };
void reset_region(void *region, const struct Edit *edits, size_t count) {
    typedef unsigned char (*Setter)(void*, const int*, const void*, int, void*);
    Setter set = (Setter)0x1c59b20;
    for (size_t i = 0; i < count; ++i)
        set(region, &edits[i].x, edits[i].block, 0, 0);
}
''')
    library = helper.with_suffix('.so')
    subprocess.run(['cc', '-O2', '-fPIC', '-shared', str(helper), '-o', str(library)],
                   check=True, capture_output=True)
    text = alloc(4096)
    inf.write_memory(text, str(library).encode() + b'\0')
    handle = int(gdb.parse_and_eval(f'((void*(*)(const char*,int))dlopen)((char*){text},2)'))
    def symbol(name):
        inf.write_memory(text, name.encode() + b'\0')
        return int(gdb.parse_and_eval(
            f'((void*(*)(void*,const char*))dlsym)((void*){handle},(char*){text})'))
    reset = symbol('reset_region')
    edits_ptr = alloc(1_000_000)

    def put(edits):
        if not edits: return
        inf.write_memory(edits_ptr, b''.join(
            struct.pack('<iiiiQ', *p[:3], 0, pointers[p[3]]) for p in edits))
        call(reset, 'void(*)(void*,void*,unsigned long)', f'{region},{edits_ptr},{len(edits)}')

    probes, writes = [], []
    class Entry(gdb.Breakpoint):
        def __init__(self): super().__init__('*0x1b59d60', internal=True)
        def stop(self):
            probes.append([None, None, None]); return False
    class Mat(gdb.Breakpoint):
        def __init__(self, address, which):
            super().__init__(f'*{address:#x}', internal=True); self.which = which
        def stop(self):
            if probes and probes[-1][self.which] is None:
                probes[-1][self.which] = int(gdb.parse_and_eval('$al')) & 1
            return False
    class Write(gdb.Breakpoint):
        def stop(self):
            p = int(gdb.parse_and_eval('$rsi'))
            writes.append([*struct.unpack('<iii', mem(p, 12)),
                           block_name(int(gdb.parse_and_eval('$rdx'))),
                           int(gdb.parse_and_eval('$ecx'))])
            return False
    entry = Entry()
    mats = [Mat(0x1b59db4, 0), Mat(0x1b59dc7, 1), Mat(0x1b59ddd, 2)]
    write = Write('*0x1c59b20', internal=True)
    observers = [entry, write] + mats
    for bp in observers: bp.enabled = False

    blocks = ['air:0', 'stone:0', 'dirt:0', 'grass:0', 'sand:0', 'water:0',
              'leaves:0', 'log:0', 'gravel:0', 'vine:0']
    cases = []
    for block in blocks:
        for dx in (0, 8, -16):
            put([[dx, 6, 0, block]])
            at = (dx, 6, 0)
            inf.write_memory(pos, struct.pack('<iii', *at))
            probes.clear(); writes.clear()
            for bp in observers: bp.enabled = True
            call(0x1b59cf0, 'void(*)(void*,void*,int,int,int)', f'{feature},0,0,0,0')
            result = bool(int(call(0x1b59d60, 'unsigned char(*)(void*,void*,void*)',
                                   f'{feature},{region},{pos}')) & 1)
            for bp in observers: bp.enabled = False
            cases.append({'at': list(at), 'block': block, 'result': result,
                          'probes': [list(p) for p in probes],
                          'writes': list(writes)})
    for bp in observers: bp.delete()
    (root / 'leaf.json').write_text(json.dumps({'cases': cases}, separators=(',', ':')))
    call(0x1b3b7c0, 'void(*)(void*)', str(queue))
    call(0x1c58760, 'void(*)(void*)', str(region))
    print('Native leaf cases complete:', len(cases))


leaf_cases(state)
