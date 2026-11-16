#!/usr/bin/env python3
"""Index symbols locally; emit reproducible, bounded direct/indirect call evidence."""
import argparse, hashlib, json, re, sqlite3, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=Path('reverse/symbols/generated'));a=p.parse_args()
digest=hashlib.sha256(a.binary.read_bytes()).hexdigest()
expected='69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad'
if digest!=expected: raise SystemExit('Unexpected binary hash; review target before analysis')
a.output.mkdir(parents=True,exist_ok=True)
db=a.output/'linux.sqlite';db.unlink(missing_ok=True)
con=sqlite3.connect(db);con.execute('CREATE TABLE symbols(address INTEGER,size INTEGER,kind TEXT,name TEXT)');con.execute('CREATE INDEX names ON symbols(name)')
raw=subprocess.check_output(['nm','-S','-C','--defined-only',str(a.binary)],text=True)
rows=[]
for line in raw.splitlines():
 m=re.match(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+(\S)\s+(.+)$',line)
 if m: rows.append((int(m[1],16),int(m[2],16),m[3],m[4]))
con.executemany('INSERT INTO symbols VALUES (?,?,?,?)',rows);con.commit()
targets=['ServerInstance::_update()','ServerInstance::startServerThread()', 'ServerInstance::startServerThread()::$_3::operator()() const','ServerLevel::tick()']
functions=[]
local=Path('reverse/evidence/local');local.mkdir(parents=True,exist_ok=True)
for i,name in enumerate(targets):
 matches=[r for r in rows if r[3]==name];assert len(matches)==1,name
 address,size,kind,_=matches[0]
 text=subprocess.check_output(['objdump','-d','-C',f'--start-address={address}',f'--stop-address={address+size}',str(a.binary)],text=True)
 (local/f'function_{i}.asm').write_text(text)
 calls=[]
 for line in text.splitlines():
  m=re.search(r'^\s*([0-9a-f]+):.*\bcall\s+(.+)$',line)
  if m: calls.append({'site':'0x'+m[1],'operand':m[2],'kind':'indirect' if m[2].startswith('*') else 'direct'})
 functions.append({'symbol':name,'address':hex(address),'size':size,'confidence':'VERIFIED_BINARY','calls':calls})
Path('reverse/callgraphs').mkdir(parents=True,exist_ok=True)
Path('reverse/callgraphs/initial.json').write_text(json.dumps({'sha256':digest,'tool':'GNU nm/objdump','scope':'outgoing call sites only; indirect targets unresolved; not a complete graph','functions':functions},indent=2)+'\n')
print(json.dumps({'sha256':digest,'indexed_symbols':len(rows),'database':str(db),'functions':len(functions)}))
