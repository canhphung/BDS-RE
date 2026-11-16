# User-supplied source-path mapping

The user supplied a PDB source-path extractor, a module-to-primary-source CSV and a reconstructed source tree. These are navigation metadata, not recovered source contents. The original inputs and full generated correlations are retained under ignored input/windows/source_mapping/ and reverse/evidence/local/, preserving the project's earlier restriction against publishing raw PDB-derived databases.

The CSV contains 2,500 module rows: 1,942 marked UNIQUE_BASENAME_MATCH, 1 AMBIGUOUS_BASENAME_MATCH and 557 NOT_MAPPED. These labels describe basename heuristics, not proof of the translation unit that implements every symbol. The independent local correlator finds 1,429 candidate class-prefix/module correlations against the Linux symbol index, retaining up to eight samples per candidate. They remain INFERRED. Free functions, differently named classes and platform differences can prevent a match; absence of a correlation does not prove absence of a subsystem.

Input SHA256 values:

- MODULE_TO_PRIMARY_SOURCE.csv: b545244a4bf5ea44548d5acf764328dc6db34a363ef6bf80d0e3c9e8c1a7d2ab
- ORIGINAL_SOURCE_TREE.txt: 1ace3941c3143c50f4c49af3d4ad75f0dba3f7701b42aff80b7a03623bbe0545
- pdb_extract_source_paths.py: 6e1546101247f4d172fa61835065ddc78481730abdb20331e7b513827f374852

The supplied extractor was read, not executed. It includes a GUID/Age comparison, but the Windows EXE/PDB pair is not available for independent verification in this environment. No class layouts or signatures are classified as PDB_DERIVED on the strength of these CSV/tree files. Linux ELF evidence remains the basis for wire constants and inspected control flow.

To regenerate local navigation hints after placing the supplied CSV under input/:

```sh
python3 reverse/scripts/correlate_sources.py input/windows/source_mapping/MODULE_TO_PRIMARY_SOURCE.csv
```

The script opens the generated Linux SQLite database read-only, scans symbols once, writes to an ignored output path and does not execute the supplied extractor or reconstruct repository folders from unverified paths. Current RakNet, connection-request, server-handler, properties and certificate investigations can use the candidates to select the next binary functions without treating metadata as source code.
