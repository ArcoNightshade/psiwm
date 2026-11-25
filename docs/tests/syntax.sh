#!/bin/sh
# Compile iwm sources on a non-illumos host (gcc) against the tree's own
# kernel headers with the illumos kernel warning flags (as in the real build,
# which uses -Werror).  Compiles at -O2 so unused-function warnings appear.
# Not a build: nothing is linked.
# Usage: docs/tests/syntax.sh [file.c ...]   (default: all iwm .c files)
here=$(cd "$(dirname "$0")" && pwd)
uts=$here/../../usr/src/uts
inc=$(gcc -print-file-name=include)
cd "$uts" || exit 1
[ $# -eq 0 ] && set -- common/io/iwm/*.c
rc=0
for f in "$@"; do
	out=$(gcc -c -o /dev/null -O2 -m64 -fno-pic -fno-pie -std=gnu99 \
	    -D_KERNEL -D_SYSCALL32 -D_SYSCALL32_IMPL -D_ELF64 -D_DDI_STRICT \
	    -Dsun -D__sun -D__SVR4 -D_ASM_INLINES -DDEBUG -ffreestanding \
	    -fno-builtin -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse \
	    -Wall -Wextra -Wno-missing-braces -Wno-sign-compare \
	    -Wno-unused-parameter -Wno-missing-field-initializers -Wno-switch \
	    -Wno-maybe-uninitialized -Wno-parentheses \
	    -Wno-builtin-declaration-mismatch -Wno-format -Wno-attributes \
	    -Wno-unknown-pragmas -nostdinc -I "$inc" \
	    -I common -I intel -I common/io -I intel/ia32 -I . "$f" 2>&1 |
	    grep -E '(iwm|net80211)/[^:]*:[0-9]+:[0-9]+: (error|warning)')
	if [ -n "$out" ]; then echo "$out"; rc=1; else echo "ok: $f"; fi
done
exit $rc
