/* BEGIN CSTYLED */
/*	$OpenBSD: if_iwmvar.h,v 1.79 2025/12/01 16:30:46 stsp Exp $	*/

/*
 * Copyright (c) 2014 genua mbh <info@genua.de>
 * Copyright (c) 2014 Fixup Software Ltd.
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

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 * Driver version we are currently based off of is
 * Linux 3.14.3 (tag id a2df521e42b1d9a23f620ac79dbfe8655a8391dd)
 *
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
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
/* END CSTYLED */

/*
 * Copyright 2026 lex0de <lex0de@tuta.com>
 *
 * illumos port of OpenBSD's if_iwmvar.h.  Field names follow OpenBSD so
 * that code can be compared line by line.  Differences:
 *  - bus_dma maps and mbufs are replaced by illumos DDI DMA handles and
 *    driver-owned copy buffers (struct iwm_dma_info, iwm_tx_data, iwm_rx_data);
 *  - OpenBSD tasks and timeouts are illumos taskq dispatches and
 *    timeout(9F)/ddi_periodic(9F);
 *  - the 9000-family multi-queue RX path, MSI-X, radiotap and bgscan are
 *    not carried (no illumos counterpart or not in scope).
 */

#ifndef _IF_IWMVAR_H
#define	_IF_IWMVAR_H

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/taskq.h>
#include <sys/ksynch.h>
#include <sys/mac_provider.h>
#include <sys/mac_wifi.h>
#include <sys/net80211.h>
#include <sys/net80211_ht.h>
#include <io/iwm/if_iwmcompat.h>
#include <io/iwm/if_iwmreg.h>
#include <io/iwm/if_iwm_ra.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	IWM_UCODE_SECT_MAX	16
#define	IWM_FWDMASEGSZ		(192 * 1024)
#define	IWM_FWDMASEGSZ_8000	(320 * 1024)
/* sanity check value */
#define	IWM_FWMAXSIZE		(2 * 1024 * 1024)

#define	IWM_FW_STATUS_NONE		0
#define	IWM_FW_STATUS_INPROGRESS	1
#define	IWM_FW_STATUS_DONE		2

enum iwm_ucode_type {
	IWM_UCODE_TYPE_REGULAR,
	IWM_UCODE_TYPE_INIT,
	IWM_UCODE_TYPE_WOW,
	IWM_UCODE_TYPE_REGULAR_USNIFFER,
	IWM_UCODE_TYPE_MAX
};

struct iwm_fw_info {
	void *fw_rawdata;
	size_t fw_rawsize;
	int fw_status;

	struct iwm_fw_sects {
		struct iwm_fw_onesect {
			void *fws_data;
			uint32_t fws_len;
			uint32_t fws_devoff;
		} fw_sect[IWM_UCODE_SECT_MAX];
		size_t fw_totlen;
		int fw_count;
		uint32_t paging_mem_size;
	} fw_sects[IWM_UCODE_TYPE_MAX];
};

struct iwm_nvm_data {
	int n_hw_addrs;
	uint8_t hw_addr[ETHER_ADDR_LEN];

	uint8_t calib_version;
	uint16_t calib_voltage;

	uint16_t raw_temperature;
	uint16_t kelvin_temperature;
	uint16_t kelvin_voltage;
	uint16_t xtal_calib[2];

	int sku_cap_band_24GHz_enable;
	int sku_cap_band_52GHz_enable;
	int sku_cap_11n_enable;
	int sku_cap_11ac_enable;
	int sku_cap_amt_enable;
	int sku_cap_ipan_enable;
	int sku_cap_mimo_disable;

	uint8_t radio_cfg_type;
	uint8_t radio_cfg_step;
	uint8_t radio_cfg_dash;
	uint8_t radio_cfg_pnum;
	uint8_t valid_tx_ant, valid_rx_ant;

	uint16_t nvm_version;
	uint8_t max_tx_pwr_half_dbm;

	int lar_enabled;
};

/* max bufs per tfd the driver will use */
#define	IWM_MAX_CMD_TBS_PER_TFD	2

struct iwm_host_cmd {
	const void *data[IWM_MAX_CMD_TBS_PER_TFD];
	struct iwm_rx_packet *resp_pkt;
	size_t resp_pkt_len;
	int handler_status;

	uint32_t flags;
	uint16_t len[IWM_MAX_CMD_TBS_PER_TFD];
	uint8_t dataflags[IWM_MAX_CMD_TBS_PER_TFD];
	uint32_t id;
};

