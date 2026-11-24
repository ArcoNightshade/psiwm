/* BEGIN CSTYLED */
/*	$OpenBSD: ieee80211_ra.c,v 1.5 2022/03/19 10:28:44 stsp Exp $	*/

/*
 * Copyright (c) 2021 Christian Ehrhardt <ehrhardt@genua.de>
 * Copyright (c) 2016, 2021 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Theo Buehler <tb@openbsd.org>
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
 *
 * MiRA 802.11n rate adaptation, from OpenBSD sys/net80211/ieee80211_ra.c
 * (v1.5).  illumos net80211 has no shared rate adaptation, so iwm carries
 * this copy.  The algorithm and its fixed-point arithmetic are unchanged;
 * OpenBSD's ieee80211_node is replaced by struct iwm_ra_peer, which the
 * driver fills from the illumos node.  Callers serialise with sc_mtx, which
 * replaces splnet().  The rateset table comes from OpenBSD ieee80211.c,
 * with three typos fixed (MCS 10 HT40: 192 -> 162; MCS 28 HT40: 324 -> 648;
 * MCS 28 HT40 SGI: 520 -> 720, in 500 kbit/s units).
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include "if_iwm_ra.h"

const struct iwm_ht_rateset iwm_std_ratesets_11n[] = {
	/* MCS 0-7, 20MHz channel, no SGI */
	{ 8, { 13, 26, 39, 52, 78, 104, 117, 130 },
	    0x000000ff, 0, 7, 0, 0},

	/* MCS 0-7, 20MHz channel, SGI */
	{ 8, { 14, 29, 43, 58, 87, 116, 130, 144 },
	    0x000000ff, 0, 7, 0, 1 },

	/* MCS 8-15, 20MHz channel, no SGI */
	{ 8, { 26, 52, 78, 104, 156, 208, 234, 260 },
	    0x0000ff00, 8, 15, 0, 0 },

	/* MCS 8-15, 20MHz channel, SGI */
	{ 8, { 29, 58, 87, 116, 173, 231, 261, 289 },
	    0x0000ff00, 8, 15, 0, 1 },

	/* MCS 16-23, 20MHz channel, no SGI */
	{ 8, { 39, 78, 117, 156, 234, 312, 351, 390 },
	    0x00ff0000, 16, 23, 0, 0 },

	/* MCS 16-23, 20MHz channel, SGI */
	{ 8, { 43, 87, 130, 173, 260, 347, 390, 433 },
	    0x00ff0000, 16, 23, 0, 1 },

	/* MCS 24-31, 20MHz channel, no SGI */
	{ 8, { 52, 104, 156, 208, 312, 416, 468, 520 },
	    0xff000000, 24, 31, 0, 0 },

	/* MCS 24-31, 20MHz channel, SGI */
	{ 8, { 58, 116, 173, 231, 347, 462, 520, 578 },
	    0xff000000, 24, 31, 0, 1 },

	/* MCS 0-7, 40MHz channel, no SGI */
	{ 8, { 27, 54, 81, 108, 162, 216, 243, 270 },
	    0x000000ff, 0, 7, 1, 0 },

	/* MCS 0-7, 40MHz channel, SGI */
	{ 8, { 30, 60, 90, 120, 180, 240, 270, 300 },
	    0x000000ff, 0, 7, 1, 1 },

	/* MCS 8-15, 40MHz channel, no SGI */
	{ 8, { 54, 108, 162, 216, 324, 432, 486, 540 },
	    0x0000ff00, 8, 15, 1, 0 },

	/* MCS 8-15, 40MHz channel, SGI */
	{ 8, { 60, 120, 180, 240, 360, 480, 540, 600 },
	    0x0000ff00, 8, 15, 1, 1 },

	/* MCS 16-23, 40MHz channel, no SGI */
	{ 8, { 81, 162, 243, 324, 486, 648, 729, 810 },
	    0x00ff0000, 16, 23, 1, 0 },

	/* MCS 16-23, 40MHz channel, SGI */
	{ 8, { 90, 180, 270, 360, 540, 720, 810, 900 },
	    0x00ff0000, 16, 23, 1, 1 },

	/* MCS 24-31, 40MHz channel, no SGI */
	{ 8, { 108, 216, 324, 432, 648, 864, 972, 1080 },
	    0xff000000, 24, 31, 1, 0 },

	/* MCS 24-31, 40MHz channel, SGI */
	{ 8, { 120, 240, 360, 480, 720, 960, 1080, 1200 },
	    0xff000000, 24, 31, 1, 1 },
};

