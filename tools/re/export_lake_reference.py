#!/usr/bin/env python3
"""Export only a complete, unchanged-binary LakeFeature observation corpus."""
import hashlib
import json
from pathlib import Path
import sys

root, target = map(Path, sys.argv[1:])
metadata = json.loads((root / 'metadata.json').read_text())
trace = (root / 'draws.jsonl').read_bytes()
log = (root / 'gdb.log').read_text()
original_sha = '69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad'
if (metadata['binary_sha256'] != original_sha
        or hashlib.sha256(trace).hexdigest() != metadata['trace_sha256']
        or json.loads(trace.splitlines()[-1])['event'] != 'complete'
        or any(e in log for e in ('Traceback', 'Python Exception', 'Error occurred in Python'))):
    raise SystemExit('invalid/incomplete native LakeFeature capture')
raw = (root / 'lakes.json').read_bytes()
data = json.loads(raw)
cases = data['cases']
scenes = {'buried', 'surface', 'flooded', 'air_floor', 'wrong_liquid', 'upper_liquid',
          'mixed_solid', 'custom_upper', 'low_y', 'below_world', 'high_y', 'descending', 'dark_dirt'}
seeds = {0, 1, 7, 123, 0x12345678, 0x89abcdef}
if (data.get('source_context') != {'constructor_flags': [True, True], 'ticking_queue_tick': 1,
                                   'events': False, 'pending_ticks_run': False, 'input_extra_layer': 'air:0'}
        or len(cases) != len(scenes) * len(seeds)
        or {(c['scene'], c['seed']) for c in cases} != {(s, n) for s in scenes for n in seeds}
        or not any(c['result'] and c['liquid'] == 'water:0' for c in cases)
        or not any(c['result'] and c['liquid'] == 'lava:0' for c in cases)
        or not any(not c['result'] for c in cases)
        or not any(w[3] == 'grass:0' for c in cases for w in c['writes'])
        or not any(q[3] == 0 for c in cases for q in c['light'])
        or not any(q[3] > 0 for c in cases for q in c['light'])
        or any(not c['words'] or len(c['tail']) != 8 or len(c['shape']['inside']) != 4096
               or len(c['initial']) != len(c['final']) or (not c['result'] and c['writes']) for c in cases)):
    raise SystemExit('incomplete native lake branches/corpus')
data.update(binary_sha256=original_sha, provenance={
    'trace_sha256': metadata['trace_sha256'], 'oracle_sha256': hashlib.sha256(raw).hexdigest(),
    'script_sha256': hashlib.sha256(Path(__file__).with_name('gdb_lake_cases.py').read_bytes()).hexdigest(),
    'commands': metadata['commands'], 'generator_override': metadata['generator_override']})
target.write_text(json.dumps(data, separators=(',', ':')) + '\n')
print(len(cases), 'native lake cases,', sum(len(c['words']) for c in cases), 'Random words,',
      sum(len(c['writes']) for c in cases), 'placements,', sum(len(c['light']) for c in cases), 'brightness queries')
