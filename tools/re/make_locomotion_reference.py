#!/usr/bin/env python3
"""Export native locomotion observations without calculating expected outputs.

Example:
  python tools/re/make_locomotion_reference.py \
    --paths input/reference/navigation-v2 input/reference/navigation-v3 \
    --controls input/reference/controls-v3 --liquids input/reference/liquids-v3 \
    --riding input/reference/riding-v7 --walking input/reference/walking-ai-v5 \
    --output tests/reference
"""
import argparse
import hashlib
import json
from pathlib import Path

SHA = '69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad'


def read(root, name):
    metadata = json.loads((root / 'metadata.json').read_text())
    trace = (root / 'draws.jsonl').read_bytes()
    log = (root / 'gdb.log').read_text()
    if (metadata['binary_sha256'] != SHA
            or hashlib.sha256(trace).hexdigest() != metadata['trace_sha256']
            or json.loads(trace.splitlines()[-1])['event'] != 'complete'
            or 'Python Exception' in log or 'Error occurred in Python' in log):
        raise ValueError(f'incomplete/invalid native capture: {root}')
    raw = (root / name).read_bytes()
    provenance = {'oracle': name, 'oracle_sha256': hashlib.sha256(raw).hexdigest(),
                  'trace_sha256': metadata['trace_sha256'], 'commands': metadata['commands']}
    return json.loads(raw), provenance


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--paths', nargs='+', type=Path, required=True)
    for name in ('controls', 'liquids', 'riding', 'walking', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    args = p.parse_args()
    paths = {'binary_sha256': SHA, 'sources': [], 'scenes': []}
    for root in args.paths:
        data, source = read(root, 'paths.json')
        paths['sources'].append(source)
        paths['scenes'].extend(data['scenes'])
    paths['controls'], source = read(args.controls, 'controls.json')
    paths['sources'].append(source)
    liquids, source = read(args.liquids, 'liquids_effects.json')
    # This sequential scene did not enter the native lava branch. Retain
    # the liquid registry, but never label those calls as lava-physics evidence.
    liquids['cases'] = [c for c in liquids['cases'] if c['kind'] != 'lava']
    liquids.update(binary_sha256=SHA, sources=[source])
    riding, source = read(args.riding, 'riding.json')
    if not riding['wasd']:
        raise ValueError('native WASD component was not active')
    riding.update(binary_sha256=SHA, sources=[source])
    walking, source = read(args.walking, 'walking_ai.json')
    walking.update(binary_sha256=SHA, sources=[source])
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in (('navigation', paths), ('mob_environment', liquids),
                       ('riding', riding), ('walking_ai', walking)):
        (args.output / f'{name}_bds_1.6.0.15.json').write_text(
            json.dumps(data, separators=(',', ':')) + '\n')


if __name__ == '__main__':
    main()
