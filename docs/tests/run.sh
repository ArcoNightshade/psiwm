#!/bin/sh
# Host-side checks for pure iwm code. Run from anywhere on a machine with gcc
# and python3; needs no illumos kernel. Usage: docs/tests/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../usr/src/uts/common/io/iwm
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/sys"
cat > "$tmp/sys/types.h" <<'H'
#ifndef T_TYPES
#define T_TYPES
#include <stdint.h>
#include <stddef.h>
typedef unsigned int uint_t;
typedef enum { B_FALSE, B_TRUE } boolean_t;
#endif
H
echo '#include <errno.h>' > "$tmp/sys/errno.h"

# 1. Firmware TLV parser must accept every packaged firmware image.
gcc -w -I"$tmp" -I"$src" -o "$tmp/fwtest" "$here/fwtest.c" "$src/if_iwmfw.c"
for fw in "$src"/fw-iw/iwm-*; do
	"$tmp/fwtest" "$fw" | head -1 | grep -q 'parse=0 ' ||
	    { echo "FAIL: firmware parse $fw"; exit 1; }
	echo "ok: firmware parse $(basename "$fw")"
done

# 2. Rate ladder and rate encoding (functions extracted from if_iwmhw.c).
python3 - "$src/if_iwmhw.c" "$tmp/extract.inc" <<'PY'
import sys
s = open(sys.argv[1]).read()
def func(name):
    i = s.index("\n" + name + "(")
    j = s.rfind("\nstatic", 0, i)
    k = s.index("\n}\n", i) + 3
    return s[j:k]
a = s.index("struct iwm_rs_step {")
b = s.index("#define\tIWM_RS_LADDER")
open(sys.argv[2], "w").write(func("iwm_tx_rate_encode") + s[a:b] +
    "#define IWM_RS_LADDER 16\n" + func("iwm_rs_ladder"))
PY
gcc -w -I"$tmp" -o "$tmp/rs" "$here/rs.c"
"$tmp/rs" > "$tmp/rs.out"
if ! cmp -s "$tmp/rs.out" "$here/rs.expected"; then
	echo "FAIL: rate ladder output changed"; diff "$here/rs.expected" "$tmp/rs.out"; exit 1
fi
echo "ok: rate ladder"
