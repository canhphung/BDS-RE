#!/usr/bin/env python3
"""Distill native settled motion/push calls and an explicit move/fall matrix.
Usage: make_mob_motion_reference.py NATURAL_RUN MATRIX_RUN MOTION_OUT RANDOM_OUT
Outputs are observations; no expected values are calculated with BDS-RE.
"""
import hashlib
import json
from pathlib import Path
import sys

SHA='69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad'
def load(root):
    root=Path(root)
    raw=(root/'draws.jsonl').read_bytes()
    metadata=json.loads((root/'metadata.json').read_text())
    if metadata['binary_sha256']!=SHA or hashlib.sha256(raw).hexdigest()!=metadata['trace_sha256'] or json.loads(raw.splitlines()[-1])['event']!='complete':
        raise ValueError('original trace provenance/completion mismatch')
    return root,metadata

def read(root,name):return json.loads((root/name).read_text())
def source(root,name,metadata):return {'trace_sha256':metadata['trace_sha256'],'oracle_sha256':hashlib.sha256((root/name).read_bytes()).hexdigest(),'commands':metadata['commands']}

def main():
    natural,nmeta=load(sys.argv[1]);matrix,mmeta=load(sys.argv[2])
    records=[json.loads(line) for line in (natural/'mob_motion.jsonl').read_text().splitlines()]
    # Scene commands run after player initialization. Retain the settled scene
    # after 25 ticks; earlier calls used geometry from before the wall existed.
    # Push function inputs are independent of wall geometry, so retain all.
    first=nmeta['completion']['joined_tick']+25
    records=[r for r in records if r.get('kind')=='push' or r['tick']>=first]
    geometry=[]
    ids={'air':0,'stone':1,'grass':2,'dirt':3,'bedrock':4}
    for command in nmeta['commands']:
        args=command.split()
        if args[0]=='fill':
            x0,y0,z0,x1,y1,z1=map(int,args[1:7])
            geometry += [[x,y,z,ids[args[7]]] for x in range(x0,x1+1) for y in range(y0,y1+1) for z in range(z0,z1+1)]
    direct=read(matrix,'mob_move_cases.json')
    if not records or len(direct['cases'])!=88:raise ValueError('incomplete native motion matrix')
    fixture={'binary_sha256':SHA,'generator':'tools/re/make_mob_motion_reference.py',
        'scope':'Land/air travel with captured control speed, collision/step/slide, and non-riding actor push. Does not validate navigation or runtime AI decisions.',
        'scenes':[{'name':'settled wall, natural movement and overlapping cows','source':source(natural,'mob_motion.jsonl',nmeta),'first_travel_tick':first,'initial_blocks':geometry,'cases':records},
                  {'name':'explicit step inputs and retained-motion fall','source':source(matrix,'mob_move_cases.json',mmeta),'initial_blocks':direct['geometry'],'cases':direct['cases']}]}
    Path(sys.argv[3]).write_text(json.dumps(fixture,separators=(',',':'))+'\n')
    random=read(matrix,'random_pos_cases.json')
    if len(random['cases'])!=96:raise ValueError('incomplete native RandomPos cases')
    random.update(binary_sha256=SHA,source=source(matrix,'random_pos_cases.json',mmeta),
                  scope='Non-restricted land animal with navigation, own Random explicitly seeded, default clear first-day lighting; no direction/swim/fly branches.')
    Path(sys.argv[4]).write_text(json.dumps(random,separators=(',',':'))+'\n')
    print('Native motion cases:',sum(len(s['cases']) for s in fixture['scenes']),'; RandomPos:',len(random['cases']))
if __name__=='__main__':main()
