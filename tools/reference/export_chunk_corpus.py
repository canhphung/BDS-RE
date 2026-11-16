"""Export every FullChunkData body from a reference JSON capture.

Usage: python export_chunk_corpus.py capture.json tests/reference/chunks_bds_1.6.0.15
Outputs .zlib (unsigned-varint-length-prefixed packet bodies) and .json provenance.
Prints count and independent cell digest for the corresponding C++ regression test.
"""
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path
from bedrock_client import uvarint
from chunk_codec import parse_chunk

source, output = Path(sys.argv[1]), Path(sys.argv[2])
capture = json.loads(source.read_text())
packets = [bytes.fromhex(p['body']) for p in capture['packets'] if p['id'] == 0x3a]
if not packets:
    raise ValueError('capture has no FullChunkData packets')
raw = b''.join(uvarint(len(body)) + body for body in packets)
compressed = zlib.compress(raw, 9)
output.with_suffix(output.suffix + '.zlib').write_bytes(compressed)
digest = 14695981039346656037
for body in packets:
    for section in parse_chunk(body)['sections']:
        for cells in section:
            for cell in cells:
                for byte in struct.pack('<I', cell):
                    digest = ((digest ^ byte) * 1099511628211) & ((1 << 64) - 1)
metadata = {
    'source': 'BDS 1.6.0.15 Linux; tools/reference/capture_reference.py',
    'original_sha256': '69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad',
    'capture_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'captured_at': capture['captured_at'], 'requested_radius': capture['requested_chunk_radius'],
    'packet_count': len(packets), 'fixture_sha256': hashlib.sha256(compressed).hexdigest(),
    'inflated_bytes': len(raw), 'cells_fnv1a64': str(digest),
    'suffix_policy': 'Border count and block-entity NBT preserved verbatim; not semantically decoded.'}
output.with_suffix(output.suffix + '.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(json.dumps(metadata))
