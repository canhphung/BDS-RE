"""Native TreeFeature cases in a paused original server.

TreeFeature is constructed with its real constructor 0x1b59cf0
`(Actor*, int leafType, int trunkType, bool)` and called through
`place(BlockSource&, BlockPos const&, Random&, int)` 0x1b5a290, the real body
that every subclass funnels into.

Dependencies recorded, with the position each call was made on:
BlockSource::getMaxHeight 0x1b50550, Block::mayPlaceOn 0x1e6a540,
BlockSource::getBlock 0x1c58ad0, BlockSource::isEmptyBlock 0x1c60cb0, and
every write through Feature::_placeBlock 0x1c59b20. Every Random word is
recorded. No feature code, RNG draw or expected result is patched.

Use with capture_level_random.py --gdb-post-script.
"""
import json
import os
import random
import struct
import subprocess
from pathlib import Path

import gdb


def tree_cases(probe):
    for bp in gdb.breakpoints():
        bp.enabled = False
    inf = gdb.selected_inferior()

    def mem(a, n):
        return bytes(inf.read_memory(a, n))

    def q(a):
        return struct.unpack('<Q', mem(a, 8))[0]

    def call(a, t, args):
        return gdb.parse_and_eval(f'(({t}){a})({args})')

    def alloc(n):
        return int(gdb.parse_and_eval(f'(void*)malloc({n})'))

    player = q(q(probe['level'] + 0x50))
    live_region = int(call(0x21a6660, 'void*(*)(void*)', str(player)))
    region, queue, tick = alloc(0xc0), alloc(0x48), alloc(8)
    call(0x1c584f0, 'void(*)(void*,void*,void*,void*,int,int)',
         f'{region},{probe["level"]},{q(live_region+0x28)},{q(live_region+0x20)},1,1')
    call(0x1b3b5b0, 'void(*)(void*,unsigned long)', f'{tick},1')
    call(0x1b3b5d0, 'void(*)(void*,void*)', f'{queue},{tick}')
    call(0x1c59360, 'void(*)(void*,void*)', f'{region},{queue}')

    pos, rng, feature = alloc(32), alloc(0x9d4), alloc(0x80)
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
            ('BedrockBlocks::mAir', 'air:0'),
            ('VanillaBlocks::mStone', 'stone:0'),
            ('VanillaBlocks::mDirt', 'dirt:0'),
            ('VanillaBlocks::mGrass', 'grass:0'),
            ('VanillaBlocks::mSand', 'sand:0'),
            ('VanillaBlocks::mGravel', 'gravel:0'),
            ('VanillaBlocks::mStillWater', 'water:0'),
            ('VanillaBlocks::mLeaves', 'leaves:0'),
            ('VanillaBlocks::mLog', 'log:0'),
            ('VanillaBlocks::mBedrock', 'bedrock:0'),
            ('VanillaBlocks::mClay', 'clay:0')):
        try:
            block = global_block(symbol)
        except gdb.error:
            continue
        pointers[alias] = block
        names[block] = alias

    if 'leaves:0' not in pointers or 'log:0' not in pointers:
        raise RuntimeError('tree block globals not resolved')

    root = Path(os.environ['ORACLE_OUT_DIR'])
    helper = root / 'tree_setup.c'
    helper.write_text('''#include <stddef.h>
struct Edit { int x, y, z, padding; const void *block; };
void reset_region(void *region, const struct Edit *edits, size_t count) {
    typedef unsigned char (*Setter)(void*, const int*, const void*, int, void*);
    Setter set = (Setter)0x1c59b20;
    for (size_t i = 0; i < count; ++i)
        set(region, &edits[i].x, edits[i].block, 0, 0);
}
void snapshot_region(void *region, const int *xyz, size_t count, const void **out) {
    typedef const void *(*Getter)(void*, const int*);
    Getter get = (Getter)0x1c58ad0;
    for (size_t i = 0; i < count; ++i)
        out[i] = get(region, xyz + 3 * i);
}
''')
    library = helper.with_suffix('.so')
    subprocess.run(['cc', '-O2', '-fPIC', '-shared', str(helper), '-o', str(library)],
                   check=True, capture_output=True)
    text = alloc(4096)
    inf.write_memory(text, str(library).encode() + b'\0')
    handle = int(gdb.parse_and_eval(f'((void*(*)(const char*,int))dlopen)((char*){text},2)'))
    if not handle:
        raise RuntimeError('native setup helper could not be loaded')

    def symbol(name):
        inf.write_memory(text, name.encode() + b'\0')
        return int(gdb.parse_and_eval(
            f'((void*(*)(void*,const char*))dlsym)((void*){handle},(char*){text})'))

    reset, snapshot = symbol('reset_region'), symbol('snapshot_region')
    edits_ptr, coords_ptr, blocks_ptr = alloc(6_000_000), alloc(3_000_000), alloc(3_000_000)

    def put(edits):
        if not edits:
            return
        inf.write_memory(edits_ptr, b''.join(
            struct.pack('<iiiiQ', *p[:3], 0, pointers[p[3]]) for p in edits))
        call(reset, 'void(*)(void*,void*,unsigned long)', f'{region},{edits_ptr},{len(edits)}')

    def scan(coords):
        inf.write_memory(coords_ptr, b''.join(struct.pack('<iii', *p) for p in coords))
        call(snapshot, 'void(*)(void*,void*,unsigned long,void*)',
             f'{region},{coords_ptr},{len(coords)},{blocks_ptr}')
        return [block_name(b) for b in
                struct.unpack('<%dQ' % len(coords), mem(blocks_ptr, 8 * len(coords)))]

    words, writes, queries = [], [], []

    class Draw(gdb.Breakpoint):
        def stop(self):
            if struct.unpack('<Q', mem(int(gdb.parse_and_eval('$rbp')) - 8, 8))[0] == rng:
                words.append(int(gdb.parse_and_eval('$eax')) & 0xFFFFFFFF)
            return False

    class Write(gdb.Breakpoint):
        def stop(self):
            p = int(gdb.parse_and_eval('$rsi'))
            writes.append([*struct.unpack('<iii', mem(p, 12)),
                           block_name(int(gdb.parse_and_eval('$rdx'))),
                           int(gdb.parse_and_eval('$ecx'))])
            return False

    class Query(gdb.Breakpoint):
        def __init__(self, address, kind, register):
            super().__init__(f'*{address:#x}', internal=True)
            self.kind, self.register = kind, register

        def stop(self):
            pointer = int(gdb.parse_and_eval(f'${self.register}'))
            entry = {'kind': self.kind,
                     'position': list(struct.unpack('<iii', mem(pointer, 12)))}
            queries.append(entry)
            QueryResult(int(gdb.parse_and_eval('*(void**)$rsp')), entry)
            return False

    class QueryResult(gdb.Breakpoint):
        def __init__(self, address, entry):
            super().__init__(f'*{address:#x}', internal=True)
            self.entry = entry

        def stop(self):
            try:
                self.entry['raw'] = int(gdb.parse_and_eval('$rax')) & 0xFFFFFFFF
                self.entry['result'] = int(gdb.parse_and_eval('$al')) & 1
            except gdb.error:
                self.entry['raw'] = 0
                self.entry['result'] = 0
            self.enabled = False
            one_shot.append(self)
            return False

    one_shot = []
    draw = Draw('*0x187e704', internal=True)
    write = Write('*0x1c59b20', internal=True)
    query_breakpoints = [
        Query(0x1e6a540, 'may_place_on', 'rdx'),   # Block::mayPlaceOn(this, BlockSource&, BlockPos)
        Query(0x1c58ad0, 'get_block', 'rsi'),      # BlockSource::getBlock(this, BlockPos)
        Query(0x1c60cb0, 'is_empty', 'rsi'),       # BlockSource::isEmptyBlock(this, BlockPos)
    ]
    observers = [draw, write] + query_breakpoints
    for bp in observers:
        bp.enabled = False

    bx = bz = -16
    coords = [(bx + x, y, bz + z) for x in range(20) for z in range(20) for y in range(2, 32)]

    r = random.Random(3313)
    cases = []
    for n in range(16):
        ground = ('grass:0', 'dirt:0', 'sand:0', 'stone:0')[n % 4]
        height = 4 + (n % 3) * 2
        at = (bx + 8, height + 1, bz + 8)
        edits = []
        for dx in range(-6, 7):
            for dz in range(-6, 7):
                for y in range(2, 32):
                    name = 'air:0'
                    if y < height:
                        name = 'stone:0'
                    elif y == height:
                        name = ground
                    edits.append([at[0] + dx, y, at[2] + dz, name])
        # Every fourth case puts an obstruction above the origin so the
        # _prepareSpawn / free-space path rejects.
        if n % 4 == 3:
            for y in range(at[1], at[1] + 4):
                edits.append([at[0], y, at[2], 'stone:0'])
        put(edits)
        before = scan(coords)
        seed = r.randrange(2 ** 32)
        call(0x187e350, 'void(*)(void*,unsigned)', f'{rng},{seed}')
        inf.write_memory(pos, struct.pack('<iii', *at))
        tree_height = 5 + (n % 3)
        leaf_type = int(pointers['leaves:0'])
        trunk_type = int(pointers['log:0'])
        words.clear()
        writes.clear()
        queries.clear()
        for bp in observers:
            bp.enabled = True
        call(0x1b59cf0, 'void(*)(void*,void*,int,int,int)',
             f'{feature},0,{leaf_type},{trunk_type},0')
        mode = os.environ.get('TREE_ORACLE_MODE', 'place')
        if mode == 'fallen':
            # _placeFallenTrunk(this, BlockSource&, BlockPos const&, Random&, int):
            # same register shape as _placeTrunk.
            result = bool(int(call(0x1b5a8a0, 'unsigned char(*)(void*,void*,void*,void*,int)',
                                   f'{feature},{region},{pos},{rng},{tree_height}')) & 1)
        elif mode == 'trunk':
            # _placeTrunk(this, BlockSource&, BlockPos const&, Random&, int):
            # rdi=this, rsi=region, rdx=pos, rcx=rng, r8d=height.
            result = bool(int(call(0x1b5ac20, 'unsigned char(*)(void*,void*,void*,void*,int)',
                                   f'{feature},{region},{pos},{rng},{tree_height}')) & 1)
        elif mode == 'prepare':
            # _prepareSpawn(this, BlockSource&, BlockPos const&, int):
            # rdi=this, rsi=region, rdx=pos, ecx=height.
            result = bool(int(call(0x1b59e30, 'unsigned char(*)(void*,void*,void*,int)',
                                   f'{feature},{region},{pos},{tree_height}')) & 1)
        else:
            result = bool(int(call(0x1b5a290, 'unsigned char(*)(void*,void*,void*,void*,int)',
                                   f'{feature},{region},{pos},{rng},{tree_height}')) & 1)
        for bp in observers:
            bp.enabled = False
        for bp in one_shot:
            bp.delete()
        one_shot.clear()
        after = scan(coords)
        tail = [int(call(0x187d470, 'int(*)(void*)', str(rng))) for _ in range(4)]
        cases.append({'at': list(at), 'seed': seed, 'height': tree_height,
                      'ground': ground, 'obstructed': n % 4 == 3, 'result': result,
                      'words': list(words), 'writes': list(writes),
                      'queries': list(queries),
                      'initial': [[*p, b] for p, b in zip(coords, before)],
                      'final': after, 'tail': tail})
        print('Native tree:', n, height, result, len(writes), 'writes',
              len(words), 'words', flush=True)
    for bp in observers:
        bp.delete()
    (root / 'tree.json').write_text(json.dumps({'cases': cases}, separators=(',', ':')))
    call(0x1b3b7c0, 'void(*)(void*)', str(queue))
    call(0x1c58760, 'void(*)(void*)', str(region))
    print('Native tree cases complete:', len(cases))


tree_cases(state)
