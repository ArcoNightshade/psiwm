/*
 * Copyright (c) 2021 Christian Ehrhardt <ehrhardt@genua.de>
 * Copyright (c) 2016, 2021 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Theo Buehler <tb@openbsd.org>
 * Copyright (c) 2006 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copyright 2026 lex0de <lex0de@tuta.com>
 */

/*
 * Rate adaptation for iwm: MiRA (HT) and AMRR (legacy), ported from OpenBSD
 * net80211 (ieee80211_ra.h v1.2, ieee80211_amrr.h v1.6, ieee80211_node.h
 * rateset definitions).
 */

#ifndef _IF_IWM_RA_H
#define	_IF_IWM_RA_H

#include <sys/types.h>
#include <sys/net80211.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	IWM_HT_RATESET_SISO		0
#define	IWM_HT_RATESET_SISO_SGI		1
#define	IWM_HT_RATESET_MIMO2		2
#define	IWM_HT_RATESET_MIMO2_SGI	3
#define	IWM_HT_RATESET_MIMO3		4
#define	IWM_HT_RATESET_MIMO3_SGI	5
#define	IWM_HT_RATESET_MIMO4		6
#define	IWM_HT_RATESET_MIMO4_SGI	7
#define	IWM_HT_RATESET_SISO_40		8
#define	IWM_HT_RATESET_SISO_SGI40	9
#define	IWM_HT_RATESET_MIMO2_40		10
#define	IWM_HT_RATESET_MIMO2_SGI40	11
#define	IWM_HT_RATESET_MIMO3_40		12
#define	IWM_HT_RATESET_MIMO3_SGI40	13
#define	IWM_HT_RATESET_MIMO4_40		14
#define	IWM_HT_RATESET_MIMO4_SGI40	15
#define	IWM_HT_NUM_RATESETS		16
#define	IWM_HT_RATESET_MAX_NRATES	8
#define	IWM_HT_RATESET_NUM_MCS		32

struct iwm_ht_rateset {
	uint32_t nrates;
	uint32_t rates[IWM_HT_RATESET_MAX_NRATES];	/* 500 kbit/s units */
	uint32_t mcs_mask;
	int min_mcs;
	int max_mcs;
	int chan40;
	int sgi;
};

extern const struct iwm_ht_rateset iwm_std_ratesets_11n[];

/*
 * What MiRA needs to know about the peer.  OpenBSD reads these from its
 * ieee80211_node; the driver fills them from the illumos node and its own
 * channel configuration, and reads txmcs back after iwm_ra_choose_peer().
 */
struct iwm_ra_peer {
	int txmcs;		/* current Tx MCS (in/out) */
	int chan40;		/* peer and channel support 40 MHz */
	int sgi;		/* short guard interval usable */
	uint32_t valid_mcs;	/* peer Rx MCS & our Tx MCS, MCS 0-31 */
};

struct iwm_ra_goodput_stats {
	uint64_t measured;
	uint64_t average;
	uint64_t stddeviation;
	uint64_t loss;
	uint32_t nprobe_pkts;
	uint32_t nprobe_fail;
};

struct iwm_ra_node {
	uint32_t valid_probes;
	uint32_t valid_rates;
	uint32_t candidate_rates;
	uint32_t probed_rates;
	int probing;
#define	IWM_RA_NOT_PROBING	0x0
#define	IWM_RA_PROBING_DOWN	0x1
#define	IWM_RA_PROBING_UP	0x2
#define	IWM_RA_PROBING_INTER	0x4
	int best_mcs;
	struct iwm_ra_goodput_stats g[IWM_HT_RATESET_NUM_MCS];
};

void iwm_ra_node_init(struct iwm_ra_node *);
void iwm_ra_add_stats_ht(struct iwm_ra_node *, struct iwm_ra_peer *,
    int, uint32_t, uint32_t);
void iwm_ra_choose_peer(struct iwm_ra_node *, struct iwm_ra_peer *);
const struct iwm_ht_rateset *iwm_ra_get_ht_rateset(int, int, int);

struct iwm_amrr {
	uint_t amrr_min_success_threshold;
	uint_t amrr_max_success_threshold;
};

#define	IWM_AMRR_MIN_SUCCESS_THRESHOLD	1
#define	IWM_AMRR_MAX_SUCCESS_THRESHOLD	15

struct iwm_amrr_node {
	uint_t amn_success;
	uint_t amn_recovery;
	uint_t amn_success_threshold;
	uint_t amn_txcnt;
	uint_t amn_retrycnt;
};

void iwm_amrr_node_init(const struct iwm_amrr *, struct iwm_amrr_node *);
void iwm_amrr_choose(struct iwm_amrr *, struct ieee80211_node *,
    struct iwm_amrr_node *);

#ifdef __cplusplus
}
#endif

#endif /* _IF_IWM_RA_H */
