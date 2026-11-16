#!/usr/bin/env python3
"""Correlate a user-supplied module CSV with a local Linux symbol index.

Outputs heuristic metadata locally, never source code or PDB match assertions.
"""
import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import sqlite3

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("csv", type=Path)
p.add_argument("--symbols", type=Path, default=Path("reverse/symbols/generated/linux.sqlite"))
p.add_argument("--out", type=Path, default=Path("reverse/evidence/local/source_correlations.json"))
a = p.parse_args()
required = {"module_index", "compiled_object", "candidate_primary_cpp", "method"}
with a.csv.open(encoding="utf-8-sig", newline="") as file:
    reader = csv.DictReader(file)
    if not required.issubset(reader.fieldnames or []):
        raise SystemExit("unrecognized source-map columns")
    rows = list(reader)
if len(rows) > 100_000 or len({r["module_index"] for r in rows}) != len(rows):
    raise SystemExit("excessive or duplicate module indices")
connection = sqlite3.connect(a.symbols.resolve().as_uri() + "?mode=ro", uri=True)
# Scan symbols once; avoid a full LIKE scan for every module in a large CSV.
eligible = [(row, PureWindowsPath(row["compiled_object"]).stem) for row in rows
            if row["method"] == "UNIQUE_BASENAME_MATCH"]
wanted = {stem.casefold() for _, stem in eligible if re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", stem)}
buckets = {stem: [] for stem in wanted}
for address, size, name in connection.execute("SELECT address,size,name FROM symbols WHERE kind IN ('T','t','W','w')"):
    prefix = name.split("(", 1)[0]
    for component in {part.casefold() for part in re.findall(r"([A-Za-z][A-Za-z0-9]*)::", prefix)} & wanted:
        if len(buckets[component]) < 8:
            buckets[component].append({"address": hex(address), "size": size, "symbol": name})
results = []
for row, stem in eligible:
    samples = buckets.get(stem.casefold(), [])
    if samples:
        results.append({
            "module_index": row["module_index"], "object_basename": stem,
            "candidate_primary_source": row["candidate_primary_cpp"],
            "confidence": "INFERRED", "method": "object basename versus demangled symbol prefix component",
            "samples": samples,
        })

report = {
    "input_sha256": hashlib.sha256(a.csv.read_bytes()).hexdigest(),
    "module_rows": len(rows), "methods": dict(collections.Counter(r["method"] for r in rows)),
    "exe_pdb_match": "NOT_INDEPENDENTLY_VERIFIED",
    "scope": "heuristic navigation hints; no recovered source, ownership, type layouts or code provenance assertion",
    "correlations": results,
}
a.out.parent.mkdir(parents=True, exist_ok=True)
a.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"module_rows": len(rows), "candidate_correlations": len(results), "out": str(a.out)}))
