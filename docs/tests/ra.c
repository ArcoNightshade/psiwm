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
 * Host-side test of the driver's rate control (if_iwm_ra.c MiRA and
 * if_iwm_amrr.c AMRR), compiled against stub headers by run.sh.  Each case
 * simulates a channel where a rate succeeds or fails deterministically and
 * checks where the algorithm settles.
 */

#include <stdio.h>
#include <stdlib.h>
#include "if_iwm_ra.h"

static int fails;

static void
check(int cond, const char *what)
{
	printf("%s: %s\n", cond ? "ok" : "FAIL", what);
	if (!cond)
		fails++;
}

/*
 * Run MiRA for n frames, one Tx report per frame as the driver does.
 * Frames at MCS <= good_max (within each stream count) succeed, others
 * fail.  Returns MiRA's settled best MCS; *maxmcs is the highest MCS used
 * in the last 1000 frames.  MiRA keeps probing one step above the best
 * rate (as upstream; on hardware the LQ retry chain falls back), so the
 * current MCS alone says little.
 */
static int
mira_run(struct iwm_ra_peer *p, int n, int good_max, int *maxmcs)
{
	struct iwm_ra_node rn;
	int i;

	iwm_ra_node_init(&rn);
	*maxmcs = 0;
	for (i = 0; i < n; i++) {
		int mcs = p->txmcs;
		int ok = (mcs % 8) <= good_max;

		iwm_ra_add_stats_ht(&rn, p, mcs, 1, ok ? 0 : 1);
		iwm_ra_choose_peer(&rn, p);
		if (i >= n - 1000 && p->txmcs > *maxmcs)
			*maxmcs = p->txmcs;
	}
	return (rn.probing ? rn.best_mcs : p->txmcs);
}

int
main(void)
{
	struct iwm_ra_peer p;
	struct iwm_amrr amrr = { IWM_AMRR_MIN_SUCCESS_THRESHOLD,
	    IWM_AMRR_MAX_SUCCESS_THRESHOLD };
	struct iwm_amrr_node amn;
	struct ieee80211_node ni;
	const struct iwm_ht_rateset *rs;
	int best, maxmcs, i;

	/* Rate set lookup. */
	rs = iwm_ra_get_ht_rateset(7, 0, 0);
	check(rs->min_mcs == 0 && rs->max_mcs == 7 && rs->nrates == 8,
	    "SISO 20 MHz rate set is MCS 0-7");
	rs = iwm_ra_get_ht_rateset(12, 1, 1);
	check(rs->min_mcs == 8 && rs->max_mcs == 15 && rs->chan40 &&
	    rs->sgi, "MIMO2 40 MHz SGI rate set is MCS 8-15");

	/* Clean SISO channel: climb to MCS 7. */
	p.txmcs = 0; p.chan40 = 0; p.sgi = 0; p.valid_mcs = 0xff;
	best = mira_run(&p, 5000, 7, &maxmcs);
	check(best == 7, "MiRA SISO clean channel reaches MCS 7");

	/* Clean MIMO channel at 40 MHz: climb to MCS 15. */
	p.txmcs = 0; p.chan40 = 1; p.sgi = 1; p.valid_mcs = 0xffff;
	best = mira_run(&p, 20000, 7, &maxmcs);
	check(best == 15, "MiRA MIMO2 40 MHz clean channel reaches MCS 15");

	/* Only MCS 0-4 get through: best rate 4, probes no higher than 5. */
	p.txmcs = 7; p.chan40 = 0; p.sgi = 0; p.valid_mcs = 0xff;
	best = mira_run(&p, 20000, 4, &maxmcs);
	check(best == 4, "MiRA settles on MCS 4 when MCS 5-7 fail");
	check(maxmcs <= 5, "MiRA probes at most one step above it");

	/* MCS never leaves the peer's valid set. */
	p.txmcs = 0; p.chan40 = 0; p.sgi = 0; p.valid_mcs = 0x0f;
	best = mira_run(&p, 5000, 7, &maxmcs);
	check(best == 3 && maxmcs <= 3, "MiRA respects peer MCS set (0-3)");

	/* Rate table: MCS 10 at 40 MHz is 81 Mbit/s (OpenBSD has 96). */
	rs = iwm_ra_get_ht_rateset(10, 1, 0);
	check(rs->rates[2] == 162, "MCS 10 HT40 rate is 81 Mbit/s");

	/* AMRR on an 11g rate set (12 rates). */
	ni.in_rates.ir_nrates = 12;
	ni.in_txrate = 0;
	iwm_amrr_node_init(&amrr, &amn);
	for (i = 0; i < 200; i++) {
		amn.amn_txcnt += 20;		/* no retries */
		iwm_amrr_choose(&amrr, &ni, &amn);
	}
	check(ni.in_txrate == 11, "AMRR clean channel reaches top rate");
	for (i = 0; i < 200; i++) {
		amn.amn_txcnt += 20;
		amn.amn_retrycnt += 20;		/* every frame retried */
		iwm_amrr_choose(&amrr, &ni, &amn);
	}
	check(ni.in_txrate == 0, "AMRR lossy channel drops to lowest rate");

	return (fails != 0);
}