/* One physically contiguous, consistent DMA allocation. */
struct iwm_dma_info {
	ddi_dma_handle_t	dma_hdl;
	ddi_acc_handle_t	acc_hdl;
	ddi_dma_cookie_t	cookie;
	uint_t			ncookies;
	bus_addr_t		paddr;
	void			*vaddr;
	size_t			size;
	size_t			length;
};

struct iwm_fw_paging {
	struct iwm_dma_info fw_paging_block;
	uint32_t fw_paging_size;
};

#define	IWM_TX_RING_COUNT	256
#define	IWM_TX_RING_LOMARK	192
#define	IWM_TX_RING_HIMARK	224

/* For aggregation queues, index must be aligned to frame sequence number. */
#define	IWM_AGG_SSN_TO_TXQ_IDX(x)	((x) & (IWM_TX_RING_COUNT - 1))

/*
 * Frame payloads are copied into per-slot buffers.  Frames from illumos
 * net80211 are at most IEEE80211_MTU plus 802.11/LLC/CCMP overhead, well
 * under 2 KB; two slots share each page so no slot crosses a page.
 */
#define	IWM_TXBUF_SIZE		2048
#define	IWM_TXBUF_PER_PAGE	2

struct iwm_tx_data {
	bus_addr_t	cmd_paddr;
	bus_addr_t	scratch_paddr;
	caddr_t		buf;		/* payload copy buffer */
	bus_addr_t	buf_paddr;
	int		buf_page;	/* index into ring->buf_dma[] */
	mblk_t		*m;		/* non-NULL while slot is in use */
	struct iwm_node *in;
	int txmcs;
	int txrate;

	/* A-MPDU subframes */
	int ampdu_txmcs;
	int ampdu_txnss;
	int ampdu_nframes;
};

struct iwm_tx_ring {
	struct iwm_dma_info	desc_dma;
	struct iwm_dma_info	cmd_dma;
	struct iwm_dma_info	*buf_dma;	/* payload pages, or NULL */
	size_t			buf_npages;
	struct iwm_tfd		*desc;
	struct iwm_device_cmd	*cmd;
	struct iwm_tx_data	data[IWM_TX_RING_COUNT];
	int			qid;
	int			queued;
	int			cur;
	int			tail;
};

#define	IWM_RX_RING_COUNT	256
/* Linux driver optionally uses 8k buffer */
#define	IWM_RBUF_SIZE		4096

struct iwm_rx_data {
	struct iwm_dma_info	dma;
};

struct iwm_rx_ring {
	struct iwm_dma_info	free_desc_dma;
	struct iwm_dma_info	stat_dma;
	uint32_t		*desc;
	struct iwm_rb_status	*stat;
	struct iwm_rx_data	data[IWM_RX_RING_COUNT];
	int			cur;
};

#define	IWM_FLAG_USE_ICT	0x01	/* using Interrupt Cause Table */
#define	IWM_FLAG_RFKILL		0x02	/* radio kill switch is set */
#define	IWM_FLAG_SCANNING	0x04	/* scan in progress */
#define	IWM_FLAG_MAC_ACTIVE	0x08	/* MAC context added to firmware */
#define	IWM_FLAG_BINDING_ACTIVE	0x10	/* MAC->PHY binding added to firmware */
#define	IWM_FLAG_STA_ACTIVE	0x20	/* AP added to firmware station table */
#define	IWM_FLAG_TE_ACTIVE	0x40	/* time event is scheduled */
#define	IWM_FLAG_HW_ERR		0x80	/* hardware error occurred */
#define	IWM_FLAG_SHUTDOWN	0x100	/* shutting down; new tasks forbidden */
#define	IWM_FLAG_BGSCAN		0x200	/* background scan in progress */
#define	IWM_FLAG_TXFLUSH	0x400	/* Tx queue flushing in progress */
/* illumos additions */
#define	IWM_FLAG_RUNNING	0x1000	/* device initialised (IFF_RUNNING) */
#define	IWM_FLAG_SUSPENDED	0x2000	/* DDI_SUSPEND */
#define	IWM_FLAG_OACTIVE	0x4000	/* Tx blocked; call mac_tx_update */
#define	IWM_FLAG_UP		0x8000	/* mac_start() called (IFF_UP) */
#define	IWM_FLAG_NET_ATTACHED	0x10000	/* ieee80211_attach() done */

struct iwm_ucode_status {
	uint32_t uc_error_event_table;
	uint32_t uc_umac_error_event_table;
	uint32_t uc_log_event_table;

	int uc_ok;
	int uc_intr;
};

#define	IWM_CMD_RESP_MAX	PAGESIZE