static int	iwm_ra_next_intra_rate(struct iwm_ra_node *,
	    struct iwm_ra_peer *);
static const struct iwm_ht_rateset *  iwm_ra_next_rateset(
		    struct iwm_ra_node *, struct iwm_ra_peer *);
static int	iwm_ra_best_mcs_in_rateset(struct iwm_ra_node *,
	    const struct iwm_ht_rateset *);
static void	iwm_ra_probe_next_rateset(struct iwm_ra_node *,
	    struct iwm_ra_peer *, const struct iwm_ht_rateset *);
static int	iwm_ra_next_mcs(struct iwm_ra_node *,
	    struct iwm_ra_peer *);
static void	iwm_ra_probe_done(struct iwm_ra_node *);
static int	iwm_ra_intra_mode_ra_finished(
	    struct iwm_ra_node *, struct iwm_ra_peer *);
static void	iwm_ra_trigger_next_rateset(struct iwm_ra_node *,
	    struct iwm_ra_peer *);
static int	iwm_ra_inter_mode_ra_finished(
	    struct iwm_ra_node *, struct iwm_ra_peer *);
static int	iwm_ra_best_rate(struct iwm_ra_node *,
	    struct iwm_ra_peer *);
static void	iwm_ra_probe_next_rate(struct iwm_ra_node *,
	    struct iwm_ra_peer *);
static int	iwm_ra_probe_valid(struct iwm_ra_goodput_stats *);

/* We use fixed point arithmetic with 64 bit integers. */
#define RA_FP_SHIFT	21
#define RA_FP_INT(x)	(x ## ULL << RA_FP_SHIFT) /* the integer x */
#define RA_FP_1	RA_FP_INT(1)

/* Multiply two fixed point numbers. */
#define RA_FP_MUL(a, b) \
	(((a) * (b)) >> RA_FP_SHIFT)

/* Divide two fixed point numbers. */
#define RA_FP_DIV(a, b) \
	(b == 0 ? (uint64_t)-1 : (((a) << RA_FP_SHIFT) / (b)))

#define DPRINTF(x)	do { ; } while (0)
#define DPRINTFN(n, x)	do { ; } while (0)


const struct iwm_ht_rateset *
iwm_ra_get_ht_rateset(int mcs, int chan40, int sgi)
{
	const struct iwm_ht_rateset *rs;
	int i;

	for (i = 0; i < IWM_HT_NUM_RATESETS; i++) {
		rs = &iwm_std_ratesets_11n[i];
		if (chan40 == rs->chan40 && sgi == rs->sgi &&
		    mcs >= rs->min_mcs && mcs <= rs->max_mcs)
			return rs;
	}

	/*
	 * OpenBSD panics here.  The MCS comes from driver state, so fall
	 * back to the basic SISO set rather than take the system down.
	 */
	return &iwm_std_ratesets_11n[IWM_HT_RATESET_SISO];
}

/*
 * Update goodput statistics.
 */

static uint64_t
iwm_ra_get_txrate(int mcs, int chan40, int sgi)
{
	const struct iwm_ht_rateset *rs;
	uint64_t txrate;

	rs = iwm_ra_get_ht_rateset(mcs, chan40, sgi);
	txrate = rs->rates[mcs - rs->min_mcs];
	txrate <<= RA_FP_SHIFT; /* convert to fixed-point */
	txrate *= 500; /* convert to kbit/s */
	txrate /= 1000; /* convert to mbit/s */

	return txrate;
}

/*
 * Rate selection.
 */

/* A rate's goodput has to be at least this much larger to be "better". */
#define IWM_RA_RATE_THRESHOLD	(RA_FP_1 / 64) /* ~ 0.015 */

static int
iwm_ra_next_lower_intra_rate(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	const struct iwm_ht_rateset *rs;
	int i, next;

	rs = iwm_ra_get_ht_rateset(ni->txmcs,
	    ni->chan40, ni->sgi);
	if (ni->txmcs == rs->min_mcs)
		return rs->min_mcs;

	next = ni->txmcs;
	for (i = rs->nrates - 1; i >= 0; i--) {
		if ((rn->valid_rates & (1 << (i + rs->min_mcs))) == 0)
			continue;
		if (i + rs->min_mcs < ni->txmcs) {
			next = i + rs->min_mcs;
			break;
		}
	}

	return next;
}

