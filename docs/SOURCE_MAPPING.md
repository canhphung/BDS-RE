# User-supplied source-path mapping

The user supplied a PDB source-path extractor, a module-to-primary-source CSV and a reconstructed source tree. These are navigation metadata, not recovered source contents. The original inputs and full generated correlations are retained under ignored input/windows/source_mapping/ and reverse/evidence/local/, preserving the project's earlier restriction against publishing raw PDB-derived databases.

As of 2026-11-18 the Windows EXE/PDB pair is available in `input/windows/bds/` (see [INPUT.md](INPUT.md)); its GUID+Age match is independently verified, so the metadata below is regenerated from the PDB itself rather than taken from the supplied extractor.

The regenerated CSV contains 2,500 module rows: 1,867 UNIQUE_BASENAME_MATCH, 0 AMBIGUOUS_BASENAME_MATCH and 633 NOT_MAPPED. These labels describe basename heuristics, not proof of the translation unit that implements every symbol. The earlier user-supplied CSV carried a different split (1,942 / 1 / 557) under an unknown classification rule; its files were not present here. The independent local correlator finds 1,430 candidate class-prefix/module correlations against the Linux symbol index, retaining up to eight samples per candidate. They remain INFERRED. Free functions, differently named classes and platform differences can prevent a match; absence of a correlation does not prove absence of a subsystem.

Input SHA256 values:

- MODULE_TO_PRIMARY_SOURCE.csv: `0c204d95e25a4f83da7654425481366bd4be83426a0004baf8e0af3f72bda805` (regenerated from the PDB)
- ORIGINAL_SOURCE_TREE.txt: `4806e1c92605647ab0d288a72afe5f7046212deecd91ce62fd6f59eb269f1a21` (regenerated from the PDB)
- pdb_extract_source_paths.py (previously supplied, not present here): 6e1546101247f4d172fa61835065ddc78481730abdb20331e7b513827f374852

The supplied extractor was read, not executed. Windows-side *type* layouts are still not classified as PDB_DERIVED: only public symbol records (S_PUB32) and compiland file lists have been read, and no Linux↔Windows function identity is claimed from matching names alone. Linux ELF evidence remains the basis for wire constants and inspected control flow.

To regenerate the metadata and local navigation hints (needs `llvm-pdbutil` from Ubuntu `llvm-14`):

```sh
python3 reverse/scripts/correlate_sources.py input/windows/source_mapping/MODULE_TO_PRIMARY_SOURCE.csv
```

The script opens the generated Linux SQLite database read-only, scans symbols once, writes to an ignored output path and does not execute the supplied extractor or reconstruct repository folders from unverified paths. Current RakNet, connection-request, server-handler, properties and certificate investigations can use the candidates to select the next binary functions without treating metadata as source code.
