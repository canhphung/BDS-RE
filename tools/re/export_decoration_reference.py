#!/usr/bin/env python3
"""Validate/export original decoration feature observations."""
import hashlib
import json
from pathlib import Path
import sys

root,target=map(Path,sys.argv[1:])
metadata=json.loads((root/'metadata.json').read_text())
trace=(root/'draws.jsonl').read_bytes()
log=(root/'gdb.log').read_text()
if (metadata['binary_sha256']!='69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad'
        or hashlib.sha256(trace).hexdigest()!=metadata['trace_sha256']
        or json.loads(trace.splitlines()[-1])['event']!='complete'
        or 'Traceback' in log or 'Python Exception' in log):
    raise SystemExit('invalid/incomplete native decoration capture')
raw=(root/'ore.json').read_bytes()
data=json.loads(raw)
if (len(data['cases'])!=64 or sum(len(c['writes']) for c in data['cases'])<100
        or len(data.get('batches',[]))!=16
        or not any(len(c['calls'])>120 for c in data['batches'])
        or not any(w[3]=='gold_ore:0' and w[1]>=32 for c in data['batches'] if c['biome']==37 for w in c['writes'])):
    raise SystemExit('incomplete or empty native ore corpus')
data.update(binary_sha256=metadata['binary_sha256'],provenance={
    'trace_sha256':metadata['trace_sha256'],'oracle_sha256':hashlib.sha256(raw).hexdigest(),
    'script_sha256':hashlib.sha256(Path(__file__).with_name('gdb_ore_cases.py').read_bytes()).hexdigest(),
    'commands':metadata['commands']})
target.write_text(json.dumps(data,separators=(',',':'))+'\n')