static int
iwm_ra_next_intra_rate(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	const struct iwm_ht_rateset *rs;
	int i, next;

	rs = iwm_ra_get_ht_rateset(ni->txmcs,
	    ni->chan40, ni->sgi);
	if (ni->txmcs == rs->max_mcs)
		return rs->max_mcs;

	next = ni->txmcs;
	for (i = 0; i < rs->nrates; i++) {
		if ((rn->valid_rates & (1 << (i + rs->min_mcs))) == 0)
			continue;
		if (i + rs->min_mcs > ni->txmcs) {
			next = i + rs->min_mcs;
			break;
		}
	}

	return next;
}

static const struct iwm_ht_rateset *
iwm_ra_next_rateset(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	const struct iwm_ht_rateset *rs, *rsnext;
	int next;
	int chan40 = ni->chan40;
	int sgi = ni->sgi;
	int mcs = ni->txmcs;

	rs = iwm_ra_get_ht_rateset(mcs, chan40, sgi);
	if (rn->probing & IWM_RA_PROBING_UP) {
		if (rs->max_mcs == 7) {	/* MCS 0-7 */
			if (chan40)
				next = sgi ? IWM_HT_RATESET_MIMO2_SGI40 :
				    IWM_HT_RATESET_MIMO2_40;
			else
				next = sgi ? IWM_HT_RATESET_MIMO2_SGI :
				    IWM_HT_RATESET_MIMO2;
		} else if (rs->max_mcs == 15) {	/* MCS 8-15 */
			if (chan40)
				next = sgi ? IWM_HT_RATESET_MIMO3_SGI40 :
				    IWM_HT_RATESET_MIMO3_40;
			else
				next = sgi ? IWM_HT_RATESET_MIMO3_SGI :
				    IWM_HT_RATESET_MIMO3;
		} else if (rs->max_mcs == 23) {	/* MCS 16-23 */
			if (chan40)
				next = sgi ? IWM_HT_RATESET_MIMO4_SGI40 :
				    IWM_HT_RATESET_MIMO4_40;
			else
				next = sgi ? IWM_HT_RATESET_MIMO4_SGI :
				    IWM_HT_RATESET_MIMO4;
		} else				/* MCS 24-31 */
			return NULL;
	} else if (rn->probing & IWM_RA_PROBING_DOWN) {
		if (rs->min_mcs == 24) {	/* MCS 24-31 */
			if (chan40)
				next = sgi ? IWM_HT_RATESET_MIMO3_SGI40 :
				    IWM_HT_RATESET_MIMO3_40;
			else
				next = sgi ? IWM_HT_RATESET_MIMO3_SGI :
				    IWM_HT_RATESET_MIMO3;
		} else if (rs->min_mcs == 16) {	/* MCS 16-23 */
			if (chan40)
				next = sgi ? IWM_HT_RATESET_MIMO2_SGI40 :
				    IWM_HT_RATESET_MIMO2_40;
			else
				next = sgi ? IWM_HT_RATESET_MIMO2_SGI :
				    IWM_HT_RATESET_MIMO2;
		} else if (rs->min_mcs == 8) {	/* MCS 8-15 */
			if (chan40)
				next = sgi ? IWM_HT_RATESET_SISO_SGI40 :
				    IWM_HT_RATESET_SISO_40;
			else
				next = sgi ? IWM_HT_RATESET_SISO_SGI :
				    IWM_HT_RATESET_SISO;
		} else				/* MCS 0-7 */
			return NULL;
	} else
		panic("%s: invalid probing mode %d", __func__, rn->probing);

	rsnext = &iwm_std_ratesets_11n[next];
	if ((rsnext->mcs_mask & rn->valid_rates) == 0)
		return NULL;

	return rsnext;
}

static int
iwm_ra_best_mcs_in_rateset(struct iwm_ra_node *rn,
    const struct iwm_ht_rateset *rs)
{
	uint64_t gmax = 0;
	int i, best_mcs = rs->min_mcs;

	for (i = 0; i < rs->nrates; i++) {
		int mcs = rs->min_mcs + i;
		struct iwm_ra_goodput_stats *g = &rn->g[mcs];
		if (((1 << mcs) & rn->valid_rates) == 0)
			continue;
		if (g->measured > gmax + IWM_RA_RATE_THRESHOLD) {
			gmax = g->measured;
			best_mcs = mcs;
		}
	}

	return best_mcs;
}

