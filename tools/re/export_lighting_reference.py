#!/usr/bin/env python3
"""Copy a completed original lighting oracle with validated trace provenance.

Usage: export_lighting_reference.py TRACE_DIRECTORY OUT.json
All expected pairs, combined brightness, RNG words and mutations are native
observations. The exporter does not calculate expectations using BDS-RE.
"""
import hashlib
import json
from pathlib import Path
import sys

root=Path(sys.argv[1])
raw=(root/'draws.jsonl').read_bytes()
metadata=json.loads((root/'metadata.json').read_text())
fixture=json.loads((root/'lighting.json').read_text())
if hashlib.sha256(raw).hexdigest()!=metadata['trace_sha256'] or json.loads(raw.splitlines()[-1])['event']!='complete':
    raise ValueError('incomplete or changed original trace')
if fixture['source_trace_sha256']!=metadata['trace_sha256'] or fixture['binary_sha256']!=metadata['binary_sha256']:
    raise ValueError('oracle provenance mismatch')
if len(fixture['queries'])!=2029 or len(fixture['block_ticks'])!=fixture.get('case_count',128) or len(fixture['clear_sky_time_cases'])!=247:
    raise ValueError('incomplete native lighting/tick oracle')
if fixture['generator_override']!='flat':
    raise ValueError('this fixture requires the explicit flat-generator input')
Path(sys.argv[2]).write_text(json.dumps(fixture,indent=1)+'\n')
print(len(fixture['queries']),'native light pairs/raw points;',len(fixture['block_ticks']),'tickBlocks calls')
