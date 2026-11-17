#!/bin/bash
# Build the demangled symbol table used by dis.sh and vt.py, and a run directory.
source "$(dirname "$0")/env.sh"
nm -S -C --defined-only "$BDS_BIN" | sort > "$BDS_SYMS"
if [ ! -d "$BDS_RT" ]; then
    cp -r "$(dirname "$BDS_BIN")" "$BDS_RT"
    cat > "$BDS_RT/server.properties" <<'P'
server-name=Oracle
level-name=O
level-seed=123
level-type=FLAT
server-port=19220
server-portv6=19221
online-mode=false
gamemode=survival
difficulty=normal
view-distance=8
tick-distance=4
allow-cheats=true
P
fi
wc -l "$BDS_SYMS"