static void
iwm_ra_probe_next_rateset(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni, const struct iwm_ht_rateset *rsnext)
{
	const struct iwm_ht_rateset *rs;
	struct iwm_ra_goodput_stats *g;
	int best_mcs, i;

	/* Find most recently measured best MCS from the current rateset. */
	rs = iwm_ra_get_ht_rateset(ni->txmcs,
	    ni->chan40, ni->sgi);
	best_mcs = iwm_ra_best_mcs_in_rateset(rn, rs);

	/* Switch to the next rateset. */
	ni->txmcs = rsnext->min_mcs;
	if ((rn->valid_rates & (1 << rsnext->min_mcs)) == 0)
		ni->txmcs = iwm_ra_next_intra_rate(rn, ni);

	/* Select the lowest rate from the next rateset with loss-free
	 * goodput close to the current best measurement. */
	g = &rn->g[best_mcs];
	for (i = 0; i < rsnext->nrates; i++) {
		int mcs = rsnext->min_mcs + i;
		uint64_t txrate = rsnext->rates[i];

		if ((rn->valid_rates & (1 << mcs)) == 0)
			continue;

		txrate = txrate * 500; /* convert to kbit/s */
		txrate <<= RA_FP_SHIFT; /* convert to fixed-point */
		txrate /= 1000; /* convert to mbit/s */

		if (txrate > g->measured + IWM_RA_RATE_THRESHOLD) {
			ni->txmcs = mcs;
			break;
		}
	}
	/* If all rates are lower the maximum rate is the closest match. */
	if (i == rsnext->nrates)
		ni->txmcs = rsnext->max_mcs;

	/* Add rates from the next rateset as candidates. */
	rn->candidate_rates |= (1 << ni->txmcs);
	if (rn->probing & IWM_RA_PROBING_UP) {
		rn->candidate_rates |=
		  (1 << iwm_ra_next_intra_rate(rn, ni));
	} else if (rn->probing & IWM_RA_PROBING_DOWN) {
		rn->candidate_rates |=
		    (1 << iwm_ra_next_lower_intra_rate(rn, ni));
	} else
		panic("%s: invalid probing mode %d", __func__, rn->probing);
}

static int
iwm_ra_next_mcs(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	int next;

	if (rn->probing & IWM_RA_PROBING_DOWN)
		next = iwm_ra_next_lower_intra_rate(rn, ni);
	else if (rn->probing & IWM_RA_PROBING_UP)
		next = iwm_ra_next_intra_rate(rn, ni);
	else
		panic("%s: invalid probing mode %d", __func__, rn->probing);

	return next;
}

static void
iwm_ra_probe_clear(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	struct iwm_ra_goodput_stats *g = &rn->g[ni->txmcs];

	g->nprobe_pkts = 0;
	g->nprobe_fail = 0;
}

static void
iwm_ra_probe_done(struct iwm_ra_node *rn)
{
	rn->probing = IWM_RA_NOT_PROBING;
	rn->probed_rates = 0;
	rn->valid_probes = 0;
	rn->candidate_rates = 0;
}

static int
iwm_ra_intra_mode_ra_finished(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	const struct iwm_ht_rateset *rs;
	struct iwm_ra_goodput_stats *g = &rn->g[ni->txmcs];
	int next_mcs, best_mcs;
	uint64_t next_rate;
	int chan40 = ni->chan40;
	int sgi = ni->sgi;

	rn->probed_rates = (rn->probed_rates | (1 << ni->txmcs));

	/* Check if the min/max MCS in this rateset has been probed. */
	rs = iwm_ra_get_ht_rateset(ni->txmcs, chan40, sgi);
	if (rn->probing & IWM_RA_PROBING_DOWN) {
		if (ni->txmcs == rs->min_mcs ||
		    rn->probed_rates & (1 << rs->min_mcs)) {
			iwm_ra_trigger_next_rateset(rn, ni);
			return 1;
		}
	} else if (rn->probing & IWM_RA_PROBING_UP) {
		if (ni->txmcs == rs->max_mcs ||
		    rn->probed_rates & (1 << rs->max_mcs)) {
			iwm_ra_trigger_next_rateset(rn, ni);
			return 1;
		}
	}

	/*
	 * Check if the measured goodput is loss-free and better than the
	 * loss-free goodput of the candidate rate.
	 */
	next_mcs = iwm_ra_next_mcs(rn, ni);
	if (next_mcs == ni->txmcs) {
		iwm_ra_trigger_next_rateset(rn, ni);
		return 1;
	}
	next_rate = iwm_ra_get_txrate(next_mcs, chan40, sgi);
	if (g->loss == 0 &&
	    g->measured >= next_rate + IWM_RA_RATE_THRESHOLD) {
		iwm_ra_trigger_next_rateset(rn, ni);
		return 1;
	}

	/* Check if we had a better measurement at a previously probed MCS. */
	best_mcs = iwm_ra_best_mcs_in_rateset(rn, rs);
	if (best_mcs != ni->txmcs && (rn->probed_rates & (1 << best_mcs))) {
		if ((rn->probing & IWM_RA_PROBING_UP) &&
		    best_mcs < ni->txmcs) {
			iwm_ra_trigger_next_rateset(rn, ni);
			return 1;
		}
		if ((rn->probing & IWM_RA_PROBING_DOWN) &&
		    best_mcs > ni->txmcs) {
			iwm_ra_trigger_next_rateset(rn, ni);
			return 1;
		}
	}

	/* Check if all rates in the set of candidate rates have been probed. */
	if ((rn->candidate_rates & rn->probed_rates) == rn->candidate_rates) {
		/* Remain in the current rateset until above checks trigger. */
		rn->probing &= ~IWM_RA_PROBING_INTER;
		return 1;
	}

	return 0;
}

