#!/bin/bash
# usage: run_original.sh [STDIN] - start the original from $BDS_RT (fresh world) without gdb.
source "$(dirname "$0")/env.sh"
pkill -f bedrock_server; sleep 1; cd "$BDS_RT"; rm -rf worlds
(LD_PRELOAD=$BDS_SHIM LD_LIBRARY_PATH=$BDS_SSL/lib:. ./bedrock_server < "${1:-/dev/null}" > log.txt 2>&1 &)
for i in $(seq 1 120); do grep -q "IPv4 supported" log.txt && break; sleep 1; done
