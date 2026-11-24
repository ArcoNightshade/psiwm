#!/bin/sh
# Host-side checks for iwm. Run from anywhere on a machine with gcc and
# python3; needs no illumos kernel. Usage: docs/tests/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../usr/src/uts/common/io/iwm
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. Packaged firmware images walk cleanly and have the capabilities the
#    driver needs (DQA, UMAC scan).
python3 "$here/fwcheck.py" "$src"/fw-iw/iwm-*

# 2. Rate control (MiRA and AMRR), compiled from the driver sources against
#    minimal stub headers.
mkdir -p "$tmp/sys"
cat > "$tmp/sys/types.h" <<'H'
#ifndef T_TYPES
#define T_TYPES
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef unsigned int uint_t;
#endif
H
echo '#include <string.h>' > "$tmp/sys/systm.h"
cat > "$tmp/sys/cmn_err.h" <<'H'
#include <stdio.h>
#include <stdlib.h>
#define panic(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)
H
cat > "$tmp/sys/net80211.h" <<'H'
#ifndef T_NET80211
#define T_NET80211
#include <sys/types.h>
#define IEEE80211_RATE_VAL 0x7f
struct ieee80211_rateset { uint8_t ir_nrates; uint8_t ir_rates[15]; };
struct ieee80211_node { int32_t in_txrate; struct ieee80211_rateset in_rates; };
#endif
H
gcc -Wall -Werror -I"$tmp" -I"$src" -o "$tmp/ra" "$here/ra.c" \
    "$src/if_iwm_ra.c" "$src/if_iwm_amrr.c"
"$tmp/ra"

# 3. Kernel sources type-check against the tree's headers.
"$here/syntax.sh"

# 4. Prototype header is current.
python3 "$here/genproto.py" "$src/if_iwm.c" "$tmp/proto.h" >/dev/null
cmp -s "$tmp/proto.h" "$src/if_iwm_proto.h" ||
    { echo "FAIL: if_iwm_proto.h is stale; run genproto.py"; exit 1; }
echo "ok: if_iwm_proto.h current"
