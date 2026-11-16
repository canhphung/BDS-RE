"""Independent protocol-282 chunk reader for reference and live socket tests."""
import struct
from analyze_capture import Reader


def parse_chunk(body):
    r = Reader(body)
    x, z = r.varint(), r.varint()
    s = Reader(r.take(r.uvarint()))
    if r.at != len(body):
        raise ValueError('trailing FullChunkData bytes')
    sections = []
    for _ in range(s.take(1)[0]):
        version, count = s.take(2)
        if version != 8 or not 1 <= count <= 2:
            raise ValueError('unsupported subchunk format')
        layers = []
        for _ in range(count):
            header = s.take(1)[0]
            bits = header >> 1
            if not header & 1 or bits not in (1, 2, 3, 4, 5, 6, 8, 16):
                raise ValueError('invalid network palette header')
            per_word = 32 // bits
            words = struct.unpack('<' + 'I' * ((4096 + per_word - 1) // per_word),
                                  s.take(((4096 + per_word - 1) // per_word) * 4))
            palette = [s.varint() for _ in range(s.varint())]
            cells = [palette[(words[i // per_word] >> (i % per_word * bits)) & ((1 << bits) - 1)] for i in range(4096)]
            layers.append(cells)
        sections.append(layers)
    heights = struct.unpack('<256H', s.take(512))
    biomes = s.take(256)
    suffix = s.take(len(s.data) - s.at)
    if not suffix:
        raise ValueError('missing border count')
    return {'x': x, 'z': z, 'sections': sections, 'heights': heights, 'biomes': biomes, 'suffix': suffix}
