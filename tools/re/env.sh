# Source this: paths for the reverse-engineering helpers. Override before sourcing.
# BDS_BIN   the original binary (input/linux/bds/bedrock_server, gitignored)
# BDS_SYMS  symbol table made by make_syms.sh
# BDS_RT    a throw-away copy of input/linux/bds used as the run directory
# BDS_SSL   OpenSSL 1.0.2u install prefix (lib/ holds libssl.so.1.0.0)
# BDS_SHIM  ipv6_shim.so built from tools/reference/ipv6_shim.c
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
: "${BDS_BIN:=$REPO/input/linux/bds/bedrock_server}"
: "${BDS_WORK:=${TMPDIR:-/tmp}/bds-re-work}"
: "${BDS_SYMS:=$BDS_WORK/syms.txt}"
: "${BDS_RT:=$BDS_WORK/orig_rt}"
: "${BDS_SSL:=$REPO/input/reference-runtime/openssl-local}"
: "${BDS_SHIM:=$REPO/input/reference-runtime/ipv6_shim.so}"
export REPO BDS_BIN BDS_WORK BDS_SYMS BDS_RT BDS_SSL BDS_SHIM
mkdir -p "$BDS_WORK"