/* lower blocks contain EEPROM image and calibration data */
#define	IWM_OTP_LOW_IMAGE_SIZE_FAMILY_7000	16384
#define	IWM_OTP_LOW_IMAGE_SIZE_FAMILY_8000	32768

#define	IWM_TE_SESSION_PROTECTION_MAX_TIME_MS	1000
#define	IWM_TE_SESSION_PROTECTION_MIN_TIME_MS	400

enum IWM_CMD_MODE {
	IWM_CMD_ASYNC		= (1 << 0),
	IWM_CMD_WANT_RESP	= (1 << 1),
	IWM_CMD_SEND_IN_RFKILL	= (1 << 2),
};
enum iwm_hcmd_dataflag {
	IWM_HCMD_DFL_NOCOPY	= (1 << 0),
	IWM_HCMD_DFL_DUP	= (1 << 1),
};

#define	IWM_NUM_PAPD_CH_GROUPS	9
#define	IWM_NUM_TXP_CH_GROUPS	9

struct iwm_phy_db_entry {
	uint16_t size;
	uint8_t *data;
};

struct iwm_phy_db {
	struct iwm_phy_db_entry	cfg;
	struct iwm_phy_db_entry	calib_nch;
	struct iwm_phy_db_entry	calib_ch_group_papd[IWM_NUM_PAPD_CH_GROUPS];
	struct iwm_phy_db_entry	calib_ch_group_txp[IWM_NUM_TXP_CH_GROUPS];
};

struct iwm_phy_ctxt {
	uint16_t id;
	uint16_t color;
	uint32_t ref;
	struct ieee80211_channel *channel;
	uint8_t sco;		/* 40 MHz secondary channel offset */
	uint8_t vht_chan_width;
};

/* OpenBSD's HTOP0 secondary channel offset values, used by phy_ctxt. */
#define	IWM_HTOP0_SCO_SCN	0
#define	IWM_HTOP0_SCO_SCA	1
#define	IWM_HTOP0_SCO_SCB	3

struct iwm_bf_data {
	int bf_enabled;		/* filtering	*/
	int ba_enabled;		/* abort	*/
	int ave_beacon_signal;
	int last_cqm_event;
};

struct iwm_rxq_dup_data {
	uint16_t last_seq[IWM_MAX_TID_COUNT + 1];
	uint8_t last_sub_frame[IWM_MAX_TID_COUNT + 1];
};

struct iwm_ba_task_data {
	uint32_t		start_tidmask;
	uint32_t		stop_tidmask;
};

/* Per-TID Rx BlockAck state the firmware needs (from ADDBA requests). */
struct iwm_rx_ba {
	uint16_t		ssn;
	uint16_t		winsize;
	uint16_t		timeout;
};

struct iwm_softc {
	/* ic must be first: net80211 callbacks cast ic back to the softc. */
	struct ieee80211com sc_ic;
	dev_info_t	*sc_dip;
	int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
	/* net80211 defaults we chain to from our BlockAck hooks */
	void (*sc_recv_action)(struct ieee80211_node *, const uint8_t *,
	    const uint8_t *);
	int (*sc_addba_response)(struct ieee80211_node *,
	    struct ieee80211_tx_ampdu *, int, int, int);
	void (*sc_addba_stop)(struct ieee80211_node *,
	    struct ieee80211_tx_ampdu *);
	int sc_newstate_pending;
	int attached;

	struct iwm_amrr sc_amrr;
	timeout_id_t sc_calib_to;
	timeout_id_t sc_led_blink_to;
	ddi_periodic_t sc_watchdog;

	/*
	 * Deferred work.  OpenBSD runs init_task on systq and the others on
	 * its single-threaded sc_nswq; illumos uses two single-threaded taskqs
	 * with the same split.  sc_task_refs counts dispatched tasks so stop
	 * and detach can wait for them (OpenBSD refcnt task_refs).
	 */
	taskq_t		*sc_initq;
	taskq_t		*sc_nswq;
	uint_t		sc_task_refs;
	kcondvar_t	sc_task_cv;
	uint32_t	sc_task_pending;	/* IWM_TASK_* bits queued */
#define	IWM_TASK_INIT		0x01
#define	IWM_TASK_NEWSTATE	0x02
#define	IWM_TASK_BA		0x04
#define	IWM_TASK_MAC_CTXT	0x08
#define	IWM_TASK_DEFER		0x10
	enum ieee80211_state	ns_nstate;
	int			ns_arg;

	int			deauth_sent;

