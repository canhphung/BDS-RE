"""Native BonusChestFeature cases in a paused original server.

BonusChestFeature::place 0x1b526b0 is called for real. The feature creates its
OWN local Random seeded from the caller's seed (0x1b52da), so the oracle tracks
two streams: the caller's Random (which must stay untouched) and the local one
(which supplies the loot-table seed). Both are recorded with their object
addresses so the C++ side can prove the separation.

Dependencies recorded: BlockSource::findNextTopSolidBlockUnder 0x1c61410,
BlockSource::isEmptyBlock 0x1c60cb0, BlockSource::isConsideredSolidBlock
0x1c5af40, BlockSource::getBlockEntity 0x1c5b8e0, and the block writes through
Feature::_placeBlock 0x1c59b20. No feature code, RNG draw or expected result is
patched.

Use with capture_level_random.py --gdb-post-script.
"""
import json
import os
import random
import struct
import subprocess
from pathlib import Path

import gdb


def bonus_chest_cases(probe):
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

    # A real BlockSource over the live chunks with the component-oracle flags,
    # exactly as the lake and vegetation oracles do.
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
            ('VanillaBlocks::mChest', 'chest:0'),
            ('VanillaBlocks::mTorch', 'torch:0'),
            ('VanillaBlocks::mBedrock', 'bedrock:0')):
        block = global_block(symbol)
        pointers[alias] = block
        names[block] = alias

    root = Path(os.environ['ORACLE_OUT_DIR'])
    helper = root / 'bonus_chest_setup.c'
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
    edits_ptr, coords_ptr, blocks_ptr = alloc(4_000_000), alloc(2_000_000), alloc(2_000_000)

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

    caller_words, local_words, writes, queries = [], [], [], []
    local_address = [None]
    caller_words_active = [False]
    one_shot = []

    class Draw(gdb.Breakpoint):
        """Record every _genRandInt32 result with the Random object's address.

        The draw is observed at the function's single return epilogue
        (0x187e704). _genRandInt32 saves `this` at -0x8(%rbp) on entry.
        """

        def __init__(self):
            super().__init__('*0x187e704', internal=True)

        def stop(self):
            if not caller_words_active[0]:
                return False
            # _genRandInt32 stores `this` at -0x8(%rbp) (0x187e468) and the
            # return epilogue at 0x187e704 runs with rbp still pointing at that
            # frame, so read the object from there.
            owner = struct.unpack('<Q', mem(int(gdb.parse_and_eval('$rbp')) - 8, 8))[0]
            value = int(gdb.parse_and_eval('$eax')) & 0xFFFFFFFF
            if owner == rng:
                caller_words.append(value)
            elif local_address[0] is not None:
                local_words.append(value)
            return False

    draw = Draw()

    class LocalRandom(gdb.Breakpoint):
        """Record the feature's own Random object address (its ctor's `this`)."""

        def stop(self):
            if not caller_words_active[0]:
                return False
            candidate = int(gdb.parse_and_eval('$rdi'))
            if candidate != rng:
                local_address[0] = candidate
            return False

    local_ctor = LocalRandom('*0x187e350', internal=True)

    class Write(gdb.Breakpoint):
        def stop(self):
            p = int(gdb.parse_and_eval('$rsi'))
            writes.append([*struct.unpack('<iii', mem(p, 12)),
                           block_name(int(gdb.parse_and_eval('$rdx'))),
                           int(gdb.parse_and_eval('$ecx'))])
            return False

    class Query(gdb.Breakpoint):
        """Record a dependency call's position and its native result."""

        def __init__(self, address, kind, register, top_solid=False):
            super().__init__(f'*{address:#x}', internal=True)
            self.kind, self.register, self.top_solid = kind, register, top_solid

        def stop(self):
            if not queries:
                return False
            pointer = int(gdb.parse_and_eval(f'${self.register}'))
            entry = {'kind': self.kind,
                     'position': list(struct.unpack('<iii', mem(pointer, 12)))}
            queries[-1].append(entry)
            QueryResult(int(gdb.parse_and_eval('*(void**)$rsp')), entry)
            return False

    class QueryResult(gdb.Breakpoint):
        def __init__(self, address, entry):
            super().__init__(f'*{address:#x}', internal=True)
            self.entry = entry

        def stop(self):
            try:
                self.entry['raw'] = int(gdb.parse_and_eval('$eax')) & 0xFFFFFFFF
                self.entry['result'] = int(gdb.parse_and_eval('$al')) & 1
            except gdb.error:
                self.entry['raw'] = 0
                self.entry['result'] = 0
            self.enabled = False
            one_shot.append(self)
            return False

    write = Write('*0x1c59b20', internal=True)
    query_breakpoints = [
        Query(0x1c61410, 'top_solid', 'rsi'),      # findNextTopSolidBlockUnder
        Query(0x1c60cb0, 'is_empty', 'rsi'),       # isEmptyBlock
        Query(0x1c5af40, 'solid_below', 'rsi'),    # isConsideredSolidBlock
    ]
    observers = [draw, local_ctor, write] + query_breakpoints
    for bp in observers:
        bp.enabled = False

    # Arena: a 24x24 patch with varied surface heights so
    # findNextTopSolidBlockUnder has to descend.
    bx = bz = -16
    coords = [(bx + x, y, bz + z) for x in range(24) for z in range(24) for y in range(2, 26)]

    def scene(surface, gaps):
        edits = []
        for dx in range(-8, 9):
            for dz in range(-8, 9):
                height = surface + ((dx * 7 + dz * 3) % 3)
                for y in range(2, height + 1):
                    edits.append([bx + 8 + dx, y, bz + 8 + dz, 'stone:0'])
                edits.append([bx + 8 + dx, height, bz + 8 + dz, 'grass:0'])
                for y in range(height + 1, 26):
                    edits.append([bx + 8 + dx, y, bz + 8 + dz, 'air:0'])
        for gx, gz in gaps:
            for y in range(20, 26):
                edits.append([bx + 8 + gx, y, bz + 8 + gz, 'air:0'])
        return edits

    r = random.Random(9041)
    cases = []
    for n in range(12):
        surface = 4 + (n % 4) * 3
        gaps = [(0, 0)] if n % 3 == 0 else ([(2, 2), (-2, -2)] if n % 3 == 1 else [])
        edits = scene(surface, gaps)
        put(edits)
        before = scan(coords)
        seed = r.randrange(2 ** 32)
        call(0x187e350, 'void(*)(void*,unsigned)', f'{rng},{seed}')
        at = (bx + 8, 22, bz + 8)
        inf.write_memory(pos, struct.pack('<iii', *at))
        caller_words.clear()
        local_words.clear()
        writes.clear()
        queries.append([])
        local_address[0] = None
        caller_words_active[0] = True
        for bp in observers:
            bp.enabled = True
        # The constructor takes no arguments; the default one is at 0x1b3b760.
        call(0x1b3b760, 'void(*)(void*)', str(feature))
        result = bool(int(call(0x1b526b0, 'unsigned char(*)(void*,void*,void*,void*)',
                               f'{feature},{region},{pos},{rng}')) & 1)
        for bp in observers:
            bp.enabled = False
        caller_words_active[0] = False
        for bp in one_shot:
            bp.delete()
        one_shot.clear()
        after = scan(coords)
        tail = [int(call(0x187d470, 'int(*)(void*)', str(rng))) for _ in range(4)]
        cases.append({'at': list(at), 'seed': seed, 'scene': n, 'surface': surface,
                      'gaps': gaps, 'result': result,
                      'words': list(caller_words), 'local_words': list(local_words),
                      'writes': list(writes), 'queries': queries[-1],
                      'initial': [[*p, b] for p, b in zip(coords, before)],
                      'final': after, 'tail': tail})
        print('Native bonus chest:', n, result, len(writes), 'writes',
              len(caller_words), 'caller words', len(local_words), 'local words', flush=True)
    for bp in observers:
        bp.delete()
    (root / 'bonus_chest.json').write_text(json.dumps({'cases': cases}, separators=(',', ':')))
    call(0x1b3b7c0, 'void(*)(void*)', str(queue))
    call(0x1c58760, 'void(*)(void*)', str(region))
    print('Native bonus chest cases complete:', len(cases))


bonus_chest_cases(state)
