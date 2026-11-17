#!/usr/bin/env python3
"""Validate a completed native capture and export its water oracle fixture."""
import hashlib
import json
from pathlib import Path
import sys

root, destination = map(Path, sys.argv[1:])
metadata = json.loads((root / 'metadata.json').read_text())
trace = (root / 'draws.jsonl').read_bytes()
if (metadata['binary_sha256'] != '69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad'
        or hashlib.sha256(trace).hexdigest() != metadata['trace_sha256']
        or json.loads(trace.splitlines()[-1])['event'] != 'complete'):
    raise SystemExit('invalid/incomplete native trace')
log = (root / 'gdb.log').read_text()
if 'Traceback' in log or 'Python Exception' in log:
    raise SystemExit('native oracle failed')
oracle = (root / 'water.json').read_bytes()
fixture = json.loads(oracle)
if (len(fixture['cases']) != 78 or fixture['tick_delay'] != 5
        or fixture['horizontal_faces'] != [2, 5, 3, 4]
        or [s['tick'] for s in fixture['queue']['steps']] != list(range(1, 121))
        or any('flow_half' not in c or 'motion_after' not in c for c in fixture['cases'])):
    raise SystemExit('incomplete water oracle')
fixture.update(binary_sha256=metadata['binary_sha256'], provenance={
    'trace_sha256': metadata['trace_sha256'], 'oracle_sha256': hashlib.sha256(oracle).hexdigest(),
    'script_sha256': hashlib.sha256(Path(__file__).with_name('gdb_water_cases.py').read_bytes()).hexdigest(),
    'generator': metadata['generator_override'], 'commands': metadata['commands'],
    'method': 'Native _getSpread then _tick(false), primary-layer snapshots; no queued ticks advanced.'})
destination.write_text(json.dumps(fixture, separators=(',', ':'))+'\n')
