#!/bin/bash
# usage: dis.sh ADDR [SIZE_HEX] - compact disassembly of the original. Hides
# rbp spills/reloads and push/pop; use plain objdump when register flow matters.
source "$(dirname "$0")/env.sh"
a=$1; n=$2
if [ -z "$n" ]; then n=$(grep -m1 "^0*${a#0x} " "$BDS_SYMS" | awk '{print "0x"$2}'); fi
objdump -d --no-show-raw-insn -C --start-address=$a --stop-address=$((a+n)) "$BDS_BIN" | grep -vE "^$|Disassembly|file format" | sed 's/^ *//' | cut -c1-160 | grep -vE "^\S+:\s+mov +-?0x[0-9a-f]+\(%rbp\),%r[a-z0-9]+$|^\S+:\s+mov +%r[a-z0-9]+,-?0x[0-9a-f]+\(%rbp\)$|^\S+:\s+(push|pop) +%rbp|mov +%rsp,%rbp|^\S+:\s+(nop|data16|cs nopw)"
