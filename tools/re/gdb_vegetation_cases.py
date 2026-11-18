"""Native vegetation/small-surface feature cases, run in a paused original server.

Every feature is constructed by its REAL constructor and called through its real
virtual entry; no feature code, RNG draw, condition or expected result is
patched. The setup-only C helper loops over the original BlockSource setter and
getter to initialise/snapshot large scenes quickly.

Covered (with their constructors):

  TallGrassFeature(Block const&) 0x1b66370 -> place 0x1b66570
  FlowerFeature(Block const&)    0x1b5ef20 -> place 0x1b5ef70
  PumpkinFeature()               0x1dac580 -> place 0x1b52430
  CactusFeature()                0x1dabd40 -> place 0x1b5d3b0
  WaterlilyFeature()             0x1dac160 -> place 0x1b53000
  ReedsFeature()                 0x1dab920 -> place 0x1b5d070
  VinesFeature()                 0x1db25e0 -> place 0x1b531a0

Use with capture_level_random.py --gdb-post-script. Each case records every raw
Random word, every Feature::_placeBlock attempt (position, block, flags) and the
final block snapshot, so the C++ translation must reproduce the native call
order, not just the visible result.
"""
import json
import os
import random
import struct
import subprocess
from pathlib import Path

import gdb


def vegetation_cases(probe):
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

    # Mirror the lake oracle: a real BlockSource over the live chunks with the
    # original event-suppression flags (true, true) plus a Tick(1) queue. The
    # component oracle stops before neighbor events or pending tick jobs.
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

    # Resolve block globals by their demangled symbol, never by a hardcoded
    # address: VanillaBlocks:: holds Block const* and VanillaBlockTypes:: holds
    # BlockLegacy const*, and the two families are easy to confuse.
    def global_block(symbol):
        address = int(gdb.parse_and_eval("&'" + symbol + "'"))
        return q(address)

    for symbol, alias in (
            ('BedrockBlocks::mAir', 'air:0'), ('VanillaBlocks::mStone', 'stone:0'),
            ('VanillaBlocks::mDirt', 'dirt:0'), ('VanillaBlocks::mGrass', 'grass:0'),
            ('VanillaBlocks::mPodzol', 'podzol:0'), ('VanillaBlocks::mMycelium', 'mycelium:0'),
            ('VanillaBlocks::mStillWater', 'water:0'), ('VanillaBlocks::mStillLava', 'lava:0'),
            ('VanillaBlocks::mSand', 'sand:0'), ('VanillaBlocks::mGravel', 'gravel:0'),
            ('VanillaBlocks::mBedrock', 'bedrock:0'), ('VanillaBlocks::mIce', 'ice:0'),
            ('VanillaBlocks::mTallgrass', 'tall_grass:0'),
            ('VanillaBlocks::mYellowFlower', 'flower:0'),
            ('VanillaBlocks::mRedFlower', 'flower_red:0'),
            ('VanillaBlocks::mPumpkin', 'pumpkin:0'), ('VanillaBlocks::mCactus', 'cactus:0'),
            ('VanillaBlocks::mWaterlily', 'waterlily:0'), ('VanillaBlocks::mReeds', 'reed:0'),
            ('VanillaBlocks::mVine', 'vine:0'), ('VanillaBlocks::mChest', 'chest:0'),
            ('VanillaBlocks::mTorch', 'torch:0')):
        block = global_block(symbol)
        pointers[alias] = block
        names[block] = alias
    # TallGrassFeature and FlowerFeature place exactly the Block they were
    # constructed with / given as a parameter, so the global pointers are enough;
    # no BlockState variant is invented here.

    root = Path(os.environ['ORACLE_OUT_DIR'])
    helper = root / 'vegetation_setup.c'
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
        payload = []
        for x, y, z, name in edits:
            if name not in pointers:
                raise RuntimeError('unknown block in scene: ' + name)
            payload.append(struct.pack('<iiiiQ', x, y, z, 0, pointers[name]))
        inf.write_memory(edits_ptr, b''.join(payload))
        call(reset, 'void(*)(void*,void*,unsigned long)', f'{region},{edits_ptr},{len(edits)}')

    def scan(coords):
        inf.write_memory(coords_ptr, b''.join(struct.pack('<iii', *p) for p in coords))
        call(snapshot, 'void(*)(void*,void*,unsigned long,void*)',
             f'{region},{coords_ptr},{len(coords)},{blocks_ptr}')
        return [block_name(b) for b in
                struct.unpack('<%dQ' % len(coords), mem(blocks_ptr, 8 * len(coords)))]

    words, writes = [], []

    class Draw(gdb.Breakpoint):
        def stop(self):
            if q(int(gdb.parse_and_eval('$rbp')) - 8) == rng:
                words.append(int(gdb.parse_and_eval('$eax')) & 0xffffffff)
            return False

    class Write(gdb.Breakpoint):
        def stop(self):
            p = int(gdb.parse_and_eval('$rsi'))
            writes.append([*struct.unpack('<iii', mem(p, 12)),
                           block_name(int(gdb.parse_and_eval('$rdx'))),
                           int(gdb.parse_and_eval('$ecx'))])
            return False

    draw = Draw('*0x187e704', internal=True)
    write = Write('*0x1c59b20', internal=True)

    # Native dependency answers, recorded per case and replayed by the host
    # test. The result is read at the call's return address, where %al still
    # holds it, so the answer is the original's own predicate result.
    dependencies = []
    one_shot = []

    class Result(gdb.Breakpoint):
        def __init__(self, address, entry):
            super().__init__(f'*{address:#x}', internal=True)
            self.entry = entry

        def stop(self):
            try:
                self.entry['result'] = int(gdb.parse_and_eval('$al')) & 1
            except gdb.error:
                self.entry['result'] = 0
            self.enabled = False
            one_shot.append(self)
            return False

    class Query(gdb.Breakpoint):
        """Record a predicate call's position and its native result.

        register is where the BlockPos const& argument lives:
          BlockSource::isEmptyBlock(this, BlockPos const&)      -> rsi
          BlockSource::isSolidBlockingBlock(this, BlockPos)     -> rsi
          Block::canSurvive(this, BlockSource&, BlockPos const&) -> rdx
          Block::mayPlace(this, BlockSource&, BlockPos const&)   -> rdx
          Block::mayPlaceOn(this, BlockSource&, BlockPos const&) -> rdx
        """

        def __init__(self, address, kind, register):
            super().__init__(f'*{address:#x}', internal=True)
            self.kind, self.register = kind, register

        def stop(self):
            if not dependencies:
                return False
            pointer = int(gdb.parse_and_eval(f'${self.register}'))
            entry = {'kind': self.kind,
                     'position': list(struct.unpack('<iii', mem(pointer, 12)))}
            dependencies[-1].append(entry)
            return_address = int(gdb.parse_and_eval('*(void**)$rsp'))
            Result(return_address, entry)
            return False

    queries = [
        Query(0x1c60cb0, 'is_empty', 'rsi'),
        Query(0x1c5aea0, 'is_solid_blocking', 'rsi'),
        Query(0x1e69f10, 'can_survive', 'rdx'),
        Query(0x1e6a5a0, 'may_place', 'rdx'),
        Query(0x1e6a540, 'may_place_on', 'rdx'),
    ]
    observers = [draw, write] + queries
    for bp in observers:
        bp.enabled = False

    # A 20x20 column area around the anchor, Y 2..21, is enough for every
    # feature's nextGaussianInt spread (<= 8) plus the cactus column.
    bx = bz = -16
    coords = [(bx + x, y, bz + z) for x in range(20) for z in range(20) for y in range(2, 22)]

    def scene(anchor_x, anchor_z, ground, top=4, fill='stone:0', above='air:0', radius=10):
        edits = []
        for dx in range(-radius, radius + 1):
            for dz in range(-radius, radius + 1):
                for y in range(2, top + 1):
                    edits.append([anchor_x + dx, y, anchor_z + dz, fill])
                edits.append([anchor_x + dx, top, anchor_z + dz, ground])
                for y in range(top + 1, 22):
                    edits.append([anchor_x + dx, y, anchor_z + dz, above])
        return edits

    # Real constructors: default ones take only `this`; the two Block-taking
    # ones take the block to place.
    features = {
        'tall_grass': (0x1b66370, 'void(*)(void*,void*)', 'tall_grass:0', 0x1b66570),
        'flower': (0x1b5ef20, 'void(*)(void*,void*)', 'flower:0', 0x1b5ef70),
        'pumpkin': (0x1dac580, 'void(*)(void*)', None, 0x1b52430),
        'cactus': (0x1dabd40, 'void(*)(void*)', None, 0x1b5d3b0),
        'waterlily': (0x1dac160, 'void(*)(void*)', None, 0x1b53000),
        'reed': (0x1dab920, 'void(*)(void*)', None, 0x1b5d070),
        'vine': (0x1db25e0, 'void(*)(void*)', None, 0x1b531a0),
    }
    r = random.Random(20181)
    cases = []
    for name, (ctor, ctor_type, ctor_block, entry) in features.items():
        if ctor_block:
            call(ctor, ctor_type, f'{feature},{pointers[ctor_block]}')
        else:
            call(ctor, ctor_type, str(feature))
        for n in range(10):
            seed = r.randrange(2 ** 32)
            # Alternate anchor height and scene so survival paths differ.
            top = 4 if n % 3 else 8
            ground = 'grass:0'
            above = 'air:0'
            if name == 'waterlily':
                ground, above, top = 'water:0', 'water:0', 4
            elif name == 'vine':
                ground, top = 'stone:0', 4
            elif name == 'reed':
                ground = 'grass:0' if n % 2 else 'sand:0'
            at = (bx + 8, top + 1 + (0 if n % 2 else 4), bz + 8)
            edits = scene(at[0], at[2], ground, top=top, above=above)
            if name in ('flower', 'tall_grass') and n % 4 == 1:
                edits = [e for e in edits if not (e[1] == top and e[0] == at[0] + 1 and e[2] == at[2])]
                edits.append([at[0] + 1, top, at[2], 'mycelium:0' if n % 8 == 1 else 'podzol:0'])
            put(edits)
            before = scan(coords)
            call(0x187e350, 'void(*)(void*,unsigned)', f'{rng},{seed}')
            inf.write_memory(pos, struct.pack('<iii', *at))
            words.clear()
            writes.clear()
            dependencies.append([])
            for bp in observers:
                bp.enabled = True
            result = bool(int(call(entry, 'unsigned char(*)(void*,void*,void*,void*)',
                                   f'{feature},{region},{pos},{rng}')) & 1)
            for bp in observers:
                bp.enabled = False
            for bp in one_shot:
                bp.delete()
            one_shot.clear()
            after = scan(coords)
            tail = [int(call(0x187d470, 'int(*)(void*)', str(rng))) for _ in range(4)]
            cases.append({'feature': name, 'at': list(at), 'seed': seed,
                          'initial': [[*p, b] for p, b in zip(coords, before)],
                          'result': result, 'words': list(words),
                          'writes': list(writes), 'final': after, 'tail': tail,
                          'dependencies': dependencies[-1]})
            print('Native vegetation:', name, n, result, len(writes), 'writes',
                  len(words), 'words', flush=True)
    for bp in observers:
        bp.delete()
    (root / 'vegetation.json').write_text(json.dumps({'cases': cases}, separators=(',', ':')))
    call(0x1b3b7c0, 'void(*)(void*)', str(queue))
    call(0x1c58760, 'void(*)(void*)', str(region))
    print('Native vegetation cases complete:', len(cases))


vegetation_cases(state)
