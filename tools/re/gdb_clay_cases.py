"""Native ClayFeature cases in a paused original server.

ClayFeature::place 0x1b55840 is called for real through the feature's vtable
(the constructor 0x1b557f0 only stores the size). Dependencies recorded:
BlockSource::getBlock 0x1c58ad0, Block::getMaterial 0x1e68cf0 through
Material::isType 0x1c56be0, and the writes through Feature::_placeBlock
0x1c59b20. No feature code, RNG draw or expected result is patched.

Use with capture_level_random.py --gdb-post-script.
"""
import json
import os
import random
import struct
import subprocess
from pathlib import Path

import gdb


def clay_cases(probe):
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
            ('VanillaBlocks::mClay', 'clay:0'),
            ('VanillaBlocks::mBedrock', 'bedrock:0')):
        block = global_block(symbol)
        pointers[alias] = block
        names[block] = alias

    root = Path(os.environ['ORACLE_OUT_DIR'])
    helper = root / 'clay_setup.c'
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
        """Record a block query's position and which cell it belongs to.

        Two getBlock call sites matter and they mean different things:
          * 0x1b5586e tests the ANCHOR's material (water or clay) and decides
            whether the feature proceeds at all;
          * 0x1b55ac5 tests a candidate cell and only dirt becomes clay.
        The return address tells them apart.
        """

        def __init__(self, address, register):
            super().__init__(f'*{address:#x}', internal=True)
            self.register = register

        def stop(self):
            pointer = int(gdb.parse_and_eval(f'${self.register}'))
            return_address = int(gdb.parse_and_eval('*(void**)$rsp'))
            entry = {'kind': 'anchor_block' if return_address == 0x1b55873 else 'candidate_block',
                     'position': list(struct.unpack('<iii', mem(pointer, 12)))}
            queries.append(entry)
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

    one_shot = []
    draw = Draw('*0x187e704', internal=True)
    write = Write('*0x1c59b20', internal=True)
    # BlockSource::getBlock(this, BlockPos const&) 0x1c58ad0: the BlockPos is
    # rsi. Each getBlock in this feature is followed by the material test on
    # the returned block, so the recorded position is the cell being tested.
    query = Query(0x1c58ad0, 'rsi')
    observers = [draw, write, query]
    for bp in observers:
        bp.enabled = False

    bx = bz = -16
    coords = [(bx + x, y, bz + z) for x in range(24) for z in range(24) for y in range(2, 26)]

    r = random.Random(7717)
    cases = []
    for n in range(14):
        size = (3, 5, 8, 12)[n % 4]
        at = (bx + 8, 6 + (n % 3) * 4, bz + 8)
        edits = []
        for dx in range(-7, 8):
            for dz in range(-7, 8):
                # 0x1b5587b..0x1b558b7 accepts when the target's material is
                # water OR the target is already clay; anything else returns
                # false immediately. The scene therefore puts the anchor in a
                # water column for the accept cases and in stone for the
                # reject cases.
                # 0x1b55ad8..0x1b55af1: the cell that becomes clay must be DIRT
                # (VanillaBlockTypes::mDirt), not stone or sand.
                target_is_water = n % 3 != 2
                for y in range(2, 26):
                    name = 'dirt:0'
                    if (dx + dz + y) % 7 == 0:
                        name = 'stone:0'
                    if target_is_water and (dx * dx + dz * dz) <= 36 and y >= 5:
                        name = 'water:0'
                    if n % 3 == 1 and y == at[1] + 1 and (dx * dx + dz * dz) <= 36:
                        name = 'clay:0'
                    edits.append([at[0] + dx, y, at[2] + dz, name])
        put(edits)
        before = scan(coords)
        seed = r.randrange(2 ** 32)
        call(0x187e350, 'void(*)(void*,unsigned)', f'{rng},{seed}')
        inf.write_memory(pos, struct.pack('<iii', *at))
        words.clear()
        writes.clear()
        queries.clear()
        for bp in observers:
            bp.enabled = True
        call(0x1b557f0, 'void(*)(void*,int)', f'{feature},{size}')
        result = bool(int(call(0x1b55840, 'unsigned char(*)(void*,void*,void*,void*)',
                               f'{feature},{region},{pos},{rng}')) & 1)
        for bp in observers:
            bp.enabled = False
        for bp in one_shot:
            bp.delete()
        one_shot.clear()
        after = scan(coords)
        tail = [int(call(0x187d470, 'int(*)(void*)', str(rng))) for _ in range(4)]
        cases.append({'at': list(at), 'seed': seed, 'size': size, 'result': result,
                      'words': list(words), 'writes': list(writes),
                      'queries': list(queries),
                      'initial': [[*p, b] for p, b in zip(coords, before)],
                      'final': after, 'tail': tail})
        print('Native clay:', n, size, result, len(writes), 'writes',
              len(words), 'words', flush=True)
    for bp in observers:
        bp.delete()
    (root / 'clay.json').write_text(json.dumps({'cases': cases}, separators=(',', ':')))
    call(0x1b3b7c0, 'void(*)(void*)', str(queue))
    call(0x1c58760, 'void(*)(void*)', str(region))
    print('Native clay cases complete:', len(cases))


clay_cases(state)
