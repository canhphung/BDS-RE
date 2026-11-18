#!/bin/bash
# Rebuild the gitignored input/ tree from the two supplied BDS 1.6.0.15 archives.
#
#   BDS_LINUX_ZIP=/path/BDS-Linux.zip BDS_WINDOWS_ZIP=/path/BDS-windows.zip \
#       tools/re/rebuild_input.sh
#
# Creates (all under input/, ignored by Git):
#   input/linux/bds/            unpacked Linux distribution (bedrock_server + libCrypto.so + data)
#   input/windows/bds/          unpacked Windows distribution (bedrock_server.exe + .pdb + data)
#   input/reference-runtime/bds/            symlinks to input/linux/bds (tools/re/env.sh $BDS_RT source)
#   input/reference-runtime/openssl-local/  OpenSSL 1.0.2u built shared (libssl/libcrypto 1.0.0)
#   input/reference-runtime/ipv6_shim.so    built from tools/reference/ipv6_shim.c
#   input/windows/source_mapping/           PDB-derived module/source navigation metadata
#   reverse/symbols/generated/linux.sqlite  symbol index (reverse/scripts/index_elf.py)
#
# Verify only, without rebuilding:  tools/re/rebuild_input.sh --verify
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$REPO"

LINUX_SHA=69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad
LINUX_ZIP_SHA=aafdef8e5d711abb56814273108cc00febcd54cac1dca4d942a2c169dd158e07
WINDOWS_ZIP_SHA=182902f542756389fb7c849c7d1768680cf3c5d9824dc09d3976acdfa95eeb1e
LIBCRYPTO_SHA=a9fb4397dd236e34e583f9d5cff5a00d0bda3fbdfd1c12245072ba2f23b5d034
OPENSSL_TARBALL_SHA=ecd0c6ffb493dd06707d38b14bb4d8c2288bb7033735606569d8f90f89669d16
OPENSSL_TARBALL=openssl-1.0.2u.tar.gz
OPENSSL_URL=https://www.openssl.org/source/old/1.0.2/$OPENSSL_TARBALL

die() { echo "error: $*" >&2; exit 1; }
sha_of() { sha256sum "$1" | cut -d' ' -f1; }
expect() { # expect PATH SHA WHAT
    [ -e "$1" ] || die "missing $1"
    local got; got=$(sha_of "$1")
    [ "$got" = "$2" ] || die "$3: sha256 $got, expected $2"
}

verify() {
    expect input/linux/bds/bedrock_server "$LINUX_SHA" "original Linux server"
    expect input/linux/bds/libCrypto.so "$LIBCRYPTO_SHA" "shipped libCrypto.so"
    [ -x input/reference-runtime/bds/bedrock_server ] || die "runtime copy is not executable"
    [ -f input/reference-runtime/openssl-local/lib/libssl.so.1.0.0 ] || die "OpenSSL 1.0.2u not built"
    [ -f input/reference-runtime/ipv6_shim.so ] || die "ipv6_shim.so not built"
    [ -x input/windows/bds/bedrock_server.exe ] || true   # exec bit is meaningless on Linux
    [ -f input/windows/bds/bedrock_server.pdb ] || die "Windows PDB missing"
    [ -f reverse/symbols/generated/linux.sqlite ] || die "symbol index missing"
    echo "input/ verified: Linux server, libCrypto, Windows EXE+PDB, OpenSSL 1.0.2u, ipv6 shim, symbol index"
}

unpack() { # unpack ZIP DEST
    python3 - "$1" "$2" <<'PY'
import os, sys, zipfile
source, dest = sys.argv[1], sys.argv[2]
os.makedirs(dest, exist_ok=True)
with zipfile.ZipFile(source) as archive:
    if archive.testzip() is not None:
        raise SystemExit("CRC failure in " + source)
    archive.extractall(dest)
PY
}

build_shim() {
    gcc -shared -fPIC -O2 -o input/reference-runtime/ipv6_shim.so \
        tools/reference/ipv6_shim.c -ldl
}

build_openssl() {
    local prefix=$REPO/input/reference-runtime/openssl-local
    if [ -f "$prefix/lib/libssl.so.1.0.0" ]; then
        echo "OpenSSL 1.0.2u already built at $prefix"
        return
    fi
    local scratch=${TMPDIR:-/tmp}/bds-re-openssl
    mkdir -p "$scratch"
    local tarball=$scratch/$OPENSSL_TARBALL
    [ -f "$tarball" ] || curl -fsSL -o "$tarball" "$OPENSSL_URL"
    expect "$tarball" "$OPENSSL_TARBALL_SHA" "OpenSSL 1.0.2u source tarball"
    tar xzf "$tarball" -C "$scratch"
    ( cd "$scratch/openssl-1.0.2u" \
      && ./config shared no-ssl2 no-ssl3 --prefix="$prefix" \
      && make -j"$(nproc)" \
      && make install_sw )
}

build_runtime_dir() {
    # Older documents cite input/linux/bedrock_server; keep that path working.
    ln -sfn bds/bedrock_server input/linux/bedrock_server
    mkdir -p input/reference-runtime/bds
    for name in bedrock_server libCrypto.so behavior_packs resource_packs definitions \
                structures bedrock_server_how_to.html release-notes.txt; do
        ln -sfn "../../linux/bds/$name" "input/reference-runtime/bds/$name"
    done
}

build_symbol_index() {
    python3 reverse/scripts/index_elf.py input/linux/bds/bedrock_server
    bash tools/re/make_syms.sh > /dev/null
}

