#!/bin/bash
# usage: run_gdb.sh GDB_PY TRACE_OUT [STDIN]
# Starts the original from $BDS_RT under gdb with a Python breakpoint script
# (env TRACE_OUT names its log) and waits until it listens. STDIN may be a FIFO
# for console commands (create it and keep a writer: (sleep 99999 > fifo &)).
source "$(dirname "$0")/env.sh"
pkill -f bedrock_server; sleep 1; cd "$BDS_RT"; rm -rf worlds; rm -f "$2"
(TRACE_OUT=$2 gdb -q -batch -ex "set pagination off" -ex "handle SIGPIPE nostop noprint pass" \
  -ex "set environment LD_PRELOAD=$BDS_SHIM" -ex "set environment LD_LIBRARY_PATH=$BDS_SSL/lib:." \
  -ex "break main" -ex run -ex "source $1" -ex continue --args ./bedrock_server < "${3:-/dev/null}" > "$BDS_RT/gdb.txt" 2>&1 &)
for i in $(seq 1 200); do grep -q "IPv4 supported" "$BDS_RT/gdb.txt" && break; sleep 1; done
