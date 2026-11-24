/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 lex0de <lex0de@tuta.com>
 */

/*
 * Small OpenBSD-to-illumos shims so that code taken from OpenBSD's iwm(4)
 * keeps its original shape.  Only mechanical equivalents belong here;
 * anything with different semantics is rewritten in if_iwm.c instead.
 */

#ifndef _IF_IWMCOMPAT_H
#define	_IF_IWMCOMPAT_H

#include <sys/types.h>
#include <sys/byteorder.h>
#include <sys/sysmacros.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t	bus_addr_t;

#define	le16toh(x)	LE_16(x)
#define	htole16(x)	LE_16(x)
#define	le32toh(x)	LE_32(x)
#define	htole32(x)	LE_32(x)
#define	le64toh(x)	LE_64(x)
#define	htole64(x)	LE_64(x)
#define	le16_to_cpup(_a_)	(le16toh(*(const uint16_t *)(_a_)))
#define	le32_to_cpup(_a_)	(le32toh(*(const uint32_t *)(_a_)))

#ifndef nitems
#define	nitems(_a)	(sizeof (_a) / sizeof ((_a)[0]))
#endif

#ifndef isset
#define	setbit(a, i)	((a)[(i) / NBBY] |= 1 << ((i) % NBBY))
#define	clrbit(a, i)	((a)[(i) / NBBY] &= ~(1 << ((i) % NBBY)))
#define	isset(a, i)	((a)[(i) / NBBY] & (1 << ((i) % NBBY)))
#define	isclr(a, i)	(((a)[(i) / NBBY] & (1 << ((i) % NBBY))) == 0)
#endif

#define	DELAY(us)	drv_usecwait(us)

/* OpenBSD net80211 EDCA access categories have illumos WME's values. */
#define	EDCA_AC_BE	WME_AC_BE
#define	EDCA_AC_BK	WME_AC_BK
#define	EDCA_AC_VI	WME_AC_VI
#define	EDCA_AC_VO	WME_AC_VO
#define	EDCA_NUM_AC	WME_NUM_AC

/* net80211 field accessors (illumos keeps these in net80211_impl.h). */
#ifndef MS
#define	MS(_v, _f)	(((_v) & _f) >> _f##_S)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _IF_IWMCOMPAT_H */
