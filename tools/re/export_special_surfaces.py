#!/usr/bin/env python3
"""Export complete native special-surface observations, without recalculating them."""
import hashlib
import json
from pathlib import Path
import sys

root, target = map(Path,sys.argv[1:])
metadata=json.loads((root/'metadata.json').read_text())
trace=(root/'draws.jsonl').read_bytes()
log=(root/'gdb.log').read_text()
if (metadata['binary_sha256']!='69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad'
        or hashlib.sha256(trace).hexdigest()!=metadata['trace_sha256']
        or json.loads(trace.splitlines()[-1])['event']!='complete'
        or 'Traceback' in log or 'Python Exception' in log):
    raise SystemExit('incomplete/failed native capture')
raw=(root/'special_surfaces.json').read_bytes()
data=json.loads(raw)
if len(data['cases'])!=576 or len(data['bands'])!=18 or not all(c['words'] for c in data['cases']):
    raise SystemExit('incomplete native surface corpus')
data.update(binary_sha256=metadata['binary_sha256'],provenance={
    'trace_sha256':metadata['trace_sha256'], 'oracle_sha256':hashlib.sha256(raw).hexdigest(),
    'script_sha256':hashlib.sha256(Path(__file__).with_name('gdb_special_surfaces.py').read_bytes()).hexdigest(),
    'method':'Native refreshBiome, buildSurfaceAt, complete column-Random draw streams and eight subsequent nextInt outputs.'})
target.write_text(json.dumps(data,separators=(',',':'))+'\n')