	/* Arguments for the firmware BlockAck setup/teardown task. */
	struct iwm_ba_task_data	ba_rx;
	struct iwm_ba_task_data	ba_tx;
	struct iwm_rx_ba	sc_rx_ba[IWM_MAX_TID_COUNT];

	/* Register access. */
	ddi_acc_handle_t	sc_pcih;
	ddi_acc_handle_t	sc_regh;
	caddr_t			sc_regs;
	off_t			sc_sz;

	/* Interrupts. */
	ddi_intr_handle_t	sc_intr_htable[1];
	int			sc_intr_type;
	int			sc_intr_cap;
	uint_t			sc_intr_pri;
	int			sc_msix;

	/*
	 * sc_mtx protects the device, rings and driver state and is taken by
	 * the interrupt handler.  sc_rwlock serialises init/stop/newstate and
	 * ioctls (OpenBSD ioctl_rwl).
	 */
	kmutex_t		sc_mtx;
	krwlock_t		sc_rwlock;
	kcondvar_t		sc_cmd_cv;	/* command responses */
	kcondvar_t		sc_fw_cv;	/* firmware upload/alive/init */
	kcondvar_t		sc_txq_cv;	/* Tx queue drain */

	/* TX scheduler rings. */
	struct iwm_dma_info		sched_dma;
	uint32_t			sched_base;

	/* TX/RX rings. */
	struct iwm_tx_ring txq[IWM_MAX_QUEUES];
	struct iwm_rx_ring rxq;
	int qfullmsk;
	int qenablemsk;
	int cmdqid;

	int sc_sf_state;

	/* ICT table. */
	struct iwm_dma_info	ict_dma;
	int			ict_cur;

	int sc_hw_rev;
#define	IWM_SILICON_A_STEP	0
#define	IWM_SILICON_B_STEP	1
#define	IWM_SILICON_C_STEP	2
#define	IWM_SILICON_D_STEP	3
	int sc_hw_id;
	int sc_device_family;
#define	IWM_DEVICE_FAMILY_7000	1
#define	IWM_DEVICE_FAMILY_8000	2
#define	IWM_DEVICE_FAMILY_9000	3

	struct iwm_dma_info kw_dma;
	struct iwm_dma_info fw_dma;

	int sc_fw_chunk_done;
	int sc_init_complete;
#define	IWM_INIT_COMPLETE	0x01
#define	IWM_CALIB_COMPLETE	0x02

	struct iwm_ucode_status sc_uc;
	enum iwm_ucode_type sc_uc_current;
	char sc_fwver[32];

	int sc_capaflags;
	int sc_capa_max_probe_len;
	int sc_capa_n_scan_channels;
	uint8_t sc_ucode_api[howmany(IWM_NUM_UCODE_TLV_API, NBBY)];
	uint8_t sc_enabled_capa[howmany(IWM_NUM_UCODE_TLV_CAPA, NBBY)];
#define	IWM_MAX_FW_CMD_VERSIONS	64
	struct iwm_fw_cmd_version cmd_versions[IWM_MAX_FW_CMD_VERSIONS];
	int n_cmd_versions;

	int sc_intmask;
	int sc_flags;

	uint32_t sc_fh_init_mask;
	uint32_t sc_hw_init_mask;
	uint32_t sc_fh_mask;
	uint32_t sc_hw_mask;

	int sc_generation;

	int sc_cap_off;		/* PCIe caps */

	const char *sc_fwname;
	size_t sc_fwdmasegsz;
	size_t sc_nvm_max_section_size;
	struct iwm_fw_info sc_fw;
	uint32_t sc_fw_phy_config;
	uint32_t sc_extra_phy_config;
	struct iwm_tlv_calib_ctrl sc_default_calib[IWM_UCODE_TYPE_MAX];

	struct iwm_nvm_data sc_nvm;
	struct iwm_phy_db sc_phy_db;

	struct iwm_bf_data sc_bf;

	int sc_tx_timer[IWM_MAX_QUEUES];
	int sc_rx_ba_sessions;
	uint32_t sc_rx_ba_tidmask;	/* TIDs with a firmware Rx BA session */
	int tx_ba_queue_mask;

	int sc_scan_last_antenna;

	int sc_fixed_ridx;

	int sc_staid;
	int sc_nodecolor;

	uint8_t *sc_cmd_resp_pkt[IWM_TX_RING_COUNT];
	uint8_t sc_cmd_done[IWM_TX_RING_COUNT];	/* illumos: response seen */
	size_t sc_cmd_resp_len[IWM_TX_RING_COUNT];
	int sc_nic_locks;

	struct iwm_rx_phy_info sc_last_phy_info;
	int sc_ampdu_ref;