build_source_mapping() {
    python3 - "$PWD/input/windows/bds/bedrock_server.pdb" "$PWD/input/windows/source_mapping" <<'PY'
"""Derive module/source navigation metadata from the MSVC PDB file lists.

Uses llvm-pdbutil (Ubuntu package llvm-14) to read each compiland's file list,
then classifies every module as UNIQUE_BASENAME_MATCH / AMBIGUOUS_BASENAME_MATCH
/ NOT_MAPPED by comparing the compiled object's basename with the basenames of
the source files the same module references. Output is navigation metadata only:
no source content, no type layouts, no proof that a Windows module corresponds
to a given Linux translation unit.
"""
import collections, csv, json, re, shutil, subprocess, sys
from pathlib import Path

pdb, outdir = Path(sys.argv[1]), Path(sys.argv[2])
outdir.mkdir(parents=True, exist_ok=True)
pdbutil = shutil.which("llvm-pdbutil") or "/usr/lib/llvm-14/bin/llvm-pdbutil"
if not Path(pdbutil).exists():
    raise SystemExit("llvm-pdbutil not found (apt-get install llvm-14)")
dump = subprocess.run([pdbutil, "dump", "--modules", "--files", str(pdb)],
                      capture_output=True, text=True, check=True).stdout

modules, current = {}, None
for line in dump.splitlines():
    m = re.match(r"^Mod (\d{4}) \| `([^`]*)`", line)
    if m:
        current = int(m.group(1))
        modules.setdefault(current, {"obj": m.group(2), "files": []})
        continue
    f = re.match(r"^- \((?:SHA-256|MD5): ([0-9A-F]+)\) (.+)$", line)
    if f and current is not None:
        modules[current]["files"].append(f.group(2))

def stem(path): return Path(path.replace("\\", "/")).stem.casefold()
def is_cpp(path): return path.lower().endswith((".cpp", ".cxx", ".cc"))
def is_minecraft(obj): return "darwinwork" in obj.lower()

tree = sorted({f for m in modules.values() for f in m["files"]})
(outdir / "ORIGINAL_SOURCE_TREE.txt").write_text("\n".join(tree) + "\n")
global_stem = collections.defaultdict(set)
for f in tree:
    global_stem[stem(f)].add(f)

rows = []
for index, module in sorted(modules.items()):
    obj = module["obj"]
    own = [f for f in module["files"] if is_cpp(f)]
    if [f for f in own if stem(f) == stem(obj)]:
        method, primary = "UNIQUE_BASENAME_MATCH", next(f for f in own if stem(f) == stem(obj))
    elif len(own) == 1:
        method, primary = "UNIQUE_BASENAME_MATCH", own[0]
    elif len(own) > 1:
        method, primary = "AMBIGUOUS_BASENAME_MATCH", sorted(own)[0]
    else:
        pool = [f for f in global_stem.get(stem(obj), ()) if is_cpp(f)]
        if len(pool) == 1:
            method, primary = "UNIQUE_BASENAME_MATCH", pool[0]
        elif len(pool) > 1:
            method, primary = "AMBIGUOUS_BASENAME_MATCH", sorted(pool)[0]
        else:
            method, primary = "NOT_MAPPED", ""
    if not is_minecraft(obj) and method == "UNIQUE_BASENAME_MATCH":
        # A third-party/CRT object is not a Minecraft translation unit.
        method, primary = "NOT_MAPPED", ""
    rows.append((index, obj, primary, method))

with (outdir / "MODULE_TO_PRIMARY_SOURCE.csv").open("w", newline="", encoding="utf-8") as file:
    writer = csv.writer(file)
    writer.writerow(["module_index", "compiled_object", "candidate_primary_cpp", "method"])
    writer.writerows(rows)
counts = collections.Counter(row[3] for row in rows)
print(json.dumps({"modules": len(rows), "methods": counts, "source_files": len(tree)}))
PY
}

case "${1:-}" in
    --verify) verify; exit 0 ;;
    "") ;;
    *) die "unknown argument: $1" ;;
esac

[ -n "${BDS_LINUX_ZIP:-}" ] || die "set BDS_LINUX_ZIP to the supplied BDS-Linux.zip"
[ -n "${BDS_WINDOWS_ZIP:-}" ] || die "set BDS_WINDOWS_ZIP to the supplied BDS-windows.zip"
expect "$BDS_LINUX_ZIP" "$LINUX_ZIP_SHA" "BDS-Linux.zip"
expect "$BDS_WINDOWS_ZIP" "$WINDOWS_ZIP_SHA" "BDS-windows.zip"

echo "== unpacking Linux distribution"
unpack "$BDS_LINUX_ZIP" input/linux/bds
echo "== unpacking Windows distribution"
unpack "$BDS_WINDOWS_ZIP" input/windows/bds
expect input/linux/bds/bedrock_server "$LINUX_SHA" "original Linux server"
expect input/linux/bds/libCrypto.so "$LIBCRYPTO_SHA" "shipped libCrypto.so"
chmod +x input/linux/bds/bedrock_server

echo "== OpenSSL 1.0.2u (the shipped libCrypto.so links libssl/libcrypto 1.0)"

build_openssl
echo "== ipv6 shim (hosts without kernel IPv6)"
build_shim
echo "== reference runtime directory"
build_runtime_dir
echo "== symbol index and demangled table"
build_symbol_index
echo "== Windows PDB source navigation metadata"
build_source_mapping

verify
echo
echo "Reference runs:"
echo "  source tools/re/env.sh; bash tools/re/run_original.sh        # original, plain"
echo "  python3 tools/re/capture_level_random.py --runtime input/reference-runtime/bds \\"
echo "      --library-path input/reference-runtime/openssl-local/lib \\"
echo "      --preload input/reference-runtime/ipv6_shim.so --output <new-dir> --players A,B --ticks 200"