static void
iwm_ra_trigger_next_rateset(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	const struct iwm_ht_rateset *rsnext;

	rsnext = iwm_ra_next_rateset(rn, ni);
	if (rsnext) {
		iwm_ra_probe_next_rateset(rn, ni, rsnext);
		rn->probing |= IWM_RA_PROBING_INTER;
	} else
		rn->probing &= ~IWM_RA_PROBING_INTER;
}

static int
iwm_ra_inter_mode_ra_finished(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	return ((rn->probing & IWM_RA_PROBING_INTER) == 0);
}

static int
iwm_ra_best_rate(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	int i, best = rn->best_mcs;
	uint64_t gmax = rn->g[rn->best_mcs].measured;

	for (i = 0; i < IWM_HT_RATESET_NUM_MCS; i++) {
		struct iwm_ra_goodput_stats *g = &rn->g[i];
		if (((1 << i) & rn->valid_rates) == 0)
			continue;
		if (g->measured > gmax + IWM_RA_RATE_THRESHOLD) {
			gmax = g->measured;
			best = i;
		}
	}

	return best;
}

static void
iwm_ra_probe_next_rate(struct iwm_ra_node *rn,
    struct iwm_ra_peer *ni)
{
	/* Select the next rate to probe. */
	rn->probed_rates |= (1 << ni->txmcs);
	ni->txmcs = iwm_ra_next_mcs(rn, ni);
}

static int
iwm_ra_probe_valid(struct iwm_ra_goodput_stats *g)
{
	/* 128 packets make up a valid probe in any case. */
	if (g->nprobe_pkts >= 128)
		return 1;

	/* 8 packets with > 75% loss make a valid probe, too. */
	if (g->nprobe_pkts >= 8 &&
	    g->nprobe_pkts - g->nprobe_fail < g->nprobe_pkts / 4)
		return 1;

	return 0;
}

void
iwm_ra_add_stats_ht(struct iwm_ra_node *rn, struct iwm_ra_peer *ni,
    int mcs, uint32_t total, uint32_t fail)
{
	static const uint64_t alpha = RA_FP_1 / 8; /* 1/8 = 0.125 */
	static const uint64_t beta =  RA_FP_1 / 4; /* 1/4 = 0.25 */
	struct iwm_ra_goodput_stats *g;
	uint64_t sfer, rate, delta;

	/*
	 * Ignore invalid values. These values may come from hardware
	 * so asserting valid values via panic is not appropriate.
	 */
	if (mcs < 0 || mcs >= IWM_HT_RATESET_NUM_MCS)
		return;
	if (total == 0)
		return;


	g = &rn->g[mcs];
	g->nprobe_pkts += total;
	g->nprobe_fail += fail;

	if (!iwm_ra_probe_valid(g)) {
		return;
	}
	rn->valid_probes |= 1U << mcs;

	if (g->nprobe_fail > g->nprobe_pkts) {
		DPRINTF(("%s fail %u > pkts %u\n",
		    "peer",
		    g->nprobe_fail, g->nprobe_pkts));
		g->nprobe_fail = g->nprobe_pkts;
	}

	sfer = g->nprobe_fail << RA_FP_SHIFT;
	sfer /= g->nprobe_pkts;
	g->nprobe_fail = 0;
	g->nprobe_pkts = 0;

	rate = iwm_ra_get_txrate(mcs,
	    ni->chan40,
	    ni->sgi);

	g->loss = sfer * 100;
	g->measured = RA_FP_MUL(RA_FP_1 - sfer, rate);
	g->average = RA_FP_MUL(RA_FP_1 - alpha, g->average);
	g->average += RA_FP_MUL(alpha, g->measured);

	g->stddeviation = RA_FP_MUL(RA_FP_1 - beta, g->stddeviation);
	if (g->average > g->measured)
		delta = g->average - g->measured;
	else
		delta = g->measured - g->average;
	g->stddeviation += RA_FP_MUL(beta, delta);

}