	uint32_t sc_time_event_uid;

	/* phy contexts.  we only use the first one */
	struct iwm_phy_ctxt sc_phyctxt[IWM_NUM_PHY_CTX];

	struct iwm_notif_statistics sc_stats;
	int sc_noise;

	int host_interrupt_operation_mode;
	int sc_ltr_enabled;
	enum iwm_nvm_type nvm_type;

	int sc_mqrx_supported;
	int sc_integrated;
	int sc_ltr_delay;
	int sc_xtal_latency;
	int sc_low_latency_xtal;

	/*
	 * Paging parameters - All of the parameters should be set by the
	 * opmode when paging is enabled
	 */
	struct iwm_fw_paging fw_paging_db[IWM_NUM_OF_FW_PAGING_BLOCKS];
	uint16_t num_of_paging_blk;
	uint16_t num_of_pages_in_last_blk;

	/* illumos: received frames handed to net80211 after sc_mtx drops. */
	mblk_t		*sc_rx_head;
	mblk_t		*sc_rx_tail;
	/* illumos: work the interrupt handler does after dropping sc_mtx */
	int		sc_tx_update;		/* call mac_tx_update() */
	int		sc_bmiss_pending;	/* ieee80211_beacon_miss() */
	int		sc_endscan_pending;	/* call ieee80211_end_scan() */
	uint32_t	sc_bar_pending;		/* TIDs needing a BAR */
	uint16_t	sc_bar_ssn[IWM_MAX_TID_COUNT];
	/* Tx BlockAck window start per TID (OpenBSD ni_tx_ba ba_winstart) */
	uint16_t	sc_tx_ba_winstart[IWM_MAX_TID_COUNT];

	/* illumos: interface statistics (MAC_STAT_*) */
	uint64_t	sc_ipackets, sc_opackets, sc_rbytes, sc_obytes;
	uint64_t	sc_ierrors, sc_oerrors, sc_noxmtbuf;

	/* illumos: firmware error recovery accounting */
	uint32_t	sc_fw_restarts;
};

struct iwm_node {
	struct ieee80211_node in_ni;
	struct iwm_phy_ctxt *in_phyctxt;
	uint8_t in_macaddr[ETHER_ADDR_LEN];

	uint16_t in_id;
	uint16_t in_color;

	struct iwm_amrr_node in_amn;
	struct iwm_ra_node in_rn;
	int in_txmcs;		/* OpenBSD ni_txmcs */
	int lq_rate_mismatch;

	struct iwm_rxq_dup_data dup_data;

	/* For use with the ADD_STA command. */
	uint32_t tfd_queue_msk;
	uint16_t tid_disable_ampdu;
};
/* One time unit in microseconds (OpenBSD IEEE80211_DUR_TU). */
#define	IWM_DUR_TU		1024

/* 12-bit sequence number comparison (OpenBSD SEQ_LT). */
#define	IWM_SEQ_LT(a, b)	\
	((((uint16_t)(a) - (uint16_t)(b)) & 0xfff) > 2048)

#define	IWM_STATION_ID		0
#define	IWM_AUX_STA_ID		1
#define	IWM_MONITOR_STA_ID	2

#define	IWM_ICT_SIZE		4096
#define	IWM_ICT_COUNT		(IWM_ICT_SIZE / sizeof (uint32_t))
#define	IWM_ICT_PADDR_SHIFT	12

/* Register access (OpenBSD IWM_READ etc., which use bus_space). */
#define	IWM_READ(sc, reg)						\
	ddi_get32((sc)->sc_regh, (uint32_t *)((sc)->sc_regs + (reg)))
#define	IWM_WRITE(sc, reg, val)						\
	ddi_put32((sc)->sc_regh, (uint32_t *)((sc)->sc_regs + (reg)), (val))
#define	IWM_WRITE_1(sc, reg, val)					\
	ddi_put8((sc)->sc_regh, (uint8_t *)((sc)->sc_regs + (reg)), (val))
#define	IWM_SETBITS(sc, reg, mask)					\
	IWM_WRITE(sc, reg, IWM_READ(sc, reg) | (mask))
#define	IWM_CLRBITS(sc, reg, mask)					\
	IWM_WRITE(sc, reg, IWM_READ(sc, reg) & ~(mask))
/* ddi_get/put on strict-order device mappings are already ordered. */
#define	IWM_BARRIER_WRITE(sc)		((void)0)
#define	IWM_BARRIER_READ_WRITE(sc)	((void)0)

#ifdef __cplusplus
}
#endif

#endif /* _IF_IWMVAR_H */
