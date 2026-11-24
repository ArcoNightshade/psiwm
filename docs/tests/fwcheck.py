#!/usr/bin/env python3
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# Copyright 2026 lex0de <lex0de@tuta.com>
#
# Check a packaged iwm firmware image the way iwm_read_firmware() walks it:
# header magic, 4-byte aligned TLVs that stay inside the file, INIT and
# regular (RT) sections present, and the capabilities the driver relies on
# (DQA queues and UMAC scan; the port has no non-DQA Tx aggregation path).
# Usage: fwcheck.py image...
import struct, sys

MAGIC = 0x0a4c5749
SEC_RT, SEC_INIT, API, CAPA, FW_VERSION = 19, 20, 29, 30, 36
CAPA_UMAC_SCAN, CAPA_DQA = 2, 12

def check(path):
    d = open(path, 'rb').read()
    if len(d) < 88:
        return 'too short'
    zero, magic = struct.unpack_from('<II', d, 0)
    if zero != 0 or magic != MAGIC:
        return 'bad magic'
    off, sec, capa, ver = 88, {SEC_RT: 0, SEC_INIT: 0}, set(), None
    while off + 8 <= len(d):
        typ, ln = struct.unpack_from('<II', d, off)
        off += 8
        if off + ln > len(d):
            return 'TLV %d runs past end of file' % typ
        if typ in sec:
            sec[typ] += 1
        elif typ == CAPA and ln == 8:
            idx, bits = struct.unpack_from('<II', d, off)
            capa |= {idx * 32 + i for i in range(32) if bits >> i & 1}
        elif typ == FW_VERSION and ln >= 12:
            ver = struct.unpack_from('<III', d, off)
        off += (ln + 3) & ~3
    if off != len(d):
        return 'trailing bytes after last TLV'
    if not sec[SEC_RT] or not sec[SEC_INIT]:
        return 'missing INIT or RT sections'
    for c, name in ((CAPA_DQA, 'DQA'), (CAPA_UMAC_SCAN, 'UMAC scan')):
        if c not in capa:
            return 'firmware lacks %s capability' % name
    return 'ok (RT %d, INIT %d sections, version %s)' % (
        sec[SEC_RT], sec[SEC_INIT], ('%d.%08x.%d' % ver) if ver else '?')

rc = 0
for p in sys.argv[1:]:
    r = check(p)
    print('%s: %s' % ('ok' if r.startswith('ok') else 'FAIL', p.split('/')[-1] + ' ' + r))
    rc |= not r.startswith('ok')
sys.exit(rc)