void
iwm_ra_choose_peer(struct iwm_ra_node *rn, struct iwm_ra_peer *ni)
{
	struct iwm_ra_goodput_stats *g = &rn->g[ni->txmcs];
	int chan40 = ni->chan40;
	int sgi = ni->sgi;
	const struct iwm_ht_rateset *rs, *rsnext;


	if (rn->valid_rates == 0)
		rn->valid_rates = ni->valid_mcs;

	if (rn->probing) {
		/* Probe another rate or settle at the best rate. */
		if (!(rn->valid_probes & (1UL << ni->txmcs))) {
			return;
		}
		iwm_ra_probe_clear(rn, ni);
		if (!iwm_ra_intra_mode_ra_finished(rn, ni)) {
			iwm_ra_probe_next_rate(rn, ni);
			DPRINTFN(3, ("probing MCS %d\n", ni->txmcs));
		} else if (iwm_ra_inter_mode_ra_finished(rn, ni)) {
			rn->best_mcs = iwm_ra_best_rate(rn, ni);
			ni->txmcs = rn->best_mcs;
			iwm_ra_probe_done(rn);
		}

		return;
	} else {
		rn->valid_probes = 0;
	}

	rs = iwm_ra_get_ht_rateset(ni->txmcs, chan40, sgi);
	if ((g->measured >> RA_FP_SHIFT) == 0LL ||
	    (g->average >= 3 * g->stddeviation &&
	    g->measured < g->average - 3 * g->stddeviation)) {
		/* Channel becomes bad. Probe downwards. */
		rn->probing = IWM_RA_PROBING_DOWN;
		rn->probed_rates = 0;
		if (ni->txmcs == rs->min_mcs) {
			rsnext = iwm_ra_next_rateset(rn, ni);
			if (rsnext) {
				iwm_ra_probe_next_rateset(rn, ni,
				    rsnext);
			} else {
				/* Cannot probe further down. */
				rn->probing = IWM_RA_NOT_PROBING;
			}
		} else {
			ni->txmcs = iwm_ra_next_mcs(rn, ni);
			rn->candidate_rates = (1 << ni->txmcs);
		}
	} else if (g->loss < 2 * RA_FP_1 ||
	    g->measured > g->average + 3 * g->stddeviation) {
		/* Channel becomes good. */
		rn->probing = IWM_RA_PROBING_UP;
		rn->probed_rates = 0;
		if (ni->txmcs == rs->max_mcs) {
			rsnext = iwm_ra_next_rateset(rn, ni);
			if (rsnext) {
				iwm_ra_probe_next_rateset(rn, ni,
				    rsnext);
			} else {
				/* Cannot probe further up. */
				rn->probing = IWM_RA_NOT_PROBING;
			}
		} else {
			ni->txmcs = iwm_ra_next_mcs(rn, ni);
			rn->candidate_rates = (1 << ni->txmcs);
		}
	} else {
		/* Remain at current rate. */
		rn->probing = IWM_RA_NOT_PROBING;
		rn->probed_rates = 0;
		rn->candidate_rates = 0;
	}


	if (rn->probing) {
		if (rn->probing & IWM_RA_PROBING_UP)
			DPRINTFN(2, ("channel becomes good; probe up\n"));
		else
			DPRINTFN(2, ("channel becomes bad; probe down\n"));

		DPRINTFN(3, ("measured: %s Mbit/s\n",
		    ra_fp_sprintf(g->measured)));
		DPRINTFN(3, ("average: %s Mbit/s\n",
		    ra_fp_sprintf(g->average)));
		DPRINTFN(3, ("stddeviation: %s\n",
		    ra_fp_sprintf(g->stddeviation)));
		DPRINTFN(3, ("loss: %s%%\n", ra_fp_sprintf(g->loss)));
	}
}

void
iwm_ra_node_init(struct iwm_ra_node *rn)
{
	memset(rn, 0, sizeof(*rn));
}
/* END CSTYLED */
