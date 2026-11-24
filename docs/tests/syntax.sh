#!/bin/sh
# Syntax/type-check iwm sources on a non-illumos host (gcc), against the
# tree's own kernel headers. Not a build: no link, no illumos compiler flags.
# Usage: docs/tests/syntax.sh [file.c ...]   (default: all iwm .c files)
here=$(cd "$(dirname "$0")" && pwd)
uts=$here/../../usr/src/uts
inc=$(gcc -print-file-name=include)
cd "$uts" || exit 1
[ $# -eq 0 ] && set -- common/io/iwm/*.c
rc=0
for f in "$@"; do
	out=$(gcc -fsyntax-only -m64 -std=gnu99 -D_KERNEL -D_SYSCALL32 \
	    -D_DDI_STRICT -Dsun -D__sun -D_ASM_INLINES -DDEBUG -Wall \
	    -Wno-builtin-declaration-mismatch -Wno-format -Wno-attributes \
	    -Wno-unknown-pragmas -nostdinc -include stdarg.h -I "$inc" \
	    -I common -I intel -I common/io -I intel/ia32 -I . "$f" 2>&1 |
	    grep -E 'iwm/[^:]*:[0-9]+:[0-9]+: (error|warning)')
	if [ -n "$out" ]; then echo "$out"; rc=1; else echo "ok: $f"; fi
done
exit $rc
