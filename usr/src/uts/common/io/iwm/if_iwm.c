/* BEGIN CSTYLED */
/*	$OpenBSD: if_iwm.c,v 1.419 2025/12/01 16:30:46 stsp Exp $	*/

/*
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017 Stefan Sperling <stsp@openbsd.org>
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
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
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
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 Intel Deutschland GmbH
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
 */

/*
 * iwm(4D): Intel Dual Band Wireless-AC 7000/8000 series driver.
 *
 * This is a port of OpenBSD's sys/dev/pci/if_iwm.c (v1.419) to illumos.
 * Hardware and firmware handling is kept as close to OpenBSD as possible,
 * including function names and order, so that OpenBSD fixes can be applied
 * by comparing the two files.  The parts that differ are:
 *
 *  - DMA uses DDI handles; Tx frames and Rx buffers are copied through
 *    driver-owned DMA buffers instead of loading mbufs.
 *  - net80211 is illumos's (FreeBSD-derived) stack.  State changes are
 *    deferred to a taskq exactly like OpenBSD's newstate task; Rx BlockAck
 *    is driven from ic_recv_action, Tx aggregation from ic_addba_*, keys
 *    from ic_crypto hooks, and rate adaptation (AMRR/MiRA) is carried in
 *    if_iwm_amrr.c and if_iwm_ra.c.
 *  - Received frames are collected under sc_mtx in the interrupt handler and
 *    passed to net80211 after the mutex is dropped.
 *  - The GLDv3 MAC entry points replace the ifnet ioctl/start interface.
 *  - Not carried: 9000-family multi-queue Rx and MSI-X, radiotap/BPF,
 *    background scanning (no illumos net80211 support), 802.11ac/VHT
 *    (illumos net80211 has no VHT; planned as a separate stage).
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/firmload.h>
#include <sys/strsun.h>
#include <sys/stat.h>
#include <sys/policy.h>
#include <sys/disp.h>
#include <sys/note.h>
#include <sys/mac_provider.h>
#include <sys/mac_wifi.h>
#include <sys/net80211.h>
#include <sys/net80211_proto.h>
#include <sys/net80211_ht.h>
#include <sys/net80211_crypto.h>
#include <inet/wifi_ioctl.h>

#include "if_iwmvar.h"

#ifdef IWM_DEBUG
#define	DPRINTF(x)	do { if (iwm_debug > 0) cmn_err x; } while (0)
#define	DPRINTFN(n, x)	do { if (iwm_debug >= (n)) cmn_err x; } while (0)
int iwm_debug = 1;
#else
#define	DPRINTF(x)	do { } while (0)
#define	DPRINTFN(n, x)	do { } while (0)
#endif

static void *iwm_soft_state;

static ddi_device_acc_attr_t iwm_reg_accattr = {
	.devacc_attr_version = DDI_DEVICE_ATTR_V0,
	.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC,
	.devacc_attr_dataorder = DDI_STRICTORDER_ACC,
	.devacc_attr_access = DDI_DEFAULT_ACC
};

static ddi_device_acc_attr_t iwm_dma_accattr = {
	.devacc_attr_version = DDI_DEVICE_ATTR_V0,
	.devacc_attr_endian_flags = DDI_NEVERSWAP_ACC,
	.devacc_attr_dataorder = DDI_STRICTORDER_ACC,
	.devacc_attr_access = DDI_DEFAULT_ACC
};

/* Supported devices; OpenBSD iwm_devices[] and the 8000 cases of attach. */
static const struct iwm_devid {
	uint16_t	device;
	const char	*fwname;
	int		family;
} iwm_devices[] = {
	{ 0x24f3, "iwm-8000C-36", IWM_DEVICE_FAMILY_8000 },	/* 8260 */
	{ 0x24f4, "iwm-8000C-36", IWM_DEVICE_FAMILY_8000 },	/* 8260 */
	{ 0x24fd, "iwm-8265-36", IWM_DEVICE_FAMILY_8000 },	/* 8265 */
};

/* illumos net80211 legacy rate sets */
static const struct ieee80211_rateset iwm_rateset_11a =
	{ 8, { 12, 18, 24, 36, 48, 72, 96, 108 } };
static const struct ieee80211_rateset iwm_rateset_11b =
	{ 4, { 2, 4, 11, 22 } };
static const struct ieee80211_rateset iwm_rateset_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

/* BEGIN CSTYLED */
const uint8_t iwm_nvm_channels[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44 , 48, 52, 56, 60, 64,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165
};

const uint8_t iwm_nvm_channels_8000[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
	96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173, 177, 181
};

#define	IWM_NUM_2GHZ_CHANNELS	14

const struct iwm_rate {
	uint16_t rate;
	uint8_t plcp;
	uint8_t ht_plcp;
} iwm_rates[] = {
		/* Legacy */		/* HT */
	{   2,	IWM_RATE_1M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP  },
	{   4,	IWM_RATE_2M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP },
	{  11,	IWM_RATE_5M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP  },
	{  22,	IWM_RATE_11M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP },
	{  12,	IWM_RATE_6M_PLCP,	IWM_RATE_HT_SISO_MCS_0_PLCP },
	{  18,	IWM_RATE_9M_PLCP,	IWM_RATE_HT_SISO_MCS_INV_PLCP  },
	{  24,	IWM_RATE_12M_PLCP,	IWM_RATE_HT_SISO_MCS_1_PLCP },
	{  26,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_8_PLCP },
	{  36,	IWM_RATE_18M_PLCP,	IWM_RATE_HT_SISO_MCS_2_PLCP },
	{  48,	IWM_RATE_24M_PLCP,	IWM_RATE_HT_SISO_MCS_3_PLCP },
	{  52,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_9_PLCP },
	{  72,	IWM_RATE_36M_PLCP,	IWM_RATE_HT_SISO_MCS_4_PLCP },
	{  78,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_10_PLCP },
	{  96,	IWM_RATE_48M_PLCP,	IWM_RATE_HT_SISO_MCS_5_PLCP },
	{ 104,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_11_PLCP },
	{ 108,	IWM_RATE_54M_PLCP,	IWM_RATE_HT_SISO_MCS_6_PLCP },
	{ 128,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_SISO_MCS_7_PLCP },
	{ 156,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_12_PLCP },
	{ 208,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_13_PLCP },
	{ 234,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_14_PLCP },
	{ 260,	IWM_RATE_INVM_PLCP,	IWM_RATE_HT_MIMO2_MCS_15_PLCP },
};
#define	IWM_RIDX_CCK	0
#define	IWM_RIDX_OFDM	4
#define	IWM_RIDX_MAX	(nitems(iwm_rates)-1)
#define	IWM_RIDX_IS_CCK(_i_) ((_i_) < IWM_RIDX_OFDM)
#define	IWM_RIDX_IS_OFDM(_i_) ((_i_) >= IWM_RIDX_OFDM)
#define	IWM_RVAL_IS_OFDM(_i_) ((_i_) >= 12 && (_i_) != 22)

/* Convert an MCS index into an iwm_rates[] index. */
const int iwm_ht_mcs2ridx[] = {
	IWM_RATE_MCS_0_INDEX,
	IWM_RATE_MCS_1_INDEX,
	IWM_RATE_MCS_2_INDEX,
	IWM_RATE_MCS_3_INDEX,
	IWM_RATE_MCS_4_INDEX,
	IWM_RATE_MCS_5_INDEX,
	IWM_RATE_MCS_6_INDEX,
	IWM_RATE_MCS_7_INDEX,
	IWM_RATE_MCS_8_INDEX,
	IWM_RATE_MCS_9_INDEX,
	IWM_RATE_MCS_10_INDEX,
	IWM_RATE_MCS_11_INDEX,
	IWM_RATE_MCS_12_INDEX,
	IWM_RATE_MCS_13_INDEX,
	IWM_RATE_MCS_14_INDEX,
	IWM_RATE_MCS_15_INDEX,
};
/* END CSTYLED */

struct iwm_nvm_section {
	uint16_t length;
	uint8_t *data;
};

#include "if_iwm_proto.h"
static uint8_t
iwm_lookup_cmd_ver(struct iwm_softc *sc, uint8_t grp, uint8_t cmd)
{
	const struct iwm_fw_cmd_version *entry;
	int i;

	for (i = 0; i < sc->n_cmd_versions; i++) {
		entry = &sc->cmd_versions[i];
		if (entry->group == grp && entry->cmd == cmd)
			return (entry->cmd_ver);
	}

	return (IWM_FW_CMD_VER_UNKNOWN);
}

static int
iwm_is_mimo_ht_plcp(uint8_t ht_plcp)
{
	return (ht_plcp != IWM_RATE_HT_SISO_MCS_INV_PLCP &&
	    (ht_plcp & IWM_RATE_HT_MCS_NSS_MSK));
}

static int
iwm_is_mimo_ht_mcs(int mcs)
{
	int ridx = iwm_ht_mcs2ridx[mcs];
	return (iwm_is_mimo_ht_plcp(iwm_rates[ridx].ht_plcp));

}

static int
iwm_store_cscheme(struct iwm_softc *sc, uint8_t *data, size_t dlen)
{
	struct iwm_fw_cscheme_list *l = (void *)data;

	if (dlen < sizeof (*l) ||
	    dlen < sizeof (l->size) + l->size * sizeof (*l->cs))
		return (EINVAL);

	/* we don't actually store anything for now, always use s/w crypto */

	return (0);
}

static int
iwm_firmware_store_section(struct iwm_softc *sc, enum iwm_ucode_type type,
    uint8_t *data, size_t dlen)
{
	struct iwm_fw_sects *fws;
	struct iwm_fw_onesect *fwone;

	if (type >= IWM_UCODE_TYPE_MAX)
		return (EINVAL);
	if (dlen < sizeof (uint32_t))
		return (EINVAL);

	fws = &sc->sc_fw.fw_sects[type];
	if (fws->fw_count >= IWM_UCODE_SECT_MAX)
		return (EINVAL);

	fwone = &fws->fw_sect[fws->fw_count];

	/* first 32bit are device load offset */
	memcpy(&fwone->fws_devoff, data, sizeof (uint32_t));

	/* rest is data */
	fwone->fws_data = data + sizeof (uint32_t);
	fwone->fws_len = dlen - sizeof (uint32_t);

	fws->fw_count++;
	fws->fw_totlen += fwone->fws_len;

	return (0);
}

#define	IWM_DEFAULT_SCAN_CHANNELS	40
/* Newer firmware might support more channels. Raise this value if needed. */
#define	IWM_MAX_SCAN_CHANNELS		52 /* as of 8265-34 firmware image */

struct iwm_tlv_calib_data {
	uint32_t ucode_type;
	struct iwm_tlv_calib_ctrl calib;
} __packed;

static int
iwm_set_default_calib(struct iwm_softc *sc, const void *data)
{
	const struct iwm_tlv_calib_data *def_calib = data;
	uint32_t ucode_type = le32toh(def_calib->ucode_type);

	if (ucode_type >= IWM_UCODE_TYPE_MAX)
		return (EINVAL);

	sc->sc_default_calib[ucode_type].flow_trigger =
	    def_calib->calib.flow_trigger;
	sc->sc_default_calib[ucode_type].event_trigger =
	    def_calib->calib.event_trigger;

	return (0);
}

static void
iwm_fw_info_free(struct iwm_fw_info *fw)
{
	if (fw->fw_rawdata != NULL)
		kmem_free(fw->fw_rawdata, fw->fw_rawsize);
	fw->fw_rawdata = NULL;
	fw->fw_rawsize = 0;
	/* don't touch fw->fw_status */
	memset(fw->fw_sects, 0, sizeof (fw->fw_sects));
}

static void
iwm_fw_version_str(char *buf, size_t bufsize,
    uint32_t major, uint32_t minor, uint32_t api)
{
	/*
	 * Starting with major version 35 the Linux driver prints the minor
	 * version in hexadecimal.
	 */
	if (major >= 35)
		(void) snprintf(buf, bufsize, "%u.%08x.%u", major, minor, api);
	else
		(void) snprintf(buf, bufsize, "%u.%u.%u", major, minor, api);
}

static int
iwm_read_firmware(struct iwm_softc *sc)
{
	struct iwm_fw_info *fw = &sc->sc_fw;
	struct iwm_tlv_ucode_header *uhdr;
	struct iwm_ucode_tlv tlv;
	uint32_t tlv_type;
	uint8_t *data;
	uint32_t usniffer_img;
	uint32_t paging_mem_size;
	int err;
	size_t len;

	if (fw->fw_status == IWM_FW_STATUS_DONE)
		return (0);

	/* Callers hold sc_rwlock as writer, so loads are not concurrent. */
	fw->fw_status = IWM_FW_STATUS_INPROGRESS;

	if (fw->fw_rawdata != NULL)
		iwm_fw_info_free(fw);

	err = iwm_loadfirmware(sc, sc->sc_fwname,
	    &fw->fw_rawdata, &fw->fw_rawsize);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not read firmware %s "
		    "(error %d)", sc->sc_fwname, err);
		goto out;
	}

	/* The TLV walk below dereferences the header. */
	if (fw->fw_rawsize < sizeof (struct iwm_tlv_ucode_header)) {
		err = EINVAL;
		goto out;
	}

	sc->sc_capaflags = 0;
	sc->sc_capa_n_scan_channels = IWM_DEFAULT_SCAN_CHANNELS;
	memset(sc->sc_enabled_capa, 0, sizeof (sc->sc_enabled_capa));
	memset(sc->sc_ucode_api, 0, sizeof (sc->sc_ucode_api));
	sc->n_cmd_versions = 0;

	uhdr = (void *)fw->fw_rawdata;
	if (*(uint32_t *)fw->fw_rawdata != 0 ||
	    le32toh(uhdr->magic) != IWM_TLV_UCODE_MAGIC) {
		dev_err(sc->sc_dip, CE_WARN, "!invalid firmware %s",
		    sc->sc_fwname);
		err = EINVAL;
		goto out;
	}

	iwm_fw_version_str(sc->sc_fwver, sizeof (sc->sc_fwver),
	    IWM_UCODE_MAJOR(le32toh(uhdr->ver)),
	    IWM_UCODE_MINOR(le32toh(uhdr->ver)),
	    IWM_UCODE_API(le32toh(uhdr->ver)));

	data = uhdr->data;
	len = fw->fw_rawsize - sizeof (*uhdr);

	while (len >= sizeof (tlv)) {
		size_t tlv_len;
		void *tlv_data;

		memcpy(&tlv, data, sizeof (tlv));
		tlv_len = le32toh(tlv.length);
		tlv_type = le32toh(tlv.type);

		len -= sizeof (tlv);
		data += sizeof (tlv);
		tlv_data = data;

		if (len < tlv_len) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!firmware too short: %zu bytes", len);
			err = EINVAL;
			goto parse_out;
		}

		switch (tlv_type) {
		case IWM_UCODE_TLV_PROBE_MAX_LEN:
			if (tlv_len < sizeof (uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capa_max_probe_len
			    = le32toh(*(uint32_t *)tlv_data);
			if (sc->sc_capa_max_probe_len >
			    IWM_SCAN_OFFLOAD_PROBE_REQ_SIZE) {
				err = EINVAL;
				goto parse_out;
			}
			break;
		case IWM_UCODE_TLV_PAN:
			if (tlv_len) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capaflags |= IWM_UCODE_TLV_FLAGS_PAN;
			break;
		case IWM_UCODE_TLV_FLAGS:
			if (tlv_len < sizeof (uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			/*
			 * Apparently there can be many flags, but Linux driver
			 * parses only the first one, and so do we.
			 *
			 * XXX: why does this override IWM_UCODE_TLV_PAN?
			 * Intentional or a bug?  Observations from
			 * current firmware file:
			 *  1) TLV_PAN is parsed first
			 *  2) TLV_FLAGS contains TLV_FLAGS_PAN
			 * ==> this resets TLV_PAN to itself... hnnnk
			 */
			sc->sc_capaflags = le32toh(*(uint32_t *)tlv_data);
			break;
		case IWM_UCODE_TLV_CSCHEME:
			err = iwm_store_cscheme(sc, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_NUM_OF_CPU: {
			uint32_t num_cpu;
			if (tlv_len != sizeof (uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			num_cpu = le32toh(*(uint32_t *)tlv_data);
			if (num_cpu < 1 || num_cpu > 2) {
				err = EINVAL;
				goto parse_out;
			}
			break;
		}
		case IWM_UCODE_TLV_SEC_RT:
			err = iwm_firmware_store_section(sc,
			    IWM_UCODE_TYPE_REGULAR, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_SEC_INIT:
			err = iwm_firmware_store_section(sc,
			    IWM_UCODE_TYPE_INIT, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_SEC_WOWLAN:
			err = iwm_firmware_store_section(sc,
			    IWM_UCODE_TYPE_WOW, tlv_data, tlv_len);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_DEF_CALIB:
			if (tlv_len != sizeof (struct iwm_tlv_calib_data)) {
				err = EINVAL;
				goto parse_out;
			}
			err = iwm_set_default_calib(sc, tlv_data);
			if (err)
				goto parse_out;
			break;
		case IWM_UCODE_TLV_PHY_SKU:
			if (tlv_len != sizeof (uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_fw_phy_config = le32toh(*(uint32_t *)tlv_data);
			break;

		case IWM_UCODE_TLV_API_CHANGES_SET: {
			struct iwm_ucode_api *api;
			uint32_t idx;
			int i;
			if (tlv_len != sizeof (*api)) {
				err = EINVAL;
				goto parse_out;
			}
			api = (struct iwm_ucode_api *)tlv_data;
			idx = le32toh(api->api_index);
			if (idx >= howmany(IWM_NUM_UCODE_TLV_API, 32)) {
				err = EINVAL;
				goto parse_out;
			}
			for (i = 0; i < 32; i++) {
				if ((le32toh(api->api_flags) & (1 << i)) == 0)
					continue;
				setbit(sc->sc_ucode_api, i + (32 * idx));
			}
			break;
		}

		case IWM_UCODE_TLV_ENABLED_CAPABILITIES: {
			struct iwm_ucode_capa *capa;
			uint32_t idx;
			int i;
			if (tlv_len != sizeof (*capa)) {
				err = EINVAL;
				goto parse_out;
			}
			capa = (struct iwm_ucode_capa *)tlv_data;
			idx = le32toh(capa->api_index);
			/* Unknown capability word: skip it (as Linux). */
			if (idx >= howmany(IWM_NUM_UCODE_TLV_CAPA, 32))
				break;
			for (i = 0; i < 32; i++) {
				if ((le32toh(capa->api_capa) & (1 << i)) == 0)
					continue;
				setbit(sc->sc_enabled_capa, i + (32 * idx));
			}
			break;
		}

		case IWM_UCODE_TLV_CMD_VERSIONS: {
			/*
			 * Truncate a copy: tlv_len must stay intact to find
			 * the next TLV (OpenBSD shrinks it in place).
			 */
			size_t cmd_len = tlv_len -
			    tlv_len % sizeof (struct iwm_fw_cmd_version);

			if (sc->n_cmd_versions != 0) {
				err = EINVAL;
				goto parse_out;
			}
			if (cmd_len > sizeof (sc->cmd_versions)) {
				err = EINVAL;
				goto parse_out;
			}
			memcpy(&sc->cmd_versions[0], tlv_data, cmd_len);
			sc->n_cmd_versions =
			    cmd_len / sizeof (struct iwm_fw_cmd_version);
			break;
		}

		case IWM_UCODE_TLV_SDIO_ADMA_ADDR:
		case IWM_UCODE_TLV_FW_GSCAN_CAPA:
			/* ignore, not used by current driver */
			break;

		case IWM_UCODE_TLV_SEC_RT_USNIFFER:
			err = iwm_firmware_store_section(sc,
			    IWM_UCODE_TYPE_REGULAR_USNIFFER, tlv_data,
			    tlv_len);
			if (err)
				goto parse_out;
			break;

		case IWM_UCODE_TLV_PAGING:
			if (tlv_len != sizeof (uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			paging_mem_size = le32toh(*(const uint32_t *)tlv_data);

			if (paging_mem_size > IWM_MAX_PAGING_IMAGE_SIZE) {
				dev_err(sc->sc_dip, CE_WARN, "!Driver only "
				    "supports up to %u bytes for paging image "
				    "(%u requested)", IWM_MAX_PAGING_IMAGE_SIZE,
				    paging_mem_size);
				err = EINVAL;
				goto out;
			}
			if (paging_mem_size & (IWM_FW_PAGING_SIZE - 1)) {
				dev_err(sc->sc_dip, CE_WARN,
				    "!Paging: image isn't multiple of %u",
				    IWM_FW_PAGING_SIZE);
				err = EINVAL;
				goto out;
			}

			fw->fw_sects[IWM_UCODE_TYPE_REGULAR].paging_mem_size =
			    paging_mem_size;
			usniffer_img = IWM_UCODE_TYPE_REGULAR_USNIFFER;
			fw->fw_sects[usniffer_img].paging_mem_size =
			    paging_mem_size;
			break;

		case IWM_UCODE_TLV_N_SCAN_CHANNELS:
			if (tlv_len != sizeof (uint32_t)) {
				err = EINVAL;
				goto parse_out;
			}
			sc->sc_capa_n_scan_channels =
			    le32toh(*(uint32_t *)tlv_data);
			if (sc->sc_capa_n_scan_channels >
			    IWM_MAX_SCAN_CHANNELS) {
				err = ERANGE;
				goto parse_out;
			}
			break;

		case IWM_UCODE_TLV_FW_VERSION:
			if (tlv_len != sizeof (uint32_t) * 3) {
				err = EINVAL;
				goto parse_out;
			}

			iwm_fw_version_str(sc->sc_fwver, sizeof (sc->sc_fwver),
			    le32toh(((uint32_t *)tlv_data)[0]),
			    le32toh(((uint32_t *)tlv_data)[1]),
			    le32toh(((uint32_t *)tlv_data)[2]));
			break;

		case IWM_UCODE_TLV_FW_DBG_DEST:
		case IWM_UCODE_TLV_FW_DBG_CONF:
		case IWM_UCODE_TLV_UMAC_DEBUG_ADDRS:
		case IWM_UCODE_TLV_LMAC_DEBUG_ADDRS:
		case IWM_UCODE_TLV_TYPE_DEBUG_INFO:
		case IWM_UCODE_TLV_TYPE_BUFFER_ALLOCATION:
		case IWM_UCODE_TLV_TYPE_HCMD:
		case IWM_UCODE_TLV_TYPE_REGIONS:
		case IWM_UCODE_TLV_TYPE_TRIGGERS:
			break;

		case IWM_UCODE_TLV_HW_TYPE:
			break;

		case IWM_UCODE_TLV_FW_MEM_SEG:
			break;

		/* undocumented TLVs found in iwm-9000-43 image */
		case 0x1000003:
		case 0x1000004:
			break;

		default:
			err = EINVAL;
			goto parse_out;
		}

		/*
		 * Check for size_t overflow and ignore missing padding at
		 * end of firmware file.
		 */
		if (roundup(tlv_len, 4) > len)
			break;

		len -= roundup(tlv_len, 4);
		data += roundup(tlv_len, 4);
	}

	ASSERT(err == 0);

parse_out:
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!firmware parse error %d, "
		    "section type %d", err, tlv_type);
	}

out:
	if (err) {
		fw->fw_status = IWM_FW_STATUS_NONE;
		if (fw->fw_rawdata != NULL)
			iwm_fw_info_free(fw);
	} else
		fw->fw_status = IWM_FW_STATUS_DONE;

	return (err);
}

static uint32_t
iwm_read_prph_unlocked(struct iwm_softc *sc, uint32_t addr)
{
	IWM_WRITE(sc,
	    IWM_HBUS_TARG_PRPH_RADDR, ((addr & 0x000fffff) | (3 << 24)));
	IWM_BARRIER_READ_WRITE(sc);
	return (IWM_READ(sc, IWM_HBUS_TARG_PRPH_RDAT));
}

static uint32_t
iwm_read_prph(struct iwm_softc *sc, uint32_t addr)
{
	iwm_nic_assert_locked(sc);
	return (iwm_read_prph_unlocked(sc, addr));
}

static void
iwm_write_prph_unlocked(struct iwm_softc *sc, uint32_t addr, uint32_t val)
{
	IWM_WRITE(sc,
	    IWM_HBUS_TARG_PRPH_WADDR, ((addr & 0x000fffff) | (3 << 24)));
	IWM_BARRIER_WRITE(sc);
	IWM_WRITE(sc, IWM_HBUS_TARG_PRPH_WDAT, val);
}

static void
iwm_write_prph(struct iwm_softc *sc, uint32_t addr, uint32_t val)
{
	iwm_nic_assert_locked(sc);
	iwm_write_prph_unlocked(sc, addr, val);
}

static int
iwm_read_mem(struct iwm_softc *sc, uint32_t addr, void *buf, int dwords)
{
	int offs, err = 0;
	uint32_t *vals = buf;

	if (iwm_nic_lock(sc)) {
		IWM_WRITE(sc, IWM_HBUS_TARG_MEM_RADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			vals[offs] = IWM_READ(sc, IWM_HBUS_TARG_MEM_RDAT);
		iwm_nic_unlock(sc);
	} else {
		err = EBUSY;
	}
	return (err);
}

static int
iwm_write_mem(struct iwm_softc *sc, uint32_t addr, const void *buf, int dwords)
{
	int offs;
	const uint32_t *vals = buf;

	if (iwm_nic_lock(sc)) {
		IWM_WRITE(sc, IWM_HBUS_TARG_MEM_WADDR, addr);
		/* WADDR auto-increments */
		for (offs = 0; offs < dwords; offs++) {
			uint32_t val = vals ? vals[offs] : 0;
			IWM_WRITE(sc, IWM_HBUS_TARG_MEM_WDAT, val);
		}
		iwm_nic_unlock(sc);
	} else {
		return (EBUSY);
	}
	return (0);
}

static int
iwm_write_mem32(struct iwm_softc *sc, uint32_t addr, uint32_t val)
{
	return (iwm_write_mem(sc, addr, &val, 1));
}

static int
iwm_poll_bit(struct iwm_softc *sc, int reg, uint32_t bits, uint32_t mask,
    int timo)
{
	for (;;) {
		if ((IWM_READ(sc, reg) & mask) == (bits & mask)) {
			return (1);
		}
		if (timo < 10) {
			return (0);
		}
		timo -= 10;
		DELAY(10);
	}
}

static int
iwm_nic_lock(struct iwm_softc *sc)
{
	if (sc->sc_nic_locks > 0) {
		iwm_nic_assert_locked(sc);
		sc->sc_nic_locks++;
		return (1); /* already locked */
	}

	IWM_SETBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000)
		DELAY(2);

	if (iwm_poll_bit(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
	    IWM_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP, 150000)) {
		sc->sc_nic_locks++;
		return (1);
	}

	dev_err(sc->sc_dip, CE_WARN, "!acquiring device failed");
	return (0);
}

static void
iwm_nic_assert_locked(struct iwm_softc *sc)
{
	if (sc->sc_nic_locks <= 0)
		panic("iwm%d: nic locks counter %d",
		    ddi_get_instance(sc->sc_dip), sc->sc_nic_locks);
}

static void
iwm_nic_unlock(struct iwm_softc *sc)
{
	if (sc->sc_nic_locks > 0) {
		if (--sc->sc_nic_locks == 0)
			IWM_CLRBITS(sc, IWM_CSR_GP_CNTRL,
			    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	} else
		dev_err(sc->sc_dip, CE_WARN, "!NIC already unlocked");
}

static int
iwm_set_bits_mask_prph(struct iwm_softc *sc, uint32_t reg, uint32_t bits,
    uint32_t mask)
{
	uint32_t val;

	if (iwm_nic_lock(sc)) {
		val = iwm_read_prph(sc, reg) & mask;
		val |= bits;
		iwm_write_prph(sc, reg, val);
		iwm_nic_unlock(sc);
		return (0);
	}
	return (EBUSY);
}

static int
iwm_set_bits_prph(struct iwm_softc *sc, uint32_t reg, uint32_t bits)
{
	return (iwm_set_bits_mask_prph(sc, reg, bits, ~0));
}

static int
iwm_clear_bits_prph(struct iwm_softc *sc, uint32_t reg, uint32_t bits)
{
	return (iwm_set_bits_mask_prph(sc, reg, 0, ~bits));
}


static const uint8_t iwm_etheranyaddr[ETHER_ADDR_LEN] = { 0 };
static const uint8_t iwm_etherbroadcastaddr[ETHER_ADDR_LEN] =
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/*
 * Sleep on cv for at most usec with sc_mtx held; OpenBSD tsleep_nsec().
 * Returns EWOULDBLOCK on timeout.
 */
static int
iwm_sleep(struct iwm_softc *sc, kcondvar_t *cv, int usec)
{
	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	if (cv_reltimedwait(cv, &sc->sc_mtx, drv_usectohz(usec),
	    TR_CLOCK_TICK) == -1)
		return (EWOULDBLOCK);
	return (0);
}

/*
 * illumos replacement for OpenBSD loadfirmware(9): read /kernel/firmware/iwm/
 * <name> into a kmem buffer that iwm_fw_info_free() releases.
 */
static int
iwm_loadfirmware(struct iwm_softc *sc, const char *name, void **datap,
    size_t *sizep)
{
	firmware_handle_t fh;
	off_t size;
	void *data;
	int err;

	*datap = NULL;
	*sizep = 0;
	if ((err = firmware_open("iwm", name, &fh)) != 0)
		return (err);
	size = firmware_get_size(fh);
	if (size <= 0 || size > IWM_FWMAXSIZE * 2) {
		(void) firmware_close(fh);
		return (EFBIG);
	}
	data = kmem_alloc(size, KM_SLEEP);
	err = firmware_read(fh, 0, data, size);
	(void) firmware_close(fh);
	if (err != 0) {
		kmem_free(data, size);
		return (err);
	}
	*datap = data;
	*sizep = size;
	return (0);
}

/*
 * Allocate a physically contiguous, zeroed, consistent DMA buffer
 * (OpenBSD iwm_dma_contig_alloc).  Addresses are limited to the 36 bits the
 * device can generate.
 */
static int
iwm_dma_contig_alloc(struct iwm_softc *sc, struct iwm_dma_info *dma,
    size_t size, size_t alignment)
{
	ddi_dma_attr_t attr = {
		.dma_attr_version	= DMA_ATTR_V0,
		.dma_attr_addr_lo	= 0,
		.dma_attr_addr_hi	= 0xfffffffffULL,
		.dma_attr_count_max	= 0xfffffffffULL,
		.dma_attr_align		= alignment,
		.dma_attr_burstsizes	= 0x7ff,
		.dma_attr_minxfer	= 1,
		.dma_attr_maxxfer	= 0xfffffffffULL,
		.dma_attr_seg		= 0xfffffffffULL,
		.dma_attr_sgllen	= 1,
		.dma_attr_granular	= 1,
		.dma_attr_flags		= 0,
	};
	int err;

	bzero(dma, sizeof (*dma));
	if (ddi_dma_alloc_handle(sc->sc_dip, &attr, DDI_DMA_SLEEP, NULL,
	    &dma->dma_hdl) != DDI_SUCCESS)
		return (ENOMEM);
	if (ddi_dma_mem_alloc(dma->dma_hdl, size, &iwm_dma_accattr,
	    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL, (caddr_t *)&dma->vaddr,
	    &dma->length, &dma->acc_hdl) != DDI_SUCCESS) {
		err = ENOMEM;
		goto fail;
	}
	bzero(dma->vaddr, dma->length);
	if (ddi_dma_addr_bind_handle(dma->dma_hdl, NULL, dma->vaddr,
	    dma->length, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &dma->cookie, &dma->ncookies) != DDI_DMA_MAPPED) {
		err = ENOMEM;
		goto fail;
	}
	if (dma->ncookies != 1) {
		err = EFAULT;
		goto fail;
	}
	dma->size = size;
	dma->paddr = dma->cookie.dmac_laddress;
	return (0);

fail:	iwm_dma_contig_free(dma);
	return (err);
}

static void
iwm_dma_contig_free(struct iwm_dma_info *dma)
{
	if (dma->ncookies != 0) {
		(void) ddi_dma_unbind_handle(dma->dma_hdl);
		dma->ncookies = 0;
	}
	if (dma->acc_hdl != NULL) {
		ddi_dma_mem_free(&dma->acc_hdl);
		dma->acc_hdl = NULL;
	}
	if (dma->dma_hdl != NULL) {
		ddi_dma_free_handle(&dma->dma_hdl);
		dma->dma_hdl = NULL;
	}
	dma->vaddr = NULL;
	dma->paddr = 0;
}

/* Sync a whole DMA buffer (OpenBSD bus_dmamap_sync). */
static void
iwm_dma_sync(struct iwm_dma_info *dma, uint_t type)
{
	(void) ddi_dma_sync(dma->dma_hdl, 0, 0, type);
}

static int
iwm_alloc_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	size_t size;
	int count, i, err;

	ring->cur = 0;
	count = IWM_RX_RING_COUNT;

	/* Allocate RX descriptors (256-byte aligned). */
	size = count * sizeof (uint32_t);
	err = iwm_dma_contig_alloc(sc, &ring->free_desc_dma, size, 256);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not allocate RX ring DMA memory");
		goto fail;
	}
	ring->desc = ring->free_desc_dma.vaddr;

	/* Allocate RX status area (16-byte aligned). */
	err = iwm_dma_contig_alloc(sc, &ring->stat_dma,
	    sizeof (*ring->stat), 16);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not allocate RX status DMA memory");
		goto fail;
	}
	ring->stat = ring->stat_dma.vaddr;

	/*
	 * Each slot owns one 4 KB buffer for its lifetime; received frames are
	 * copied out (OpenBSD swaps in a fresh mbuf instead).
	 */
	for (i = 0; i < count; i++) {
		err = iwm_dma_contig_alloc(sc, &ring->data[i].dma,
		    IWM_RBUF_SIZE, 256);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!could not allocate RX buffer");
			goto fail;
		}
		err = iwm_rx_addbuf(sc, IWM_RBUF_SIZE, i);
		if (err)
			goto fail;
	}
	iwm_dma_sync(&ring->free_desc_dma, DDI_DMA_SYNC_FORDEV);
	return (0);

fail:	iwm_free_rx_ring(sc, ring);
	return (err);
}

static void
iwm_disable_rx_dma(struct iwm_softc *sc)
{
	int ntries;

	if (iwm_nic_lock(sc)) {
		IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
		for (ntries = 0; ntries < 1000; ntries++) {
			if (IWM_READ(sc, IWM_FH_MEM_RSSR_RX_STATUS_REG) &
			    IWM_FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE)
				break;
			DELAY(10);
		}
		iwm_nic_unlock(sc);
	}
}

static void
iwm_reset_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	_NOTE(ARGUNUSED(sc))
	ring->cur = 0;
	memset(ring->stat, 0, sizeof (*ring->stat));
	iwm_dma_sync(&ring->stat_dma, DDI_DMA_SYNC_FORDEV);
}

static void
iwm_free_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring)
{
	int i;

	_NOTE(ARGUNUSED(sc))
	iwm_dma_contig_free(&ring->free_desc_dma);
	iwm_dma_contig_free(&ring->stat_dma);
	for (i = 0; i < IWM_RX_RING_COUNT; i++)
		iwm_dma_contig_free(&ring->data[i].dma);
}

static int
iwm_alloc_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring, int qid)
{
	bus_addr_t paddr;
	size_t size;
	int i, err;

	ring->qid = qid;
	ring->queued = 0;
	ring->cur = 0;
	ring->tail = 0;

	/* Allocate TX descriptors (256-byte aligned). */
	size = IWM_TX_RING_COUNT * sizeof (struct iwm_tfd);
	err = iwm_dma_contig_alloc(sc, &ring->desc_dma, size, 256);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not allocate TX ring DMA memory");
		goto fail;
	}
	ring->desc = ring->desc_dma.vaddr;

	/*
	 * There is no need to allocate DMA buffers for unused rings.
	 * 7k/8k/9k hardware supports up to 31 Tx rings which is more
	 * than we currently need.
	 *
	 * In DQA mode we use 1 command queue + 4 DQA mgmt/data queues.
	 * The command is queue 0 (sc->txq[0]), and 4 mgmt/data frame queues
	 * are sc->tqx[IWM_DQA_MIN_MGMT_QUEUE + ac], i.e. sc->txq[5:8],
	 * in order to provide one queue per EDCA category.
	 * Tx aggregation requires additional queues, one queue per TID for
	 * which aggregation is enabled. We map TID 0-7 to sc->txq[10:17].
	 *
	 * In non-DQA mode, we use rings 0 through 9 (0-3 are EDCA, 9 is cmd),
	 * and Tx aggregation is not supported.
	 *
	 * Unfortunately, we cannot tell if DQA will be used until the
	 * firmware gets loaded later, so just allocate sufficient rings
	 * in order to satisfy both cases.  Payload buffers are allocated by
	 * iwm_alloc_txbufs() once a ring is actually used.
	 */
	if (qid > IWM_LAST_AGG_TX_QUEUE)
		return (0);

	size = IWM_TX_RING_COUNT * sizeof (struct iwm_device_cmd);
	err = iwm_dma_contig_alloc(sc, &ring->cmd_dma, size, 4);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not allocate cmd DMA memory");
		goto fail;
	}
	ring->cmd = ring->cmd_dma.vaddr;

	paddr = ring->cmd_dma.paddr;
	for (i = 0; i < IWM_TX_RING_COUNT; i++) {
		struct iwm_tx_data *data = &ring->data[i];

		data->cmd_paddr = paddr;
		data->scratch_paddr = paddr + sizeof (struct iwm_cmd_header)
		    + offsetof(struct iwm_tx_cmd, scratch);
		paddr += sizeof (struct iwm_device_cmd);
	}
	ASSERT(paddr == ring->cmd_dma.paddr + size);
	return (0);

fail:	iwm_free_tx_ring(sc, ring);
	return (err);
}

/*
 * Allocate per-slot payload buffers for a Tx ring that is about to be used.
 * The command queue needs room for the largest host command; frame queues
 * need IWM_TXBUF_SIZE.  Buffers are packed so that none crosses a page.
 * Thread context, no locks held.  Idempotent.
 */
static int
iwm_alloc_txbufs(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	size_t slotsz, perpage, npages, i;
	int err;

	if (ring->buf_dma != NULL)
		return (0);
	if (ring->cmd == NULL)
		return (EINVAL);
	if (ring->qid == sc->cmdqid) {
		slotsz = PAGESIZE;
		perpage = 1;
	} else {
		slotsz = IWM_TXBUF_SIZE;
		perpage = IWM_TXBUF_PER_PAGE;
	}
	npages = IWM_TX_RING_COUNT / perpage;
	ring->buf_dma = kmem_zalloc(npages * sizeof (struct iwm_dma_info),
	    KM_SLEEP);
	ring->buf_npages = npages;
	for (i = 0; i < npages; i++) {
		size_t j;

		err = iwm_dma_contig_alloc(sc, &ring->buf_dma[i], PAGESIZE,
		    PAGESIZE);
		if (err != 0) {
			iwm_free_txbufs(sc, ring);
			return (err);
		}
		for (j = 0; j < perpage; j++) {
			struct iwm_tx_data *data =
			    &ring->data[i * perpage + j];

			data->buf = (caddr_t)ring->buf_dma[i].vaddr +
			    j * slotsz;
			data->buf_paddr = ring->buf_dma[i].paddr + j * slotsz;
			data->buf_page = i;
		}
	}
	return (0);
}

static void
iwm_free_txbufs(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	size_t npages, i;

	_NOTE(ARGUNUSED(sc))
	if (ring->buf_dma == NULL)
		return;
	npages = ring->buf_npages;
	for (i = 0; i < npages; i++)
		iwm_dma_contig_free(&ring->buf_dma[i]);
	kmem_free(ring->buf_dma, npages * sizeof (struct iwm_dma_info));
	ring->buf_dma = NULL;
	for (i = 0; i < IWM_TX_RING_COUNT; i++) {
		ring->data[i].buf = NULL;
		ring->data[i].buf_paddr = 0;
	}
}

/* Release the frame and node a Tx slot holds (OpenBSD m_freem + unref). */
static void
iwm_txd_release(struct iwm_softc *sc, struct iwm_tx_data *txd)
{
	if (txd->m != NULL) {
		freemsg(txd->m);
		txd->m = NULL;
	}
	if (txd->in != NULL) {
		ieee80211_free_node(&txd->in->in_ni);
		txd->in = NULL;
	}
	_NOTE(ARGUNUSED(sc))
}

static void
iwm_reset_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	int i;

	for (i = 0; i < IWM_TX_RING_COUNT; i++)
		iwm_txd_release(sc, &ring->data[i]);
	/* Clear TX descriptors. */
	memset(ring->desc, 0, ring->desc_dma.size);
	iwm_dma_sync(&ring->desc_dma, DDI_DMA_SYNC_FORDEV);
	sc->qfullmsk &= ~(1 << ring->qid);
	sc->qenablemsk &= ~(1 << ring->qid);
	/* 7000 family NICs are locked while commands are in progress. */
	if (ring->qid == sc->cmdqid && ring->queued > 0) {
		if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
			iwm_nic_unlock(sc);
	}
	ring->queued = 0;
	ring->cur = 0;
	ring->tail = 0;
}

static void
iwm_free_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	int i;

	for (i = 0; i < IWM_TX_RING_COUNT; i++)
		iwm_txd_release(sc, &ring->data[i]);
	iwm_free_txbufs(sc, ring);
	iwm_dma_contig_free(&ring->desc_dma);
	iwm_dma_contig_free(&ring->cmd_dma);
	ring->desc = NULL;
	ring->cmd = NULL;
}
static void
iwm_enable_rfkill_int(struct iwm_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWM_CSR_INT_BIT_RF_KILL;
		IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
	} else {
		IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
		    sc->sc_fh_init_mask);
		IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
		    ~IWM_MSIX_HW_INT_CAUSES_REG_RF_KILL);
		sc->sc_hw_mask = IWM_MSIX_HW_INT_CAUSES_REG_RF_KILL;
	}

	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_9000)
		IWM_SETBITS(sc, IWM_CSR_GP_CNTRL,
		    IWM_CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN);
}

static int
iwm_check_rfkill(struct iwm_softc *sc)
{
	uint32_t v;
	int rv;

	/*
	 * "documentation" is not really helpful here:
	 *  27:	HW_RF_KILL_SW
	 *	Indicates state of (platform's) hardware RF-Kill switch
	 *
	 * But apparently when it's off, it's on ...
	 */
	v = IWM_READ(sc, IWM_CSR_GP_CNTRL);
	rv = (v & IWM_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW) == 0;
	if (rv) {
		sc->sc_flags |= IWM_FLAG_RFKILL;
	} else {
		sc->sc_flags &= ~IWM_FLAG_RFKILL;
	}

	return (rv);
}

static void
iwm_enable_interrupts(struct iwm_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWM_CSR_INI_SET_MASK;
		IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
	} else {
		/*
		 * fh/hw_mask keeps all the unmasked causes.
		 * Unlike msi, in msix cause is enabled when it is unset.
		 */
		sc->sc_hw_mask = sc->sc_hw_init_mask;
		sc->sc_fh_mask = sc->sc_fh_init_mask;
		IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
		    ~sc->sc_fh_mask);
		IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
		    ~sc->sc_hw_mask);
	}
}

static void
iwm_enable_fwload_interrupt(struct iwm_softc *sc)
{
	if (!sc->sc_msix) {
		sc->sc_intmask = IWM_CSR_INT_BIT_FH_TX;
		IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
	} else {
		IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
		    sc->sc_hw_init_mask);
		IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
		    ~IWM_MSIX_FH_INT_CAUSES_D2S_CH0_NUM);
		sc->sc_fh_mask = IWM_MSIX_FH_INT_CAUSES_D2S_CH0_NUM;
	}
}

static void
iwm_restore_interrupts(struct iwm_softc *sc)
{
	IWM_WRITE(sc, IWM_CSR_INT_MASK, sc->sc_intmask);
}

static void
iwm_disable_interrupts(struct iwm_softc *sc)
{
	if (!sc->sc_msix) {
		IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);

		/* acknowledge all interrupts */
		IWM_WRITE(sc, IWM_CSR_INT, ~0);
		IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, ~0);
	} else {
		IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
		    sc->sc_fh_init_mask);
		IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
		    sc->sc_hw_init_mask);
	}
}

static void
iwm_ict_reset(struct iwm_softc *sc)
{
	iwm_disable_interrupts(sc);

	memset(sc->ict_dma.vaddr, 0, IWM_ICT_SIZE);
	iwm_dma_sync(&sc->ict_dma, DDI_DMA_SYNC_FORDEV);
	sc->ict_cur = 0;

	/* Set physical address of ICT (4KB aligned). */
	IWM_WRITE(sc, IWM_CSR_DRAM_INT_TBL_REG,
	    IWM_CSR_DRAM_INT_TBL_ENABLE
	    | IWM_CSR_DRAM_INIT_TBL_WRAP_CHECK
	    | IWM_CSR_DRAM_INIT_TBL_WRITE_POINTER
	    | sc->ict_dma.paddr >> IWM_ICT_PADDR_SHIFT);

	/* Switch to ICT interrupt mode in driver. */
	sc->sc_flags |= IWM_FLAG_USE_ICT;

	IWM_WRITE(sc, IWM_CSR_INT, ~0);
	iwm_enable_interrupts(sc);
}

#define	IWM_HW_READY_TIMEOUT 50
static int
iwm_set_hw_ready(struct iwm_softc *sc)
{
	int ready;

	IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	ready = iwm_poll_bit(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
	    IWM_HW_READY_TIMEOUT);
	if (ready)
		IWM_SETBITS(sc, IWM_CSR_MBOX_SET_REG,
		    IWM_CSR_MBOX_SET_REG_OS_ALIVE);

	return (ready);
}
#undef IWM_HW_READY_TIMEOUT

static int
iwm_prepare_card_hw(struct iwm_softc *sc)
{
	int t = 0;
	int ntries;

	if (iwm_set_hw_ready(sc))
		return (0);

	IWM_SETBITS(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	DELAY(1000);

	for (ntries = 0; ntries < 10; ntries++) {
		/* If HW is not ready, prepare the conditions to check again */
		IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
		    IWM_CSR_HW_IF_CONFIG_REG_PREPARE);

		do {
			if (iwm_set_hw_ready(sc))
				return (0);
			DELAY(200);
			t += 200;
		} while (t < 150000);
		DELAY(25000);
	}

	return (ETIMEDOUT);
}

static void
iwm_apm_config(struct iwm_softc *sc)
{
	uint16_t lctl, cap;

	/*
	 * HW bug W/A for instability in PCIe bus L0S->L1 transition.
	 * Check if BIOS (or OS) enabled L1-ASPM on this device.
	 * If so (likely), disable L0S, so device moves directly L0->L1;
	 *    costs negligible amount of power savings.
	 * If not (unlikely), enable L0S, so there is at least some
	 *    power savings, even without L1.
	 */
	lctl = pci_config_get16(sc->sc_pcih, sc->sc_cap_off + PCIE_LINKCTL);
	if (lctl & PCIE_LINKCTL_ASPM_CTL_L1) {
		IWM_SETBITS(sc, IWM_CSR_GIO_REG,
		    IWM_CSR_GIO_REG_VAL_L0S_ENABLED);
	} else {
		IWM_CLRBITS(sc, IWM_CSR_GIO_REG,
		    IWM_CSR_GIO_REG_VAL_L0S_ENABLED);
	}

	cap = pci_config_get16(sc->sc_pcih, sc->sc_cap_off + PCIE_DEVCTL2);
	sc->sc_ltr_enabled = (cap & PCIE_DEVCTL2_LTR_MECH_EN) ? 1 : 0;
}

/*
 * Start up NIC's basic functionality after it has been reset
 * e.g. after platform boot or shutdown.
 * NOTE:  This does not load uCode nor start the embedded processor
 */
static int
iwm_apm_init(struct iwm_softc *sc)
{
	int err = 0;

	/* Disable L0S exit timer (platform NMI workaround) */
	if (sc->sc_device_family < IWM_DEVICE_FAMILY_8000)
		IWM_SETBITS(sc, IWM_CSR_GIO_CHICKEN_BITS,
		    IWM_CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/*
	 * Disable L0s without affecting L1;
	 *  don't wait for ICH L0s (ICH bug W/A)
	 */
	IWM_SETBITS(sc, IWM_CSR_GIO_CHICKEN_BITS,
	    IWM_CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	IWM_SETBITS(sc, IWM_CSR_DBG_HPET_MEM_REG, IWM_CSR_DBG_HPET_MEM_REG_VAL);

	/*
	 * Enable HAP INTA (interrupt from management bus) to
	 * wake device's PCI Express link L1a -> L0s
	 */
	IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	iwm_apm_config(sc);

#if 0 /* not for 7k/8k */
	/* Configure analog phase-lock-loop before activating to D0A */
	if (trans->cfg->base_params->pll_cfg_val)
		IWM_SETBITS(trans, IWM_CSR_ANA_PLL_CFG,
		    trans->cfg->base_params->pll_cfg_val);
#endif

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	IWM_SETBITS(sc, IWM_CSR_GP_CNTRL, IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. iwm_write_prph()
	 * and accesses to uCode SRAM.
	 */
	if (!iwm_poll_bit(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000)) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!timeout waiting for clock stabilization");
		err = ETIMEDOUT;
		goto out;
	}

	if (sc->host_interrupt_operation_mode) {
		/*
		 * This is a bit of an abuse - This is needed for 7260 / 3160
		 * only check host_interrupt_operation_mode even if this is
		 * not related to host_interrupt_operation_mode.
		 *
		 * Enable the oscillator to count wake up time for L1 exit. This
		 * consumes slightly more power (100uA) - but allows to be sure
		 * that we wake up from L1 on time.
		 *
		 * This looks weird: read twice the same register, discard the
		 * value, set a bit, and yet again, read that same register
		 * just to discard the value. But that's the way the hardware
		 * seems to like it.
		 */
		if (iwm_nic_lock(sc)) {
			iwm_read_prph(sc, IWM_OSC_CLK);
			iwm_read_prph(sc, IWM_OSC_CLK);
			iwm_nic_unlock(sc);
		}
		err = iwm_set_bits_prph(sc, IWM_OSC_CLK,
		    IWM_OSC_CLK_FORCE_CONTROL);
		if (err)
			goto out;
		if (iwm_nic_lock(sc)) {
			iwm_read_prph(sc, IWM_OSC_CLK);
			iwm_read_prph(sc, IWM_OSC_CLK);
			iwm_nic_unlock(sc);
		}
	}

	/*
	 * Enable DMA clock and wait for it to stabilize.
	 *
	 * Write to "CLK_EN_REG"; "1" bits enable clocks, while "0" bits
	 * do not disable clocks.  This preserves any hardware bits already
	 * set by default in "CLK_CTRL_REG" after reset.
	 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (iwm_nic_lock(sc)) {
			iwm_write_prph(sc, IWM_APMG_CLK_EN_REG,
			    IWM_APMG_CLK_VAL_DMA_CLK_RQT);
			iwm_nic_unlock(sc);
		}
		DELAY(20);

		/* Disable L1-Active */
		err = iwm_set_bits_prph(sc, IWM_APMG_PCIDEV_STT_REG,
		    IWM_APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
		if (err)
			goto out;

		/* Clear the interrupt in APMG if the NIC is in RFKILL */
		if (iwm_nic_lock(sc)) {
			iwm_write_prph(sc, IWM_APMG_RTC_INT_STT_REG,
			    IWM_APMG_RTC_INT_STT_RFKILL);
			iwm_nic_unlock(sc);
		}
	}
out:
	if (err)
		dev_err(sc->sc_dip, CE_WARN, "!apm init error %d", err);
	return (err);
}

static void
iwm_apm_stop(struct iwm_softc *sc)
{
	IWM_SETBITS(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	IWM_SETBITS(sc, IWM_CSR_HW_IF_CONFIG_REG,
	    IWM_CSR_HW_IF_CONFIG_REG_PREPARE |
	    IWM_CSR_HW_IF_CONFIG_REG_ENABLE_PME);
	DELAY(1000);
	IWM_CLRBITS(sc, IWM_CSR_DBG_LINK_PWR_MGMT_REG,
	    IWM_CSR_RESET_LINK_PWR_MGMT_DISABLED);
	DELAY(5000);

	/* stop device's busmaster DMA activity */
	IWM_SETBITS(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_STOP_MASTER);

	if (!iwm_poll_bit(sc, IWM_CSR_RESET,
	    IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED,
	    IWM_CSR_RESET_REG_FLAG_MASTER_DISABLED, 100))
		dev_err(sc->sc_dip, CE_WARN, "!timeout waiting for master");

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	IWM_CLRBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}

static void
iwm_init_msix_hw(struct iwm_softc *sc)
{
	iwm_conf_msix_hw(sc, 0);

	if (!sc->sc_msix)
		return;

	sc->sc_fh_init_mask = ~IWM_READ(sc, IWM_CSR_MSIX_FH_INT_MASK_AD);
	sc->sc_fh_mask = sc->sc_fh_init_mask;
	sc->sc_hw_init_mask = ~IWM_READ(sc, IWM_CSR_MSIX_HW_INT_MASK_AD);
	sc->sc_hw_mask = sc->sc_hw_init_mask;
}

static void
iwm_conf_msix_hw(struct iwm_softc *sc, int stopped)
{
	int vector = 0;

	if (!sc->sc_msix) {
		/* Newer chips default to MSIX. */
		if (sc->sc_mqrx_supported && !stopped && iwm_nic_lock(sc)) {
			iwm_write_prph(sc, IWM_UREG_CHICK,
			    IWM_UREG_CHICK_MSI_ENABLE);
			iwm_nic_unlock(sc);
		}
		return;
	}

	if (!stopped && iwm_nic_lock(sc)) {
		iwm_write_prph(sc, IWM_UREG_CHICK, IWM_UREG_CHICK_MSIX_ENABLE);
		iwm_nic_unlock(sc);
	}

	/* Disable all interrupts */
	IWM_WRITE(sc, IWM_CSR_MSIX_FH_INT_MASK_AD, ~0);
	IWM_WRITE(sc, IWM_CSR_MSIX_HW_INT_MASK_AD, ~0);

	/* Map fallback-queue (command/mgmt) to a single vector */
	IWM_WRITE_1(sc, IWM_CSR_MSIX_RX_IVAR(0),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	/* Map RSS queue (data) to the same vector */
	IWM_WRITE_1(sc, IWM_CSR_MSIX_RX_IVAR(1),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);

	/* Enable the RX queues cause interrupts */
	IWM_CLRBITS(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
	    IWM_MSIX_FH_INT_CAUSES_Q0 | IWM_MSIX_FH_INT_CAUSES_Q1);

	/* Map non-RX causes to the same vector */
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_D2S_CH0_NUM),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_D2S_CH1_NUM),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_S2D),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_FH_ERR),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_ALIVE),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_WAKEUP),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_IML),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_CT_KILL),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_RF_KILL),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_PERIODIC),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_SW_ERR),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_SCD),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_FH_TX),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_HW_ERR),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);
	IWM_WRITE_1(sc, IWM_CSR_MSIX_IVAR(IWM_MSIX_IVAR_CAUSE_REG_HAP),
	    vector | IWM_MSIX_NON_AUTO_CLEAR_CAUSE);

	/* Enable non-RX causes interrupts */
	IWM_CLRBITS(sc, IWM_CSR_MSIX_FH_INT_MASK_AD,
	    IWM_MSIX_FH_INT_CAUSES_D2S_CH0_NUM |
	    IWM_MSIX_FH_INT_CAUSES_D2S_CH1_NUM |
	    IWM_MSIX_FH_INT_CAUSES_S2D |
	    IWM_MSIX_FH_INT_CAUSES_FH_ERR);
	IWM_CLRBITS(sc, IWM_CSR_MSIX_HW_INT_MASK_AD,
	    IWM_MSIX_HW_INT_CAUSES_REG_ALIVE |
	    IWM_MSIX_HW_INT_CAUSES_REG_WAKEUP |
	    IWM_MSIX_HW_INT_CAUSES_REG_IML |
	    IWM_MSIX_HW_INT_CAUSES_REG_CT_KILL |
	    IWM_MSIX_HW_INT_CAUSES_REG_RF_KILL |
	    IWM_MSIX_HW_INT_CAUSES_REG_PERIODIC |
	    IWM_MSIX_HW_INT_CAUSES_REG_SW_ERR |
	    IWM_MSIX_HW_INT_CAUSES_REG_SCD |
	    IWM_MSIX_HW_INT_CAUSES_REG_FH_TX |
	    IWM_MSIX_HW_INT_CAUSES_REG_HW_ERR |
	    IWM_MSIX_HW_INT_CAUSES_REG_HAP);
}

static int
iwm_clear_persistence_bit(struct iwm_softc *sc)
{
	uint32_t hpm, wprot;

	hpm = iwm_read_prph_unlocked(sc, IWM_HPM_DEBUG);
	if (hpm != 0xa5a5a5a0 && (hpm & IWM_HPM_PERSISTENCE_BIT)) {
		wprot = iwm_read_prph_unlocked(sc, IWM_PREG_PRPH_WPROT_9000);
		if (wprot & IWM_PREG_WFPM_ACCESS) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!cannot clear persistence bit");
			return (EPERM);
		}
		iwm_write_prph_unlocked(sc, IWM_HPM_DEBUG,
		    hpm & ~IWM_HPM_PERSISTENCE_BIT);
	}

	return (0);
}

static int
iwm_start_hw(struct iwm_softc *sc)
{
	int err;

	err = iwm_prepare_card_hw(sc);
	if (err)
		return (err);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_9000) {
		err = iwm_clear_persistence_bit(sc);
		if (err)
			return (err);
	}

	/* Reset the entire device */
	IWM_WRITE(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);
	DELAY(5000);

	err = iwm_apm_init(sc);
	if (err)
		return (err);

	iwm_init_msix_hw(sc);

	iwm_enable_rfkill_int(sc);
	iwm_check_rfkill(sc);

	return (0);
}


static void
iwm_stop_device(struct iwm_softc *sc)
{
	int chnl, ntries;
	int qid;

	iwm_disable_interrupts(sc);
	sc->sc_flags &= ~IWM_FLAG_USE_ICT;

	/* Stop all DMA channels. */
	if (iwm_nic_lock(sc)) {
		/* Deactivate TX scheduler. */
		iwm_write_prph(sc, IWM_SCD_TXFACT, 0);

		for (chnl = 0; chnl < IWM_FH_TCSR_CHNL_NUM; chnl++) {
			IWM_WRITE(sc,
			    IWM_FH_TCSR_CHNL_TX_CONFIG_REG(chnl), 0);
			for (ntries = 0; ntries < 200; ntries++) {
				uint32_t r;

				r = IWM_READ(sc, IWM_FH_TSSR_TX_STATUS_REG);
				if (r & IWM_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(
				    chnl))
					break;
				DELAY(20);
			}
		}
		iwm_nic_unlock(sc);
	}
	iwm_disable_rx_dma(sc);

	iwm_reset_rx_ring(sc, &sc->rxq);

	for (qid = 0; qid < (int)nitems(sc->txq); qid++)
		iwm_reset_tx_ring(sc, &sc->txq[qid]);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (iwm_nic_lock(sc)) {
			/* Power-down device's busmaster DMA clocks */
			iwm_write_prph(sc, IWM_APMG_CLK_DIS_REG,
			    IWM_APMG_CLK_VAL_DMA_CLK_RQT);
			iwm_nic_unlock(sc);
		}
		DELAY(5);
	}

	/* Make sure (redundant) we've released our request to stay awake */
	IWM_CLRBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	if (sc->sc_nic_locks > 0)
		dev_err(sc->sc_dip, CE_WARN,
		    "!%d active NIC locks forcefully cleared",
		    sc->sc_nic_locks);
	sc->sc_nic_locks = 0;

	/* Stop the device, and put it in low power state */
	iwm_apm_stop(sc);

	/* Reset the on-board processor. */
	IWM_WRITE(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);
	DELAY(5000);

	/*
	 * Upon stop, the IVAR table gets erased, so msi-x won't
	 * work. This causes a bug in RF-KILL flows, since the interrupt
	 * that enables radio won't fire on the correct irq, and the
	 * driver won't be able to handle the interrupt.
	 * Configure the IVAR table again after reset.
	 */
	iwm_conf_msix_hw(sc, 1);

	/*
	 * Upon stop, the APM issues an interrupt if HW RF kill is set.
	 * Clear the interrupt again.
	 */
	iwm_disable_interrupts(sc);

	/* Even though we stop the HW we still want the RF kill interrupt. */
	iwm_enable_rfkill_int(sc);
	iwm_check_rfkill(sc);

	iwm_prepare_card_hw(sc);
}

static void
iwm_nic_config(struct iwm_softc *sc)
{
	uint8_t radio_cfg_type, radio_cfg_step, radio_cfg_dash;
	uint32_t mask, val, reg_val = 0;

	radio_cfg_type = (sc->sc_fw_phy_config & IWM_FW_PHY_CFG_RADIO_TYPE) >>
	    IWM_FW_PHY_CFG_RADIO_TYPE_POS;
	radio_cfg_step = (sc->sc_fw_phy_config & IWM_FW_PHY_CFG_RADIO_STEP) >>
	    IWM_FW_PHY_CFG_RADIO_STEP_POS;
	radio_cfg_dash = (sc->sc_fw_phy_config & IWM_FW_PHY_CFG_RADIO_DASH) >>
	    IWM_FW_PHY_CFG_RADIO_DASH_POS;

	reg_val |= IWM_CSR_HW_REV_STEP(sc->sc_hw_rev) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_STEP;
	reg_val |= IWM_CSR_HW_REV_DASH(sc->sc_hw_rev) <<
	    IWM_CSR_HW_IF_CONFIG_REG_POS_MAC_DASH;

	/* radio configuration */
	reg_val |= radio_cfg_type << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE;
	reg_val |= radio_cfg_step << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_STEP;
	reg_val |= radio_cfg_dash << IWM_CSR_HW_IF_CONFIG_REG_POS_PHY_DASH;

	mask = IWM_CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH |
	    IWM_CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE |
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI |
	    IWM_CSR_HW_IF_CONFIG_REG_BIT_MAC_SI;

	val = IWM_READ(sc, IWM_CSR_HW_IF_CONFIG_REG);
	val &= ~mask;
	val |= reg_val;
	IWM_WRITE(sc, IWM_CSR_HW_IF_CONFIG_REG, val);

	/*
	 * W/A : NIC is stuck in a reset state after Early PCIe power off
	 * (PCIe power is lost before PERST# is asserted), causing ME FW
	 * to lose ownership and not being able to obtain it back.
	 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
		iwm_set_bits_mask_prph(sc, IWM_APMG_PS_CTRL_REG,
		    IWM_APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS,
		    ~IWM_APMG_PS_CTRL_EARLY_PWR_OFF_RESET_DIS);
}

static int
iwm_nic_rx_init(struct iwm_softc *sc)
{
	/* The 9000-family multi-queue Rx path is not carried. */
	return (iwm_nic_rx_legacy_init(sc));
}

static int
iwm_nic_rx_legacy_init(struct iwm_softc *sc)
{
	memset(sc->rxq.stat, 0, sizeof (*sc->rxq.stat));
	iwm_dma_sync(&sc->rxq.stat_dma, DDI_DMA_SYNC_FORDEV);

	iwm_disable_rx_dma(sc);

	if (!iwm_nic_lock(sc))
		return (EBUSY);

	/* reset and flush pointers */
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_RBDCB_WPTR, 0);
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_FLUSH_RB_REQ, 0);
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_RDPTR, 0);
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

	/* Set physical address of RX ring (256-byte aligned). */
	IWM_WRITE(sc,
	    IWM_FH_RSCSR_CHNL0_RBDCB_BASE_REG,
	    sc->rxq.free_desc_dma.paddr >> 8);

	/* Set physical address of RX status (16-byte aligned). */
	IWM_WRITE(sc,
	    IWM_FH_RSCSR_CHNL0_STTS_WPTR_REG, sc->rxq.stat_dma.paddr >> 4);

	/* Enable RX. */
	IWM_WRITE(sc, IWM_FH_MEM_RCSR_CHNL0_CONFIG_REG,
	    IWM_FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL		|
	    IWM_FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY		|  /* HW bug */
	    IWM_FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL	|
	    (IWM_RX_RB_TIMEOUT << IWM_FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS) |
	    IWM_FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K		|
	    IWM_RX_QUEUE_SIZE_LOG << IWM_FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS);

	IWM_WRITE_1(sc, IWM_CSR_INT_COALESCING, IWM_HOST_INT_TIMEOUT_DEF);

	/* W/A for interrupt coalescing bug in 7260 and 3160 */
	if (sc->host_interrupt_operation_mode)
		IWM_SETBITS(sc, IWM_CSR_INT_COALESCING, IWM_HOST_INT_OPER_MODE);

	iwm_nic_unlock(sc);

	/*
	 * This value should initially be 0 (before preparing any RBs),
	 * and should be 8 after preparing the first 8 RBs (for example).
	 */
	IWM_WRITE(sc, IWM_FH_RSCSR_CHNL0_WPTR, 8);

	return (0);
}

static int
iwm_nic_tx_init(struct iwm_softc *sc)
{
	int qid, err;

	if (!iwm_nic_lock(sc))
		return (EBUSY);

	/* Deactivate TX scheduler. */
	iwm_write_prph(sc, IWM_SCD_TXFACT, 0);

	/* Set physical address of "keep warm" page (16-byte aligned). */
	IWM_WRITE(sc, IWM_FH_KW_MEM_ADDR_REG, sc->kw_dma.paddr >> 4);

	for (qid = 0; qid < (int)nitems(sc->txq); qid++) {
		struct iwm_tx_ring *txq = &sc->txq[qid];

		/* Set physical address of TX ring (256-byte aligned). */
		IWM_WRITE(sc, IWM_FH_MEM_CBBC_QUEUE(qid),
		    txq->desc_dma.paddr >> 8);
	}

	err = iwm_set_bits_prph(sc, IWM_SCD_GP_CTRL,
	    IWM_SCD_GP_CTRL_AUTO_ACTIVE_MODE |
	    IWM_SCD_GP_CTRL_ENABLE_31_QUEUES);

	iwm_nic_unlock(sc);

	return (err);
}

static int
iwm_nic_init(struct iwm_softc *sc)
{
	int err;

	iwm_apm_init(sc);
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
		iwm_set_bits_mask_prph(sc, IWM_APMG_PS_CTRL_REG,
		    IWM_APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
		    ~IWM_APMG_PS_CTRL_MSK_PWR_SRC);

	iwm_nic_config(sc);

	err = iwm_nic_rx_init(sc);
	if (err)
		return (err);

	err = iwm_nic_tx_init(sc);
	if (err)
		return (err);

	IWM_SETBITS(sc, IWM_CSR_MAC_SHADOW_REG_CTRL, 0x800fffff);

	return (0);
}

/* Map a TID to an ieee80211_edca_ac category. */
const uint8_t iwm_tid_to_ac[IWM_MAX_TID_COUNT] = {
	EDCA_AC_BE,
	EDCA_AC_BK,
	EDCA_AC_BK,
	EDCA_AC_BE,
	EDCA_AC_VI,
	EDCA_AC_VI,
	EDCA_AC_VO,
	EDCA_AC_VO,
};

/* Map ieee80211_edca_ac categories to firmware Tx FIFO. */
const uint8_t iwm_ac_to_tx_fifo[] = {
	IWM_TX_FIFO_BE,
	IWM_TX_FIFO_BK,
	IWM_TX_FIFO_VI,
	IWM_TX_FIFO_VO,
};

static int
iwm_enable_ac_txq(struct iwm_softc *sc, int qid, int fifo)
{
	int err;
	iwm_nic_assert_locked(sc);

	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, qid << 8 | 0);

	iwm_write_prph(sc, IWM_SCD_QUEUE_STATUS_BITS(qid),
	    (0 << IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE)
	    | (1 << IWM_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));

	err = iwm_clear_bits_prph(sc, IWM_SCD_AGGR_SEL, (1 << qid));
	if (err) {
		return (err);
	}

	iwm_write_prph(sc, IWM_SCD_QUEUE_RDPTR(qid), 0);

	iwm_write_mem32(sc,
	    sc->sched_base + IWM_SCD_CONTEXT_QUEUE_OFFSET(qid), 0);

	/* Set scheduler window size and frame limit. */
	iwm_write_mem32(sc,
	    sc->sched_base + IWM_SCD_CONTEXT_QUEUE_OFFSET(qid) +
	    sizeof (uint32_t),
	    ((IWM_FRAME_LIMIT << IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
	    IWM_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
	    ((IWM_FRAME_LIMIT
		<< IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
	    IWM_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));

	iwm_write_prph(sc, IWM_SCD_QUEUE_STATUS_BITS(qid),
	    (1 << IWM_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
	    (fifo << IWM_SCD_QUEUE_STTS_REG_POS_TXF) |
	    (1 << IWM_SCD_QUEUE_STTS_REG_POS_WSL) |
	    IWM_SCD_QUEUE_STTS_REG_MSK);

	if (qid == sc->cmdqid)
		iwm_write_prph(sc, IWM_SCD_EN_CTRL,
		    iwm_read_prph(sc, IWM_SCD_EN_CTRL) | (1 << qid));

	return (0);
}

static int
iwm_enable_txq(struct iwm_softc *sc, int sta_id, int qid, int fifo,
    int aggregate, uint8_t tid, uint16_t ssn)
{
	struct iwm_tx_ring *ring = &sc->txq[qid];
	struct iwm_scd_txq_cfg_cmd cmd;
	int err, idx, scd_bug;

	iwm_nic_assert_locked(sc);

	/*
	 * If we need to move the SCD write pointer by steps of
	 * 0x40, 0x80 or 0xc0, it gets stuck.
	 * This is really ugly, but this is the easiest way out for
	 * this sad hardware issue.
	 * This bug has been fixed on devices 9000 and up.
	 */
	scd_bug = !sc->sc_mqrx_supported &&
		!((ssn - ring->cur) & 0x3f) &&
		(ssn != ring->cur);
	if (scd_bug)
		ssn = (ssn + 1) & 0xfff;

	idx = IWM_AGG_SSN_TO_TXQ_IDX(ssn);
	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, qid << 8 | idx);
	ring->cur = idx;
	ring->tail = idx;

	memset(&cmd, 0, sizeof (cmd));
	cmd.tid = tid;
	cmd.scd_queue = qid;
	cmd.enable = 1;
	cmd.sta_id = sta_id;
	cmd.tx_fifo = fifo;
	cmd.aggregate = aggregate;
	cmd.ssn = htole16(ssn);
	cmd.window = IWM_FRAME_LIMIT;

	err = iwm_send_cmd_pdu(sc, IWM_SCD_QUEUE_CFG, 0,
	    sizeof (cmd), &cmd);
	if (err)
		return (err);

	sc->qenablemsk |= (1 << qid);
	return (0);
}

static int
iwm_disable_txq(struct iwm_softc *sc, int sta_id, int qid, uint8_t tid)
{
	struct iwm_scd_txq_cfg_cmd cmd;
	int err;

	memset(&cmd, 0, sizeof (cmd));
	cmd.tid = tid;
	cmd.scd_queue = qid;
	cmd.enable = 0;
	cmd.sta_id = sta_id;

	err = iwm_send_cmd_pdu(sc, IWM_SCD_QUEUE_CFG, 0, sizeof (cmd), &cmd);
	if (err)
		return (err);

	sc->qenablemsk &= ~(1 << qid);
	return (0);
}

static int
iwm_post_alive(struct iwm_softc *sc)
{
	int nwords;
	int err, chnl;

	if (!iwm_nic_lock(sc))
		return (EBUSY);

	(void) iwm_read_prph(sc, IWM_SCD_SRAM_BASE_ADDR);

	iwm_ict_reset(sc);

	iwm_nic_unlock(sc);

	/* Clear TX scheduler state in SRAM. */
	nwords = (IWM_SCD_TRANS_TBL_MEM_UPPER_BOUND -
	    IWM_SCD_CONTEXT_MEM_LOWER_BOUND)
	    / sizeof (uint32_t);
	err = iwm_write_mem(sc,
	    sc->sched_base + IWM_SCD_CONTEXT_MEM_LOWER_BOUND,
	    NULL, nwords);
	if (err)
		return (err);

	if (!iwm_nic_lock(sc))
		return (EBUSY);

	/* Set physical address of TX scheduler rings (1KB aligned). */
	iwm_write_prph(sc, IWM_SCD_DRAM_BASE_ADDR, sc->sched_dma.paddr >> 10);

	iwm_write_prph(sc, IWM_SCD_CHAINEXT_EN, 0);

	/* enable command channel */
	err = iwm_enable_ac_txq(sc, sc->cmdqid, IWM_TX_FIFO_CMD);
	if (err) {
		iwm_nic_unlock(sc);
		return (err);
	}

	/* Activate TX scheduler. */
	iwm_write_prph(sc, IWM_SCD_TXFACT, 0xff);

	/* Enable DMA channels. */
	for (chnl = 0; chnl < IWM_FH_TCSR_CHNL_NUM; chnl++) {
		IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(chnl),
		    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
		    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);
	}

	IWM_SETBITS(sc, IWM_FH_TX_CHICKEN_BITS_REG,
	    IWM_FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	iwm_nic_unlock(sc);

	/* Enable L1-Active */
	if (sc->sc_device_family < IWM_DEVICE_FAMILY_8000) {
		err = iwm_clear_bits_prph(sc, IWM_APMG_PCIDEV_STT_REG,
		    IWM_APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
	}

	return (err);
}

static struct iwm_phy_db_entry *
iwm_phy_db_get_section(struct iwm_softc *sc, uint16_t type, uint16_t chg_id)
{
	struct iwm_phy_db *phy_db = &sc->sc_phy_db;

	if (type >= IWM_PHY_DB_MAX)
		return (NULL);

	switch (type) {
	case IWM_PHY_DB_CFG:
		return (&phy_db->cfg);
	case IWM_PHY_DB_CALIB_NCH:
		return (&phy_db->calib_nch);
	case IWM_PHY_DB_CALIB_CHG_PAPD:
		if (chg_id >= IWM_NUM_PAPD_CH_GROUPS)
			return (NULL);
		return (&phy_db->calib_ch_group_papd[chg_id]);
	case IWM_PHY_DB_CALIB_CHG_TXP:
		if (chg_id >= IWM_NUM_TXP_CH_GROUPS)
			return (NULL);
		return (&phy_db->calib_ch_group_txp[chg_id]);
	default:
		return (NULL);
	}
	return (NULL);
}

static int
iwm_phy_db_set_section(struct iwm_softc *sc,
    struct iwm_calib_res_notif_phy_db *phy_db_notif)
{
	uint16_t type = le16toh(phy_db_notif->type);
	uint16_t size  = le16toh(phy_db_notif->length);
	struct iwm_phy_db_entry *entry;
	uint16_t chg_id = 0;

	if (type == IWM_PHY_DB_CALIB_CHG_PAPD ||
	    type == IWM_PHY_DB_CALIB_CHG_TXP)
		chg_id = le16toh(*(uint16_t *)phy_db_notif->data);

	entry = iwm_phy_db_get_section(sc, type, chg_id);
	if (!entry)
		return (EINVAL);

	if (entry->data)
		kmem_free(entry->data, entry->size);
	entry->data = kmem_alloc(size, KM_NOSLEEP);
	if (!entry->data) {
		entry->size = 0;
		return (ENOMEM);
	}
	memcpy(entry->data, phy_db_notif->data, size);
	entry->size = size;

	return (0);
}

static int
iwm_is_valid_channel(uint16_t ch_id)
{
	if (ch_id <= 14 ||
	    (36 <= ch_id && ch_id <= 64 && ch_id % 4 == 0) ||
	    (100 <= ch_id && ch_id <= 140 && ch_id % 4 == 0) ||
	    (145 <= ch_id && ch_id <= 165 && ch_id % 4 == 1))
		return (1);
	return (0);
}

static uint8_t
iwm_ch_id_to_ch_index(uint16_t ch_id)
{
	if (!iwm_is_valid_channel(ch_id))
		return (0xff);

	if (ch_id <= 14)
		return (ch_id - 1);
	if (ch_id <= 64)
		return ((ch_id + 20) / 4);
	if (ch_id <= 140)
		return ((ch_id - 12) / 4);
	return ((ch_id - 13) / 4);
}


static uint16_t
iwm_channel_id_to_papd(uint16_t ch_id)
{
	if (!iwm_is_valid_channel(ch_id))
		return (0xff);

	if (1 <= ch_id && ch_id <= 14)
		return (0);
	if (36 <= ch_id && ch_id <= 64)
		return (1);
	if (100 <= ch_id && ch_id <= 140)
		return (2);
	return (3);
}

static uint16_t
iwm_channel_id_to_txp(struct iwm_softc *sc, uint16_t ch_id)
{
	struct iwm_phy_db *phy_db = &sc->sc_phy_db;
	struct iwm_phy_db_chg_txp *txp_chg;
	int i;
	uint8_t ch_index = iwm_ch_id_to_ch_index(ch_id);

	if (ch_index == 0xff)
		return (0xff);

	for (i = 0; i < IWM_NUM_TXP_CH_GROUPS; i++) {
		txp_chg = (void *)phy_db->calib_ch_group_txp[i].data;
		if (!txp_chg || phy_db->calib_ch_group_txp[i].size <
		    sizeof (*txp_chg))
			return (0xff);
		/*
		 * Looking for the first channel group the max channel
		 * of which is higher than the requested channel.
		 */
		if (le16toh(txp_chg->max_channel_idx) >= ch_index)
			return (i);
	}
	return (0xff);
}

static int
iwm_phy_db_get_section_data(struct iwm_softc *sc, uint32_t type, uint8_t **data,
    uint16_t *size, uint16_t ch_id)
{
	struct iwm_phy_db_entry *entry;
	uint16_t ch_group_id = 0;

	if (type == IWM_PHY_DB_CALIB_CHG_PAPD)
		ch_group_id = iwm_channel_id_to_papd(ch_id);
	else if (type == IWM_PHY_DB_CALIB_CHG_TXP)
		ch_group_id = iwm_channel_id_to_txp(sc, ch_id);

	entry = iwm_phy_db_get_section(sc, type, ch_group_id);
	if (!entry)
		return (EINVAL);

	*data = entry->data;
	*size = entry->size;

	return (0);
}

static int
iwm_send_phy_db_cmd(struct iwm_softc *sc, uint16_t type, uint16_t length,
    void *data)
{
	struct iwm_phy_db_cmd phy_db_cmd;
	struct iwm_host_cmd cmd = {
		.id = IWM_PHY_DB_CMD,
		.flags = IWM_CMD_ASYNC,
	};

	phy_db_cmd.type = le16toh(type);
	phy_db_cmd.length = le16toh(length);

	cmd.data[0] = &phy_db_cmd;
	cmd.len[0] = sizeof (struct iwm_phy_db_cmd);
	cmd.data[1] = data;
	cmd.len[1] = length;

	return (iwm_send_cmd(sc, &cmd));
}

static int
iwm_phy_db_send_all_channel_groups(struct iwm_softc *sc, uint16_t type,
    uint8_t max_ch_groups)
{
	uint16_t i;
	int err;
	struct iwm_phy_db_entry *entry;

	for (i = 0; i < max_ch_groups; i++) {
		entry = iwm_phy_db_get_section(sc, type, i);
		if (!entry)
			return (EINVAL);

		if (!entry->size)
			continue;

		err = iwm_send_phy_db_cmd(sc, type, entry->size, entry->data);
		if (err)
			return (err);

		DELAY(1000);
	}

	return (0);
}

static int
iwm_send_phy_db_data(struct iwm_softc *sc)
{
	uint8_t *data = NULL;
	uint16_t size = 0;
	int err;

	err = iwm_phy_db_get_section_data(sc, IWM_PHY_DB_CFG, &data, &size, 0);
	if (err)
		return (err);

	err = iwm_send_phy_db_cmd(sc, IWM_PHY_DB_CFG, size, data);
	if (err)
		return (err);

	err = iwm_phy_db_get_section_data(sc, IWM_PHY_DB_CALIB_NCH,
	    &data, &size, 0);
	if (err)
		return (err);

	err = iwm_send_phy_db_cmd(sc, IWM_PHY_DB_CALIB_NCH, size, data);
	if (err)
		return (err);

	err = iwm_phy_db_send_all_channel_groups(sc,
	    IWM_PHY_DB_CALIB_CHG_PAPD, IWM_NUM_PAPD_CH_GROUPS);
	if (err)
		return (err);

	err = iwm_phy_db_send_all_channel_groups(sc,
	    IWM_PHY_DB_CALIB_CHG_TXP, IWM_NUM_TXP_CH_GROUPS);
	if (err)
		return (err);

	return (0);
}

/*
 * For the high priority TE use a time event type that has similar priority to
 * the FW's action scan priority.
 */
#define	IWM_ROC_TE_TYPE_NORMAL IWM_TE_P2P_DEVICE_DISCOVERABLE
#define	IWM_ROC_TE_TYPE_MGMT_TX IWM_TE_P2P_CLIENT_ASSOC

static int
iwm_send_time_event_cmd(struct iwm_softc *sc,
    const struct iwm_time_event_cmd *cmd)
{
	struct iwm_rx_packet *pkt;
	struct iwm_time_event_resp *resp;
	struct iwm_host_cmd hcmd = {
		.id = IWM_TIME_EVENT_CMD,
		.flags = IWM_CMD_WANT_RESP,
		.resp_pkt_len = sizeof (*pkt) + sizeof (*resp),
	};
	uint32_t resp_len;
	int err;

	hcmd.data[0] = cmd;
	hcmd.len[0] = sizeof (*cmd);
	err = iwm_send_cmd(sc, &hcmd);
	if (err)
		return (err);

	pkt = hcmd.resp_pkt;
	if (!pkt || (pkt->hdr.flags & IWM_CMD_FAILED_MSK)) {
		err = EIO;
		goto out;
	}

	resp_len = iwm_rx_packet_payload_len(pkt);
	if (resp_len != sizeof (*resp)) {
		err = EIO;
		goto out;
	}

	resp = (void *)pkt->data;
	if (le32toh(resp->status) == 0)
		sc->sc_time_event_uid = le32toh(resp->unique_id);
	else
		err = EIO;
out:
	iwm_free_resp(sc, &hcmd);
	return (err);
}

static void
iwm_protect_session(struct iwm_softc *sc, struct iwm_node *in,
    uint32_t duration, uint32_t max_delay)
{
	struct iwm_time_event_cmd time_cmd;

	/* Do nothing if a time event is already scheduled. */
	if (sc->sc_flags & IWM_FLAG_TE_ACTIVE)
		return;

	memset(&time_cmd, 0, sizeof (time_cmd));

	time_cmd.action = htole32(IWM_FW_CTXT_ACTION_ADD);
	time_cmd.id_and_color =
	    htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	time_cmd.id = htole32(IWM_TE_BSS_STA_AGGRESSIVE_ASSOC);

	time_cmd.apply_time = htole32(0);

	time_cmd.max_frags = IWM_TE_V2_FRAG_NONE;
	time_cmd.max_delay = htole32(max_delay);
	/* TODO: why do we need to interval = bi if it is not periodic? */
	time_cmd.interval = htole32(1);
	time_cmd.duration = htole32(duration);
	time_cmd.repeat = 1;
	time_cmd.policy = htole16(IWM_TE_V2_NOTIF_HOST_EVENT_START |
	    IWM_TE_V2_NOTIF_HOST_EVENT_END | IWM_T2_V2_START_IMMEDIATELY);

	if (iwm_send_time_event_cmd(sc, &time_cmd) == 0)
		sc->sc_flags |= IWM_FLAG_TE_ACTIVE;

	DELAY(100);
}

static void
iwm_unprotect_session(struct iwm_softc *sc, struct iwm_node *in)
{
	struct iwm_time_event_cmd time_cmd;

	/* Do nothing if the time event has already ended. */
	if ((sc->sc_flags & IWM_FLAG_TE_ACTIVE) == 0)
		return;

	memset(&time_cmd, 0, sizeof (time_cmd));

	time_cmd.action = htole32(IWM_FW_CTXT_ACTION_REMOVE);
	time_cmd.id_and_color =
	    htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	time_cmd.id = htole32(sc->sc_time_event_uid);

	if (iwm_send_time_event_cmd(sc, &time_cmd) == 0)
		sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;

	DELAY(100);
}

/*
 * NVM read access and content parsing.  We do not support
 * external NVM or writing NVM.
 */

/* list of NVM sections we are allowed/need to read */
const int iwm_nvm_to_read[] = {
	IWM_NVM_SECTION_TYPE_HW,
	IWM_NVM_SECTION_TYPE_SW,
	IWM_NVM_SECTION_TYPE_REGULATORY,
	IWM_NVM_SECTION_TYPE_CALIBRATION,
	IWM_NVM_SECTION_TYPE_PRODUCTION,
	IWM_NVM_SECTION_TYPE_REGULATORY_SDP,
	IWM_NVM_SECTION_TYPE_HW_8000,
	IWM_NVM_SECTION_TYPE_MAC_OVERRIDE,
	IWM_NVM_SECTION_TYPE_PHY_SKU,
};

#define	IWM_NVM_DEFAULT_CHUNK_SIZE	(2*1024)

#define	IWM_NVM_WRITE_OPCODE 1
#define	IWM_NVM_READ_OPCODE 0

static int
iwm_nvm_read_chunk(struct iwm_softc *sc, uint16_t section, uint16_t offset,
    uint16_t length, uint8_t *data, uint16_t *len)
{
	/*
	 * OpenBSD resets offset to 0 here, which makes NVM sections larger
	 * than one chunk (the 8000 regulatory section holds the LAR word at
	 * 0x507 words) read the first chunk repeatedly.  FreeBSD and Linux
	 * use the requested offset; so do we.
	 */
	struct iwm_nvm_access_cmd nvm_access_cmd = {
		.offset = htole16(offset),
		.length = htole16(length),
		.type = htole16(section),
		.op_code = IWM_NVM_READ_OPCODE,
	};
	struct iwm_nvm_access_resp *nvm_resp;
	struct iwm_rx_packet *pkt;
	struct iwm_host_cmd cmd = {
		.id = IWM_NVM_ACCESS_CMD,
		.flags = (IWM_CMD_WANT_RESP | IWM_CMD_SEND_IN_RFKILL),
		.resp_pkt_len = IWM_CMD_RESP_MAX,
		.data = { &nvm_access_cmd, },
	};
	int err, offset_read;
	size_t bytes_read;
	uint8_t *resp_data;

	cmd.len[0] = sizeof (struct iwm_nvm_access_cmd);

	err = iwm_send_cmd(sc, &cmd);
	if (err)
		return (err);

	pkt = cmd.resp_pkt;
	if (pkt->hdr.flags & IWM_CMD_FAILED_MSK) {
		err = EIO;
		goto exit;
	}

	/* Extract NVM response */
	nvm_resp = (void *)pkt->data;
	if (iwm_rx_packet_payload_len(pkt) < sizeof (*nvm_resp)) {
		err = EIO;
		goto exit;
	}

	err = le16toh(nvm_resp->status);
	bytes_read = le16toh(nvm_resp->length);
	offset_read = le16toh(nvm_resp->offset);
	resp_data = nvm_resp->data;
	if (err) {
		err = EINVAL;
		goto exit;
	}

	if (offset_read != offset) {
		err = EINVAL;
		goto exit;
	}

	if (bytes_read > length ||
	    bytes_read > iwm_rx_packet_payload_len(pkt) - sizeof (*nvm_resp)) {
		err = EINVAL;
		goto exit;
	}

	memcpy(data + offset, resp_data, bytes_read);
	*len = bytes_read;

exit:
	iwm_free_resp(sc, &cmd);
	return (err);
}

/*
 * Reads an NVM section completely.
 * NICs prior to 7000 family doesn't have a real NVM, but just read
 * section 0 which is the EEPROM. Because the EEPROM reading is unlimited
 * by uCode, we need to manually check in this case that we don't
 * overflow and try to read more than the EEPROM size.
 */
static int
iwm_nvm_read_section(struct iwm_softc *sc, uint16_t section, uint8_t *data,
    uint16_t *len, size_t max_len)
{
	uint16_t chunklen, seglen;
	int err = 0;

	chunklen = seglen = IWM_NVM_DEFAULT_CHUNK_SIZE;
	*len = 0;

	/* Read NVM chunks until exhausted (reading less than requested) */
	while (seglen == chunklen && *len < max_len) {
		chunklen = MIN(IWM_NVM_DEFAULT_CHUNK_SIZE, max_len - *len);
		err = iwm_nvm_read_chunk(sc,
		    section, *len, chunklen, data, &seglen);
		if (err)
			return (err);

		*len += seglen;
	}

	return (err);
}

static uint8_t
iwm_fw_valid_tx_ant(struct iwm_softc *sc)
{
	uint8_t tx_ant;

	tx_ant = ((sc->sc_fw_phy_config & IWM_FW_PHY_CFG_TX_CHAIN)
	    >> IWM_FW_PHY_CFG_TX_CHAIN_POS);

	if (sc->sc_nvm.valid_tx_ant)
		tx_ant &= sc->sc_nvm.valid_tx_ant;

	return (tx_ant);
}

static uint8_t
iwm_fw_valid_rx_ant(struct iwm_softc *sc)
{
	uint8_t rx_ant;

	rx_ant = ((sc->sc_fw_phy_config & IWM_FW_PHY_CFG_RX_CHAIN)
	    >> IWM_FW_PHY_CFG_RX_CHAIN_POS);

	if (sc->sc_nvm.valid_rx_ant)
		rx_ant &= sc->sc_nvm.valid_rx_ant;

	return (rx_ant);
}

static int
iwm_valid_siso_ant_rate_mask(struct iwm_softc *sc)
{
	uint8_t valid_tx_ant = iwm_fw_valid_tx_ant(sc);

	/*
	 * According to the Linux driver, antenna B should be preferred
	 * on 9k devices since it is not shared with bluetooth. However,
	 * there are 9k devices which do not support antenna B at all.
	 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_9000 &&
	    (valid_tx_ant & IWM_ANT_B))
		return (IWM_RATE_MCS_ANT_B_MSK);

	return (IWM_RATE_MCS_ANT_A_MSK);
}

static void
iwm_init_channel_map(struct iwm_softc *sc, const uint16_t * const nvm_ch_flags,
    const uint8_t *nvm_channels, int nchan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_nvm_data *data = &sc->sc_nvm;
	int ch_idx;
	struct ieee80211_channel *channel;
	uint16_t ch_flags;
	int is_5ghz;
	int hw_value;

	for (ch_idx = 0; ch_idx < nchan; ch_idx++) {
		ch_flags = le16_to_cpup(nvm_ch_flags + ch_idx);

		if (ch_idx >= IWM_NUM_2GHZ_CHANNELS &&
		    !data->sku_cap_band_52GHz_enable)
			ch_flags &= ~IWM_NVM_CHANNEL_VALID;

		if (!(ch_flags & IWM_NVM_CHANNEL_VALID))
			continue;

		hw_value = nvm_channels[ch_idx];
		if (hw_value > IEEE80211_CHAN_MAX)
			continue;
		channel = &ic->ic_sup_channels[hw_value];

		is_5ghz = ch_idx >= IWM_NUM_2GHZ_CHANNELS;
		if (!is_5ghz) {
			channel->ich_freq =
			    ieee80211_ieee2mhz(hw_value, IEEE80211_CHAN_2GHZ);
			channel->ich_flags
			    = IEEE80211_CHAN_CCK
			    | IEEE80211_CHAN_OFDM
			    | IEEE80211_CHAN_DYN
			    | IEEE80211_CHAN_2GHZ;
		} else {
			channel->ich_freq =
			    ieee80211_ieee2mhz(hw_value, IEEE80211_CHAN_5GHZ);
			channel->ich_flags =
			    IEEE80211_CHAN_5GHZ | IEEE80211_CHAN_OFDM;
		}

		if (!(ch_flags & IWM_NVM_CHANNEL_ACTIVE))
			channel->ich_flags |= IEEE80211_CHAN_PASSIVE;

		/*
		 * illumos keeps one entry per channel number, so a 40 MHz
		 * capable channel carries both HT40 flags; the secondary
		 * channel actually used comes from the AP's HT operation
		 * element (iwm_node_sco()).
		 */
		if (data->sku_cap_11n_enable) {
			channel->ich_flags |= IEEE80211_CHAN_HT20;
			if (ch_flags & IWM_NVM_CHANNEL_40MHZ)
				channel->ich_flags |= IEEE80211_CHAN_HT40;
		}
	}
}

static int
iwm_mimo_enabled(struct iwm_softc *sc)
{
	return (!sc->sc_nvm.sku_cap_mimo_disable);
}

/*
 * Build the HT MCS set we support (OpenBSD fills ic_sup_mcs; illumos takes
 * the set through ieee80211_attach_mcs()).
 */
static void
iwm_setup_ht_rates(struct iwm_softc *sc, struct ieee80211_htrateset *rs)
{
	uint8_t rx_ant;
	int i, n = 8;

	if (iwm_mimo_enabled(sc)) {
		rx_ant = iwm_fw_valid_rx_ant(sc);
		if ((rx_ant & IWM_ANT_AB) == IWM_ANT_AB ||
		    (rx_ant & IWM_ANT_BC) == IWM_ANT_BC)
			n = 16;			/* MCS 8-15 */
	}
	rs->rs_nrates = n;
	for (i = 0; i < n; i++)
		rs->rs_rates[i] = i;
}

#define	IWM_MAX_RX_BA_SESSIONS 16

/*
 * Tell the firmware about an Rx BlockAck agreement.  illumos net80211 has
 * already accepted the ADDBA request (it does not ask the driver first, as
 * OpenBSD does); if the firmware cannot take the session we tear it down
 * again with a DELBA.  Called with sc_mtx held from the BA task.
 */
static int
iwm_sta_rx_agg(struct iwm_softc *sc, struct ieee80211_node *ni, uint8_t tid,
    uint16_t ssn, uint16_t winsize, int timeout_val, int start)
{
	struct iwm_add_sta_cmd cmd;
	struct iwm_node *in = (void *)ni;
	int err;
	uint32_t status;
	size_t cmdsize;

	_NOTE(ARGUNUSED(timeout_val))
	/*
	 * Track firmware sessions per TID (OpenBSD counts only): a DELBA
	 * for a TID the firmware never accepted must not send REMOVE or
	 * decrement the count, and a renegotiated TID must not count twice.
	 */
	if (!start && (sc->sc_rx_ba_tidmask & (1 << tid)) == 0)
		return (0);
	if (start && (sc->sc_rx_ba_tidmask & (1 << tid)) == 0 &&
	    sc->sc_rx_ba_sessions >= IWM_MAX_RX_BA_SESSIONS) {
		iwm_delba(sc, ni, tid);
		return (0);
	}

	memset(&cmd, 0, sizeof (cmd));

	cmd.sta_id = IWM_STATION_ID;
	cmd.mac_id_n_color
	    = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	cmd.add_modify = IWM_STA_MODE_MODIFY;

	if (start) {
		cmd.add_immediate_ba_tid = (uint8_t)tid;
		cmd.add_immediate_ba_ssn = ssn;
		cmd.rx_ba_window = winsize;
	} else {
		cmd.remove_immediate_ba_tid = (uint8_t)tid;
	}
	cmd.modify_mask = start ? IWM_STA_MODIFY_ADD_BA_TID :
	    IWM_STA_MODIFY_REMOVE_BA_TID;

	status = IWM_ADD_STA_SUCCESS;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof (cmd);
	else
		cmdsize = sizeof (struct iwm_add_sta_cmd_v7);
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA, cmdsize, &cmd,
	    &status);
	if (!err && (status & IWM_ADD_STA_STATUS_MASK) != IWM_ADD_STA_SUCCESS)
		err = EIO;
	if (err) {
		if (start)
			iwm_delba(sc, ni, tid);
		return (err);
	}

	if (start && (sc->sc_rx_ba_tidmask & (1 << tid)) == 0) {
		sc->sc_rx_ba_tidmask |= (1 << tid);
		sc->sc_rx_ba_sessions++;
	} else if (!start) {
		sc->sc_rx_ba_tidmask &= ~(1 << tid);
		if (sc->sc_rx_ba_sessions > 0)
			sc->sc_rx_ba_sessions--;
	}

	return (0);
}

/*
 * Refuse/tear down an Rx BA session from our side (recipient DELBA).
 * net80211 may call back into the driver, so drop sc_mtx around it.
 */
static void
iwm_delba(struct iwm_softc *sc, struct ieee80211_node *ni, uint8_t tid)
{
	uint16_t args[4];

	args[0] = tid;
	args[1] = 0;				/* we are the recipient */
	args[2] = IEEE80211_REASON_UNSPECIFIED;
	args[3] = 0;
	mutex_exit(&sc->sc_mtx);
	(void) ieee80211_send_action(ni, IEEE80211_ACTION_CAT_BA,
	    IEEE80211_ACTION_BA_DELBA, args);
	mutex_enter(&sc->sc_mtx);
}

static void
iwm_mac_ctxt_task(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	int err;

	mutex_enter(&sc->sc_mtx);
	/* Clear first: a request made while we run requeues us. */
	sc->sc_task_pending &= ~IWM_TASK_MAC_CTXT;
	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) ||
	    ic->ic_state != IEEE80211_S_RUN) {
		iwm_task_done(sc, IWM_TASK_MAC_CTXT);
		mutex_exit(&sc->sc_mtx);
		return;
	}

	err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_MODIFY, 1);
	if (err)
		dev_err(sc->sc_dip, CE_WARN, "!failed to update MAC");

	iwm_unprotect_session(sc, in);

	iwm_task_done(sc, IWM_TASK_MAC_CTXT);
	mutex_exit(&sc->sc_mtx);
}

/* illumos ic_set_shortslot hook (OpenBSD ic_updateslot). */
static void
iwm_updateslot(struct ieee80211com *ic, int onoff)
{
	struct iwm_softc *sc = (struct iwm_softc *)ic;

	_NOTE(ARGUNUSED(onoff))
	/* net80211 calls this without sc_mtx (newstate, Rx of beacons). */
	mutex_enter(&sc->sc_mtx);
	if (ic->ic_state == IEEE80211_S_RUN &&
	    (sc->sc_task_pending & IWM_TASK_NEWSTATE) == 0)
		iwm_add_task(sc, sc->sc_nswq, IWM_TASK_MAC_CTXT);
	mutex_exit(&sc->sc_mtx);
}

/* illumos wme_update hook (OpenBSD ic_updateedca). */
static int
iwm_updateedca(struct ieee80211com *ic)
{
	struct iwm_softc *sc = (struct iwm_softc *)ic;

	mutex_enter(&sc->sc_mtx);
	if (ic->ic_state == IEEE80211_S_RUN &&
	    (sc->sc_task_pending & IWM_TASK_NEWSTATE) == 0)
		iwm_add_task(sc, sc->sc_nswq, IWM_TASK_MAC_CTXT);
	mutex_exit(&sc->sc_mtx);
	return (0);
}

/*
 * Enable or disable firmware Tx aggregation for a TID.  Called with sc_mtx
 * held from the BA task once net80211 has an ADDBA response (start) or the
 * session ends (stop).
 */
static int
iwm_sta_tx_agg(struct iwm_softc *sc, struct ieee80211_node *ni, uint8_t tid,
    uint16_t ssn, uint16_t winsize, int start)
{
	struct iwm_add_sta_cmd cmd;
	struct iwm_node *in = (void *)ni;
	int qid = IWM_FIRST_AGG_TX_QUEUE + tid;
	struct iwm_tx_ring *ring;
	int ac;
	int fifo;
	uint32_t status;
	int err;
	size_t cmdsize;

	_NOTE(ARGUNUSED(winsize))
	/* Ensure we can map this TID to an aggregation queue. */
	if (tid >= IWM_MAX_TID_COUNT || qid > IWM_LAST_AGG_TX_QUEUE)
		return (ENOSPC);

	if (start) {
		if ((sc->tx_ba_queue_mask & (1 << qid)) != 0)
			return (0);
	} else {
		if ((sc->tx_ba_queue_mask & (1 << qid)) == 0)
			return (0);
	}

	ring = &sc->txq[qid];
	ac = iwm_tid_to_ac[tid];
	fifo = iwm_ac_to_tx_fifo[ac];

	if (start && ring->buf_dma == NULL) {
		/* Payload buffers are allocated on first use. */
		mutex_exit(&sc->sc_mtx);
		err = iwm_alloc_txbufs(sc, ring);
		mutex_enter(&sc->sc_mtx);
		if (err) {
			iwm_ampdu_tx_refuse(sc, ni, tid);
			return (err);
		}
	}

	memset(&cmd, 0, sizeof (cmd));

	cmd.sta_id = IWM_STATION_ID;
	cmd.mac_id_n_color = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	cmd.add_modify = IWM_STA_MODE_MODIFY;

	if (start) {
		/* Enable Tx aggregation for this queue. */
		in->tid_disable_ampdu &= ~(1 << tid);
		in->tfd_queue_msk |= (1 << qid);
	} else {
		in->tid_disable_ampdu |= (1 << tid);
		/*
		 * Queue remains enabled in the TFD queue mask
		 * until we leave RUN state.
		 */
		err = iwm_flush_sta(sc, in);
		if (err)
			return (err);
	}

	cmd.tfd_queue_msk |= htole32(in->tfd_queue_msk);
	cmd.tid_disable_tx = htole16(in->tid_disable_ampdu);
	cmd.modify_mask = (IWM_STA_MODIFY_QUEUES |
	    IWM_STA_MODIFY_TID_DISABLE_TX);

	if (start && (sc->qenablemsk & (1 << qid)) == 0) {
		if (!iwm_nic_lock(sc)) {
			iwm_ampdu_tx_refuse(sc, ni, tid);
			return (EBUSY);
		}
		err = iwm_enable_txq(sc, IWM_STATION_ID, qid, fifo, 1, tid,
		    ssn);
		iwm_nic_unlock(sc);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not enable Tx "
			    "queue %d (error %d)", qid, err);
			iwm_ampdu_tx_refuse(sc, ni, tid);
			return (err);
		}
		/*
		 * If iwm_enable_txq() employed the SCD hardware bug
		 * workaround we must skip the frame with seqnum SSN.
		 */
		if (ring->cur != IWM_AGG_SSN_TO_TXQ_IDX(ssn)) {
			ssn = (ssn + 1) & 0xfff;
			ASSERT(ring->cur == IWM_AGG_SSN_TO_TXQ_IDX(ssn));
			ni->in_txseqs[tid] = ssn;
		}
		/* OpenBSD net80211 sets ba_winstart = SSN at ADDBA time. */
		sc->sc_tx_ba_winstart[tid] = ssn;
	}

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof (cmd);
	else
		cmdsize = sizeof (struct iwm_add_sta_cmd_v7);

	status = 0;
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA, cmdsize, &cmd, &status);
	if (!err && (status & IWM_ADD_STA_STATUS_MASK) != IWM_ADD_STA_SUCCESS)
		err = EIO;
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not update sta "
		    "(error %d)", err);
		if (start)
			iwm_ampdu_tx_refuse(sc, ni, tid);
		return (err);
	}

	if (start) {
		sc->tx_ba_queue_mask |= (1 << qid);
	} else {
		sc->tx_ba_queue_mask &= ~(1 << qid);

		/*
		 * Clear pending frames but keep the queue enabled.
		 * Firmware panics if we disable the queue here.
		 */
		iwm_txq_advance(sc, ring, ring->cur);
		iwm_clear_oactive(sc, ring);
	}

	return (0);
}

/*
 * The firmware could not take a Tx BA session net80211 negotiated: stop it
 * (net80211 sends the DELBA and calls our ic_addba_stop, a no-op here).
 */
static void
iwm_ampdu_tx_refuse(struct iwm_softc *sc, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct ieee80211_tx_ampdu *tap = &ni->in_tx_ampdu[TID_TO_WME_AC(tid)];

	mutex_exit(&sc->sc_mtx);
	ieee80211_ampdu_stop(ni, tap);
	mutex_enter(&sc->sc_mtx);
}

static void
iwm_ba_task(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int tid, err = 0;

	mutex_enter(&sc->sc_mtx);
	sc->sc_task_pending &= ~IWM_TASK_BA;	/* requeue on new work */
	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) ||
	    ic->ic_state != IEEE80211_S_RUN) {
		sc->ba_rx.start_tidmask = sc->ba_rx.stop_tidmask = 0;
		sc->ba_tx.start_tidmask = sc->ba_tx.stop_tidmask = 0;
		iwm_task_done(sc, IWM_TASK_BA);
		mutex_exit(&sc->sc_mtx);
		return;
	}

	for (tid = 0; tid < IWM_MAX_TID_COUNT && !err; tid++) {
		if (sc->sc_flags & IWM_FLAG_SHUTDOWN)
			break;
		if (sc->ba_rx.start_tidmask & (1 << tid)) {
			struct iwm_rx_ba *ba = &sc->sc_rx_ba[tid];
			sc->ba_rx.start_tidmask &= ~(1 << tid);
			err = iwm_sta_rx_agg(sc, ni, tid, ba->ssn,
			    ba->winsize, ba->timeout, 1);
		} else if (sc->ba_rx.stop_tidmask & (1 << tid)) {
			sc->ba_rx.stop_tidmask &= ~(1 << tid);
			err = iwm_sta_rx_agg(sc, ni, tid, 0, 0, 0, 0);
		}
	}

	for (tid = 0; tid < IWM_MAX_TID_COUNT && !err; tid++) {
		if (sc->sc_flags & IWM_FLAG_SHUTDOWN)
			break;
		if (sc->ba_tx.start_tidmask & (1 << tid)) {
			sc->ba_tx.start_tidmask &= ~(1 << tid);
			err = iwm_sta_tx_agg(sc, ni, tid,
			    ni->in_txseqs[tid] & 0xfff, IWM_FRAME_LIMIT, 1);
		} else if (sc->ba_tx.stop_tidmask & (1 << tid)) {
			sc->ba_tx.stop_tidmask &= ~(1 << tid);
			err = iwm_sta_tx_agg(sc, ni, tid, 0, 0, 0);
		}
	}

	/*
	 * We "recover" from failure to start or stop a BA session
	 * by resetting the device.
	 */
	if (err && (sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0)
		iwm_add_task(sc, sc->sc_initq, IWM_TASK_INIT);

	iwm_task_done(sc, IWM_TASK_BA);
	mutex_exit(&sc->sc_mtx);
}

/*
 * illumos ic_recv_action hook.  net80211 answers ADDBA requests itself and
 * runs the Rx reorder window; afterwards we program the firmware's Rx BA
 * state from the task (OpenBSD ic_ampdu_rx_start/stop).
 */
static void
iwm_recv_action(struct ieee80211_node *ni, const uint8_t *frm,
    const uint8_t *efrm)
{
	struct ieee80211com *ic = ni->in_ic;
	struct iwm_softc *sc = (struct iwm_softc *)ic;
	const struct ieee80211_action *ia = (const void *)frm;
	uint16_t baparamset, batimeout;
	int tid;

	sc->sc_recv_action(ni, frm, efrm);

	if (efrm - frm < 4 || ia->ia_category != IEEE80211_ACTION_CAT_BA)
		return;
	switch (ia->ia_action) {
	case IEEE80211_ACTION_BA_ADDBA_REQUEST:
		if (efrm - frm < 9)
			return;
		baparamset = LE_16(*(const uint16_t *)(frm + 3));
		batimeout = LE_16(*(const uint16_t *)(frm + 5));
		tid = MS(baparamset, IEEE80211_BAPS_TID);
		if (tid >= IWM_MAX_TID_COUNT)
			return;
		/* Accepted by net80211? */
		if ((ni->in_rx_ampdu[tid].rxa_flags &
		    IEEE80211_AGGR_XCHGPEND) == 0)
			return;
		mutex_enter(&sc->sc_mtx);
		sc->sc_rx_ba[tid].ssn = ni->in_rx_ampdu[tid].rxa_start;
		sc->sc_rx_ba[tid].winsize = ni->in_rx_ampdu[tid].rxa_wnd;
		sc->sc_rx_ba[tid].timeout = batimeout;
		sc->ba_rx.stop_tidmask &= ~(1 << tid);
		sc->ba_rx.start_tidmask |= (1 << tid);
		iwm_add_task(sc, sc->sc_nswq, IWM_TASK_BA);
		mutex_exit(&sc->sc_mtx);
		break;
	case IEEE80211_ACTION_BA_DELBA:
		baparamset = LE_16(*(const uint16_t *)(frm + 2));
		if ((baparamset & IEEE80211_DELBAPS_INIT) == 0)
			return;		/* our Tx session; see addba_stop */
		tid = MS(baparamset, IEEE80211_DELBAPS_TID);
		if (tid >= IWM_MAX_TID_COUNT)
			return;
		mutex_enter(&sc->sc_mtx);
		sc->ba_rx.start_tidmask &= ~(1 << tid);
		sc->ba_rx.stop_tidmask |= (1 << tid);
		iwm_add_task(sc, sc->sc_nswq, IWM_TASK_BA);
		mutex_exit(&sc->sc_mtx);
		break;
	default:
		break;
	}
}

/*
 * illumos ic_addba_response hook: the peer answered our ADDBA request.
 * net80211's default marks the session running; we then enable the
 * firmware's aggregation queue (OpenBSD ic_ampdu_tx_start).  Frames keep
 * using the normal queue until the task sets tx_ba_queue_mask.
 */
static int
iwm_addba_response(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
    int status, int baparamset, int batimeout)
{
	struct iwm_softc *sc = (struct iwm_softc *)ni->in_ic;
	int tid = WME_AC_TO_TID(tap->txa_ac);
	int ret;

	ret = sc->sc_addba_response(ni, tap, status, baparamset, batimeout);
	if (!IEEE80211_AMPDU_RUNNING(tap))
		return (ret);

	mutex_enter(&sc->sc_mtx);
	/* We only implement Tx aggregation with DQA-capable firmware. */
	if (!isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT) ||
	    tid >= IWM_MAX_TID_COUNT) {
		mutex_exit(&sc->sc_mtx);
		tap->txa_flags &= ~IEEE80211_AGGR_RUNNING;
		tap->txa_flags |= IEEE80211_AGGR_NAK;
		return (ret);
	}
	sc->ba_tx.stop_tidmask &= ~(1 << tid);
	sc->ba_tx.start_tidmask |= (1 << tid);
	iwm_add_task(sc, sc->sc_nswq, IWM_TASK_BA);
	mutex_exit(&sc->sc_mtx);
	return (ret);
}

/* illumos ic_addba_stop hook (OpenBSD ic_ampdu_tx_stop). */
static void
iwm_addba_stop(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap)
{
	struct iwm_softc *sc = (struct iwm_softc *)ni->in_ic;
	int tid = WME_AC_TO_TID(tap->txa_ac);
	int qid = IWM_FIRST_AGG_TX_QUEUE + tid;

	sc->sc_addba_stop(ni, tap);

	mutex_enter(&sc->sc_mtx);
	if (tid < IWM_MAX_TID_COUNT) {
		/* A start the BA task has not run yet is void now. */
		sc->ba_tx.start_tidmask &= ~(1 << tid);
		if ((sc->tx_ba_queue_mask & (1 << qid)) != 0) {
			sc->ba_tx.stop_tidmask |= (1 << tid);
			iwm_add_task(sc, sc->sc_nswq, IWM_TASK_BA);
		}
	}
	mutex_exit(&sc->sc_mtx);
}

static void
iwm_set_hw_address_8000(struct iwm_softc *sc, struct iwm_nvm_data *data,
    const uint16_t *mac_override, const uint16_t *nvm_hw)
{
	const uint8_t *hw_addr;

	if (mac_override) {
		static const uint8_t reserved_mac[] = {
			0x02, 0xcc, 0xaa, 0xff, 0xee, 0x00
		};

		hw_addr = (const uint8_t *)(mac_override +
		    IWM_MAC_ADDRESS_OVERRIDE_8000);

		/*
		 * Store the MAC address from MAO section.
		 * No byte swapping is required in MAO section
		 */
		memcpy(data->hw_addr, hw_addr, ETHER_ADDR_LEN);

		/*
		 * Force the use of the OTP MAC address in case of reserved MAC
		 * address in the NVM, or if address is given but invalid.
		 */
		if (memcmp(reserved_mac, hw_addr, ETHER_ADDR_LEN) != 0 &&
		    (memcmp(iwm_etherbroadcastaddr, data->hw_addr,
		    ETHER_ADDR_LEN) != 0) &&
		    (memcmp(iwm_etheranyaddr, data->hw_addr,
		    ETHER_ADDR_LEN) != 0) &&
		    !(data->hw_addr[0] & 1))
			return;
	}

	if (nvm_hw) {
		/* Read the mac address from WFMP registers. */
		uint32_t mac_addr0, mac_addr1;

		if (!iwm_nic_lock(sc))
			goto out;
		mac_addr0 = htole32(iwm_read_prph(sc, IWM_WFMP_MAC_ADDR_0));
		mac_addr1 = htole32(iwm_read_prph(sc, IWM_WFMP_MAC_ADDR_1));
		iwm_nic_unlock(sc);

		hw_addr = (const uint8_t *)&mac_addr0;
		data->hw_addr[0] = hw_addr[3];
		data->hw_addr[1] = hw_addr[2];
		data->hw_addr[2] = hw_addr[1];
		data->hw_addr[3] = hw_addr[0];

		hw_addr = (const uint8_t *)&mac_addr1;
		data->hw_addr[4] = hw_addr[1];
		data->hw_addr[5] = hw_addr[0];

		return;
	}
out:
	dev_err(sc->sc_dip, CE_WARN, "!mac address not found");
	memset(data->hw_addr, 0, sizeof (data->hw_addr));
}

static int
iwm_parse_nvm_data(struct iwm_softc *sc, const uint16_t *nvm_hw,
    const uint16_t *nvm_sw, const uint16_t *nvm_calib,
    const uint16_t *mac_override, const uint16_t *phy_sku,
    const uint16_t *regulatory, int n_regulatory)
{
	struct iwm_nvm_data *data = &sc->sc_nvm;
	uint8_t hw_addr[ETHER_ADDR_LEN];
	uint32_t sku;
	uint16_t lar_config;

	data->nvm_version = le16_to_cpup(nvm_sw + IWM_NVM_VERSION);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		uint16_t radio_cfg = le16_to_cpup(nvm_sw + IWM_RADIO_CFG);
		data->radio_cfg_type = IWM_NVM_RF_CFG_TYPE_MSK(radio_cfg);
		data->radio_cfg_step = IWM_NVM_RF_CFG_STEP_MSK(radio_cfg);
		data->radio_cfg_dash = IWM_NVM_RF_CFG_DASH_MSK(radio_cfg);
		data->radio_cfg_pnum = IWM_NVM_RF_CFG_PNUM_MSK(radio_cfg);

		sku = le16_to_cpup(nvm_sw + IWM_SKU);
	} else {
		uint32_t radio_cfg =
		    le32_to_cpup((uint32_t *)(phy_sku + IWM_RADIO_CFG_8000));
		data->radio_cfg_type = IWM_NVM_RF_CFG_TYPE_MSK_8000(radio_cfg);
		data->radio_cfg_step = IWM_NVM_RF_CFG_STEP_MSK_8000(radio_cfg);
		data->radio_cfg_dash = IWM_NVM_RF_CFG_DASH_MSK_8000(radio_cfg);
		data->radio_cfg_pnum = IWM_NVM_RF_CFG_PNUM_MSK_8000(radio_cfg);
		data->valid_tx_ant = IWM_NVM_RF_CFG_TX_ANT_MSK_8000(radio_cfg);
		data->valid_rx_ant = IWM_NVM_RF_CFG_RX_ANT_MSK_8000(radio_cfg);

		sku = le32_to_cpup((uint32_t *)(phy_sku + IWM_SKU_8000));
	}

	data->sku_cap_band_24GHz_enable = sku & IWM_NVM_SKU_CAP_BAND_24GHZ;
	data->sku_cap_band_52GHz_enable = sku & IWM_NVM_SKU_CAP_BAND_52GHZ;
	data->sku_cap_11n_enable = sku & IWM_NVM_SKU_CAP_11N_ENABLE;
	data->sku_cap_11ac_enable = sku & IWM_NVM_SKU_CAP_11AC_ENABLE;
	data->sku_cap_mimo_disable = sku & IWM_NVM_SKU_CAP_MIMO_DISABLE;

	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000) {
		uint16_t lar_offset = data->nvm_version < 0xE39 ?
		    IWM_NVM_LAR_OFFSET_8000_OLD : IWM_NVM_LAR_OFFSET_8000;

		/* n_regulatory is in bytes; OpenBSD does not check it. */
		if ((size_t)(lar_offset + 1) * sizeof (uint16_t) <=
		    (size_t)n_regulatory)
			lar_config = le16_to_cpup(regulatory + lar_offset);
		else
			lar_config = 0;
		data->lar_enabled = !!(lar_config &
		    IWM_NVM_LAR_ENABLED_8000);
		data->n_hw_addrs = le16_to_cpup(nvm_sw + IWM_N_HW_ADDRS_8000);
	} else
		data->n_hw_addrs = le16_to_cpup(nvm_sw + IWM_N_HW_ADDRS);


	/* The byte order is little endian 16 bit, meaning 214365 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		memcpy(hw_addr, nvm_hw + IWM_HW_ADDR, ETHER_ADDR_LEN);
		data->hw_addr[0] = hw_addr[1];
		data->hw_addr[1] = hw_addr[0];
		data->hw_addr[2] = hw_addr[3];
		data->hw_addr[3] = hw_addr[2];
		data->hw_addr[4] = hw_addr[5];
		data->hw_addr[5] = hw_addr[4];
	} else
		iwm_set_hw_address_8000(sc, data, mac_override, nvm_hw);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (sc->nvm_type == IWM_NVM_SDP) {
			iwm_init_channel_map(sc, regulatory, iwm_nvm_channels,
			    MIN(n_regulatory, nitems(iwm_nvm_channels)));
		} else {
			iwm_init_channel_map(sc, &nvm_sw[IWM_NVM_CHANNELS],
			    iwm_nvm_channels, nitems(iwm_nvm_channels));
		}
	} else
		iwm_init_channel_map(sc, &regulatory[IWM_NVM_CHANNELS_8000],
		    iwm_nvm_channels_8000,
		    MIN(n_regulatory / (int)sizeof (uint16_t),
		    (int)nitems(iwm_nvm_channels_8000)));

	/*
	 * TODO: this value will prevent some checks from failing, we need
	 * to check if this field is still needed, and if it does, where is
	 * it in the NVM.
	 */
	data->calib_version = 255;

	return (0);
}

static int
iwm_parse_nvm_sections(struct iwm_softc *sc, struct iwm_nvm_section *sections)
{
	const uint16_t *hw, *sw, *calib, *mac_override = NULL, *phy_sku = NULL;
	const uint16_t *regulatory = NULL;
	int n_regulatory = 0;

	/* Checking for required sections */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (!sections[IWM_NVM_SECTION_TYPE_SW].data ||
		    !sections[IWM_NVM_SECTION_TYPE_HW].data) {
			return (ENOENT);
		}

		hw = (const uint16_t *) sections[IWM_NVM_SECTION_TYPE_HW].data;

		if (sc->nvm_type == IWM_NVM_SDP) {
			if (!sections[IWM_NVM_SECTION_TYPE_REGULATORY_SDP].data)
				return (ENOENT);
			struct iwm_nvm_section *rs =
			    &sections[IWM_NVM_SECTION_TYPE_REGULATORY_SDP];

			regulatory = (const uint16_t *)rs->data;
			n_regulatory = rs->length;
		}
	} else if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000) {
		/* SW and REGULATORY sections are mandatory */
		if (!sections[IWM_NVM_SECTION_TYPE_SW].data ||
		    !sections[IWM_NVM_SECTION_TYPE_REGULATORY].data) {
			return (ENOENT);
		}
		/* MAC_OVERRIDE or at least HW section must exist */
		if (!sections[IWM_NVM_SECTION_TYPE_HW_8000].data &&
		    !sections[IWM_NVM_SECTION_TYPE_MAC_OVERRIDE].data) {
			return (ENOENT);
		}

		/* PHY_SKU section is mandatory in B0 */
		if (!sections[IWM_NVM_SECTION_TYPE_PHY_SKU].data) {
			return (ENOENT);
		}

		regulatory = (const uint16_t *)
		    sections[IWM_NVM_SECTION_TYPE_REGULATORY].data;
		n_regulatory = sections[IWM_NVM_SECTION_TYPE_REGULATORY].length;
		hw = (const uint16_t *)
		    sections[IWM_NVM_SECTION_TYPE_HW_8000].data;
		mac_override =
			(const uint16_t *)
			sections[IWM_NVM_SECTION_TYPE_MAC_OVERRIDE].data;
		phy_sku = (const uint16_t *)
		    sections[IWM_NVM_SECTION_TYPE_PHY_SKU].data;
	} else {
		panic("unknown device family %d", sc->sc_device_family);
	}

	sw = (const uint16_t *)sections[IWM_NVM_SECTION_TYPE_SW].data;
	calib = (const uint16_t *)
	    sections[IWM_NVM_SECTION_TYPE_CALIBRATION].data;

	/* XXX should pass in the length of every section */
	return (iwm_parse_nvm_data(sc, hw, sw, calib, mac_override,
	    phy_sku, regulatory, n_regulatory));
}

static int
iwm_nvm_init(struct iwm_softc *sc)
{
	struct iwm_nvm_section nvm_sections[IWM_NVM_NUM_OF_SECTIONS];
	int i, section, err;
	uint16_t len;
	uint8_t *buf;
	const size_t bufsz = sc->sc_nvm_max_section_size;

	memset(nvm_sections, 0, sizeof (nvm_sections));

	buf = kmem_alloc(bufsz, KM_SLEEP);

	for (i = 0; i < (int)nitems(iwm_nvm_to_read); i++) {
		section = iwm_nvm_to_read[i];
		ASSERT(section <= nitems(nvm_sections));

		err = iwm_nvm_read_section(sc, section, buf, &len, bufsz);
		if (err) {
			err = 0;
			continue;
		}
		if (len == 0)
			continue;
		nvm_sections[section].data = kmem_alloc(len, KM_SLEEP);
		memcpy(nvm_sections[section].data, buf, len);
		nvm_sections[section].length = len;
	}
	kmem_free(buf, bufsz);
	if (err == 0)
		err = iwm_parse_nvm_sections(sc, nvm_sections);

	for (i = 0; i < IWM_NVM_NUM_OF_SECTIONS; i++) {
		if (nvm_sections[i].data != NULL)
			kmem_free(nvm_sections[i].data,
			    nvm_sections[i].length);
	}

	return (err);
}

static int
iwm_firmware_load_sect(struct iwm_softc *sc, uint32_t dst_addr,
    const uint8_t *section, uint32_t byte_cnt)
{
	int err = EINVAL;
	uint32_t chunk_sz, offset;

	chunk_sz = MIN(IWM_FH_MEM_TB_MAX_LENGTH, byte_cnt);

	for (offset = 0; offset < byte_cnt; offset += chunk_sz) {
		uint32_t addr, len;
		const uint8_t *data;

		addr = dst_addr + offset;
		len = MIN(chunk_sz, byte_cnt - offset);
		data = section + offset;

		err = iwm_firmware_load_chunk(sc, addr, data, len);
		if (err)
			break;
	}

	return (err);
}

static int
iwm_firmware_load_chunk(struct iwm_softc *sc, uint32_t dst_addr,
    const uint8_t *chunk, uint32_t byte_cnt)
{
	struct iwm_dma_info *dma = &sc->fw_dma;
	int err;

	/* Copy firmware chunk into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, chunk, byte_cnt);
	(void) ddi_dma_sync(dma->dma_hdl, 0, byte_cnt, DDI_DMA_SYNC_FORDEV);

	if (dst_addr >= IWM_FW_MEM_EXTENDED_START &&
	    dst_addr <= IWM_FW_MEM_EXTENDED_END) {
		err = iwm_set_bits_prph(sc, IWM_LMPM_CHICK,
		    IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE);
		if (err)
			return (err);
	}

	sc->sc_fw_chunk_done = 0;

	if (!iwm_nic_lock(sc))
		return (EBUSY);

	IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(IWM_FH_SRVC_CHNL),
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE);
	IWM_WRITE(sc, IWM_FH_SRVC_CHNL_SRAM_ADDR_REG(IWM_FH_SRVC_CHNL),
	    dst_addr);
	IWM_WRITE(sc, IWM_FH_TFDIB_CTRL0_REG(IWM_FH_SRVC_CHNL),
	    dma->paddr & IWM_FH_MEM_TFDIB_DRAM_ADDR_LSB_MSK);
	IWM_WRITE(sc, IWM_FH_TFDIB_CTRL1_REG(IWM_FH_SRVC_CHNL),
	    (iwm_get_dma_hi_addr(dma->paddr) <<
	    IWM_FH_MEM_TFDIB_REG1_ADDR_BITSHIFT) | byte_cnt);
	IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_BUF_STS_REG(IWM_FH_SRVC_CHNL),
	    1 << IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_NUM |
	    1 << IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_POS_TB_IDX |
	    IWM_FH_TCSR_CHNL_TX_BUF_STS_REG_VAL_TFDB_VALID);
	IWM_WRITE(sc, IWM_FH_TCSR_CHNL_TX_CONFIG_REG(IWM_FH_SRVC_CHNL),
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE    |
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE |
	    IWM_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD);

	iwm_nic_unlock(sc);

	/* Wait for this segment to load. */
	err = 0;
	while (!sc->sc_fw_chunk_done) {
		err = iwm_sleep(sc, &sc->sc_fw_cv, 1000000);
		if (err)
			break;
	}

	if (!sc->sc_fw_chunk_done)
		dev_err(sc->sc_dip, CE_WARN,
		    "!fw chunk addr 0x%x len %d failed to load", dst_addr,
		    byte_cnt);

	if (dst_addr >= IWM_FW_MEM_EXTENDED_START &&
	    dst_addr <= IWM_FW_MEM_EXTENDED_END) {
		int err2 = iwm_clear_bits_prph(sc, IWM_LMPM_CHICK,
		    IWM_LMPM_CHICK_EXTENDED_ADDR_SPACE);
		if (!err)
			err = err2;
	}

	return (err);
}

static int
iwm_load_firmware_7000(struct iwm_softc *sc, enum iwm_ucode_type ucode_type)
{
	struct iwm_fw_sects *fws;
	int err, i;
	void *data;
	uint32_t dlen;
	uint32_t offset;

	fws = &sc->sc_fw.fw_sects[ucode_type];
	for (i = 0; i < fws->fw_count; i++) {
		data = fws->fw_sect[i].fws_data;
		dlen = fws->fw_sect[i].fws_len;
		offset = fws->fw_sect[i].fws_devoff;
		if (dlen > sc->sc_fwdmasegsz) {
			err = EFBIG;
		} else
			err = iwm_firmware_load_sect(sc, offset, data, dlen);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!could not load firmware chunk %u of %u", i,
			    fws->fw_count);
			return (err);
		}
	}

	iwm_enable_interrupts(sc);

	IWM_WRITE(sc, IWM_CSR_RESET, 0);

	return (0);
}

static int
iwm_load_cpu_sections_8000(struct iwm_softc *sc, struct iwm_fw_sects *fws,
    int cpu, int *first_ucode_section)
{
	int shift_param;
	int i, err = 0, sec_num = 0x1;
	uint32_t val, last_read_idx = 0;
	void *data;
	uint32_t dlen;
	uint32_t offset;

	if (cpu == 1) {
		shift_param = 0;
		*first_ucode_section = 0;
	} else {
		shift_param = 16;
		(*first_ucode_section)++;
	}

	for (i = *first_ucode_section; i < IWM_UCODE_SECT_MAX; i++) {
		last_read_idx = i;
		data = fws->fw_sect[i].fws_data;
		dlen = fws->fw_sect[i].fws_len;
		offset = fws->fw_sect[i].fws_devoff;

		/*
		 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between
		 * CPU1 to CPU2.
		 * PAGING_SEPARATOR_SECTION delimiter - separate between
		 * CPU2 non paged to CPU2 paging sec.
		 */
		if (!data || offset == IWM_CPU1_CPU2_SEPARATOR_SECTION ||
		    offset == IWM_PAGING_SEPARATOR_SECTION)
			break;

		if (dlen > sc->sc_fwdmasegsz) {
			err = EFBIG;
		} else
			err = iwm_firmware_load_sect(sc, offset, data, dlen);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not load firmware "
			    "chunk %d (error %d)", i, err);
			return (err);
		}

		/* Notify the ucode of the loaded section number and status */
		if (iwm_nic_lock(sc)) {
			val = IWM_READ(sc, IWM_FH_UCODE_LOAD_STATUS);
			val = val | (sec_num << shift_param);
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, val);
			sec_num = (sec_num << 1) | 0x1;
			iwm_nic_unlock(sc);
		} else {
			err = EBUSY;
			dev_err(sc->sc_dip, CE_WARN, "!could not load firmware "
			    "chunk %d (error %d)", i, err);
			return (err);
		}
	}

	*first_ucode_section = last_read_idx;

	if (iwm_nic_lock(sc)) {
		if (cpu == 1)
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, 0xFFFF);
		else
			IWM_WRITE(sc, IWM_FH_UCODE_LOAD_STATUS, 0xFFFFFFFF);
		iwm_nic_unlock(sc);
	} else {
		err = EBUSY;
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not finalize firmware loading (error %d)", err);
		return (err);
	}

	return (0);
}

static int
iwm_load_firmware_8000(struct iwm_softc *sc, enum iwm_ucode_type ucode_type)
{
	struct iwm_fw_sects *fws;
	int err = 0;
	int first_ucode_section;

	fws = &sc->sc_fw.fw_sects[ucode_type];

	/* configure the ucode to be ready to get the secured image */
	/* release CPU reset */
	if (iwm_nic_lock(sc)) {
		iwm_write_prph(sc, IWM_RELEASE_CPU_RESET,
		    IWM_RELEASE_CPU_RESET_BIT);
		iwm_nic_unlock(sc);
	}

	/* load to FW the binary Secured sections of CPU1 */
	err = iwm_load_cpu_sections_8000(sc, fws, 1, &first_ucode_section);
	if (err)
		return (err);

	/* load to FW the binary sections of CPU2 */
	err = iwm_load_cpu_sections_8000(sc, fws, 2, &first_ucode_section);
	if (err)
		return (err);

	iwm_enable_interrupts(sc);
	return (0);
}

static int
iwm_load_firmware(struct iwm_softc *sc, enum iwm_ucode_type ucode_type)
{
	int err;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));

	sc->sc_uc.uc_intr = 0;
	sc->sc_uc.uc_ok = 0;

	if (sc->sc_device_family >= IWM_DEVICE_FAMILY_8000)
		err = iwm_load_firmware_8000(sc, ucode_type);
	else
		err = iwm_load_firmware_7000(sc, ucode_type);

	if (err)
		return (err);

	/* wait for the firmware to load */
	err = 0;
	while (!sc->sc_uc.uc_intr) {
		err = iwm_sleep(sc, &sc->sc_fw_cv, 1000000);
		if (err)
			break;
	}
	if (err || !sc->sc_uc.uc_ok)
		dev_err(sc->sc_dip, CE_WARN, "!could not load firmware");

	return (err);
}

static int
iwm_start_fw(struct iwm_softc *sc, enum iwm_ucode_type ucode_type)
{
	int err;

	IWM_WRITE(sc, IWM_CSR_INT, ~0);

	err = iwm_nic_init(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!unable to init nic");
		return (err);
	}

	/* make sure rfkill handshake bits are cleared */
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR,
	    IWM_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable firmware load interrupt */
	IWM_WRITE(sc, IWM_CSR_INT, ~0);
	iwm_enable_fwload_interrupt(sc);

	/* really make sure rfkill handshake bits are cleared */
	/* maybe we should write a few times more?  just to make sure */
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);
	IWM_WRITE(sc, IWM_CSR_UCODE_DRV_GP1_CLR, IWM_CSR_UCODE_SW_BIT_RFKILL);

	return (iwm_load_firmware(sc, ucode_type));
}

static int
iwm_send_tx_ant_cfg(struct iwm_softc *sc, uint8_t valid_tx_ant)
{
	struct iwm_tx_ant_cfg_cmd tx_ant_cmd = {
		.valid = htole32(valid_tx_ant),
	};

	return (iwm_send_cmd_pdu(sc, IWM_TX_ANT_CONFIGURATION_CMD,
	    0, sizeof (tx_ant_cmd), &tx_ant_cmd));
}

static int
iwm_send_phy_cfg_cmd(struct iwm_softc *sc)
{
	struct iwm_phy_cfg_cmd phy_cfg_cmd;
	enum iwm_ucode_type ucode_type = sc->sc_uc_current;

	phy_cfg_cmd.phy_cfg = htole32(sc->sc_fw_phy_config |
	    sc->sc_extra_phy_config);
	phy_cfg_cmd.calib_control.event_trigger =
	    sc->sc_default_calib[ucode_type].event_trigger;
	phy_cfg_cmd.calib_control.flow_trigger =
	    sc->sc_default_calib[ucode_type].flow_trigger;

	return (iwm_send_cmd_pdu(sc, IWM_PHY_CONFIGURATION_CMD, 0,
	    sizeof (phy_cfg_cmd), &phy_cfg_cmd));
}

static int
iwm_send_dqa_cmd(struct iwm_softc *sc)
{
	struct iwm_dqa_enable_cmd dqa_cmd = {
		.cmd_queue = htole32(IWM_DQA_CMD_QUEUE),
	};
	uint32_t cmd_id;

	cmd_id = iwm_cmd_id(IWM_DQA_ENABLE_CMD, IWM_DATA_PATH_GROUP, 0);
	return (iwm_send_cmd_pdu(sc, cmd_id, 0, sizeof (dqa_cmd), &dqa_cmd));
}

static int
iwm_load_ucode_wait_alive(struct iwm_softc *sc,
    enum iwm_ucode_type ucode_type)
{
	enum iwm_ucode_type old_type = sc->sc_uc_current;
	struct iwm_fw_sects *fw = &sc->sc_fw.fw_sects[ucode_type];
	int err;

	err = iwm_read_firmware(sc);
	if (err)
		return (err);

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
		sc->cmdqid = IWM_DQA_CMD_QUEUE;
	else
		sc->cmdqid = IWM_CMD_QUEUE;

	sc->sc_uc_current = ucode_type;
	err = iwm_start_fw(sc, ucode_type);
	if (err) {
		sc->sc_uc_current = old_type;
		return (err);
	}

	err = iwm_post_alive(sc);
	if (err)
		return (err);

	/*
	 * configure and operate fw paging mechanism.
	 * driver configures the paging flow only once, CPU2 paging image
	 * included in the IWM_UCODE_INIT image.
	 */
	if (fw->paging_mem_size) {
		err = iwm_save_fw_paging(sc, fw);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!failed to save the FW paging image");
			return (err);
		}

		err = iwm_send_paging_cmd(sc, fw);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!failed to send the paging cmd");
			iwm_free_fw_paging(sc);
			return (err);
		}
	}

	return (0);
}

static int
iwm_run_init_mvm_ucode(struct iwm_softc *sc, int justnvm)
{
	const int wait_flags = (IWM_INIT_COMPLETE | IWM_CALIB_COMPLETE);
	int err;

	if ((sc->sc_flags & IWM_FLAG_RFKILL) && !justnvm) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!radio is disabled by hardware switch");
		return (EPERM);
	}

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	sc->sc_init_complete = 0;
	err = iwm_load_ucode_wait_alive(sc, IWM_UCODE_TYPE_INIT);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!failed to load init firmware");
		return (err);
	}

	if (sc->sc_device_family < IWM_DEVICE_FAMILY_8000) {
		err = iwm_send_bt_init_conf(sc);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!could not init bt coex (error %d)", err);
			return (err);
		}
	}

	if (justnvm) {
		err = iwm_nvm_init(sc);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!failed to read nvm");
			return (err);
		}

		if (IEEE80211_ADDR_EQ(iwm_etheranyaddr, sc->sc_ic.ic_macaddr))
			IEEE80211_ADDR_COPY(sc->sc_ic.ic_macaddr,
			    sc->sc_nvm.hw_addr);

		return (0);
	}

	err = iwm_sf_config(sc, IWM_SF_INIT_OFF);
	if (err) {
		return (err);
	}

	/* Send TX valid antennas before triggering calibrations */
	err = iwm_send_tx_ant_cfg(sc, iwm_fw_valid_tx_ant(sc));
	if (err) {
		return (err);
	}

	/*
	 * Send phy configurations command to init uCode
	 * to start the 16.0 uCode init image internal calibrations.
	 */
	err = iwm_send_phy_cfg_cmd(sc);
	if (err) {
		return (err);
	}

	/*
	 * Nothing to do but wait for the init complete and phy DB
	 * notifications from the firmware.
	 */
	while ((sc->sc_init_complete & wait_flags) != wait_flags) {
		err = iwm_sleep(sc, &sc->sc_fw_cv, 2000000);
		if (err)
			break;
	}

	return (err);
}

static int
iwm_config_ltr(struct iwm_softc *sc)
{
	struct iwm_ltr_config_cmd cmd = {
		.flags = htole32(IWM_LTR_CFG_FLAG_FEATURE_ENABLE),
	};

	if (!sc->sc_ltr_enabled)
		return (0);

	return (iwm_send_cmd_pdu(sc, IWM_LTR_CONFIG, 0, sizeof (cmd), &cmd));
}


static int
iwm_rx_addbuf(struct iwm_softc *sc, int size, int idx)
{
	struct iwm_rx_ring *ring = &sc->rxq;
	struct iwm_rx_data *data = &ring->data[idx];

	_NOTE(ARGUNUSED(size))
	/*
	 * Slots keep their DMA buffer; frames are copied out in iwm_rx_mpdu()
	 * (OpenBSD loads a fresh mbuf cluster here instead).
	 */
	iwm_dma_sync(&data->dma, DDI_DMA_SYNC_FORDEV);
	ring->desc[idx] = htole32(data->dma.paddr >> 8);
	(void) ddi_dma_sync(ring->free_desc_dma.dma_hdl,
	    idx * sizeof (uint32_t), sizeof (uint32_t), DDI_DMA_SYNC_FORDEV);
	return (0);
}

/*
 * RSSI values are reported by the FW as positive values - need to negate
 * to obtain their dBM.  Account for missing antennas by replacing 0
 * values by -256dBm: practically 0 power and a non-feasible 8 bit value.
 */
static int
iwm_get_signal_strength(struct iwm_softc *sc, struct iwm_rx_phy_info *phy_info)
{
	int energy_a, energy_b, energy_c, max_energy;
	uint32_t val;

	_NOTE(ARGUNUSED(sc))
	val = le32toh(phy_info->non_cfg_phy[IWM_RX_INFO_ENERGY_ANT_ABC_IDX]);
	energy_a = (val & IWM_RX_INFO_ENERGY_ANT_A_MSK) >>
	    IWM_RX_INFO_ENERGY_ANT_A_POS;
	energy_a = energy_a ? -energy_a : -256;
	energy_b = (val & IWM_RX_INFO_ENERGY_ANT_B_MSK) >>
	    IWM_RX_INFO_ENERGY_ANT_B_POS;
	energy_b = energy_b ? -energy_b : -256;
	energy_c = (val & IWM_RX_INFO_ENERGY_ANT_C_MSK) >>
	    IWM_RX_INFO_ENERGY_ANT_C_POS;
	energy_c = energy_c ? -energy_c : -256;
	max_energy = MAX(energy_a, energy_b);
	max_energy = MAX(max_energy, energy_c);

	return (max_energy);
}

static void
iwm_rx_rx_phy_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data)
{
	struct iwm_rx_phy_info *phy_info = (void *)pkt->data;

	_NOTE(ARGUNUSED(data))
	if (iwm_rx_packet_payload_len(pkt) < sizeof (*phy_info))
		return;
	memcpy(&sc->sc_last_phy_info, phy_info, sizeof (sc->sc_last_phy_info));
}

/*
 * Retrieve the average noise (in dBm) among receivers.
 */
static int
iwm_get_noise(const struct iwm_statistics_rx_non_phy *stats)
{
	int i, total, nbant, noise;

	total = nbant = noise = 0;
	for (i = 0; i < 3; i++) {
		noise = le32toh(stats->beacon_silence_rssi[i]) & 0xff;
		if (noise) {
			total += noise;
			nbant++;
		}
	}

	/* There should be at least one antenna but check anyway. */
	return ((nbant == 0) ? -127 : (total / nbant) - 107);
}

/*
 * Check firmware decryption of a received unicast frame protected with our
 * hardware pairwise CCMP key (OpenBSD iwm_rx_hwdecrypt).  The firmware
 * leaves the CCMP header and MIC in place; illumos net80211 then checks
 * the PN and strips them because the key has no IEEE80211_KEY_SWCRYPT.
 * Returns non-zero if the frame must be dropped.
 */
static int
iwm_rx_hwdecrypt(struct iwm_softc *sc, mblk_t *m, uint32_t rx_pkt_status)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	uint8_t type, subtype, keyid;
	int hdrlen;

	wh = (struct ieee80211_frame *)m->b_rptr;

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	if (type == IEEE80211_FC0_TYPE_CTL)
		return (0);

	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if ((wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_QOS) &&
	    (subtype & IEEE80211_FC0_SUBTYPE_NODATA))
		return (0);

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    !(wh->i_fc[1] & IEEE80211_FC1_WEP))
		return (0);

	hdrlen = ieee80211_hdrspace(ic, wh);
	if (MBLKL(m) < hdrlen + IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN)
		return (1);
	keyid = m->b_rptr[hdrlen + IEEE80211_WEP_IVLEN] >> 6;
	k = &ic->ic_nw_keys[keyid & 3];
	if (k->wk_cipher == NULL ||
	    k->wk_cipher->ic_cipher != IEEE80211_CIPHER_AES_CCM ||
	    (k->wk_flags & IEEE80211_KEY_SWCRYPT))
		return (0);	/* software key; net80211 decrypts */

	if ((rx_pkt_status & IWM_RX_MPDU_RES_STATUS_SEC_ENC_MSK) !=
	    IWM_RX_MPDU_RES_STATUS_SEC_CCM_ENC)
		return (1);
	/* Check whether decryption was successful or not. */
	if ((rx_pkt_status &
	    (IWM_RX_MPDU_RES_STATUS_DEC_DONE |
	    IWM_RX_MPDU_RES_STATUS_MIC_OK)) !=
	    (IWM_RX_MPDU_RES_STATUS_DEC_DONE |
	    IWM_RX_MPDU_RES_STATUS_MIC_OK))
		return (1);
	return (0);
}

/*
 * Queue a received frame for iwm_rx_deliver(), which hands it to
 * net80211 once sc_mtx is dropped (OpenBSD collects an mbuf_list).
 * The normalised RSSI rides in b_band.
 */
static void
iwm_rx_frame(struct iwm_softc *sc, mblk_t *m, int rssi, uint32_t tstamp)
{
	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	m->b_band = (uchar_t)MAX(0, MIN(rssi, 255));
	/*
	 * Device timestamp (GP2, usec) for net80211's in_rstamp; the MAC
	 * context uses it for DTIM timing (OpenBSD rxi_tstamp).  b_prev is
	 * unused while the frame sits on our list.
	 */
	m->b_prev = (mblk_t *)(uintptr_t)tstamp;
	m->b_next = NULL;
	if (sc->sc_rx_tail == NULL)
		sc->sc_rx_head = m;
	else
		sc->sc_rx_tail->b_next = m;
	sc->sc_rx_tail = m;
	sc->sc_ipackets++;
	sc->sc_rbytes += MBLKL(m);
}

/* Pass queued frames to net80211.  Called without sc_mtx. */
static void
iwm_rx_deliver(struct iwm_softc *sc, mblk_t *m)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	mblk_t *next;
	uint32_t tstamp;

	ASSERT(!MUTEX_HELD(&sc->sc_mtx));
	for (; m != NULL; m = next) {
		next = m->b_next;
		m->b_next = NULL;
		ni = ieee80211_find_rxnode(ic,
		    (struct ieee80211_frame *)m->b_rptr);
		tstamp = (uint32_t)(uintptr_t)m->b_prev;
		m->b_prev = NULL;
		(void) ieee80211_input(ic, m, ni, m->b_band, tstamp);
		ieee80211_free_node(ni);
	}
}

static void
iwm_rx_mpdu(struct iwm_softc *sc, void *pktdata, size_t maxlen)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_rx_phy_info *phy_info;
	struct iwm_rx_mpdu_res_start *rx_res;
	uint32_t len;
	uint32_t rx_pkt_status;
	int rssi;
	mblk_t *m;

	phy_info = &sc->sc_last_phy_info;
	rx_res = (struct iwm_rx_mpdu_res_start *)pktdata;
	if (maxlen < sizeof (*rx_res) + sizeof (uint32_t)) {
		sc->sc_ierrors++;
		return;
	}
	len = le16toh(rx_res->byte_count);
	if (len < sizeof (struct ieee80211_frame)) {
		sc->sc_ierrors++;
		return;
	}
	if (len > maxlen - sizeof (*rx_res) - sizeof (uint32_t)) {
		sc->sc_ierrors++;
		return;
	}

	if (phy_info->cfg_phy_cnt > 20)
		return;

	memcpy(&rx_pkt_status, (uint8_t *)pktdata + sizeof (*rx_res) + len,
	    sizeof (rx_pkt_status));
	rx_pkt_status = le32toh(rx_pkt_status);
	if (!(rx_pkt_status & IWM_RX_MPDU_RES_STATUS_CRC_OK) ||
	    !(rx_pkt_status & IWM_RX_MPDU_RES_STATUS_OVERRUN_OK))
		return; /* drop */

	if ((m = allocb(len, BPRI_MED)) == NULL) {
		sc->sc_ierrors++;
		return;
	}
	memcpy(m->b_wptr, (uint8_t *)pktdata + sizeof (*rx_res), len);
	m->b_wptr += len;

	if (iwm_rx_hwdecrypt(sc, m, rx_pkt_status)) {
		sc->sc_ierrors++;
		freemsg(m);
		return;
	}

	rssi = iwm_get_signal_strength(sc, phy_info);
	rssi = (0 - IWM_MIN_DBM) + rssi;	/* normalize */
	rssi = MIN(rssi, ic->ic_maxrssi);	/* clip to max. 100% */

	iwm_rx_frame(sc, m, rssi, le32toh(phy_info->system_timestamp));
}

/* Describe the peer for MiRA (OpenBSD reads this from ieee80211_node). */
static void
iwm_ra_peer(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_ra_peer *p)
{
	struct ieee80211_node *ni = &in->in_ni;
	int i;

	p->txmcs = in->in_txmcs;
	p->chan40 = (in->in_phyctxt != NULL &&
	    in->in_phyctxt->sco != IWM_HTOP0_SCO_SCN);
	if (p->chan40)
		p->sgi = (ni->in_htcap & IEEE80211_HTCAP_SHORTGI40) != 0;
	else
		p->sgi = (ni->in_htcap & IEEE80211_HTCAP_SHORTGI20) != 0;
	p->valid_mcs = 0;
	for (i = 0; i < ni->in_htrates.rs_nrates; i++) {
		int mcs = ni->in_htrates.rs_rates[i] & IEEE80211_RATE_VAL;

		if (mcs >= IWM_HT_RATESET_NUM_MCS)
			continue;
		if (mcs >= 8 && (!iwm_mimo_enabled(sc) ||
		    (iwm_fw_valid_tx_ant(sc) & IWM_ANT_AB) != IWM_ANT_AB))
			continue;
		p->valid_mcs |= (1U << mcs);
	}
}

static void
iwm_ra_choose(struct iwm_softc *sc, struct ieee80211_node *ni)
{
	struct iwm_node *in = (void *)ni;
	struct iwm_ra_peer peer;
	int old_txmcs = in->in_txmcs;

	iwm_ra_peer(sc, in, &peer);
	iwm_ra_choose_peer(&in->in_rn, &peer);
	in->in_txmcs = peer.txmcs;

	/*
	 * If RA has chosen a new TX rate we must update
	 * the firmware's LQ rate table.
	 */
	if (in->in_txmcs != old_txmcs)
		iwm_setrates(in, 1);
}

static void
iwm_ra_add_stats(struct iwm_softc *sc, struct iwm_node *in, int mcs,
    uint32_t total, uint32_t fail)
{
	struct iwm_ra_peer peer;

	iwm_ra_peer(sc, in, &peer);
	iwm_ra_add_stats_ht(&in->in_rn, &peer, mcs, total, fail);
}

static void
iwm_ht_single_rate_control(struct iwm_softc *sc, struct ieee80211_node *ni,
    int txmcs, uint8_t failure_frame, int txfail)
{
	struct iwm_node *in = (void *)ni;

	/* Ignore Tx reports which don't match our last LQ command. */
	if (txmcs != in->in_txmcs) {
		if (++in->lq_rate_mismatch > 15) {
			/* Try to sync firmware with the driver... */
			iwm_setrates(in, 1);
			in->lq_rate_mismatch = 0;
		}
	} else {
		int mcs = txmcs;
		struct iwm_ra_peer peer;
		const struct iwm_ht_rateset *rs;
		unsigned int retries = 0, i;

		iwm_ra_peer(sc, in, &peer);
		rs = iwm_ra_get_ht_rateset(txmcs, peer.chan40, peer.sgi);
		in->lq_rate_mismatch = 0;

		for (i = 0; i < failure_frame; i++) {
			if (mcs > rs->min_mcs) {
				iwm_ra_add_stats(sc, in, mcs, 1, 1);
				mcs--;
			} else
				retries++;
		}

		if (txfail && failure_frame == 0) {
			iwm_ra_add_stats(sc, in, txmcs, 1, 1);
		} else {
			iwm_ra_add_stats(sc, in, mcs, retries + 1, retries);
		}

		iwm_ra_choose(sc, ni);
	}
}

static void
iwm_rx_tx_cmd_single(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_node *in, int txmcs, int txrate)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	struct iwm_tx_resp *tx_resp = (void *)pkt->data;
	int status = le16toh(tx_resp->status.status) & IWM_TX_STATUS_MSK;
	uint32_t initial_rate = le32toh(tx_resp->initial_rate);
	int txfail;

	_NOTE(ARGUNUSED(txmcs))
	ASSERT(tx_resp->frame_count == 1);

	txfail = (status != IWM_TX_STATUS_SUCCESS &&
	    status != IWM_TX_STATUS_DIRECT_DONE);

	/*
	 * Update rate control statistics.
	 * Only report frames which were actually queued with the currently
	 * selected Tx rate. Because Tx queues are relatively long we may
	 * encounter previously selected rates here during Tx bursts.
	 * Providing feedback based on such frames can lead to suboptimal
	 * Tx rate control decisions.
	 */
	if ((ni->in_flags & IEEE80211_NODE_HT) == 0) {
		if (txrate != ni->in_txrate) {
			if (++in->lq_rate_mismatch > 15) {
				/* Try to sync firmware with the driver... */
				iwm_setrates(in, 1);
				in->lq_rate_mismatch = 0;
			}
		} else {
			in->lq_rate_mismatch = 0;

			in->in_amn.amn_txcnt++;
			if (txfail)
				in->in_amn.amn_retrycnt++;
			if (tx_resp->failure_frame > 0)
				in->in_amn.amn_retrycnt++;
		}
	} else if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE &&
	    ic->ic_state == IEEE80211_S_RUN &&
	    (initial_rate & IWM_RATE_MCS_HT_MSK)) {
		int txmcs = initial_rate &
		    (IWM_RATE_HT_MCS_RATE_CODE_MSK | IWM_RATE_HT_MCS_NSS_MSK);
		iwm_ht_single_rate_control(sc, ni, txmcs,
		    tx_resp->failure_frame, txfail);
	}

	if (txfail)
		sc->sc_oerrors++;
	ic->ic_stats.is_tx_retries += tx_resp->failure_frame;
	if (txfail)
		ic->ic_stats.is_tx_failed++;
}

static void
iwm_txd_done(struct iwm_softc *sc, struct iwm_tx_data *txd)
{
	ASSERT(txd->in);
	iwm_txd_release(sc, txd);
	txd->ampdu_nframes = 0;
	txd->ampdu_txmcs = 0;
	txd->ampdu_txnss = 0;
}

static void
iwm_txq_advance(struct iwm_softc *sc, struct iwm_tx_ring *ring, int idx)
{
	struct iwm_tx_data *txd;

	while (ring->tail != idx) {
		txd = &ring->data[ring->tail];
		if (txd->m != NULL) {
			iwm_reset_sched(sc, ring->qid, ring->tail,
			    IWM_STATION_ID);
			iwm_txd_done(sc, txd);
			ring->queued--;
		}
		ring->tail = (ring->tail + 1) % IWM_TX_RING_COUNT;
	}

	cv_broadcast(&sc->sc_txq_cv);
}

/* OpenBSD ieee80211_output_ba_move_window(): record the new BA window. */
static void
iwm_ba_move_window(struct iwm_softc *sc, struct ieee80211_node *ni, int tid,
    uint16_t ssn)
{
	struct ieee80211_tx_ampdu *tap = &ni->in_tx_ampdu[TID_TO_WME_AC(tid)];

	sc->sc_tx_ba_winstart[tid] = ssn;
	tap->txa_start = ssn;
}

static void
iwm_ampdu_tx_done(struct iwm_softc *sc, struct iwm_cmd_header *cmd_hdr,
    struct iwm_node *in, struct iwm_tx_ring *txq, uint32_t initial_rate,
    uint8_t nframes, uint8_t failure_frame, uint16_t ssn, int status,
    struct iwm_agg_tx_status *agg_status)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int tid = cmd_hdr->qid - IWM_FIRST_AGG_TX_QUEUE;
	struct iwm_tx_data *txdata = &txq->data[cmd_hdr->idx];
	struct ieee80211_node *ni = &in->in_ni;
	int txfail = (status != IWM_TX_STATUS_SUCCESS &&
	    status != IWM_TX_STATUS_DIRECT_DONE);

	if (ic->ic_state != IEEE80211_S_RUN)
		return;

	if (nframes > 1) {
		int i;
		/*
		 * Collect information about this A-MPDU.
		 */

		for (i = 0; i < nframes; i++) {
			uint8_t qid = agg_status[i].qid;
			uint8_t idx = agg_status[i].idx;
			uint16_t txstatus = (le16toh(agg_status[i].status) &
			    IWM_AGG_TX_STATE_STATUS_MSK);

			if (txstatus != IWM_AGG_TX_STATE_TRANSMITTED)
				continue;

			if (qid != cmd_hdr->qid)
				continue;

			txdata = &txq->data[idx];
			if (txdata->m == NULL)
				continue;

			/* The Tx rate was the same for all subframes. */
			if (initial_rate & IWM_RATE_MCS_HT_MSK) {
				txdata->ampdu_txmcs = initial_rate &
				    (IWM_RATE_HT_MCS_RATE_CODE_MSK |
				    IWM_RATE_HT_MCS_NSS_MSK);
				txdata->ampdu_nframes = nframes;
			}
		}
		return;
	}

	if ((sc->tx_ba_queue_mask & (1 << cmd_hdr->qid)) == 0)
		return;
	if (IWM_SEQ_LT(ssn, sc->sc_tx_ba_winstart[tid]))
		return;

	/*
	 * Skip rate control if our Tx rate is fixed.
	 * Don't report frames to MiRA which were sent at a different
	 * Tx rate than in_txmcs.
	 */
	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE) {
		if (txdata->ampdu_nframes > 1) {
			/*
			 * This frame was once part of an A-MPDU.
			 * Report one failed A-MPDU Tx attempt.
			 * The firmware might have made several such
			 * attempts but we don't keep track of this.
			 */
			iwm_ra_add_stats(sc, in, txdata->ampdu_txmcs, 1, 1);
		}

		/* Report the final single-frame Tx attempt. */
		if (initial_rate & IWM_RATE_MCS_HT_MSK) {
			int txmcs = initial_rate &
			    (IWM_RATE_HT_MCS_RATE_CODE_MSK |
			    IWM_RATE_HT_MCS_NSS_MSK);
			iwm_ht_single_rate_control(sc, ni, txmcs,
			    failure_frame, txfail);
		}
	}

	/* Ask the peer to move its window past the failed frame. */
	if (txfail) {
		sc->sc_bar_pending |= (1 << tid);
		sc->sc_bar_ssn[tid] = ssn;
	}

	/*
	 * SSN corresponds to the first (perhaps not yet transmitted) frame
	 * in firmware's BA window. Firmware is not going to retransmit any
	 * frames before its BA window so mark them all as done.
	 */
	iwm_ba_move_window(sc, ni, tid, ssn);
	iwm_txq_advance(sc, txq, IWM_AGG_SSN_TO_TXQ_IDX(ssn));
	iwm_clear_oactive(sc, txq);
}

static void
iwm_rx_tx_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data)
{
	struct iwm_cmd_header *cmd_hdr = &pkt->hdr;
	int idx = cmd_hdr->idx;
	int qid = cmd_hdr->qid;
	struct iwm_tx_ring *ring;
	struct iwm_tx_data *txd;
	struct iwm_tx_resp *tx_resp = (void *)pkt->data;
	uint32_t ssn;
	uint32_t len = iwm_rx_packet_len(pkt);

	_NOTE(ARGUNUSED(data))
	/* Sanity checks. */
	if (sizeof (*tx_resp) > len)
		return;
	if (qid < IWM_FIRST_AGG_TX_QUEUE && tx_resp->frame_count > 1)
		return;
	if (qid > IWM_LAST_AGG_TX_QUEUE)
		return;
	if (sizeof (*tx_resp) + sizeof (ssn) +
	    tx_resp->frame_count * sizeof (tx_resp->status) > len)
		return;

	ring = &sc->txq[qid];
	sc->sc_tx_timer[qid] = 0;

	txd = &ring->data[idx];
	if (txd->m == NULL)
		return;

	memcpy(&ssn, &tx_resp->status + tx_resp->frame_count, sizeof (ssn));
	ssn = le32toh(ssn) & 0xfff;
	if (qid >= IWM_FIRST_AGG_TX_QUEUE) {
		int status;
		status = le16toh(tx_resp->status.status) & IWM_TX_STATUS_MSK;
		iwm_ampdu_tx_done(sc, cmd_hdr, txd->in, ring,
		    le32toh(tx_resp->initial_rate), tx_resp->frame_count,
		    tx_resp->failure_frame, ssn, status, &tx_resp->status);
	} else {
		/*
		 * Even though this is not an agg queue, we must only free
		 * frames before the firmware's starting sequence number.
		 */
		iwm_rx_tx_cmd_single(sc, pkt, txd->in, txd->txmcs, txd->txrate);
		iwm_txq_advance(sc, ring, IWM_AGG_SSN_TO_TXQ_IDX(ssn));
		iwm_clear_oactive(sc, ring);
	}
}

/*
 * Tx ring has drained below the low mark: let the MAC layer resume
 * (OpenBSD restarts the ifq).  mac_tx_update() is called after sc_mtx
 * drops, from iwm_intr().
 */
static void
iwm_clear_oactive(struct iwm_softc *sc, struct iwm_tx_ring *ring)
{
	if (ring->queued < IWM_TX_RING_LOMARK) {
		sc->qfullmsk &= ~(1 << ring->qid);
		if (sc->qfullmsk == 0 && (sc->sc_flags & IWM_FLAG_OACTIVE)) {
			sc->sc_flags &= ~IWM_FLAG_OACTIVE;
			sc->sc_tx_update = 1;
		}
	}
}

static void
iwm_ampdu_rate_control(struct iwm_softc *sc, struct ieee80211_node *ni,
    struct iwm_tx_ring *txq, int tid, uint16_t seq, uint16_t ssn)
{
	struct iwm_node *in = (void *)ni;
	int idx, end_idx;

	_NOTE(ARGUNUSED(tid))
	/*
	 * Update Tx rate statistics for A-MPDUs before firmware's BA window.
	 */
	idx = IWM_AGG_SSN_TO_TXQ_IDX(seq);
	end_idx = IWM_AGG_SSN_TO_TXQ_IDX(ssn);
	while (idx != end_idx) {
		struct iwm_tx_data *txdata = &txq->data[idx];
		if (txdata->m != NULL && txdata->ampdu_nframes > 1) {
			/*
			 * We can assume that this subframe has been ACKed
			 * because ACK failures come as single frames and
			 * before failing an A-MPDU subframe the firmware
			 * sends it as a single frame at least once.
			 */
			iwm_ra_add_stats(sc, in, txdata->ampdu_txmcs, 1, 0);
			/* Report this frame only once. */
			txdata->ampdu_nframes = 0;
		}

		idx = (idx + 1) % IWM_TX_RING_COUNT;
	}

	iwm_ra_choose(sc, ni);
}

static void
iwm_rx_compressed_ba(struct iwm_softc *sc, struct iwm_rx_packet *pkt)
{
	struct iwm_ba_notif *ban = (void *)pkt->data;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct iwm_node *in = (void *)ni;
	struct iwm_tx_ring *ring;
	uint16_t ssn;
	int qid;

	if (ic->ic_state != IEEE80211_S_RUN)
		return;

	if (iwm_rx_packet_payload_len(pkt) < sizeof (*ban))
		return;

	if (ban->sta_id != IWM_STATION_ID ||
	    !IEEE80211_ADDR_EQ(in->in_macaddr, ban->sta_addr))
		return;

	qid = le16toh(ban->scd_flow);
	if (qid < IWM_FIRST_AGG_TX_QUEUE || qid > IWM_LAST_AGG_TX_QUEUE)
		return;

	/* Protect against a firmware bug where the queue/TID are off. */
	if (qid != IWM_FIRST_AGG_TX_QUEUE + ban->tid)
		return;

	sc->sc_tx_timer[qid] = 0;

	if ((sc->tx_ba_queue_mask & (1 << qid)) == 0)
		return;

	ring = &sc->txq[qid];

	/*
	 * The firmware's new BA window starting sequence number
	 * corresponds to the first hole in ban->scd_ssn, implying
	 * that all frames between our window start and 'ssn'
	 * (non-inclusive) have been acked.  As in OpenBSD we track the
	 * window start ourselves rather than trusting ban->seq_ctl.
	 */
	ssn = le16toh(ban->scd_ssn);

	if (IWM_SEQ_LT(ssn, sc->sc_tx_ba_winstart[ban->tid]))
		return;

	/* Skip rate control if our Tx rate is fixed. */
	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE)
		iwm_ampdu_rate_control(sc, ni, ring, ban->tid,
		    sc->sc_tx_ba_winstart[ban->tid], ssn);

	/*
	 * SSN corresponds to the first (perhaps not yet transmitted) frame
	 * in firmware's BA window. Firmware is not going to retransmit any
	 * frames before its BA window so mark them all as done.
	 */
	iwm_ba_move_window(sc, ni, ban->tid, ssn);
	iwm_txq_advance(sc, ring, IWM_AGG_SSN_TO_TXQ_IDX(ssn));
	iwm_clear_oactive(sc, ring);
}

static void
iwm_rx_bmiss(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_missed_beacons_notif *mbn = (void *)pkt->data;
	uint32_t missed;

	_NOTE(ARGUNUSED(data))
	if ((ic->ic_opmode != IEEE80211_M_STA) ||
	    (ic->ic_state != IEEE80211_S_RUN))
		return;
	if (iwm_rx_packet_payload_len(pkt) < sizeof (*mbn))
		return;

	missed = le32toh(mbn->consec_missed_beacons_since_last_rx);
	if (missed > ic->ic_bmissthreshold) {
		/*
		 * Rather than go directly to scan state, net80211's
		 * ieee80211_beacon_miss() sends a directed probe request
		 * first and only rescans if that goes unanswered (OpenBSD
		 * does the same by hand).  Called after sc_mtx drops.
		 */
		sc->sc_bmiss_pending = 1;
	}
}
static int
iwm_binding_cmd(struct iwm_softc *sc, struct iwm_node *in, uint32_t action)
{
	struct iwm_binding_cmd cmd;
	struct iwm_phy_ctxt *phyctxt = in->in_phyctxt;
	uint32_t mac_id = IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color);
	int i, err, active = (sc->sc_flags & IWM_FLAG_BINDING_ACTIVE);
	uint32_t status;
	size_t len;

	if (action == IWM_FW_CTXT_ACTION_ADD && active)
		return (EBUSY);		/* OpenBSD panics here */
	if (action == IWM_FW_CTXT_ACTION_REMOVE && !active)
		return (ENOENT);

	if (phyctxt == NULL) /* XXX race with iwm_stop() */
		return (EINVAL);

	memset(&cmd, 0, sizeof (cmd));

	cmd.id_and_color
	    = htole32(IWM_FW_CMD_ID_AND_COLOR(phyctxt->id, phyctxt->color));
	cmd.action = htole32(action);
	cmd.phy = htole32(IWM_FW_CMD_ID_AND_COLOR(phyctxt->id, phyctxt->color));

	cmd.macs[0] = htole32(mac_id);
	for (i = 1; i < IWM_MAX_MACS_IN_BINDING; i++)
		cmd.macs[i] = htole32(IWM_FW_CTXT_INVALID);

	if (IEEE80211_IS_CHAN_2GHZ(phyctxt->channel) ||
	    !isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_CDB_SUPPORT))
		cmd.lmac_id = htole32(IWM_LMAC_24G_INDEX);
	else
		cmd.lmac_id = htole32(IWM_LMAC_5G_INDEX);

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT))
		len = sizeof (cmd);
	else
		len = sizeof (struct iwm_binding_cmd_v1);
	status = 0;
	err = iwm_send_cmd_pdu_status(sc, IWM_BINDING_CONTEXT_CMD, len, &cmd,
	    &status);
	if (err == 0 && status != 0)
		err = EIO;

	return (err);
}

static void
iwm_phy_ctxt_cmd_hdr(struct iwm_softc *sc, struct iwm_phy_ctxt *ctxt,
    struct iwm_phy_context_cmd *cmd, uint32_t action, uint32_t apply_time)
{
	memset(cmd, 0, sizeof (struct iwm_phy_context_cmd));

	cmd->id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(ctxt->id,
	    ctxt->color));
	cmd->action = htole32(action);
	cmd->apply_time = htole32(apply_time);
}

static void
iwm_phy_ctxt_cmd_data(struct iwm_softc *sc, struct iwm_phy_context_cmd *cmd,
    struct ieee80211_channel *chan, uint8_t chains_static,
    uint8_t chains_dynamic, uint8_t sco, uint8_t vht_chan_width)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t active_cnt, idle_cnt;

	cmd->ci.band = IEEE80211_IS_CHAN_2GHZ(chan) ?
	    IWM_PHY_BAND_24 : IWM_PHY_BAND_5;
	cmd->ci.channel = ieee80211_chan2ieee(ic, chan);
	_NOTE(ARGUNUSED(vht_chan_width))
	if (chan->ich_flags & IEEE80211_CHAN_HT40) {
		if (sco == IWM_HTOP0_SCO_SCA) {
			/* secondary chan above -> control chan below */
			cmd->ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
			cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE40;
		} else if (sco == IWM_HTOP0_SCO_SCB) {
			/* secondary chan below -> control chan above */
			cmd->ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_ABOVE;
			cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE40;
		} else {
			cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE20;
			cmd->ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
		}
	} else {
		cmd->ci.width = IWM_PHY_VHT_CHANNEL_MODE20;
		cmd->ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
	}

	/* Set rx the chains */
	idle_cnt = chains_static;
	active_cnt = chains_dynamic;

	cmd->rxchain_info = htole32(iwm_fw_valid_rx_ant(sc) <<
					IWM_PHY_RX_CHAIN_VALID_POS);
	cmd->rxchain_info |= htole32(idle_cnt << IWM_PHY_RX_CHAIN_CNT_POS);
	cmd->rxchain_info |= htole32(active_cnt <<
	    IWM_PHY_RX_CHAIN_MIMO_CNT_POS);

	cmd->txchain_info = htole32(iwm_fw_valid_tx_ant(sc));
}

static int
iwm_phy_ctxt_cmd_uhb(struct iwm_softc *sc, struct iwm_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action,
    uint32_t apply_time, uint8_t sco, uint8_t vht_chan_width)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_phy_context_cmd_uhb cmd;
	uint8_t active_cnt, idle_cnt;
	struct ieee80211_channel *chan = ctxt->channel;

	memset(&cmd, 0, sizeof (cmd));
	cmd.id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(ctxt->id,
	    ctxt->color));
	cmd.action = htole32(action);
	cmd.apply_time = htole32(apply_time);

	cmd.ci.band = IEEE80211_IS_CHAN_2GHZ(chan) ?
	    IWM_PHY_BAND_24 : IWM_PHY_BAND_5;
	cmd.ci.channel = htole32(ieee80211_chan2ieee(ic, chan));
	_NOTE(ARGUNUSED(vht_chan_width))
	if (chan->ich_flags & IEEE80211_CHAN_HT40) {
		if (sco == IWM_HTOP0_SCO_SCA) {
			/* secondary chan above -> control chan below */
			cmd.ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
			cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE40;
		} else if (sco == IWM_HTOP0_SCO_SCB) {
			/* secondary chan below -> control chan above */
			cmd.ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_ABOVE;
			cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE40;
		} else {
			cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE20;
			cmd.ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
		}
	} else {
		cmd.ci.width = IWM_PHY_VHT_CHANNEL_MODE20;
		cmd.ci.ctrl_pos = IWM_PHY_VHT_CTRL_POS_1_BELOW;
	}

	idle_cnt = chains_static;
	active_cnt = chains_dynamic;
	cmd.rxchain_info = htole32(iwm_fw_valid_rx_ant(sc) <<
					IWM_PHY_RX_CHAIN_VALID_POS);
	cmd.rxchain_info |= htole32(idle_cnt << IWM_PHY_RX_CHAIN_CNT_POS);
	cmd.rxchain_info |= htole32(active_cnt <<
	    IWM_PHY_RX_CHAIN_MIMO_CNT_POS);
	cmd.txchain_info = htole32(iwm_fw_valid_tx_ant(sc));

	return (iwm_send_cmd_pdu(sc, IWM_PHY_CONTEXT_CMD, 0, sizeof (cmd),
	    &cmd));
}

static int
iwm_phy_ctxt_cmd(struct iwm_softc *sc, struct iwm_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action,
    uint32_t apply_time, uint8_t sco, uint8_t vht_chan_width)
{
	struct iwm_phy_context_cmd cmd;

	/*
	 * Intel increased the size of the fw_channel_info struct and neglected
	 * to bump the phy_context_cmd struct, which contains an fw_channel_info
	 * member in the middle.
	 * To keep things simple we use a separate function to handle the larger
	 * variant of the phy context command.
	 */
	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS))
		return (iwm_phy_ctxt_cmd_uhb(sc, ctxt, chains_static,
		    chains_dynamic, action, apply_time, sco, vht_chan_width));

	iwm_phy_ctxt_cmd_hdr(sc, ctxt, &cmd, action, apply_time);

	iwm_phy_ctxt_cmd_data(sc, &cmd, ctxt->channel,
	    chains_static, chains_dynamic, sco, vht_chan_width);

	return (iwm_send_cmd_pdu(sc, IWM_PHY_CONTEXT_CMD, 0,
	    sizeof (struct iwm_phy_context_cmd), &cmd));
}

static int
iwm_send_cmd(struct iwm_softc *sc, struct iwm_host_cmd *hcmd)
{
	struct iwm_tx_ring *ring = &sc->txq[sc->cmdqid];
	struct iwm_tfd *desc;
	struct iwm_tx_data *txdata;
	struct iwm_device_cmd *cmd;
	bus_addr_t paddr;
	uint32_t addr_lo;
	int err = 0, i, paylen, off;
	int idx, code, async, group_id;
	size_t hdrlen, datasz;
	uint8_t *data;
	int generation = sc->sc_generation;
	clock_t deadline;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	code = hcmd->id;
	async = hcmd->flags & IWM_CMD_ASYNC;
	idx = ring->cur;

	for (i = 0, paylen = 0; i < (int)nitems(hcmd->len); i++) {
		paylen += hcmd->len[i];
	}

	/* If this command waits for a response, allocate response buffer. */
	hcmd->resp_pkt = NULL;
	if (hcmd->flags & IWM_CMD_WANT_RESP) {
		uint8_t *resp_buf;
		ASSERT(!async);
		ASSERT(hcmd->resp_pkt_len >= sizeof (struct iwm_rx_packet));
		ASSERT(hcmd->resp_pkt_len <= IWM_CMD_RESP_MAX);
		if (sc->sc_cmd_resp_pkt[idx] != NULL)
			return (ENOSPC);
		resp_buf = kmem_zalloc(hcmd->resp_pkt_len, KM_NOSLEEP);
		if (resp_buf == NULL)
			return (ENOMEM);
		sc->sc_cmd_resp_pkt[idx] = resp_buf;
		sc->sc_cmd_resp_len[idx] = hcmd->resp_pkt_len;
	} else {
		sc->sc_cmd_resp_pkt[idx] = NULL;
	}

	desc = &ring->desc[idx];
	txdata = &ring->data[idx];

	group_id = iwm_cmd_groupid(code);
	if (group_id != 0) {
		hdrlen = sizeof (cmd->hdr_wide);
		datasz = sizeof (cmd->data_wide);
	} else {
		hdrlen = sizeof (cmd->hdr);
		datasz = sizeof (cmd->data);
	}

	if (paylen > datasz) {
		/*
		 * Command is too large to fit in pre-allocated space; use the
		 * slot's page-sized payload buffer (OpenBSD maps an mbuf).
		 */
		size_t totlen = hdrlen + paylen;
		if (paylen > IWM_MAX_CMD_PAYLOAD_SIZE ||
		    txdata->buf == NULL || totlen > PAGESIZE) {
			dev_err(sc->sc_dip, CE_WARN, "!firmware command too "
			    "long (%lu bytes)", (ulong_t)totlen);
			err = EINVAL;
			goto out;
		}
		cmd = (struct iwm_device_cmd *)(void *)txdata->buf;
		paddr = txdata->buf_paddr;
	} else {
		cmd = &ring->cmd[idx];
		paddr = txdata->cmd_paddr;
	}

	if (group_id != 0) {
		cmd->hdr_wide.opcode = iwm_cmd_opcode(code);
		cmd->hdr_wide.group_id = group_id;
		cmd->hdr_wide.qid = ring->qid;
		cmd->hdr_wide.idx = idx;
		cmd->hdr_wide.length = htole16(paylen);
		cmd->hdr_wide.version = iwm_cmd_version(code);
		data = cmd->data_wide;
	} else {
		cmd->hdr.code = code;
		cmd->hdr.flags = 0;
		cmd->hdr.qid = ring->qid;
		cmd->hdr.idx = idx;
		data = cmd->data;
	}

	for (i = 0, off = 0; i < (int)nitems(hcmd->data); i++) {
		if (hcmd->len[i] == 0)
			continue;
		memcpy(data + off, hcmd->data[i], hcmd->len[i]);
		off += hcmd->len[i];
	}
	ASSERT(off == paylen);

	/* lo field is not aligned */
	addr_lo = htole32((uint32_t)paddr);
	memcpy(&desc->tbs[0].lo, &addr_lo, sizeof (uint32_t));
	desc->tbs[0].hi_n_len  = htole16(iwm_get_dma_hi_addr(paddr)
	    | ((hdrlen + paylen) << 4));
	desc->num_tbs = 1;

	if (paylen > datasz) {
		iwm_dma_sync(&ring->buf_dma[txdata->buf_page],
		    DDI_DMA_SYNC_FORDEV);
	} else {
		(void) ddi_dma_sync(ring->cmd_dma.dma_hdl,
		    (char *)(void *)cmd - (char *)(void *)ring->cmd_dma.vaddr,
		    hdrlen + paylen, DDI_DMA_SYNC_FORDEV);
	}
	(void) ddi_dma_sync(ring->desc_dma.dma_hdl,
	    (char *)(void *)desc - (char *)(void *)ring->desc_dma.vaddr,
	    sizeof (*desc), DDI_DMA_SYNC_FORDEV);

	/*
	 * Wake up the NIC to make sure that the firmware will see the host
	 * command - we will let the NIC sleep once all the host commands
	 * returned. This needs to be done only on 7000 family NICs.
	 */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000) {
		if (ring->queued == 0 && !iwm_nic_lock(sc)) {
			err = EBUSY;
			goto out;
		}
	}

	iwm_update_sched(sc, ring->qid, ring->cur, 0, 0);

	/* Kick command ring. */
	sc->sc_cmd_done[idx] = 0;
	ring->queued++;
	ring->cur = (ring->cur + 1) % IWM_TX_RING_COUNT;
	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	if (!async) {
		/* OpenBSD: tsleep_nsec(desc, PCATCH, "iwmcmd", 1s) */
		deadline = ddi_get_lbolt() + drv_usectohz(1000000);
		while (!sc->sc_cmd_done[idx] &&
		    generation == sc->sc_generation) {
			if (cv_timedwait(&sc->sc_cmd_cv, &sc->sc_mtx,
			    deadline) == -1)
				break;
		}
		if (sc->sc_cmd_done[idx] && generation == sc->sc_generation) {
			/* Response buffer will be freed in iwm_free_resp(). */
			hcmd->resp_pkt = (void *)sc->sc_cmd_resp_pkt[idx];
			sc->sc_cmd_resp_pkt[idx] = NULL;
		} else {
			/* if hardware is no longer up, return error */
			err = (generation != sc->sc_generation) ?
			    ENXIO : EWOULDBLOCK;
			if (generation == sc->sc_generation &&
			    sc->sc_cmd_resp_pkt[idx] != NULL) {
				kmem_free(sc->sc_cmd_resp_pkt[idx],
				    sc->sc_cmd_resp_len[idx]);
				sc->sc_cmd_resp_pkt[idx] = NULL;
			}
		}
	}
out:
	return (err);
}

static int
iwm_send_cmd_pdu(struct iwm_softc *sc, uint32_t id, uint32_t flags,
    uint16_t len, const void *data)
{
	struct iwm_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
		.flags = flags,
	};

	return (iwm_send_cmd(sc, &cmd));
}

static int
iwm_send_cmd_status(struct iwm_softc *sc, struct iwm_host_cmd *cmd,
    uint32_t *status)
{
	struct iwm_rx_packet *pkt;
	struct iwm_cmd_response *resp;
	int err, resp_len;

	ASSERT((cmd->flags & IWM_CMD_WANT_RESP) == 0);
	cmd->flags |= IWM_CMD_WANT_RESP;
	cmd->resp_pkt_len = sizeof (*pkt) + sizeof (*resp);

	err = iwm_send_cmd(sc, cmd);
	if (err)
		return (err);

	pkt = cmd->resp_pkt;
	if (pkt == NULL)
		return (EIO);
	if (pkt->hdr.flags & IWM_CMD_FAILED_MSK) {
		iwm_free_resp(sc, cmd);
		return (EIO);
	}

	resp_len = iwm_rx_packet_payload_len(pkt);
	if (resp_len != sizeof (*resp)) {
		iwm_free_resp(sc, cmd);
		return (EIO);
	}

	resp = (void *)pkt->data;
	*status = le32toh(resp->status);
	iwm_free_resp(sc, cmd);
	return (err);
}

static int
iwm_send_cmd_pdu_status(struct iwm_softc *sc, uint32_t id, uint16_t len,
    const void *data, uint32_t *status)
{
	struct iwm_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
	};

	return (iwm_send_cmd_status(sc, &cmd, status));
}

static void
iwm_free_resp(struct iwm_softc *sc, struct iwm_host_cmd *hcmd)
{
	_NOTE(ARGUNUSED(sc))
	ASSERT((hcmd->flags & (IWM_CMD_WANT_RESP)) == IWM_CMD_WANT_RESP);
	if (hcmd->resp_pkt != NULL)
		kmem_free(hcmd->resp_pkt, hcmd->resp_pkt_len);
	hcmd->resp_pkt = NULL;
}

static void
iwm_cmd_done(struct iwm_softc *sc, int qid, int idx, int code)
{
	struct iwm_tx_ring *ring = &sc->txq[sc->cmdqid];

	_NOTE(ARGUNUSED(code))
	if (qid != sc->cmdqid) {
		return;	/* Not a command ack. */
	}

	sc->sc_cmd_done[idx] = 1;
	cv_broadcast(&sc->sc_cmd_cv);

	if (ring->queued == 0) {
		DPRINTF((CE_CONT, "!iwm: unexpected firmware response to "
		    "command 0x%x", code));
	} else if (--ring->queued == 0) {
		/*
		 * 7000 family NICs are locked while commands are in progress.
		 * All commands are now done so we may unlock the NIC again.
		 */
		if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
			iwm_nic_unlock(sc);
	}
}

static void
iwm_update_sched(struct iwm_softc *sc, int qid, int idx, uint8_t sta_id,
    uint16_t len)
{
	struct iwm_agn_scd_bc_tbl *scd_bc_tbl;
	uint16_t val;

	scd_bc_tbl = sc->sched_dma.vaddr;

	len += IWM_TX_CRC_SIZE + IWM_TX_DELIMITER_SIZE;
	if (sc->sc_capaflags & IWM_UCODE_TLV_FLAGS_DW_BC_TABLE)
		len = roundup(len, 4) / 4;

	val = htole16(sta_id << 12 | len);


	/* Update TX scheduler. */
	scd_bc_tbl[qid].tfd_offset[idx] = val;
	if (idx < IWM_TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[qid].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX + idx] = val;
	(void) ddi_dma_sync(sc->sched_dma.dma_hdl,
	    (char *)&scd_bc_tbl[qid] - (char *)sc->sched_dma.vaddr,
	    sizeof (*scd_bc_tbl), DDI_DMA_SYNC_FORDEV);
}

static void
iwm_reset_sched(struct iwm_softc *sc, int qid, int idx, uint8_t sta_id)
{
	struct iwm_agn_scd_bc_tbl *scd_bc_tbl;
	uint16_t val;

	scd_bc_tbl = sc->sched_dma.vaddr;

	val = htole16(1 | (sta_id << 12));


	/* Update TX scheduler. */
	scd_bc_tbl[qid].tfd_offset[idx] = val;
	if (idx < IWM_TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[qid].tfd_offset[IWM_TFD_QUEUE_SIZE_MAX + idx] = val;

	(void) ddi_dma_sync(sc->sched_dma.dma_hdl,
	    (char *)&scd_bc_tbl[qid] - (char *)sc->sched_dma.vaddr,
	    sizeof (*scd_bc_tbl), DDI_DMA_SYNC_FORDEV);
}


/* OpenBSD ieee80211_min_basic_rate(), on the illumos BSS node. */
static int
iwm_min_basic_rate(struct ieee80211com *ic)
{
	struct ieee80211_rateset *rs = &ic->ic_bss->in_rates;
	int i, min, rval;

	min = -1;

	for (i = 0; i < rs->ir_nrates; i++) {
		if ((rs->ir_rates[i] & IEEE80211_RATE_BASIC) == 0)
			continue;
		rval = (rs->ir_rates[i] & IEEE80211_RATE_VAL);
		if (min == -1)
			min = rval;
		else if (rval < min)
			min = rval;
	}

	/* Default to 1 Mbit/s on 2GHz and 6 Mbit/s on 5GHz. */
	if (min == -1) {
		struct ieee80211_channel *c = ic->ic_bss->in_chan;

		min = (c == NULL || c == IEEE80211_CHAN_ANYC ||
		    IEEE80211_IS_CHAN_2GHZ(c)) ? 2 : 12;
	}

	return (min);
}

/*
 * Fill in various bit for management frames, and leave them
 * unfilled for data frames (firmware takes care of that).
 * Return the selected legacy TX rate, or zero if HT is used.
 */
static uint8_t
iwm_tx_fill_cmd(struct iwm_softc *sc, struct iwm_node *in,
    struct ieee80211_frame *wh, struct iwm_tx_cmd *tx)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	const struct iwm_rate *rinfo;
	int type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	int min_ridx = iwm_rval2ridx(iwm_min_basic_rate(ic));
	int ridx, rate_flags;
	uint8_t rate = 0;

	tx->rts_retry_limit = IWM_RTS_DFAULT_RETRY_LIMIT;
	tx->data_retry_limit = IWM_LOW_RETRY_LIMIT;

	if (min_ridx < 0)
		min_ridx = 0;
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA) {
		/* for non-data, use the lowest supported rate */
		ridx = min_ridx;
		tx->data_retry_limit = IWM_MGMT_DFAULT_RETRY_LIMIT;
	} else if (ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE &&
	    sc->sc_fixed_ridx >= 0) {
		ridx = sc->sc_fixed_ridx;
	} else {
		int i;
		/* Use firmware rateset retry table. */
		tx->initial_rate_index = 0;
		tx->tx_flags |= htole32(IWM_TX_CMD_FLG_STA_RATE);
		if (ni->in_flags & IEEE80211_NODE_HT)
			return (0);
		ridx = (ni->in_chan != NULL &&
		    ni->in_chan != IEEE80211_CHAN_ANYC &&
		    IEEE80211_IS_CHAN_5GHZ(ni->in_chan)) ?
		    IWM_RIDX_OFDM : IWM_RIDX_CCK;
		for (i = 0; i < (int)nitems(iwm_rates); i++) {
			if (ni->in_txrate >= 0 &&
			    ni->in_txrate < ni->in_rates.ir_nrates &&
			    iwm_rates[i].rate ==
			    (ni->in_rates.ir_rates[ni->in_txrate] &
			    IEEE80211_RATE_VAL)) {
				ridx = i;
				break;
			}
		}
		return (iwm_rates[ridx].rate & 0xff);
	}

	rinfo = &iwm_rates[ridx];
	if (iwm_is_mimo_ht_plcp(rinfo->ht_plcp))
		rate_flags = IWM_RATE_MCS_ANT_AB_MSK;
	else
		rate_flags = iwm_valid_siso_ant_rate_mask(sc);
	if (IWM_RIDX_IS_CCK(ridx))
		rate_flags |= IWM_RATE_MCS_CCK_MSK;
	if ((ni->in_flags & IEEE80211_NODE_HT) &&
	    type == IEEE80211_FC0_TYPE_DATA &&
	    rinfo->ht_plcp != IWM_RATE_HT_SISO_MCS_INV_PLCP) {
		uint8_t sco = iwm_node_sco(ni);

		rate_flags |= IWM_RATE_MCS_HT_MSK;
		if ((sco == IWM_HTOP0_SCO_SCA || sco == IWM_HTOP0_SCO_SCB) &&
		    in->in_phyctxt != NULL && in->in_phyctxt->sco == sco) {
			rate_flags |= IWM_RATE_MCS_CHAN_WIDTH_40;
			if (ni->in_htcap & IEEE80211_HTCAP_SHORTGI40)
				rate_flags |= IWM_RATE_MCS_SGI_MSK;
		} else if (ni->in_htcap & IEEE80211_HTCAP_SHORTGI20)
			rate_flags |= IWM_RATE_MCS_SGI_MSK;
		tx->rate_n_flags = htole32(rate_flags | rinfo->ht_plcp);
	} else
		tx->rate_n_flags = htole32(rate_flags | rinfo->plcp);

	return (rate);
}

#define	TB0_SIZE 16

/*
 * Queue an 802.11 frame (net80211 has already added the 802.11 header
 * and, for protected frames, done crypto encapsulation).  Takes ownership
 * of m on success.  Called with sc_mtx held.  Pairwise CCMP keys are in
 * hardware: illumos's ccmp_encap has written the IV and the firmware adds
 * the MIC (OpenBSD builds the IV here itself).
 */
static int
iwm_tx(struct iwm_softc *sc, mblk_t *m, struct ieee80211_node *ni, int ac)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ni;
	struct iwm_tx_ring *ring;
	struct iwm_tx_data *data;
	struct iwm_tfd *desc;
	struct iwm_device_cmd *cmd;
	struct iwm_tx_cmd *tx;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	uint32_t flags;
	uint_t hdrlen;
	uint8_t tid, type, subtype;
	int totlen, pad, paylen;
	int qid, hasqos;
	mblk_t *mp;
	size_t off;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	if (MBLKL(m) < sizeof (struct ieee80211_frame_min))
		return (EINVAL);
	wh = (struct ieee80211_frame *)m->b_rptr;
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if (type == IEEE80211_FC0_TYPE_CTL)
		hdrlen = sizeof (struct ieee80211_frame_min);
	else
		hdrlen = ieee80211_hdrsize(wh);
	if (MBLKL(m) < hdrlen)
		return (EINVAL);

	hasqos = (type == IEEE80211_FC0_TYPE_DATA &&
	    (subtype & IEEE80211_FC0_SUBTYPE_QOS));
	if (type == IEEE80211_FC0_TYPE_DATA)
		tid = IWM_TID_NON_QOS;
	else
		tid = IWM_MAX_TID_COUNT;

	/*
	 * Map EDCA categories to Tx data queues.
	 *
	 * We use static data queue assignments even in DQA mode. We do not
	 * need to share Tx queues between stations because we only implement
	 * client mode; the firmware's station table contains only one entry
	 * which represents our access point.
	 */
	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
		qid = IWM_DQA_MIN_MGMT_QUEUE + ac;
	else
		qid = ac;

	/* If possible, put this frame on an aggregation queue. */
	if (hasqos) {
		struct ieee80211_qosframe *qwh = (void *)wh;
		int qostid = qwh->i_qos[0] & IEEE80211_QOS_TID;
		int agg_qid = IWM_FIRST_AGG_TX_QUEUE + qostid;

		if (qostid < IWM_MAX_TID_COUNT &&
		    !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
		    subtype != IEEE80211_FC0_SUBTYPE_QOS_NULL &&
		    (sc->tx_ba_queue_mask & (1 << agg_qid)) &&
		    IEEE80211_AMPDU_RUNNING(
		    &ni->in_tx_ampdu[TID_TO_WME_AC(qostid)])) {
			qid = agg_qid;
			tid = qostid;
			ac = TID_TO_WME_AC(qostid);
		}
		/*
		 * illumos net80211 marks aggregated frames with the Block
		 * Ack policy; A-MPDU subframes carry Normal Ack (implicit
		 * BAR) on the air, as in OpenBSD.
		 */
		if ((qwh->i_qos[0] & IEEE80211_QOS_ACKPOLICY) ==
		    IEEE80211_QOS_ACKPOLICY_BA)
			qwh->i_qos[0] &= ~IEEE80211_QOS_ACKPOLICY;
	}

	ring = &sc->txq[qid];
	data = &ring->data[ring->cur];
	if (ring->cmd == NULL || data->buf == NULL || data->m != NULL)
		return (ENOBUFS);
	desc = &ring->desc[ring->cur];
	memset(desc, 0, sizeof (*desc));

	cmd = &ring->cmd[ring->cur];
	cmd->hdr.code = IWM_TX_CMD;
	cmd->hdr.flags = 0;
	cmd->hdr.qid = ring->qid;
	cmd->hdr.idx = ring->cur;

	tx = (void *)cmd->data;
	memset(tx, 0, sizeof (*tx));

	(void) iwm_tx_fill_cmd(sc, in, wh, tx);

	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		k = &ic->ic_nw_keys[ic->ic_def_txkey & 3];
		if (k->wk_cipher == NULL ||
		    k->wk_cipher->ic_cipher != IEEE80211_CIPHER_AES_CCM ||
		    (k->wk_flags & IEEE80211_KEY_SWCRYPT))
			k = NULL;	/* software crypto, already done */
	}
	totlen = msgdsize(m);

	flags = 0;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		flags |= IWM_TX_CMD_FLG_ACK;
	}

	if (type == IEEE80211_FC0_TYPE_DATA &&
	    !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (totlen + IEEE80211_CRC_LEN > ic->ic_rtsthreshold ||
	    (ic->ic_flags & IEEE80211_F_USEPROT)))
		flags |= IWM_TX_CMD_FLG_PROT_REQUIRE;

	tx->sta_id = IWM_STATION_ID;

	if (type == IEEE80211_FC0_TYPE_MGT) {
		if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
		    subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
			tx->pm_frame_timeout = htole16(3);
		else
			tx->pm_frame_timeout = htole16(2);
	} else {
		if (type == IEEE80211_FC0_TYPE_CTL &&
		    subtype == IEEE80211_FC0_SUBTYPE_BAR &&
		    MBLKL(m) >= hdrlen + 2) {
			uint8_t *barfrm = m->b_rptr + hdrlen;
			uint16_t ctl = barfrm[0] | (barfrm[1] << 8);

			tid = MS(ctl, IEEE80211_BAPS_TID);
			flags |= IWM_TX_CMD_FLG_ACK | IWM_TX_CMD_FLG_BAR;
			tx->data_retry_limit = IWM_BAR_DFAULT_RETRY_LIMIT;
		}

		tx->pm_frame_timeout = htole16(0);
	}

	if (hdrlen & 3) {
		/* First segment length must be a multiple of 4. */
		flags |= IWM_TX_CMD_FLG_MH_PAD;
		tx->offload_assist |= htole16(IWM_TX_CMD_OFFLD_PAD);
		pad = 4 - (hdrlen & 3);
	} else
		pad = 0;

	tx->len = htole16(totlen);
	tx->tid_tspec = tid;
	tx->life_time = htole32(IWM_TX_CMD_LIFE_TIME_INFINITE);

	/* Set physical address of "scratch area". */
	tx->dram_lsb_ptr = htole32(data->scratch_paddr);
	tx->dram_msb_ptr = iwm_get_dma_hi_addr(data->scratch_paddr);

	/* Copy 802.11 header in TX command. */
	memcpy(((uint8_t *)tx) + sizeof (*tx), wh, hdrlen);

	if (k != NULL) {
		/* The CCMP IV follows the header in the payload. */
		tx->sec_ctl = IWM_TX_CMD_SEC_CCM;
		memcpy(tx->key, k->wk_key, MIN(sizeof (tx->key), k->wk_keylen));
		/* TX scheduler includes CCMP MIC length. */
		totlen += IEEE80211_WEP_MICLEN;
	} else {
		tx->sec_ctl = 0;
	}

	flags |= IWM_TX_CMD_FLG_BT_DIS;
	if (!hasqos)
		flags |= IWM_TX_CMD_FLG_SEQ_CTL;

	tx->tx_flags |= htole32(flags);

	/* Copy the payload (everything after the 802.11 header). */
	paylen = msgdsize(m) - hdrlen;
	if (paylen > IWM_TXBUF_SIZE) {
		sc->sc_oerrors++;
		return (EFBIG);
	}
	off = 0;
	for (mp = m; mp != NULL; mp = mp->b_cont) {
		size_t len = MBLKL(mp), skip = 0;

		if (mp == m)
			skip = hdrlen;
		if (len <= skip)
			continue;
		memcpy(data->buf + off, mp->b_rptr + skip, len - skip);
		off += len - skip;
	}
	ASSERT(off == (size_t)paylen);

	data->m = m;
	data->in = (struct iwm_node *)ieee80211_ref_node(ni);
	data->txmcs = in->in_txmcs;
	data->txrate = ni->in_txrate;
	data->ampdu_txmcs = in->in_txmcs; /* updated upon Tx interrupt */
	data->ampdu_txnss = 1;

	/* Fill TX descriptor. */
	desc->num_tbs = 2 + (paylen > 0 ? 1 : 0);

	desc->tbs[0].lo = htole32(data->cmd_paddr);
	desc->tbs[0].hi_n_len = htole16(iwm_get_dma_hi_addr(data->cmd_paddr) |
	    (TB0_SIZE << 4));
	desc->tbs[1].lo = htole32(data->cmd_paddr + TB0_SIZE);
	desc->tbs[1].hi_n_len = htole16(iwm_get_dma_hi_addr(data->cmd_paddr) |
	    ((sizeof (struct iwm_cmd_header) + sizeof (*tx)
	    + hdrlen + pad - TB0_SIZE) << 4));

	/* The payload buffer is one DMA segment that does not cross a page. */
	if (paylen > 0) {
		desc->tbs[2].lo = htole32(data->buf_paddr);
		desc->tbs[2].hi_n_len =
		    htole16(iwm_get_dma_hi_addr(data->buf_paddr)
		    | (paylen << 4));
		(void) ddi_dma_sync(ring->buf_dma[data->buf_page].dma_hdl,
		    data->buf - (caddr_t)ring->buf_dma[data->buf_page].vaddr,
		    paylen, DDI_DMA_SYNC_FORDEV);
	}

	(void) ddi_dma_sync(ring->cmd_dma.dma_hdl,
	    (char *)(void *)cmd - (char *)(void *)ring->cmd_dma.vaddr,
	    sizeof (*cmd), DDI_DMA_SYNC_FORDEV);
	(void) ddi_dma_sync(ring->desc_dma.dma_hdl,
	    (char *)(void *)desc - (char *)(void *)ring->desc_dma.vaddr,
	    sizeof (*desc), DDI_DMA_SYNC_FORDEV);

	iwm_update_sched(sc, ring->qid, ring->cur, tx->sta_id, totlen);

	/* Kick TX ring. */
	ring->cur = (ring->cur + 1) % IWM_TX_RING_COUNT;
	IWM_WRITE(sc, IWM_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	/* Mark TX ring as full if we reach a certain threshold. */
	if (++ring->queued > IWM_TX_RING_HIMARK) {
		sc->qfullmsk |= 1 << ring->qid;
	}

	if (sc->sc_flags & IWM_FLAG_RUNNING)
		sc->sc_tx_timer[ring->qid] = 15;

	sc->sc_opackets++;
	sc->sc_obytes += totlen;
	return (0);
}

static int
iwm_flush_tx_path(struct iwm_softc *sc, int tfd_queue_msk)
{
	struct iwm_tx_path_flush_cmd flush_cmd = {
		.sta_id = htole32(IWM_STATION_ID),
		.tid_mask = htole16(0xffff),
	};
	int err;

	_NOTE(ARGUNUSED(tfd_queue_msk))
	err = iwm_send_cmd_pdu(sc, IWM_TXPATH_FLUSH, 0,
	    sizeof (flush_cmd), &flush_cmd);
	if (err)
		dev_err(sc->sc_dip, CE_WARN, "!Flushing tx queue failed: %d",
		    err);
	return (err);
}

#define	IWM_FLUSH_WAIT_MS	2000

static int
iwm_wait_tx_queues_empty(struct iwm_softc *sc)
{
	int i, err;

	for (i = 0; i < IWM_MAX_QUEUES; i++) {
		struct iwm_tx_ring *ring = &sc->txq[i];

		if (i == sc->cmdqid)
			continue;

		while (ring->queued > 0) {
			err = iwm_sleep(sc, &sc->sc_txq_cv,
			    IWM_FLUSH_WAIT_MS * 1000);
			if (err)
				return (err);
		}
	}

	return (0);
}
static void
iwm_led_enable(struct iwm_softc *sc)
{
	IWM_WRITE(sc, IWM_CSR_LED_REG, IWM_CSR_LED_REG_TURN_ON);
}

static void
iwm_led_disable(struct iwm_softc *sc)
{
	IWM_WRITE(sc, IWM_CSR_LED_REG, IWM_CSR_LED_REG_TURN_OFF);
}

static int
iwm_led_is_enabled(struct iwm_softc *sc)
{
	return (IWM_READ(sc, IWM_CSR_LED_REG) == IWM_CSR_LED_REG_TURN_ON);
}

#define	IWM_LED_BLINK_TIMEOUT_MSEC    200

static void
iwm_led_blink_timeout(void *arg)
{
	struct iwm_softc *sc = arg;

	mutex_enter(&sc->sc_mtx);
	if (sc->sc_led_blink_to == 0) {
		mutex_exit(&sc->sc_mtx);
		return;
	}
	if (iwm_led_is_enabled(sc))
		iwm_led_disable(sc);
	else
		iwm_led_enable(sc);

	sc->sc_led_blink_to = timeout(iwm_led_blink_timeout, sc,
	    drv_usectohz(IWM_LED_BLINK_TIMEOUT_MSEC * 1000));
	mutex_exit(&sc->sc_mtx);
}

static void
iwm_led_blink_start(struct iwm_softc *sc)
{
	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	if (sc->sc_led_blink_to == 0)
		sc->sc_led_blink_to = timeout(iwm_led_blink_timeout, sc,
		    drv_usectohz(IWM_LED_BLINK_TIMEOUT_MSEC * 1000));
	iwm_led_enable(sc);
}

static void
iwm_led_blink_stop(struct iwm_softc *sc)
{
	timeout_id_t tid;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	tid = sc->sc_led_blink_to;
	sc->sc_led_blink_to = 0;
	if (tid != 0) {
		/* untimeout() may wait for the callback, which takes sc_mtx */
		mutex_exit(&sc->sc_mtx);
		(void) untimeout(tid);
		mutex_enter(&sc->sc_mtx);
	}
	iwm_led_disable(sc);
}

#define	IWM_POWER_KEEP_ALIVE_PERIOD_SEC    25

static int
iwm_beacon_filter_send_cmd(struct iwm_softc *sc,
    struct iwm_beacon_filter_cmd *cmd)
{
	return (iwm_send_cmd_pdu(sc, IWM_REPLY_BEACON_FILTERING_CMD,
	    0, sizeof (struct iwm_beacon_filter_cmd), cmd));
}

static void
iwm_beacon_filter_set_cqm_params(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_beacon_filter_cmd *cmd)
{
	_NOTE(ARGUNUSED(in))
	cmd->ba_enable_beacon_abort = htole32(sc->sc_bf.ba_enabled);
}

static int
iwm_update_beacon_abort(struct iwm_softc *sc, struct iwm_node *in, int enable)
{
	struct iwm_beacon_filter_cmd cmd = {
		IWM_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter = htole32(1),
		.ba_enable_beacon_abort = htole32(enable),
	};

	if (!sc->sc_bf.bf_enabled)
		return (0);

	sc->sc_bf.ba_enabled = enable;
	iwm_beacon_filter_set_cqm_params(sc, in, &cmd);
	return (iwm_beacon_filter_send_cmd(sc, &cmd));
}

static void
iwm_power_build_cmd(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_power_cmd *cmd)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = &in->in_ni;
	int dtim_period, dtim_msec, keep_alive;

	cmd->id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	if (ni->in_dtim_period)
		dtim_period = ni->in_dtim_period;
	else
		dtim_period = 1;

	/*
	 * Regardless of power management state the driver must set
	 * keep alive period. FW will use it for sending keep alive NDPs
	 * immediately after association. Check that keep alive period
	 * is at least 3 * DTIM.
	 */
	dtim_msec = dtim_period * ni->in_intval;
	keep_alive = MAX(3 * dtim_msec, 1000 * IWM_POWER_KEEP_ALIVE_PERIOD_SEC);
	keep_alive = roundup(keep_alive, 1000) / 1000;
	cmd->keep_alive_seconds = htole16(keep_alive);

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		cmd->flags = htole16(IWM_POWER_FLAGS_POWER_SAVE_ENA_MSK);
}

static int
iwm_power_mac_update_mode(struct iwm_softc *sc, struct iwm_node *in)
{
	int err;
	int ba_enable;
	struct iwm_mac_power_cmd cmd;

	memset(&cmd, 0, sizeof (cmd));

	iwm_power_build_cmd(sc, in, &cmd);

	err = iwm_send_cmd_pdu(sc, IWM_MAC_PM_POWER_TABLE, 0,
	    sizeof (cmd), &cmd);
	if (err != 0)
		return (err);

	ba_enable = !!(cmd.flags &
	    htole16(IWM_POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK));
	return (iwm_update_beacon_abort(sc, in, ba_enable));
}

static int
iwm_power_update_device(struct iwm_softc *sc)
{
	struct iwm_device_power_cmd cmd = { 0 };
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		cmd.flags = htole16(IWM_DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK);

	return (iwm_send_cmd_pdu(sc,
	    IWM_POWER_TABLE_CMD, 0, sizeof (cmd), &cmd));
}

static int
iwm_disable_beacon_filter(struct iwm_softc *sc)
{
	struct iwm_beacon_filter_cmd cmd;
	int err;

	memset(&cmd, 0, sizeof (cmd));

	err = iwm_beacon_filter_send_cmd(sc, &cmd);
	if (err == 0)
		sc->sc_bf.bf_enabled = 0;

	return (err);
}

static int
iwm_add_sta_cmd(struct iwm_softc *sc, struct iwm_node *in, int update)
{
	struct iwm_add_sta_cmd add_sta_cmd;
	struct ieee80211_node *ni = &in->in_ni;
	int err, ac;
	uint32_t status, aggsize;
	const uint32_t max_aggsize = (IWM_STA_FLG_MAX_AGG_SIZE_64K >>
		    IWM_STA_FLG_MAX_AGG_SIZE_SHIFT);
	size_t cmdsize;

	if (!update && (sc->sc_flags & IWM_FLAG_STA_ACTIVE))
		return (EBUSY);		/* OpenBSD panics here */

	memset(&add_sta_cmd, 0, sizeof (add_sta_cmd));

	add_sta_cmd.sta_id = IWM_STATION_ID;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		add_sta_cmd.station_type = IWM_STA_LINK;
	add_sta_cmd.mac_id_n_color
	    = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id, in->in_color));
	for (ac = 0; ac < EDCA_NUM_AC; ac++) {
		int qid = ac;
		if (isset(sc->sc_enabled_capa,
		    IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
			qid += IWM_DQA_MIN_MGMT_QUEUE;
		in->tfd_queue_msk |= (1 << qid);
	}
	if (!update)
		IEEE80211_ADDR_COPY(&add_sta_cmd.addr, in->in_macaddr);
	add_sta_cmd.add_modify = update ? 1 : 0;
	add_sta_cmd.station_flags_msk
	    |= htole32(IWM_STA_FLG_FAT_EN_MSK | IWM_STA_FLG_MIMO_EN_MSK);
	if (update) {
		add_sta_cmd.modify_mask |= (IWM_STA_MODIFY_QUEUES |
		    IWM_STA_MODIFY_TID_DISABLE_TX);
	}
	add_sta_cmd.tid_disable_tx = htole16(in->tid_disable_ampdu);
	add_sta_cmd.tfd_queue_msk = htole32(in->tfd_queue_msk);

	if (ni->in_flags & IEEE80211_NODE_HT) {
		int i, mimo2 = 0;

		add_sta_cmd.station_flags_msk
		    |= htole32(IWM_STA_FLG_MAX_AGG_SIZE_MSK |
		    IWM_STA_FLG_AGG_MPDU_DENS_MSK);

		/* Peer receives MCS 8-15 (OpenBSD ni_rxmcs[1] != 0)? */
		for (i = 0; i < ni->in_htrates.rs_nrates; i++) {
			int mcs = ni->in_htrates.rs_rates[i] &
			    IEEE80211_RATE_VAL;
			if (mcs >= 8 && mcs <= 15)
				mimo2 = 1;
		}
		if (iwm_mimo_enabled(sc) && mimo2) {
			add_sta_cmd.station_flags |=
			    htole32(IWM_STA_FLG_MIMO_EN_MIMO2);
		}

		if (iwm_node_sco(ni) != IWM_HTOP0_SCO_SCN) {
			add_sta_cmd.station_flags |= htole32(
			    IWM_STA_FLG_FAT_EN_40MHZ);
		}

		/* illumos in_htparam holds the peer's A-MPDU parameters */
		aggsize = MS(ni->in_htparam, IEEE80211_HTCAP_MAXRXAMPDU);
		if (aggsize > max_aggsize)
			aggsize = max_aggsize;
		add_sta_cmd.station_flags |= htole32((aggsize <<
		    IWM_STA_FLG_MAX_AGG_SIZE_SHIFT) &
		    IWM_STA_FLG_MAX_AGG_SIZE_MSK);

		switch (MS(ni->in_htparam, IEEE80211_HTCAP_MPDUDENSITY)) {
		case IEEE80211_HTCAP_MPDUDENSITY_2:
			add_sta_cmd.station_flags
			    |= htole32(IWM_STA_FLG_AGG_MPDU_DENS_2US);
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_4:
			add_sta_cmd.station_flags
			    |= htole32(IWM_STA_FLG_AGG_MPDU_DENS_4US);
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_8:
			add_sta_cmd.station_flags
			    |= htole32(IWM_STA_FLG_AGG_MPDU_DENS_8US);
			break;
		case IEEE80211_HTCAP_MPDUDENSITY_16:
			add_sta_cmd.station_flags
			    |= htole32(IWM_STA_FLG_AGG_MPDU_DENS_16US);
			break;
		default:
			break;
		}
	}

	status = IWM_ADD_STA_SUCCESS;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof (add_sta_cmd);
	else
		cmdsize = sizeof (struct iwm_add_sta_cmd_v7);
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA, cmdsize,
	    &add_sta_cmd, &status);
	if (!err && (status & IWM_ADD_STA_STATUS_MASK) != IWM_ADD_STA_SUCCESS)
		err = EIO;

	return (err);
}

/*
 * 40 MHz secondary channel offset in use with the BSS (OpenBSD
 * ni_htop0 & SCO_MASK, gated by ieee80211_node_supports_ht_chan40()).
 * illumos keeps the AP's offset in in_ht2ndchan, which has the same
 * encoding; in_chw is 40 only if the AP allows 40 MHz operation.
 */
static uint8_t
iwm_node_sco(struct ieee80211_node *ni)
{
	struct ieee80211_channel *c = ni->in_chan;

	if ((ni->in_flags & IEEE80211_NODE_HT) == 0 || ni->in_chw != 40 ||
	    (ni->in_htcap & IEEE80211_HTCAP_CHWIDTH40) == 0 ||
	    c == NULL || c == IEEE80211_CHAN_ANYC ||
	    (c->ich_flags & IEEE80211_CHAN_HT40) == 0)
		return (IWM_HTOP0_SCO_SCN);
	if (ni->in_ht2ndchan == IEEE80211_HTINFO_2NDCHAN_ABOVE)
		return (IWM_HTOP0_SCO_SCA);
	if (ni->in_ht2ndchan == IEEE80211_HTINFO_2NDCHAN_BELOW)
		return (IWM_HTOP0_SCO_SCB);
	return (IWM_HTOP0_SCO_SCN);
}

static int
iwm_add_aux_sta(struct iwm_softc *sc)
{
	struct iwm_add_sta_cmd cmd;
	int err, qid;
	uint32_t status;
	size_t cmdsize;

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT)) {
		qid = IWM_DQA_AUX_QUEUE;
		err = iwm_enable_txq(sc, IWM_AUX_STA_ID, qid,
		    IWM_TX_FIFO_MCAST, 0, IWM_MAX_TID_COUNT, 0);
	} else {
		qid = IWM_AUX_QUEUE;
		err = iwm_enable_ac_txq(sc, qid, IWM_TX_FIFO_MCAST);
	}
	if (err)
		return (err);

	memset(&cmd, 0, sizeof (cmd));
	cmd.sta_id = IWM_AUX_STA_ID;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmd.station_type = IWM_STA_AUX_ACTIVITY;
	cmd.mac_id_n_color =
	    htole32(IWM_FW_CMD_ID_AND_COLOR(IWM_MAC_INDEX_AUX, 0));
	cmd.tfd_queue_msk = htole32(1 << qid);
	cmd.tid_disable_tx = htole16(0xffff);

	status = IWM_ADD_STA_SUCCESS;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof (cmd);
	else
		cmdsize = sizeof (struct iwm_add_sta_cmd_v7);
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA, cmdsize, &cmd,
	    &status);
	if (!err && (status & IWM_ADD_STA_STATUS_MASK) != IWM_ADD_STA_SUCCESS)
		err = EIO;

	return (err);
}

static int
iwm_drain_sta(struct iwm_softc *sc, struct iwm_node *in, int drain)
{
	struct iwm_add_sta_cmd cmd;
	int err;
	uint32_t status;
	size_t cmdsize;

	memset(&cmd, 0, sizeof (cmd));
	cmd.mac_id_n_color = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	cmd.sta_id = IWM_STATION_ID;
	cmd.add_modify = IWM_STA_MODE_MODIFY;
	cmd.station_flags = drain ? htole32(IWM_STA_FLG_DRAIN_FLOW) : 0;
	cmd.station_flags_msk = htole32(IWM_STA_FLG_DRAIN_FLOW);

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_STA_TYPE))
		cmdsize = sizeof (cmd);
	else
		cmdsize = sizeof (struct iwm_add_sta_cmd_v7);

	status = IWM_ADD_STA_SUCCESS;
	err = iwm_send_cmd_pdu_status(sc, IWM_ADD_STA,
	    cmdsize, &cmd, &status);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not update sta (error %d)",
		    err);
		return (err);
	}

	switch (status & IWM_ADD_STA_STATUS_MASK) {
	case IWM_ADD_STA_SUCCESS:
		break;
	default:
		err = EIO;
		dev_err(sc->sc_dip, CE_WARN, "!Couldn't %s draining for "
		    "station", drain ? "enable" : "disable");
		break;
	}

	return (err);
}

static int
iwm_flush_sta(struct iwm_softc *sc, struct iwm_node *in)
{
	int err;

	sc->sc_flags |= IWM_FLAG_TXFLUSH;

	err = iwm_drain_sta(sc, in, 1);
	if (err)
		goto done;

	err = iwm_flush_tx_path(sc, in->tfd_queue_msk);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not flush Tx path (error %d)", err);
		goto done;
	}

	/*
	 * Flushing Tx rings may fail if the AP has disappeared.
	 * We can rely on iwm_newstate_task() to reset everything and begin
	 * scanning again if we are left with outstanding frames on queues.
	 */
	err = iwm_wait_tx_queues_empty(sc);
	if (err)
		goto done;

	err = iwm_drain_sta(sc, in, 0);
done:
	sc->sc_flags &= ~IWM_FLAG_TXFLUSH;
	return (err);
}

static int
iwm_rm_sta_cmd(struct iwm_softc *sc, struct iwm_node *in)
{
	struct iwm_rm_sta_cmd rm_sta_cmd;
	int err;

	_NOTE(ARGUNUSED(in))
	if ((sc->sc_flags & IWM_FLAG_STA_ACTIVE) == 0)
		return (ENOENT);		/* OpenBSD panics here */

	memset(&rm_sta_cmd, 0, sizeof (rm_sta_cmd));
	rm_sta_cmd.sta_id = IWM_STATION_ID;

	err = iwm_send_cmd_pdu(sc, IWM_REMOVE_STA, 0, sizeof (rm_sta_cmd),
	    &rm_sta_cmd);

	return (err);
}

static uint16_t
iwm_scan_rx_chain(struct iwm_softc *sc)
{
	uint16_t rx_chain;
	uint8_t rx_ant;

	rx_ant = iwm_fw_valid_rx_ant(sc);
	rx_chain = rx_ant << IWM_PHY_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << IWM_PHY_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << IWM_PHY_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << IWM_PHY_RX_CHAIN_DRIVER_FORCE_POS;
	return (htole16(rx_chain));
}

static uint32_t
iwm_scan_rate_n_flags(struct iwm_softc *sc, int flags, int no_cck)
{
	uint32_t tx_ant;
	int i, ind;

	for (i = 0, ind = sc->sc_scan_last_antenna;
	    i < IWM_RATE_MCS_ANT_NUM; i++) {
		ind = (ind + 1) % IWM_RATE_MCS_ANT_NUM;
		if (iwm_fw_valid_tx_ant(sc) & (1 << ind)) {
			sc->sc_scan_last_antenna = ind;
			break;
		}
	}
	tx_ant = (1 << sc->sc_scan_last_antenna) << IWM_RATE_MCS_ANT_POS;

	if ((flags & IEEE80211_CHAN_2GHZ) && !no_cck)
		return (htole32(IWM_RATE_1M_PLCP | IWM_RATE_MCS_CCK_MSK |
		    tx_ant));
	else
		return (htole32(IWM_RATE_6M_PLCP | tx_ant));
}

static uint8_t
iwm_lmac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_lmac *chan, int n_ssids, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	uint8_t nchan;

	for (nchan = 0, c = &ic->ic_sup_channels[1];
	    c <= &ic->ic_sup_channels[IEEE80211_CHAN_MAX] &&
	    nchan < sc->sc_capa_n_scan_channels;
	    c++) {
		if (c->ich_flags == 0)
			continue;

		chan->channel_num = htole16(ieee80211_chan2ieee(ic, c));
		chan->iter_count = htole16(1);
		chan->iter_interval = 0;
		chan->flags = htole32(IWM_UNIFIED_SCAN_CHANNEL_PARTIAL);
		if (n_ssids != 0 && !bgscan)
			chan->flags |= htole32(1 << 1); /* select SSID 0 */
		chan++;
		nchan++;
	}

	return (nchan);
}

static uint8_t
iwm_umac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_umac *chan, int n_ssids, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	uint8_t nchan;

	for (nchan = 0, c = &ic->ic_sup_channels[1];
	    c <= &ic->ic_sup_channels[IEEE80211_CHAN_MAX] &&
	    nchan < sc->sc_capa_n_scan_channels;
	    c++) {
		if (c->ich_flags == 0)
			continue;

		chan->channel_num = ieee80211_chan2ieee(ic, c);
		chan->iter_count = 1;
		chan->iter_interval = htole16(0);
		if (n_ssids != 0 && !bgscan)
			chan->flags = htole32(1 << 0); /* select SSID 0 */
		chan++;
		nchan++;
	}

	return (nchan);
}

static int
iwm_fill_probe_req_v1(struct iwm_softc *sc, struct iwm_scan_probe_req_v1 *preq1)
{
	struct iwm_scan_probe_req preq2;
	int err, i;

	err = iwm_fill_probe_req(sc, &preq2);
	if (err)
		return (err);

	preq1->mac_header = preq2.mac_header;
	for (i = 0; i < (int)nitems(preq1->band_data); i++)
		preq1->band_data[i] = preq2.band_data[i];
	preq1->common_data = preq2.common_data;
	memcpy(preq1->buf, preq2.buf, sizeof (preq1->buf));
	return (0);
}

/* Append a Supported Rates element (OpenBSD ieee80211_add_rates). */
static uint8_t *
iwm_add_rates(uint8_t *frm, const struct ieee80211_rateset *rs)
{
	int nrates = MIN(rs->ir_nrates, IEEE80211_RATE_SIZE);

	*frm++ = IEEE80211_ELEMID_RATES;
	*frm++ = nrates;
	memcpy(frm, rs->ir_rates, nrates);
	return (frm + nrates);
}

/* Append an Extended Supported Rates element (ieee80211_add_xrates). */
static uint8_t *
iwm_add_xrates(uint8_t *frm, const struct ieee80211_rateset *rs)
{
	int nrates = rs->ir_nrates - IEEE80211_RATE_SIZE;

	*frm++ = IEEE80211_ELEMID_XRATES;
	*frm++ = nrates;
	memcpy(frm, rs->ir_rates + IEEE80211_RATE_SIZE, nrates);
	return (frm + nrates);
}

static int
iwm_fill_probe_req(struct iwm_softc *sc, struct iwm_scan_probe_req *preq)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh = (struct ieee80211_frame *)preq->buf;
	struct ieee80211_rateset *rs;
	size_t remain = sizeof (preq->buf);
	uint8_t *frm, *pos;

	memset(preq, 0, sizeof (*preq));

	if (remain < sizeof (*wh) + 2)
		return (ENOBUFS);

	/*
	 * Build a probe request frame.  Most of the following code is a
	 * copy & paste of what is done in net80211.
	 */
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_PROBE_REQ;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, iwm_etherbroadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_macaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, iwm_etherbroadcastaddr);
	*(uint16_t *)&wh->i_dur[0] = 0;	/* filled by HW */
	*(uint16_t *)&wh->i_seq[0] = 0;	/* filled by HW */

	frm = (uint8_t *)(wh + 1);

	*frm++ = IEEE80211_ELEMID_SSID;
	*frm++ = 0;
	/* hardware inserts SSID */

	/* Tell firmware where the MAC header and SSID IE are. */
	preq->mac_header.offset = 0;
	preq->mac_header.len = htole16(frm - (uint8_t *)wh);
	remain -= frm - (uint8_t *)wh;

	/* Fill in 2GHz IEs and tell firmware where they are. */
	rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
	if (rs->ir_nrates > IEEE80211_RATE_SIZE) {
		if (remain < 4 + rs->ir_nrates)
			return (ENOBUFS);
	} else if (remain < 2 + rs->ir_nrates)
		return (ENOBUFS);
	preq->band_data[0].offset = htole16(frm - (uint8_t *)wh);
	pos = frm;
	frm = iwm_add_rates(frm, rs);
	if (rs->ir_nrates > IEEE80211_RATE_SIZE)
		frm = iwm_add_xrates(frm, rs);
	remain -= frm - pos;

	if (isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT)) {
		if (remain < 3)
			return (ENOBUFS);
		*frm++ = IEEE80211_ELEMID_DSPARMS;
		*frm++ = 1;
		*frm++ = 0;
		remain -= 3;
	}
	preq->band_data[0].len = htole16(frm - pos);

	if (sc->sc_nvm.sku_cap_band_52GHz_enable) {
		/* Fill in 5GHz IEs. */
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
		if (rs->ir_nrates > IEEE80211_RATE_SIZE) {
			if (remain < 4 + rs->ir_nrates)
				return (ENOBUFS);
		} else if (remain < 2 + rs->ir_nrates)
			return (ENOBUFS);
		preq->band_data[1].offset = htole16(frm - (uint8_t *)wh);
		pos = frm;
		frm = iwm_add_rates(frm, rs);
		if (rs->ir_nrates > IEEE80211_RATE_SIZE)
			frm = iwm_add_xrates(frm, rs);
		preq->band_data[1].len = htole16(frm - pos);
		remain -= frm - pos;
	}

	/* Send 11n IEs on both 2GHz and 5GHz bands. */
	preq->common_data.offset = htole16(frm - (uint8_t *)wh);
	pos = frm;
	if ((ic->ic_flags_ext & IEEE80211_FEXT_HT) && ic->ic_bss != NULL) {
		if (remain < 28)
			return (ENOBUFS);
		frm = ieee80211_add_htcap(frm, ic->ic_bss);
		/* XXX add WME info? */
		remain -= frm - pos;
	}

	preq->common_data.len = htole16(frm - pos);

	return (0);
}

static int
iwm_lmac_scan(struct iwm_softc *sc, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_host_cmd hcmd = {
		.id = IWM_SCAN_OFFLOAD_REQUEST_CMD,
		.len = { 0, },
		.data = { NULL, },
		.flags = 0,
	};
	struct iwm_scan_req_lmac *req;
	struct iwm_scan_probe_req_v1 *preq;
	size_t req_len;
	int err, async = bgscan;

	req_len = sizeof (struct iwm_scan_req_lmac) +
	    (sizeof (struct iwm_scan_channel_cfg_lmac) *
	    sc->sc_capa_n_scan_channels) +
	    sizeof (struct iwm_scan_probe_req_v1);
	if (req_len > IWM_MAX_CMD_PAYLOAD_SIZE)
		return (ENOMEM);
	req = kmem_zalloc(req_len, async ? KM_NOSLEEP : KM_SLEEP);
	if (req == NULL)
		return (ENOMEM);

	hcmd.len[0] = (uint16_t)req_len;
	hcmd.data[0] = (void *)req;
	hcmd.flags |= async ? IWM_CMD_ASYNC : 0;

	/* These timings correspond to iwlwifi's UNASSOC scan. */
	req->active_dwell = 10;
	req->passive_dwell = 110;
	req->fragmented_dwell = 44;
	req->extended_dwell = 90;
	if (bgscan) {
		req->max_out_time = htole32(120);
		req->suspend_time = htole32(120);
	} else {
		req->max_out_time = htole32(0);
		req->suspend_time = htole32(0);
	}
	req->scan_prio = htole32(IWM_SCAN_PRIORITY_HIGH);
	req->rx_chain_select = iwm_scan_rx_chain(sc);
	req->iter_num = htole32(1);
	req->delay = 0;

	req->scan_flags = htole32(IWM_LMAC_SCAN_FLAG_PASS_ALL |
	    IWM_LMAC_SCAN_FLAG_ITER_COMPLETE |
	    IWM_LMAC_SCAN_FLAG_EXTENDED_DWELL);
	if (ic->ic_des_esslen == 0)
		req->scan_flags |= htole32(IWM_LMAC_SCAN_FLAG_PASSIVE);
	else
		req->scan_flags |=
		    htole32(IWM_LMAC_SCAN_FLAG_PRE_CONNECTION);
	if (isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT) &&
	    isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
		req->scan_flags |= htole32(IWM_LMAC_SCAN_FLAGS_RRM_ENABLED);

	req->flags = htole32(IWM_PHY_BAND_24);
	if (sc->sc_nvm.sku_cap_band_52GHz_enable)
		req->flags |= htole32(IWM_PHY_BAND_5);
	req->filter_flags =
	    htole32(IWM_MAC_FILTER_ACCEPT_GRP | IWM_MAC_FILTER_IN_BEACON);

	/* Tx flags 2 GHz. */
	req->tx_cmd[0].tx_flags = htole32(IWM_TX_CMD_FLG_SEQ_CTL |
	    IWM_TX_CMD_FLG_BT_DIS);
	req->tx_cmd[0].rate_n_flags =
	    iwm_scan_rate_n_flags(sc, IEEE80211_CHAN_2GHZ, 1 /* XXX */);
	req->tx_cmd[0].sta_id = IWM_AUX_STA_ID;

	/* Tx flags 5 GHz. */
	req->tx_cmd[1].tx_flags = htole32(IWM_TX_CMD_FLG_SEQ_CTL |
	    IWM_TX_CMD_FLG_BT_DIS);
	req->tx_cmd[1].rate_n_flags =
	    iwm_scan_rate_n_flags(sc, IEEE80211_CHAN_5GHZ, 1 /* XXX */);
	req->tx_cmd[1].sta_id = IWM_AUX_STA_ID;

	/* Check if we're doing an active directed scan. */
	if (ic->ic_des_esslen != 0) {
		req->direct_scan[0].id = IEEE80211_ELEMID_SSID;
		req->direct_scan[0].len = ic->ic_des_esslen;
		memcpy(req->direct_scan[0].ssid, ic->ic_des_essid,
		    ic->ic_des_esslen);
	}

	req->n_channels = iwm_lmac_scan_fill_channels(sc,
	    (struct iwm_scan_channel_cfg_lmac *)req->data,
	    ic->ic_des_esslen != 0, bgscan);

	preq = (struct iwm_scan_probe_req_v1 *)(req->data +
	    (sizeof (struct iwm_scan_channel_cfg_lmac) *
	    sc->sc_capa_n_scan_channels));
	err = iwm_fill_probe_req_v1(sc, preq);
	if (err) {
		kmem_free(req, req_len);
		return (err);
	}

	/* Specify the scan plan: We'll do one iteration. */
	req->schedule[0].iterations = 1;
	req->schedule[0].full_scan_mul = 1;

	/* Disable EBS. */
	req->channel_opt[0].non_ebs_ratio = 1;
	req->channel_opt[1].non_ebs_ratio = 1;

	err = iwm_send_cmd(sc, &hcmd);
	kmem_free(req, req_len);
	return (err);
}

static int
iwm_config_umac_scan(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_scan_config *scan_config;
	int err, nchan;
	size_t cmd_size;
	struct ieee80211_channel *c;
	struct iwm_host_cmd hcmd = {
		.id = iwm_cmd_id(IWM_SCAN_CFG_CMD, IWM_LONG_GROUP, 0),
		.flags = 0,
	};
	static const uint32_t rates = (IWM_SCAN_CONFIG_RATE_1M |
	    IWM_SCAN_CONFIG_RATE_2M | IWM_SCAN_CONFIG_RATE_5M |
	    IWM_SCAN_CONFIG_RATE_11M | IWM_SCAN_CONFIG_RATE_6M |
	    IWM_SCAN_CONFIG_RATE_9M | IWM_SCAN_CONFIG_RATE_12M |
	    IWM_SCAN_CONFIG_RATE_18M | IWM_SCAN_CONFIG_RATE_24M |
	    IWM_SCAN_CONFIG_RATE_36M | IWM_SCAN_CONFIG_RATE_48M |
	    IWM_SCAN_CONFIG_RATE_54M);

	cmd_size = sizeof (*scan_config) + sc->sc_capa_n_scan_channels;

	scan_config = kmem_zalloc(cmd_size, KM_SLEEP);

	scan_config->tx_chains = htole32(iwm_fw_valid_tx_ant(sc));
	scan_config->rx_chains = htole32(iwm_fw_valid_rx_ant(sc));
	scan_config->legacy_rates = htole32(rates |
	    IWM_SCAN_CONFIG_SUPPORTED_RATE(rates));

	/* These timings correspond to iwlwifi's UNASSOC scan. */
	scan_config->dwell_active = 10;
	scan_config->dwell_passive = 110;
	scan_config->dwell_fragmented = 44;
	scan_config->dwell_extended = 90;
	scan_config->out_of_channel_time = htole32(0);
	scan_config->suspend_time = htole32(0);

	IEEE80211_ADDR_COPY(scan_config->mac_addr, sc->sc_ic.ic_macaddr);

	scan_config->bcast_sta_id = IWM_AUX_STA_ID;
	scan_config->channel_flags = 0;

	for (c = &ic->ic_sup_channels[1], nchan = 0;
	    c <= &ic->ic_sup_channels[IEEE80211_CHAN_MAX] &&
	    nchan < sc->sc_capa_n_scan_channels; c++) {
		if (c->ich_flags == 0)
			continue;
		scan_config->channel_array[nchan++] =
		    ieee80211_chan2ieee(ic, c);
	}

	scan_config->flags = htole32(IWM_SCAN_CONFIG_FLAG_ACTIVATE |
	    IWM_SCAN_CONFIG_FLAG_ALLOW_CHUB_REQS |
	    IWM_SCAN_CONFIG_FLAG_SET_TX_CHAINS |
	    IWM_SCAN_CONFIG_FLAG_SET_RX_CHAINS |
	    IWM_SCAN_CONFIG_FLAG_SET_AUX_STA_ID |
	    IWM_SCAN_CONFIG_FLAG_SET_ALL_TIMES |
	    IWM_SCAN_CONFIG_FLAG_SET_LEGACY_RATES |
	    IWM_SCAN_CONFIG_FLAG_SET_MAC_ADDR |
	    IWM_SCAN_CONFIG_FLAG_SET_CHANNEL_FLAGS|
	    IWM_SCAN_CONFIG_N_CHANNELS(nchan) |
	    IWM_SCAN_CONFIG_FLAG_CLEAR_FRAGMENTED);

	hcmd.data[0] = scan_config;
	hcmd.len[0] = cmd_size;

	err = iwm_send_cmd(sc, &hcmd);
	kmem_free(scan_config, cmd_size);
	return (err);
}

static int
iwm_umac_scan_size(struct iwm_softc *sc)
{
	int base_size = IWM_SCAN_REQ_UMAC_SIZE_V1;
	int tail_size;

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
		base_size = IWM_SCAN_REQ_UMAC_SIZE_V8;
	else if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
		base_size = IWM_SCAN_REQ_UMAC_SIZE_V7;
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER))
		tail_size = sizeof (struct iwm_scan_req_umac_tail_v2);
	else
		tail_size = sizeof (struct iwm_scan_req_umac_tail_v1);

	return (base_size + sizeof (struct iwm_scan_channel_cfg_umac) *
	    sc->sc_capa_n_scan_channels + tail_size);
}

static struct iwm_scan_umac_chan_param *
iwm_get_scan_req_umac_chan_param(struct iwm_softc *sc,
    struct iwm_scan_req_umac *req)
{
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
		return (&req->v8.channel);

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
		return (&req->v7.channel);

	return (&req->v1.channel);
}

static void *
iwm_get_scan_req_umac_data(struct iwm_softc *sc, struct iwm_scan_req_umac *req)
{
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2))
		return ((void *)&req->v8.data);

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL))
		return ((void *)&req->v7.data);

	return ((void *)&req->v1.data);

}

/* adaptive dwell max budget time [TU] for full scan */
#define	IWM_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN 300
/* adaptive dwell max budget time [TU] for directed scan */
#define	IWM_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN 100
/* adaptive dwell default high band APs number */
#define	IWM_SCAN_ADWELL_DEFAULT_HB_N_APS 8
/* adaptive dwell default low band APs number */
#define	IWM_SCAN_ADWELL_DEFAULT_LB_N_APS 2
/* adaptive dwell default APs number in social channels (1, 6, 11) */
#define	IWM_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL 10

static int
iwm_umac_scan(struct iwm_softc *sc, int bgscan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_host_cmd hcmd = {
		.id = iwm_cmd_id(IWM_SCAN_REQ_UMAC, IWM_LONG_GROUP, 0),
		.len = { 0, },
		.data = { NULL, },
		.flags = 0,
	};
	struct iwm_scan_req_umac *req;
	void *cmd_data, *tail_data;
	struct iwm_scan_req_umac_tail_v2 *tail;
	struct iwm_scan_req_umac_tail_v1 *tailv1;
	struct iwm_scan_umac_chan_param *chanparam;
	size_t req_len;
	int err, async = bgscan;

	req_len = iwm_umac_scan_size(sc);
	if ((req_len < IWM_SCAN_REQ_UMAC_SIZE_V1 +
	    sizeof (struct iwm_scan_req_umac_tail_v1)) ||
	    req_len > IWM_MAX_CMD_PAYLOAD_SIZE)
		return (ERANGE);
	req = kmem_zalloc(req_len, async ? KM_NOSLEEP : KM_SLEEP);
	if (req == NULL)
		return (ENOMEM);

	hcmd.len[0] = (uint16_t)req_len;
	hcmd.data[0] = (void *)req;
	hcmd.flags |= async ? IWM_CMD_ASYNC : 0;

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
		req->v7.adwell_default_n_aps_social =
			IWM_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
		req->v7.adwell_default_n_aps =
			IWM_SCAN_ADWELL_DEFAULT_LB_N_APS;

		if (ic->ic_des_esslen != 0)
			req->v7.adwell_max_budget =
			    htole16(IWM_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN);
		else
			req->v7.adwell_max_budget =
			    htole16(IWM_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);

		req->v7.scan_priority = htole32(IWM_SCAN_PRIORITY_HIGH);
		req->v7.max_out_time[IWM_SCAN_LB_LMAC_IDX] = 0;
		req->v7.suspend_time[IWM_SCAN_LB_LMAC_IDX] = 0;

		if (isset(sc->sc_ucode_api,
		    IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
			req->v8.active_dwell[IWM_SCAN_LB_LMAC_IDX] = 10;
			req->v8.passive_dwell[IWM_SCAN_LB_LMAC_IDX] = 110;
		} else {
			req->v7.active_dwell = 10;
			req->v7.passive_dwell = 110;
			req->v7.fragmented_dwell = 44;
		}
	} else {
		/* These timings correspond to iwlwifi's UNASSOC scan. */
		req->v1.active_dwell = 10;
		req->v1.passive_dwell = 110;
		req->v1.fragmented_dwell = 44;
		req->v1.extended_dwell = 90;

		req->v1.scan_priority = htole32(IWM_SCAN_PRIORITY_HIGH);
	}

	if (bgscan) {
		const uint32_t timeout = htole32(120);
		if (isset(sc->sc_ucode_api,
		    IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
			req->v8.max_out_time[IWM_SCAN_LB_LMAC_IDX] = timeout;
			req->v8.suspend_time[IWM_SCAN_LB_LMAC_IDX] = timeout;
		} else if (isset(sc->sc_ucode_api,
		    IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
			req->v7.max_out_time[IWM_SCAN_LB_LMAC_IDX] = timeout;
			req->v7.suspend_time[IWM_SCAN_LB_LMAC_IDX] = timeout;
		} else {
			req->v1.max_out_time = timeout;
			req->v1.suspend_time = timeout;
		}
	}

	req->ooc_priority = htole32(IWM_SCAN_PRIORITY_HIGH);

	cmd_data = iwm_get_scan_req_umac_data(sc, req);
	chanparam = iwm_get_scan_req_umac_chan_param(sc, req);
	chanparam->count = iwm_umac_scan_fill_channels(sc,
	    (struct iwm_scan_channel_cfg_umac *)cmd_data,
	    ic->ic_des_esslen != 0, bgscan);
	chanparam->flags = 0;

	tail_data = (uint8_t *)cmd_data +
	    sizeof (struct iwm_scan_channel_cfg_umac) *
	    sc->sc_capa_n_scan_channels;
	tail = tail_data;
	/* tail v1 layout differs in preq and direct_scan member fields. */
	tailv1 = tail_data;

	req->general_flags = htole32(IWM_UMAC_SCAN_GEN_FLAGS_PASS_ALL |
	    IWM_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE);
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL_V2)) {
		req->v8.general_flags2 =
			IWM_UMAC_SCAN_GEN_FLAGS2_ALLOW_CHNL_REORDER;
	}

	if (ic->ic_des_esslen != 0) {
		if (isset(sc->sc_ucode_api,
		    IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER)) {
			tail->direct_scan[0].id = IEEE80211_ELEMID_SSID;
			tail->direct_scan[0].len = ic->ic_des_esslen;
			memcpy(tail->direct_scan[0].ssid, ic->ic_des_essid,
			    ic->ic_des_esslen);
		} else {
			tailv1->direct_scan[0].id = IEEE80211_ELEMID_SSID;
			tailv1->direct_scan[0].len = ic->ic_des_esslen;
			memcpy(tailv1->direct_scan[0].ssid, ic->ic_des_essid,
			    ic->ic_des_esslen);
		}
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT);
	} else
		req->general_flags |= htole32(IWM_UMAC_SCAN_GEN_FLAGS_PASSIVE);

	if (isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT) &&
	    isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED);

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_ADAPTIVE_DWELL)) {
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_ADAPTIVE_DWELL);
	} else {
		req->general_flags |=
		    htole32(IWM_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL);
	}

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_SCAN_EXT_CHAN_VER))
		err = iwm_fill_probe_req(sc, &tail->preq);
	else
		err = iwm_fill_probe_req_v1(sc, &tailv1->preq);
	if (err) {
		kmem_free(req, req_len);
		return (err);
	}

	/* Specify the scan plan: We'll do one iteration. */
	tail->schedule[0].interval = 0;
	tail->schedule[0].iter_count = 1;

	err = iwm_send_cmd(sc, &hcmd);
	kmem_free(req, req_len);
	return (err);
}

static void
iwm_mcc_update(struct iwm_softc *sc, struct iwm_mcc_chub_notif *notif)
{
	char alpha2[3];

	(void) snprintf(alpha2, sizeof (alpha2), "%c%c",
	    (le16toh(notif->mcc) & 0xff00) >> 8, le16toh(notif->mcc) & 0xff);

	DPRINTF((CE_CONT, "!iwm%d: firmware has detected regulatory "
	    "domain '%s' (0x%x)", ddi_get_instance(sc->sc_dip), alpha2,
	    le16toh(notif->mcc)));
	_NOTE(ARGUNUSED(sc))

	/* TODO: Schedule a task to send MCC_UPDATE_CMD? */
}

static uint8_t
iwm_ridx2rate(struct ieee80211_rateset *rs, int ridx)
{
	int i;
	uint8_t rval;

	for (i = 0; i < rs->ir_nrates; i++) {
		rval = (rs->ir_rates[i] & IEEE80211_RATE_VAL);
		if (rval == iwm_rates[ridx].rate)
			return (rs->ir_rates[i]);
	}

	return (0);
}

static int
iwm_rval2ridx(int rval)
{
	int ridx;

	for (ridx = 0; ridx < (int)nitems(iwm_rates); ridx++) {
		if (iwm_rates[ridx].plcp == IWM_RATE_INVM_PLCP)
			continue;
		if (rval == iwm_rates[ridx].rate)
			break;
	}

	return (ridx);
}

static void
iwm_ack_rates(struct iwm_softc *sc, struct iwm_node *in, int *cck_rates,
    int *ofdm_rates)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211_rateset *rs = &ni->in_rates;
	int lowest_present_ofdm = -1;
	int lowest_present_cck = -1;
	uint8_t cck = 0;
	uint8_t ofdm = 0;
	int i;

	_NOTE(ARGUNUSED(sc))
	if (ni->in_chan == NULL || ni->in_chan == IEEE80211_CHAN_ANYC ||
	    IEEE80211_IS_CHAN_2GHZ(ni->in_chan)) {
		for (i = IWM_FIRST_CCK_RATE; i < IWM_FIRST_OFDM_RATE; i++) {
			if ((iwm_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
				continue;
			cck |= (1 << i);
			if (lowest_present_cck == -1 || lowest_present_cck > i)
				lowest_present_cck = i;
		}
	}
	for (i = IWM_FIRST_OFDM_RATE; i <= IWM_LAST_NON_HT_RATE; i++) {
		if ((iwm_ridx2rate(rs, i) & IEEE80211_RATE_BASIC) == 0)
			continue;
		ofdm |= (1 << (i - IWM_FIRST_OFDM_RATE));
		if (lowest_present_ofdm == -1 || lowest_present_ofdm > i)
			lowest_present_ofdm = i;
	}

	/*
	 * Now we've got the basic rates as bitmaps in the ofdm and cck
	 * variables. This isn't sufficient though, as there might not
	 * be all the right rates in the bitmap. E.g. if the only basic
	 * rates are 5.5 Mbps and 11 Mbps, we still need to add 1 Mbps
	 * and 6 Mbps because the 802.11-2007 standard says in 9.6:
	 *
	 *    [...] a STA responding to a received frame shall transmit
	 *    its Control Response frame [...] at the highest rate in the
	 *    BSSBasicRateSet parameter that is less than or equal to the
	 *    rate of the immediately previous frame in the frame exchange
	 *    sequence ([...]) and that is of the same modulation class
	 *    ([...]) as the received frame. If no rate contained in the
	 *    BSSBasicRateSet parameter meets these conditions, then the
	 *    control frame sent in response to a received frame shall be
	 *    transmitted at the highest mandatory rate of the PHY that is
	 *    less than or equal to the rate of the received frame, and
	 *    that is of the same modulation class as the received frame.
	 *
	 * As a consequence, we need to add all mandatory rates that are
	 * lower than all of the basic rates to these bitmaps.
	 */

	if (IWM_RATE_24M_INDEX < lowest_present_ofdm)
		ofdm |= IWM_RATE_BIT_MSK(24) >> IWM_FIRST_OFDM_RATE;
	if (IWM_RATE_12M_INDEX < lowest_present_ofdm)
		ofdm |= IWM_RATE_BIT_MSK(12) >> IWM_FIRST_OFDM_RATE;
	/* 6M already there or needed so always add */
	ofdm |= IWM_RATE_BIT_MSK(6) >> IWM_FIRST_OFDM_RATE;

	/*
	 * CCK is a bit more complex with DSSS vs. HR/DSSS vs. ERP.
	 * Note, however:
	 *  - if no CCK rates are basic, it must be ERP since there must
	 *    be some basic rates at all, so they're OFDM => ERP PHY
	 *    (or we're in 5 GHz, and the cck bitmap will never be used)
	 *  - if 11M is a basic rate, it must be ERP as well, so add 5.5M
	 *  - if 5.5M is basic, 1M and 2M are mandatory
	 *  - if 2M is basic, 1M is mandatory
	 *  - if 1M is basic, that's the only valid ACK rate.
	 * As a consequence, it's not as complicated as it sounds, just add
	 * any lower rates to the ACK rate bitmap.
	 */
	if (IWM_RATE_11M_INDEX < lowest_present_cck)
		cck |= IWM_RATE_BIT_MSK(11) >> IWM_FIRST_CCK_RATE;
	if (IWM_RATE_5M_INDEX < lowest_present_cck)
		cck |= IWM_RATE_BIT_MSK(5) >> IWM_FIRST_CCK_RATE;
	if (IWM_RATE_2M_INDEX < lowest_present_cck)
		cck |= IWM_RATE_BIT_MSK(2) >> IWM_FIRST_CCK_RATE;
	/* 1M already there or needed so always add */
	cck |= IWM_RATE_BIT_MSK(1) >> IWM_FIRST_CCK_RATE;

	*cck_rates = cck;
	*ofdm_rates = ofdm;
}

static void
iwm_mac_ctxt_cmd_common(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_ctx_cmd *cmd, uint32_t action)
{
#define	IWM_EXP2(x)	((1 << (x)) - 1)	/* CWmin = 2^ECWmin - 1 */
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	int cck_ack_rates, ofdm_ack_rates;
	int i;

	cmd->id_and_color = htole32(IWM_FW_CMD_ID_AND_COLOR(in->in_id,
	    in->in_color));
	cmd->action = htole32(action);

	cmd->mac_type = htole32(IWM_FW_MAC_TYPE_BSS_STA);
	cmd->tsf_id = htole32(IWM_TSF_ID_A);

	IEEE80211_ADDR_COPY(cmd->node_addr, ic->ic_macaddr);

	IEEE80211_ADDR_COPY(cmd->bssid_addr, in->in_macaddr);
	iwm_ack_rates(sc, in, &cck_ack_rates, &ofdm_ack_rates);
	cmd->cck_rates = htole32(cck_ack_rates);
	cmd->ofdm_rates = htole32(ofdm_ack_rates);

	cmd->cck_short_preamble
	    = htole32((ic->ic_flags & IEEE80211_F_SHPREAMBLE)
	    ? IWM_MAC_FLG_SHORT_PREAMBLE : 0);
	cmd->short_slot
	    = htole32((ic->ic_flags & IEEE80211_F_SHSLOT)
	    ? IWM_MAC_FLG_SHORT_SLOT : 0);

	/* illumos keeps the BSS EDCA parameters in ic_wme. */
	for (i = 0; i < EDCA_NUM_AC; i++) {
		const struct wmeParams *ac =
		    &ic->ic_wme.wme_chanParams.cap_wmeParams[i];
		int txf = iwm_ac_to_tx_fifo[i];

		cmd->ac[txf].cw_min = htole16(IWM_EXP2(ac->wmep_logcwmin));
		cmd->ac[txf].cw_max = htole16(IWM_EXP2(ac->wmep_logcwmax));
		cmd->ac[txf].aifsn = ac->wmep_aifsn;
		cmd->ac[txf].fifos_mask = (1 << txf);
		cmd->ac[txf].edca_txop = htole16(ac->wmep_txopLimit * 32);
	}
	if (ni->in_flags & IEEE80211_NODE_QOS)
		cmd->qos_flags |= htole32(IWM_MAC_QOS_FLG_UPDATE_EDCA);

	if (ni->in_flags & IEEE80211_NODE_HT) {
		/* in_htopmode uses the 802.11 HT protection encoding */
		switch (ni->in_htopmode & IEEE80211_HTINFO_OPMODE) {
		case IEEE80211_HTINFO_OPMODE_PURE:
			break;
		case IEEE80211_HTINFO_OPMODE_PROTOPT:
		case IEEE80211_HTINFO_OPMODE_MIXED:
			cmd->protection_flags |=
			    htole32(IWM_MAC_PROT_FLG_HT_PROT |
			    IWM_MAC_PROT_FLG_FAT_PROT);
			break;
		case IEEE80211_HTINFO_OPMODE_HT20PR:
			if (in->in_phyctxt &&
			    (in->in_phyctxt->sco == IWM_HTOP0_SCO_SCA ||
			    in->in_phyctxt->sco == IWM_HTOP0_SCO_SCB)) {
				cmd->protection_flags |=
				    htole32(IWM_MAC_PROT_FLG_HT_PROT |
				    IWM_MAC_PROT_FLG_FAT_PROT);
			}
			break;
		default:
			break;
		}

		cmd->qos_flags |= htole32(IWM_MAC_QOS_FLG_TGN);
	}
	if (ic->ic_flags & IEEE80211_F_USEPROT)
		cmd->protection_flags |= htole32(IWM_MAC_PROT_FLG_TGG_PROTECT);

	cmd->filter_flags = htole32(IWM_MAC_FILTER_ACCEPT_GRP);
#undef IWM_EXP2
}

static void
iwm_mac_ctxt_cmd_fill_sta(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_data_sta *sta, int assoc)
{
	struct ieee80211_node *ni = &in->in_ni;
	uint32_t dtim_off;
	uint64_t tsf;

	_NOTE(ARGUNUSED(sc))
	dtim_off = ni->in_dtim_count * ni->in_intval * IWM_DUR_TU;
	memcpy(&tsf, ni->in_tstamp.data, sizeof (tsf));
	tsf = le64toh(tsf);

	sta->is_assoc = htole32(assoc);
	sta->dtim_time = htole32(ni->in_rstamp + dtim_off);
	sta->dtim_tsf = htole64(tsf + dtim_off);
	sta->bi = htole32(ni->in_intval);
	sta->bi_reciprocal = htole32(iwm_reciprocal(ni->in_intval));
	sta->dtim_interval = htole32(ni->in_intval * ni->in_dtim_period);
	sta->dtim_reciprocal = htole32(iwm_reciprocal(sta->dtim_interval));
	sta->listen_interval = htole32(10);
	sta->assoc_id = htole32(IEEE80211_AID(ni->in_associd));
	sta->assoc_beacon_arrive_time = htole32(ni->in_rstamp);
}

static int
iwm_mac_ctxt_cmd(struct iwm_softc *sc, struct iwm_node *in, uint32_t action,
    int assoc)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct iwm_mac_ctx_cmd cmd;
	int active = (sc->sc_flags & IWM_FLAG_MAC_ACTIVE);

	/* OpenBSD panics on these. */
	if (action == IWM_FW_CTXT_ACTION_ADD && active)
		return (EBUSY);
	if (action == IWM_FW_CTXT_ACTION_REMOVE && !active)
		return (ENOENT);

	memset(&cmd, 0, sizeof (cmd));

	iwm_mac_ctxt_cmd_common(sc, in, &cmd, action);

	if (!assoc || !ni->in_associd || !ni->in_dtim_period)
		/*
		 * Allow beacons to pass through as long as we are not
		 * associated or we do not have dtim period information.
		 */
		cmd.filter_flags |= htole32(IWM_MAC_FILTER_IN_BEACON);
	else
		iwm_mac_ctxt_cmd_fill_sta(sc, in, &cmd.sta, assoc);

	return (iwm_send_cmd_pdu(sc, IWM_MAC_CONTEXT_CMD, 0, sizeof (cmd),
	    &cmd));
}

static int
iwm_update_quotas(struct iwm_softc *sc, struct iwm_node *in, int running)
{
	struct iwm_time_quota_cmd_v1 cmd;
	int i, idx, num_active_macs, quota, quota_rem;
	int colors[IWM_MAX_BINDINGS] = { -1, -1, -1, -1, };
	int n_ifs[IWM_MAX_BINDINGS] = {0, };
	uint16_t id;

	memset(&cmd, 0, sizeof (cmd));

	/* currently, PHY ID == binding ID */
	if (in && in->in_phyctxt) {
		id = in->in_phyctxt->id;
		ASSERT(id < IWM_MAX_BINDINGS);
		colors[id] = in->in_phyctxt->color;
		if (running)
			n_ifs[id] = 1;
	}

	/*
	 * The FW's scheduling session consists of
	 * IWM_MAX_QUOTA fragments. Divide these fragments
	 * equally between all the bindings that require quota
	 */
	num_active_macs = 0;
	for (i = 0; i < IWM_MAX_BINDINGS; i++) {
		cmd.quotas[i].id_and_color = htole32(IWM_FW_CTXT_INVALID);
		num_active_macs += n_ifs[i];
	}

	quota = 0;
	quota_rem = 0;
	if (num_active_macs) {
		quota = IWM_MAX_QUOTA / num_active_macs;
		quota_rem = IWM_MAX_QUOTA % num_active_macs;
	}

	for (idx = 0, i = 0; i < IWM_MAX_BINDINGS; i++) {
		if (colors[i] < 0)
			continue;

		cmd.quotas[idx].id_and_color =
			htole32(IWM_FW_CMD_ID_AND_COLOR(i, colors[i]));

		if (n_ifs[i] <= 0) {
			cmd.quotas[idx].quota = htole32(0);
			cmd.quotas[idx].max_duration = htole32(0);
		} else {
			cmd.quotas[idx].quota = htole32(quota * n_ifs[i]);
			cmd.quotas[idx].max_duration = htole32(0);
		}
		idx++;
	}

	/* Give the remainder of the session to the first binding */
	cmd.quotas[0].quota = htole32(le32toh(cmd.quotas[0].quota) + quota_rem);

	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_QUOTA_LOW_LATENCY)) {
		struct iwm_time_quota_cmd cmd_v2;

		memset(&cmd_v2, 0, sizeof (cmd_v2));
		for (i = 0; i < IWM_MAX_BINDINGS; i++) {
			cmd_v2.quotas[i].id_and_color =
			    cmd.quotas[i].id_and_color;
			cmd_v2.quotas[i].quota = cmd.quotas[i].quota;
			cmd_v2.quotas[i].max_duration =
			    cmd.quotas[i].max_duration;
		}
		return (iwm_send_cmd_pdu(sc, IWM_TIME_QUOTA_CMD, 0,
		    sizeof (cmd_v2), &cmd_v2));
	}

	return (iwm_send_cmd_pdu(sc, IWM_TIME_QUOTA_CMD, 0, sizeof (cmd),
	    &cmd));
}


/*
 * Deferred work.  OpenBSD queues struct task objects and counts them in
 * task_refs; here each task is identified by an IWM_TASK_* bit so a task is
 * queued at most once (task_add semantics) and sc_task_refs lets
 * iwm_stop()/detach wait for running tasks.  Called with sc_mtx held.
 */
static void
iwm_add_task(struct iwm_softc *sc, taskq_t *tq, uint32_t task)
{
	void (*fn)(void *);

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) && task != IWM_TASK_INIT)
		return;
	if (sc->sc_task_pending & task)
		return;

	switch (task) {
	case IWM_TASK_INIT:
		fn = iwm_init_task;
		break;
	case IWM_TASK_NEWSTATE:
		fn = iwm_newstate_task;
		break;
	case IWM_TASK_BA:
		fn = iwm_ba_task;
		break;
	case IWM_TASK_MAC_CTXT:
		fn = iwm_mac_ctxt_task;
		break;
	case IWM_TASK_DEFER:
		fn = iwm_defer_task;
		break;
	default:
		return;
	}
	/*
	 * init_task is not counted: it stops the device itself and so must
	 * not be waited for by iwm_stop() (OpenBSD runs it outside
	 * task_refs too).  taskq_destroy() waits for it at detach.
	 */
	sc->sc_task_pending |= task;
	if (task != IWM_TASK_INIT)
		sc->sc_task_refs++;
	if (taskq_dispatch(tq, fn, sc, TQ_NOSLEEP) == TASKQID_INVALID) {
		sc->sc_task_pending &= ~task;
		if (task != IWM_TASK_INIT)
			sc->sc_task_refs--;
		cv_broadcast(&sc->sc_task_cv);
	}
}

/* A task has finished (OpenBSD refcnt_rele_wake).  sc_mtx held. */
static void
iwm_task_done(struct iwm_softc *sc, uint32_t task)
{
	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	/*
	 * Tasks clear their pending bit when they start (iwm_task_start),
	 * so a request made while they run queues them again.
	 */
	if (task == IWM_TASK_INIT) {
		sc->sc_task_pending &= ~task;
		return;
	}
	ASSERT(sc->sc_task_refs > 0);
	sc->sc_task_refs--;
	cv_broadcast(&sc->sc_task_cv);
}

/*
 * Work the interrupt handler must not do itself because it calls into
 * net80211, which may transmit or change state: end of scan, beacon loss
 * and BlockAck requests.  OpenBSD does these from interrupt context.
 */
static void
iwm_defer_task(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int endscan, bmiss, tid;
	uint32_t bar;
	uint16_t bar_ssn[IWM_MAX_TID_COUNT];

	mutex_enter(&sc->sc_mtx);
	sc->sc_task_pending &= ~IWM_TASK_DEFER;	/* requeue on new work */
	endscan = sc->sc_endscan_pending;
	bmiss = sc->sc_bmiss_pending;
	bar = sc->sc_bar_pending;
	memcpy(bar_ssn, sc->sc_bar_ssn, sizeof (bar_ssn));
	sc->sc_endscan_pending = sc->sc_bmiss_pending = 0;
	sc->sc_bar_pending = 0;
	if (sc->sc_flags & IWM_FLAG_SHUTDOWN) {
		iwm_task_done(sc, IWM_TASK_DEFER);
		mutex_exit(&sc->sc_mtx);
		return;
	}
	mutex_exit(&sc->sc_mtx);

	/*
	 * illumos ieee80211_end_scan() does not check the state; a late
	 * scan-complete after a move to INIT would otherwise start a join.
	 */
	if (endscan && ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_end_scan(ic);
	if (bmiss)
		ieee80211_beacon_miss(ic);
	for (tid = 0; bar != 0 && tid < IWM_MAX_TID_COUNT; tid++) {
		struct ieee80211_tx_ampdu *tap;

		if ((bar & (1 << tid)) == 0 || ic->ic_bss == NULL ||
		    ic->ic_state != IEEE80211_S_RUN)
			continue;
		tap = &ic->ic_bss->in_tx_ampdu[TID_TO_WME_AC(tid)];
		tap->txa_start = bar_ssn[tid];
		(void) ieee80211_send_bar(ic->ic_bss, tap);
	}

	mutex_enter(&sc->sc_mtx);
	iwm_task_done(sc, IWM_TASK_DEFER);
	mutex_exit(&sc->sc_mtx);
}

static int
iwm_scan(struct iwm_softc *sc)
{
	int err;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	if (sc->sc_flags & IWM_FLAG_BGSCAN) {
		err = iwm_scan_abort(sc);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!could not abort background scan");
			return (err);
		}
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN))
		err = iwm_umac_scan(sc, 0);
	else
		err = iwm_lmac_scan(sc, 0);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not initiate scan");
		return (err);
	}

	/*
	 * illumos: net80211's own state handling (ieee80211_newstate) marks
	 * the scan and drops the BSS; it runs after this from the newstate
	 * task.  OpenBSD does those steps here.
	 */
	sc->sc_flags |= IWM_FLAG_SCANNING;
	iwm_led_blink_start(sc);
	cv_broadcast(&sc->sc_fw_cv);	/* wake iwm_init() */

	return (0);
}

static int
iwm_umac_scan_abort(struct iwm_softc *sc)
{
	struct iwm_umac_scan_abort cmd = { 0 };

	return (iwm_send_cmd_pdu(sc,
	    IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_ABORT_UMAC),
	    0, sizeof (cmd), &cmd));
}

static int
iwm_lmac_scan_abort(struct iwm_softc *sc)
{
	struct iwm_host_cmd cmd = {
		.id = IWM_SCAN_OFFLOAD_ABORT_CMD,
	};
	int err;
	uint32_t status;

	err = iwm_send_cmd_status(sc, &cmd, &status);
	if (err)
		return (err);

	if (status != IWM_CAN_ABORT_STATUS) {
		/*
		 * The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before the
		 * microcode has notified us that a scan is completed.
		 */
		return (EBUSY);
	}

	return (0);
}

static int
iwm_scan_abort(struct iwm_softc *sc)
{
	int err;

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN))
		err = iwm_umac_scan_abort(sc);
	else
		err = iwm_lmac_scan_abort(sc);

	if (err == 0)
		sc->sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
	return (err);
}

static int
iwm_phy_ctxt_update(struct iwm_softc *sc, struct iwm_phy_ctxt *phyctxt,
    struct ieee80211_channel *chan, uint8_t chains_static,
    uint8_t chains_dynamic, uint32_t apply_time, uint8_t sco,
    uint8_t vht_chan_width)
{
	uint32_t band_flags = (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ);
	int err;

	if (isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT) &&
	    (phyctxt->channel->ich_flags & band_flags) !=
	    (chan->ich_flags & band_flags)) {
		err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWM_FW_CTXT_ACTION_REMOVE, apply_time, sco,
		    vht_chan_width);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not remove PHY "
			    "context (error %d)", err);
			return (err);
		}
		phyctxt->channel = chan;
		err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWM_FW_CTXT_ACTION_ADD, apply_time, sco,
		    vht_chan_width);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not add PHY "
			    "context (error %d)", err);
			return (err);
		}
	} else {
		phyctxt->channel = chan;
		err = iwm_phy_ctxt_cmd(sc, phyctxt, chains_static,
		    chains_dynamic, IWM_FW_CTXT_ACTION_MODIFY, apply_time, sco,
		    vht_chan_width);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not update PHY "
			    "context (error %d)", err);
			return (err);
		}
	}

	phyctxt->sco = sco;
	phyctxt->vht_chan_width = vht_chan_width;
	return (0);
}

static int
iwm_auth(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	uint32_t duration;
	int generation = sc->sc_generation, err;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));

	if (in->in_ni.in_chan == NULL ||
	    in->in_ni.in_chan == IEEE80211_CHAN_ANYC)
		return (EINVAL);
	err = iwm_phy_ctxt_update(sc, &sc->sc_phyctxt[0],
	    in->in_ni.in_chan, 1, 1, 0, IWM_HTOP0_SCO_SCN, 0);
	if (err)
		return (err);
	in->in_phyctxt = &sc->sc_phyctxt[0];
	IEEE80211_ADDR_COPY(in->in_macaddr, in->in_ni.in_macaddr);
	iwm_setrates(in, 0);

	err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_ADD, 0);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not add MAC context "
		    "(error %d)", err);
		return (err);
	}
	sc->sc_flags |= IWM_FLAG_MAC_ACTIVE;

	err = iwm_binding_cmd(sc, in, IWM_FW_CTXT_ACTION_ADD);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not add binding "
		    "(error %d)", err);
		goto rm_mac_ctxt;
	}
	sc->sc_flags |= IWM_FLAG_BINDING_ACTIVE;

	in->tid_disable_ampdu = 0xffff;
	err = iwm_add_sta_cmd(sc, in, 0);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not add sta (error %d)",
		    err);
		goto rm_binding;
	}
	sc->sc_flags |= IWM_FLAG_STA_ACTIVE;

	/*
	 * Prevent the FW from wandering off channel during association
	 * by "protecting" the session with a time event.
	 */
	if (in->in_ni.in_intval)
		duration = in->in_ni.in_intval * 2;
	else
		duration = IWM_DUR_TU;
	iwm_protect_session(sc, in, duration, in->in_ni.in_intval / 2);

	return (0);

rm_binding:
	if (generation == sc->sc_generation) {
		(void) iwm_binding_cmd(sc, in, IWM_FW_CTXT_ACTION_REMOVE);
		sc->sc_flags &= ~IWM_FLAG_BINDING_ACTIVE;
	}
rm_mac_ctxt:
	if (generation == sc->sc_generation) {
		(void) iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_REMOVE, 0);
		sc->sc_flags &= ~IWM_FLAG_MAC_ACTIVE;
	}
	return (err);
}

static int
iwm_deauth(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	int err;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));

	iwm_unprotect_session(sc, in);

	if (sc->sc_flags & IWM_FLAG_STA_ACTIVE) {
		err = iwm_flush_sta(sc, in);
		if (err)
			return (err);
		err = iwm_rm_sta_cmd(sc, in);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not remove STA "
			    "(error %d)", err);
			return (err);
		}
		in->tid_disable_ampdu = 0xffff;
		sc->sc_flags &= ~IWM_FLAG_STA_ACTIVE;
		sc->sc_rx_ba_sessions = 0;
		sc->sc_rx_ba_tidmask = 0;
		sc->ba_rx.start_tidmask = 0;
		sc->ba_rx.stop_tidmask = 0;
		sc->tx_ba_queue_mask = 0;
		sc->ba_tx.start_tidmask = 0;
		sc->ba_tx.stop_tidmask = 0;
	}

	if (sc->sc_flags & IWM_FLAG_BINDING_ACTIVE) {
		err = iwm_binding_cmd(sc, in, IWM_FW_CTXT_ACTION_REMOVE);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not remove "
			    "binding (error %d)", err);
			return (err);
		}
		sc->sc_flags &= ~IWM_FLAG_BINDING_ACTIVE;
	}

	if (sc->sc_flags & IWM_FLAG_MAC_ACTIVE) {
		err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_REMOVE, 0);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not remove MAC "
			    "context (error %d)", err);
			return (err);
		}
		sc->sc_flags &= ~IWM_FLAG_MAC_ACTIVE;
	}

	/* Move unused PHY context to a default channel. */
	err = iwm_phy_ctxt_update(sc, &sc->sc_phyctxt[0],
	    iwm_default_chan(sc), 1, 1, 0, IWM_HTOP0_SCO_SCN, 0);
	if (err)
		return (err);

	return (0);
}

static int
iwm_run(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int err;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));

	/* Configure Rx chains for MIMO and configure 40 MHz channel. */
	if (ni->in_flags & IEEE80211_NODE_HT) {
		uint8_t chains = iwm_mimo_enabled(sc) ? 2 : 1;
		uint8_t sco = iwm_node_sco(ni);

		err = iwm_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, chains, chains,
		    0, sco, 0);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!failed to update PHY");
			return (err);
		}
	}

	/* Update STA again to apply HT settings. */
	err = iwm_add_sta_cmd(sc, in, 1);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not update STA "
		    "(error %d)", err);
		return (err);
	}

	/* We have now been assigned an associd by the AP. */
	err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_MODIFY, 1);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!failed to update MAC");
		return (err);
	}

	err = iwm_sf_config(sc, IWM_SF_FULL_ON);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not set sf full on "
		    "(error %d)", err);
		return (err);
	}

	err = iwm_allow_mcast(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not allow mcast "
		    "(error %d)", err);
		return (err);
	}

	err = iwm_power_update_device(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not send power command "
		    "(error %d)", err);
		return (err);
	}
	/*
	 * As in OpenBSD, beacon filtering stays disabled: the default
	 * settings stop net80211 from seeing ERP/HT protection updates.
	 */
	err = iwm_power_mac_update_mode(sc, in);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not update MAC power "
		    "(error %d)", err);
		return (err);
	}

	if (!isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DYNAMIC_QUOTA)) {
		err = iwm_update_quotas(sc, in, 1);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not update "
			    "quotas (error %d)", err);
			return (err);
		}
	}

	iwm_amrr_node_init(&sc->sc_amrr, &in->in_amn);
	iwm_ra_node_init(&in->in_rn);

	/* Start at lowest available bit-rate, AMRR will raise. */
	in->in_ni.in_txrate = 0;
	in->in_txmcs = 0;
	iwm_setrates(in, 0);

	if (sc->sc_calib_to == 0)
		sc->sc_calib_to = timeout(iwm_calib_timeout, sc,
		    drv_usectohz(500000));
	iwm_led_enable(sc);

	return (0);
}

static int
iwm_run_stop(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct ieee80211_node *ni = &in->in_ni;
	int err, tid;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));

	/*
	 * Stop Tx/Rx BA sessions now. We cannot rely on the BA task
	 * for this when moving out of RUN state since it runs in a
	 * separate thread.  (OpenBSD walks its Rx reorder buffers here;
	 * without multi-queue Rx the firmware sessions are per TID.)
	 */
	for (tid = 0; tid < IWM_MAX_TID_COUNT && sc->sc_rx_ba_sessions > 0;
	    tid++) {
		if (sc->sc_rx_ba_tidmask & (1 << tid)) {
			err = iwm_sta_rx_agg(sc, ni, tid, 0, 0, 0, 0);
			if (err)
				return (err);
		}
	}
	sc->sc_rx_ba_sessions = 0;
	sc->sc_rx_ba_tidmask = 0;
	for (tid = 0; tid < IWM_MAX_TID_COUNT; tid++) {
		int qid = IWM_FIRST_AGG_TX_QUEUE + tid;
		if ((sc->tx_ba_queue_mask & (1 << qid)) == 0)
			continue;
		err = iwm_sta_tx_agg(sc, ni, tid, 0, 0, 0);
		if (err)
			return (err);
		err = iwm_disable_txq(sc, IWM_STATION_ID, qid, tid);
		if (err)
			return (err);
		in->tfd_queue_msk &= ~(1 << qid);
	}

	err = iwm_sf_config(sc, IWM_SF_INIT_OFF);
	if (err)
		return (err);

	(void) iwm_disable_beacon_filter(sc);

	if (!isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DYNAMIC_QUOTA)) {
		err = iwm_update_quotas(sc, in, 0);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!could not update "
			    "quotas (error %d)", err);
			return (err);
		}
	}

	/* Mark station as disassociated. */
	err = iwm_mac_ctxt_cmd(sc, in, IWM_FW_CTXT_ACTION_MODIFY, 0);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!failed to update MAC");
		return (err);
	}

	/* Reset Tx chains in case MIMO or 40 MHz channels were enabled. */
	if (in->in_ni.in_flags & IEEE80211_NODE_HT) {
		err = iwm_phy_ctxt_update(sc, in->in_phyctxt,
		    in->in_phyctxt->channel, 1, 1, 0, IWM_HTOP0_SCO_SCN, 0);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN, "!failed to update PHY");
			return (err);
		}
	}

	return (0);
}

/* First valid channel; OpenBSD uses &ic->ic_channels[1]. */
static struct ieee80211_channel *
iwm_default_chan(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int i;

	for (i = 1; i <= IEEE80211_CHAN_MAX; i++) {
		if (ic->ic_sup_channels[i].ich_flags != 0)
			return (&ic->ic_sup_channels[i]);
	}
	return (&ic->ic_sup_channels[1]);
}

static struct ieee80211_node *
iwm_node_alloc(struct ieee80211com *ic)
{
	struct iwm_node *in;

	_NOTE(ARGUNUSED(ic))
	/* net80211 asserts success when called from thread context. */
	in = kmem_zalloc(sizeof (struct iwm_node),
	    servicing_interrupt() ? KM_NOSLEEP : KM_SLEEP);
	if (in == NULL)
		return (NULL);
	/* No firmware Rx aggregation until a BA session is set up. */
	in->tid_disable_ampdu = 0xffff;
	return (&in->in_ni);
}

static void
iwm_node_free(struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->in_ic;

	ic->ic_node_cleanup(ni);
	if (ni->in_wpa_ie != NULL)
		ieee80211_free(ni->in_wpa_ie);
	if (ni->in_wme_ie != NULL)
		ieee80211_free(ni->in_wme_ie);
	if (ni->in_htcap_ie != NULL)
		ieee80211_free(ni->in_htcap_ie);
	kmem_free(ni, sizeof (struct iwm_node));
}

/*
 * illumos ic_crypto.cs_key_set hook (OpenBSD ic_set_key).  As in OpenBSD,
 * only the pairwise CCMP key goes to the firmware; group keys and other
 * ciphers stay in software, which we select per key by setting
 * IEEE80211_KEY_SWCRYPT (net80211's software ciphers do not depend on it
 * being set when the key was created).
 */
static int
iwm_set_key(struct ieee80211com *ic, const struct ieee80211_key *ck,
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct iwm_softc *sc = (struct iwm_softc *)ic;
	struct ieee80211_key *k = (struct ieee80211_key *)ck;
	struct iwm_add_sta_key_cmd cmd;
	struct iwm_add_sta_key_cmd_v1 cmd_v1;
	int kid, err;

	_NOTE(ARGUNUSED(mac))
	kid = k - ic->ic_nw_keys;
	if (kid < 0 || kid >= IEEE80211_WEP_NKID)
		return (1);

	if ((k->wk_flags & IEEE80211_KEY_GROUP) || k->wk_cipher == NULL ||
	    k->wk_cipher->ic_cipher != IEEE80211_CIPHER_AES_CCM) {
		/* Fallback to software crypto for other ciphers. */
		k->wk_flags |= IEEE80211_KEY_SWCRYPT;
		return (1);
	}

	mutex_enter(&sc->sc_mtx);
	if ((sc->sc_flags & IWM_FLAG_STA_ACTIVE) == 0) {
		/* No firmware station to attach the key to. */
		mutex_exit(&sc->sc_mtx);
		k->wk_flags |= IEEE80211_KEY_SWCRYPT;
		return (1);
	}

	if (!isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_TKIP_MIC_KEYS)) {
		memset(&cmd_v1, 0, sizeof (cmd_v1));
		cmd_v1.common.key_flags = htole16(IWM_STA_KEY_FLG_CCM |
		    IWM_STA_KEY_FLG_WEP_KEY_MAP |
		    ((kid << IWM_STA_KEY_FLG_KEYID_POS) &
		    IWM_STA_KEY_FLG_KEYID_MSK));
		memcpy(cmd_v1.common.key, k->wk_key,
		    MIN(sizeof (cmd_v1.common.key), k->wk_keylen));
		cmd_v1.common.key_offset = 0;
		cmd_v1.common.sta_id = IWM_STATION_ID;
		err = iwm_send_cmd_pdu(sc, IWM_ADD_STA_KEY, IWM_CMD_ASYNC,
		    sizeof (cmd_v1), &cmd_v1);
	} else {
		memset(&cmd, 0, sizeof (cmd));
		cmd.common.key_flags = htole16(IWM_STA_KEY_FLG_CCM |
		    IWM_STA_KEY_FLG_WEP_KEY_MAP |
		    ((kid << IWM_STA_KEY_FLG_KEYID_POS) &
		    IWM_STA_KEY_FLG_KEYID_MSK));
		memcpy(cmd.common.key, k->wk_key,
		    MIN(sizeof (cmd.common.key), k->wk_keylen));
		cmd.common.key_offset = 0;
		cmd.common.sta_id = IWM_STATION_ID;
		cmd.transmit_seq_cnt = htole64(k->wk_keytsc);
		err = iwm_send_cmd_pdu(sc, IWM_ADD_STA_KEY, IWM_CMD_ASYNC,
		    sizeof (cmd), &cmd);
	}
	mutex_exit(&sc->sc_mtx);
	if (err != 0) {
		k->wk_flags |= IEEE80211_KEY_SWCRYPT;
		return (1);
	}
	k->wk_flags &= ~IEEE80211_KEY_SWCRYPT;
	return (1);
}

/* illumos ic_crypto.cs_key_delete hook (OpenBSD ic_delete_key). */
static int
iwm_delete_key(struct ieee80211com *ic, const struct ieee80211_key *k)
{
	struct iwm_softc *sc = (struct iwm_softc *)ic;
	struct iwm_add_sta_key_cmd cmd;
	struct iwm_add_sta_key_cmd_v1 cmd_v1;
	int kid;

	kid = k - ic->ic_nw_keys;
	if (kid < 0 || kid >= IEEE80211_WEP_NKID)
		return (1);
	if ((k->wk_flags & IEEE80211_KEY_SWCRYPT) || k->wk_cipher == NULL ||
	    k->wk_cipher->ic_cipher != IEEE80211_CIPHER_AES_CCM)
		return (1);	/* software key */

	mutex_enter(&sc->sc_mtx);
	if ((sc->sc_flags & IWM_FLAG_STA_ACTIVE) == 0) {
		mutex_exit(&sc->sc_mtx);
		return (1);
	}
	if (!isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_TKIP_MIC_KEYS)) {
		memset(&cmd_v1, 0, sizeof (cmd_v1));
		cmd_v1.common.key_flags = htole16(IWM_STA_KEY_NOT_VALID |
		    IWM_STA_KEY_FLG_NO_ENC | IWM_STA_KEY_FLG_WEP_KEY_MAP |
		    ((kid << IWM_STA_KEY_FLG_KEYID_POS) &
		    IWM_STA_KEY_FLG_KEYID_MSK));
		memcpy(cmd_v1.common.key, k->wk_key,
		    MIN(sizeof (cmd_v1.common.key), k->wk_keylen));
		cmd_v1.common.key_offset = 0;
		cmd_v1.common.sta_id = IWM_STATION_ID;
		(void) iwm_send_cmd_pdu(sc, IWM_ADD_STA_KEY, IWM_CMD_ASYNC,
		    sizeof (cmd_v1), &cmd_v1);
	} else {
		memset(&cmd, 0, sizeof (cmd));
		cmd.common.key_flags = htole16(IWM_STA_KEY_NOT_VALID |
		    IWM_STA_KEY_FLG_NO_ENC | IWM_STA_KEY_FLG_WEP_KEY_MAP |
		    ((kid << IWM_STA_KEY_FLG_KEYID_POS) &
		    IWM_STA_KEY_FLG_KEYID_MSK));
		memcpy(cmd.common.key, k->wk_key,
		    MIN(sizeof (cmd.common.key), k->wk_keylen));
		cmd.common.key_offset = 0;
		cmd.common.sta_id = IWM_STATION_ID;
		(void) iwm_send_cmd_pdu(sc, IWM_ADD_STA_KEY, IWM_CMD_ASYNC,
		    sizeof (cmd), &cmd);
	}
	mutex_exit(&sc->sc_mtx);
	return (1);
}

static void
iwm_calib_timeout(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in;
	struct ieee80211_node *ni;

	mutex_enter(&sc->sc_mtx);
	if (sc->sc_calib_to == 0) {
		mutex_exit(&sc->sc_mtx);
		return;
	}
	in = (void *)ic->ic_bss;
	ni = &in->in_ni;
	if (ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE &&
	    (ni->in_flags & IEEE80211_NODE_HT) == 0 &&
	    ic->ic_opmode == IEEE80211_M_STA &&
	    ic->ic_state == IEEE80211_S_RUN) {
		int old_txrate = ni->in_txrate;
		iwm_amrr_choose(&sc->sc_amrr, &in->in_ni, &in->in_amn);
		/*
		 * If AMRR has chosen a new TX rate we must update
		 * the firmware's LQ rate table.
		 */
		if (ni->in_txrate != old_txrate)
			iwm_setrates(in, 1);
	}

	sc->sc_calib_to = timeout(iwm_calib_timeout, sc,
	    drv_usectohz(500000));
	mutex_exit(&sc->sc_mtx);
}

/* Cancel the calibration timeout.  sc_mtx held on entry and exit. */
static void
iwm_calib_stop(struct iwm_softc *sc)
{
	timeout_id_t tid = sc->sc_calib_to;

	sc->sc_calib_to = 0;
	if (tid != 0) {
		mutex_exit(&sc->sc_mtx);
		(void) untimeout(tid);
		mutex_enter(&sc->sc_mtx);
	}
}

/* Does the peer receive this HT MCS?  (OpenBSD isset(ni_rxmcs, mcs)) */
static int
iwm_peer_has_mcs(struct ieee80211_node *ni, int mcs)
{
	int i;

	for (i = 0; i < ni->in_htrates.rs_nrates; i++) {
		if ((ni->in_htrates.rs_rates[i] & IEEE80211_RATE_VAL) == mcs)
			return (1);
	}
	return (0);
}

static void
iwm_set_rate_table(struct iwm_node *in, struct iwm_lq_cmd *lqcmd)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211com *ic = ni->in_ic;
	struct iwm_softc *sc = (struct iwm_softc *)ic;
	struct ieee80211_rateset *rs = &ni->in_rates;
	struct iwm_ra_peer peer;
	int i, ridx, ridx_min, ridx_max, j, mimo, tab = 0;

	/*
	 * Fill the LQ rate selection table with legacy and/or HT rates
	 * in descending order, i.e. with the node's current TX rate first.
	 * In cases where throughput of an HT rate corresponds to a legacy
	 * rate it makes no sense to add both. We rely on the fact that
	 * iwm_rates is laid out such that equivalent HT/legacy rates share
	 * the same IWM_RATE_*_INDEX value. Also, rates not applicable to
	 * legacy/HT are assumed to be marked with an 'invalid' PLCP value.
	 */
	j = 0;
	ridx_min = iwm_rval2ridx(iwm_min_basic_rate(ic));
	if (ridx_min < 0 || ridx_min > (int)IWM_RIDX_MAX)
		ridx_min = 0;
	if (in->in_txmcs < 0 || in->in_txmcs > 15)
		in->in_txmcs = 0;
	mimo = iwm_is_mimo_ht_mcs(in->in_txmcs);
	ridx_max = (mimo ? IWM_RIDX_MAX : IWM_LAST_HT_SISO_RATE);
	iwm_ra_peer(sc, in, &peer);
	for (ridx = ridx_max; ridx >= ridx_min; ridx--) {
		uint8_t plcp = iwm_rates[ridx].plcp;
		uint8_t ht_plcp = iwm_rates[ridx].ht_plcp;

		if (j >= (int)nitems(lqcmd->rs_table))
			break;
		tab = 0;
		if (ni->in_flags & IEEE80211_NODE_HT) {
			if (ht_plcp == IWM_RATE_HT_SISO_MCS_INV_PLCP)
				continue;
			/* Do not mix SISO and MIMO HT rates. */
			if ((mimo && !iwm_is_mimo_ht_plcp(ht_plcp)) ||
			    (!mimo && iwm_is_mimo_ht_plcp(ht_plcp)))
				continue;
			for (i = in->in_txmcs; i >= 0; i--) {
				if (!iwm_peer_has_mcs(ni, i))
					continue;
				if (ridx != iwm_ht_mcs2ridx[i])
					continue;
				tab = ht_plcp;
				tab |= IWM_RATE_MCS_HT_MSK;
				/* First two Tx attempts may use 40MHz/SGI. */
				if (j > 1)
					break;
				if (in->in_phyctxt != NULL &&
				    (in->in_phyctxt->sco == IWM_HTOP0_SCO_SCA ||
				    in->in_phyctxt->sco == IWM_HTOP0_SCO_SCB)) {
					tab |= IWM_RATE_MCS_CHAN_WIDTH_40;
					tab |= IWM_RATE_MCS_RTS_REQUIRED_MSK;
				}
				if (peer.sgi)
					tab |= IWM_RATE_MCS_SGI_MSK;
				break;
			}
		} else if (plcp != IWM_RATE_INVM_PLCP) {
			for (i = MIN(ni->in_txrate, rs->ir_nrates - 1);
			    i >= 0; i--) {
				if (iwm_rates[ridx].rate == (rs->ir_rates[i] &
				    IEEE80211_RATE_VAL)) {
					tab = plcp;
					break;
				}
			}
		}

		if (tab == 0)
			continue;

		if (iwm_is_mimo_ht_plcp(ht_plcp))
			tab |= IWM_RATE_MCS_ANT_AB_MSK;
		else
			tab |= iwm_valid_siso_ant_rate_mask(sc);

		if (IWM_RIDX_IS_CCK(ridx))
			tab |= IWM_RATE_MCS_CCK_MSK;
		lqcmd->rs_table[j++] = htole32(tab);
	}

	lqcmd->mimo_delim = (mimo ? j : 0);

	/* Fill the rest with the lowest possible rate */
	while (j < (int)nitems(lqcmd->rs_table)) {
		tab = iwm_rates[ridx_min].plcp;
		if (IWM_RIDX_IS_CCK(ridx_min))
			tab |= IWM_RATE_MCS_CCK_MSK;
		tab |= iwm_valid_siso_ant_rate_mask(sc);
		lqcmd->rs_table[j++] = htole32(tab);
	}
}

static void
iwm_setrates(struct iwm_node *in, int async)
{
	struct ieee80211_node *ni = &in->in_ni;
	struct ieee80211com *ic = ni->in_ic;
	struct iwm_softc *sc = (struct iwm_softc *)ic;
	struct iwm_lq_cmd lqcmd;
	struct iwm_host_cmd cmd = {
		.id = IWM_LQ_CMD,
		.len = { sizeof (lqcmd), },
	};

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	cmd.flags = async ? IWM_CMD_ASYNC : 0;

	memset(&lqcmd, 0, sizeof (lqcmd));
	lqcmd.sta_id = IWM_STATION_ID;

	if (ic->ic_flags & IEEE80211_F_USEPROT)
		lqcmd.flags |= IWM_LQ_FLAG_USE_RTS_MSK;

	iwm_set_rate_table(in, &lqcmd);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_9000 &&
	    (iwm_fw_valid_tx_ant(sc) & IWM_ANT_B))
		lqcmd.single_stream_ant_msk = IWM_ANT_B;
	else
		lqcmd.single_stream_ant_msk = IWM_ANT_A;
	lqcmd.dual_stream_ant_msk = IWM_ANT_AB;

	lqcmd.agg_time_limit = htole16(4000);	/* 4ms */
	lqcmd.agg_disable_start_th = 3;
	lqcmd.agg_frame_cnt_limit = 0x3f;

	cmd.data[0] = &lqcmd;
	(void) iwm_send_cmd(sc, &cmd);
}

static void
iwm_newstate_task(void *psc)
{
	struct iwm_softc *sc = (struct iwm_softc *)psc;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state nstate, ostate;
	int arg, err = 0, call_net80211 = 1;

	/*
	 * Like OpenBSD, this task does not take the ioctl lock: iwm_stop()
	 * holds it while waiting for tasks to finish.  IWM_FLAG_SHUTDOWN
	 * and sc_generation detect a concurrent stop.
	 */
	mutex_enter(&sc->sc_mtx);
	/* The pending bit is cleared now so a new request requeues us. */
	sc->sc_task_pending &= ~IWM_TASK_NEWSTATE;
	nstate = sc->ns_nstate;
	ostate = ic->ic_state;
	arg = sc->ns_arg;

	if (sc->sc_flags & IWM_FLAG_SHUTDOWN) {
		/* iwm_stop() is waiting for us. */
		goto done;
	}
	/*
	 * Device down (RF-kill at m_start, or stopped after the request was
	 * queued): firmware commands would only time out.
	 */
	if ((sc->sc_flags & IWM_FLAG_RUNNING) == 0)
		goto done;

	if (ostate == IEEE80211_S_SCAN) {
		if (nstate == ostate) {
			if (sc->sc_flags & IWM_FLAG_SCANNING)
				goto done;
			/* Firmware is no longer scanning. Do another scan. */
			call_net80211 = 0;
			goto next_scan;
		} else {
			iwm_led_blink_stop(sc);
			/* Leaving SCAN early (disconnect, stop). */
			if (sc->sc_flags & IWM_FLAG_SCANNING)
				(void) iwm_scan_abort(sc);
		}
	}

	if (nstate <= ostate) {
		switch (ostate) {
		case IEEE80211_S_RUN:
			iwm_calib_stop(sc);
			err = iwm_run_stop(sc);
			if (err)
				goto out;
			/* FALLTHROUGH */
		case IEEE80211_S_ASSOC:
		case IEEE80211_S_AUTH:
			if (nstate <= IEEE80211_S_AUTH) {
				err = iwm_deauth(sc);
				if (err)
					goto out;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_SCAN:
		case IEEE80211_S_INIT:
			break;
		}

		/* Die now if iwm_stop() was called while we were sleeping. */
		if (sc->sc_flags & IWM_FLAG_SHUTDOWN)
			goto done;
	}

	switch (nstate) {
	case IEEE80211_S_INIT:
		break;

	case IEEE80211_S_SCAN:
next_scan:
		err = iwm_scan(sc);
		break;

	case IEEE80211_S_AUTH:
		err = iwm_auth(sc);
		break;

	case IEEE80211_S_ASSOC:
		break;

	case IEEE80211_S_RUN:
		err = iwm_run(sc);
		break;
	}

out:
	if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0) {
		if (err) {
			iwm_add_task(sc, sc->sc_initq, IWM_TASK_INIT);
		} else if (call_net80211) {
			/*
			 * net80211 sends the management frames for the new
			 * state and may call back into the driver, so drop
			 * the mutex (OpenBSD runs this at splnet).
			 */
			mutex_exit(&sc->sc_mtx);
			(void) sc->sc_newstate(ic, nstate, arg);
			if (nstate == IEEE80211_S_RUN) {
				mac_link_update(ic->ic_mach, LINK_STATE_UP);
				mac_tx_update(ic->ic_mach);
				ieee80211_start_watchdog(ic, 1);
			} else if (ostate == IEEE80211_S_RUN) {
				mac_link_update(ic->ic_mach, LINK_STATE_DOWN);
			}
			mutex_enter(&sc->sc_mtx);
		}
	}
done:
	sc->sc_task_refs--;
	cv_broadcast(&sc->sc_task_cv);
	mutex_exit(&sc->sc_mtx);
}

/*
 * illumos ic_newstate hook.  Firmware work may sleep, so the transition is
 * done by iwm_newstate_task() (exactly as in OpenBSD).
 */
static int
iwm_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct iwm_softc *sc = (struct iwm_softc *)ic;

	mutex_enter(&sc->sc_mtx);
	/*
	 * Prevent attempts to transition towards the same state, unless
	 * we are scanning in which case a SCAN -> SCAN transition
	 * triggers another scan iteration. And AUTH -> AUTH is needed
	 * to support band-steering.
	 */
	if ((sc->sc_task_pending & IWM_TASK_NEWSTATE) &&
	    sc->ns_nstate == nstate && nstate != IEEE80211_S_SCAN &&
	    nstate != IEEE80211_S_AUTH) {
		mutex_exit(&sc->sc_mtx);
		return (0);
	}

	if (ic->ic_state == IEEE80211_S_RUN && nstate != IEEE80211_S_RUN) {
		sc->ba_rx.start_tidmask = sc->ba_rx.stop_tidmask = 0;
		sc->ba_tx.start_tidmask = sc->ba_tx.stop_tidmask = 0;
	}

	sc->ns_nstate = nstate;
	sc->ns_arg = arg;

	iwm_add_task(sc, sc->sc_nswq, IWM_TASK_NEWSTATE);
	mutex_exit(&sc->sc_mtx);

	return (0);
}

static void
iwm_endscan(struct iwm_softc *sc)
{
	if ((sc->sc_flags & (IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN)) == 0)
		return;

	sc->sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
	/* ieee80211_end_scan() runs from iwm_defer_task(). */
	sc->sc_endscan_pending = 1;
	iwm_add_task(sc, sc->sc_nswq, IWM_TASK_DEFER);
}
/*
 * Aging and idle timeouts for the different possible scenarios
 * in default configuration
 */
static const uint32_t
iwm_sf_full_timeout_def[IWM_SF_NUM_SCENARIO][IWM_SF_NUM_TIMEOUT_TYPES] = {
	{
		htole32(IWM_SF_SINGLE_UNICAST_AGING_TIMER_DEF),
		htole32(IWM_SF_SINGLE_UNICAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWM_SF_AGG_UNICAST_AGING_TIMER_DEF),
		htole32(IWM_SF_AGG_UNICAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWM_SF_MCAST_AGING_TIMER_DEF),
		htole32(IWM_SF_MCAST_IDLE_TIMER_DEF)
	},
	{
		htole32(IWM_SF_BA_AGING_TIMER_DEF),
		htole32(IWM_SF_BA_IDLE_TIMER_DEF)
	},
	{
		htole32(IWM_SF_TX_RE_AGING_TIMER_DEF),
		htole32(IWM_SF_TX_RE_IDLE_TIMER_DEF)
	},
};

/*
 * Aging and idle timeouts for the different possible scenarios
 * in single BSS MAC configuration.
 */
static const uint32_t
iwm_sf_full_timeout[IWM_SF_NUM_SCENARIO][IWM_SF_NUM_TIMEOUT_TYPES] = {
	{
		htole32(IWM_SF_SINGLE_UNICAST_AGING_TIMER),
		htole32(IWM_SF_SINGLE_UNICAST_IDLE_TIMER)
	},
	{
		htole32(IWM_SF_AGG_UNICAST_AGING_TIMER),
		htole32(IWM_SF_AGG_UNICAST_IDLE_TIMER)
	},
	{
		htole32(IWM_SF_MCAST_AGING_TIMER),
		htole32(IWM_SF_MCAST_IDLE_TIMER)
	},
	{
		htole32(IWM_SF_BA_AGING_TIMER),
		htole32(IWM_SF_BA_IDLE_TIMER)
	},
	{
		htole32(IWM_SF_TX_RE_AGING_TIMER),
		htole32(IWM_SF_TX_RE_IDLE_TIMER)
	},
};

static void
iwm_fill_sf_command(struct iwm_softc *sc, struct iwm_sf_cfg_cmd *sf_cmd,
    struct ieee80211_node *ni)
{
	int i, j, watermark;

	sf_cmd->watermark[IWM_SF_LONG_DELAY_ON] = htole32(IWM_SF_W_MARK_SCAN);

	/*
	 * If we are in association flow - check antenna configuration
	 * capabilities of the AP station, and choose the watermark accordingly.
	 */
	_NOTE(ARGUNUSED(sc))
	if (ni) {
		if (ni->in_flags & IEEE80211_NODE_HT) {
			if (iwm_peer_has_mcs(ni, 8))
				watermark = IWM_SF_W_MARK_MIMO2;
			else
				watermark = IWM_SF_W_MARK_SISO;
		} else {
			watermark = IWM_SF_W_MARK_LEGACY;
		}
	/* default watermark value for unassociated mode. */
	} else {
		watermark = IWM_SF_W_MARK_MIMO2;
	}
	sf_cmd->watermark[IWM_SF_FULL_ON] = htole32(watermark);

	for (i = 0; i < IWM_SF_NUM_SCENARIO; i++) {
		for (j = 0; j < IWM_SF_NUM_TIMEOUT_TYPES; j++) {
			sf_cmd->long_delay_timeouts[i][j] =
					htole32(IWM_SF_LONG_DELAY_AGING_TIMER);
		}
	}

	if (ni) {
		memcpy(sf_cmd->full_on_timeouts, iwm_sf_full_timeout,
		    sizeof (iwm_sf_full_timeout));
	} else {
		memcpy(sf_cmd->full_on_timeouts, iwm_sf_full_timeout_def,
		    sizeof (iwm_sf_full_timeout_def));
	}

}

static int
iwm_sf_config(struct iwm_softc *sc, int new_state)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_sf_cfg_cmd sf_cmd = {
		.state = htole32(new_state),
	};
	int err = 0;

#if 0	/* only used for models with sdio interface, in iwlwifi */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_8000)
		sf_cmd.state |= htole32(IWM_SF_CFG_DUMMY_NOTIF_OFF);
#endif

	switch (new_state) {
	case IWM_SF_UNINIT:
	case IWM_SF_INIT_OFF:
		iwm_fill_sf_command(sc, &sf_cmd, NULL);
		break;
	case IWM_SF_FULL_ON:
		iwm_fill_sf_command(sc, &sf_cmd, ic->ic_bss);
		break;
	default:
		return (EINVAL);
	}

	err = iwm_send_cmd_pdu(sc, IWM_REPLY_SF_CFG_CMD, IWM_CMD_ASYNC,
	    sizeof (sf_cmd), &sf_cmd);
	return (err);
}

static int
iwm_send_bt_init_conf(struct iwm_softc *sc)
{
	struct iwm_bt_coex_cmd bt_cmd;

	bt_cmd.mode = htole32(IWM_BT_COEX_WIFI);
	bt_cmd.enabled_modules = htole32(IWM_BT_COEX_HIGH_BAND_RET);

	return (iwm_send_cmd_pdu(sc, IWM_BT_CONFIG, 0, sizeof (bt_cmd),
	    &bt_cmd));
}

static int
iwm_send_soc_conf(struct iwm_softc *sc)
{
	struct iwm_soc_configuration_cmd cmd;
	int err;
	uint32_t cmd_id, flags = 0;

	memset(&cmd, 0, sizeof (cmd));

	/*
	 * In VER_1 of this command, the discrete value is considered
	 * an integer; In VER_2, it's a bitmask.  Since we have only 2
	 * values in VER_1, this is backwards-compatible with VER_2,
	 * as long as we don't set any other flag bits.
	 */
	if (!sc->sc_integrated) { /* VER_1 */
		flags = IWM_SOC_CONFIG_CMD_FLAGS_DISCRETE;
	} else { /* VER_2 */
		uint8_t scan_cmd_ver;
		if (sc->sc_ltr_delay != IWM_SOC_FLAGS_LTR_APPLY_DELAY_NONE)
			flags |= (sc->sc_ltr_delay &
			    IWM_SOC_FLAGS_LTR_APPLY_DELAY_MASK);
		scan_cmd_ver = iwm_lookup_cmd_ver(sc, IWM_LONG_GROUP,
		    IWM_SCAN_REQ_UMAC);
		if (scan_cmd_ver != IWM_FW_CMD_VER_UNKNOWN &&
		    scan_cmd_ver >= 2 && sc->sc_low_latency_xtal)
			flags |= IWM_SOC_CONFIG_CMD_FLAGS_LOW_LATENCY;
	}
	cmd.flags = htole32(flags);

	cmd.latency = htole32(sc->sc_xtal_latency);

	cmd_id = iwm_cmd_id(IWM_SOC_CONFIGURATION_CMD, IWM_SYSTEM_GROUP, 0);
	err = iwm_send_cmd_pdu(sc, cmd_id, 0, sizeof (cmd), &cmd);
	if (err)
		dev_err(sc->sc_dip, CE_WARN, "!failed to set soc latency: %d",
		    err);
	return (err);
}

static int
iwm_send_update_mcc_cmd(struct iwm_softc *sc, const char *alpha2)
{
	struct iwm_mcc_update_cmd mcc_cmd;
	struct iwm_host_cmd hcmd = {
		.id = IWM_MCC_UPDATE_CMD,
		.flags = IWM_CMD_WANT_RESP,
		.resp_pkt_len = IWM_CMD_RESP_MAX,
		.data = { &mcc_cmd },
	};
	struct iwm_rx_packet *pkt;
	size_t resp_len;
	int err;
	int resp_v3 = isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_LAR_SUPPORT_V3);

	if (sc->sc_device_family == IWM_DEVICE_FAMILY_8000 &&
	    !sc->sc_nvm.lar_enabled) {
		return (0);
	}

	memset(&mcc_cmd, 0, sizeof (mcc_cmd));
	mcc_cmd.mcc = htole16(alpha2[0] << 8 | alpha2[1]);
	if (isset(sc->sc_ucode_api, IWM_UCODE_TLV_API_WIFI_MCC_UPDATE) ||
	    isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_LAR_MULTI_MCC))
		mcc_cmd.source_id = IWM_MCC_SOURCE_GET_CURRENT;
	else
		mcc_cmd.source_id = IWM_MCC_SOURCE_OLD_FW;

	if (resp_v3) { /* same size as resp_v2 */
		hcmd.len[0] = sizeof (struct iwm_mcc_update_cmd);
	} else {
		hcmd.len[0] = sizeof (struct iwm_mcc_update_cmd_v1);
	}

	err = iwm_send_cmd(sc, &hcmd);
	if (err)
		return (err);

	pkt = hcmd.resp_pkt;
	if (!pkt || (pkt->hdr.flags & IWM_CMD_FAILED_MSK)) {
		err = EIO;
		goto out;
	}

	if (resp_v3) {
		struct iwm_mcc_update_resp_v3 *resp;
		resp_len = iwm_rx_packet_payload_len(pkt);
		if (resp_len < sizeof (*resp)) {
			err = EIO;
			goto out;
		}

		resp = (void *)pkt->data;
		if (resp_len != sizeof (*resp) +
		    resp->n_channels * sizeof (resp->channels[0])) {
			err = EIO;
			goto out;
		}
	} else {
		struct iwm_mcc_update_resp_v1 *resp_v1;
		resp_len = iwm_rx_packet_payload_len(pkt);
		if (resp_len < sizeof (*resp_v1)) {
			err = EIO;
			goto out;
		}

		resp_v1 = (void *)pkt->data;
		if (resp_len != sizeof (*resp_v1) +
		    resp_v1->n_channels * sizeof (resp_v1->channels[0])) {
			err = EIO;
			goto out;
		}
	}
out:
	iwm_free_resp(sc, &hcmd);
	return (err);
}

static int
iwm_send_temp_report_ths_cmd(struct iwm_softc *sc)
{
	struct iwm_temp_report_ths_cmd cmd;
	int err;

	/*
	 * In order to give responsibility for critical-temperature-kill
	 * and TX backoff to FW we need to send an empty temperature
	 * reporting command at init time.
	 */
	memset(&cmd, 0, sizeof (cmd));

	err = iwm_send_cmd_pdu(sc,
	    IWM_WIDE_ID(IWM_PHY_OPS_GROUP, IWM_TEMP_REPORTING_THRESHOLDS_CMD),
	    0, sizeof (cmd), &cmd);
	if (err)
		dev_err(sc->sc_dip, CE_WARN, "!TEMP_REPORT_THS_CMD command "
		    "failed (error %d)", err);

	return (err);
}

static void
iwm_tt_tx_backoff(struct iwm_softc *sc, uint32_t backoff)
{
	struct iwm_host_cmd cmd = {
		.id = IWM_REPLY_THERMAL_MNG_BACKOFF,
		.len = { sizeof (uint32_t), },
		.data = { &backoff, },
	};

	(void) iwm_send_cmd(sc, &cmd);
}

static void
iwm_free_fw_paging(struct iwm_softc *sc)
{
	int i;

	if (sc->fw_paging_db[0].fw_paging_block.vaddr == NULL)
		return;

	for (i = 0; i < IWM_NUM_OF_FW_PAGING_BLOCKS; i++) {
		iwm_dma_contig_free(&sc->fw_paging_db[i].fw_paging_block);
	}

	memset(sc->fw_paging_db, 0, sizeof (sc->fw_paging_db));
}

static int
iwm_fill_paging_mem(struct iwm_softc *sc, const struct iwm_fw_sects *image)
{
	int sec_idx, idx;
	uint32_t offset = 0;

	/*
	 * find where is the paging image start point:
	 * if CPU2 exist and it's in paging format, then the image looks like:
	 * CPU1 sections (2 or more)
	 * CPU1_CPU2_SEPARATOR_SECTION delimiter - separate between CPU1 to CPU2
	 * CPU2 sections (not paged)
	 * PAGING_SEPARATOR_SECTION delimiter - separate between CPU2
	 * non paged to CPU2 paging sec
	 * CPU2 paging CSS
	 * CPU2 paging image (including instruction and data)
	 */
	for (sec_idx = 0; sec_idx < IWM_UCODE_SECT_MAX; sec_idx++) {
		if (image->fw_sect[sec_idx].fws_devoff ==
		    IWM_PAGING_SEPARATOR_SECTION) {
			sec_idx++;
			break;
		}
	}

	/*
	 * If paging is enabled there should be at least 2 more sections left
	 * (one for CSS and one for Paging data)
	 */
	if (sec_idx >= (int)nitems(image->fw_sect) - 1) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!Paging: Missing CSS and/or paging sections");
		iwm_free_fw_paging(sc);
		return (EINVAL);
	}

	/* copy the CSS block to the dram */

	memcpy(sc->fw_paging_db[0].fw_paging_block.vaddr,
	    image->fw_sect[sec_idx].fws_data,
	    sc->fw_paging_db[0].fw_paging_size);


	sec_idx++;

	/*
	 * copy the paging blocks to the dram
	 * loop index start from 1 since that CSS block already copied to dram
	 * and CSS index is 0.
	 * loop stop at num_of_paging_blk since that last block is not full.
	 */
	for (idx = 1; idx < sc->num_of_paging_blk; idx++) {
		memcpy(sc->fw_paging_db[idx].fw_paging_block.vaddr,
		    (const char *)image->fw_sect[sec_idx].fws_data + offset,
		    sc->fw_paging_db[idx].fw_paging_size);


		offset += sc->fw_paging_db[idx].fw_paging_size;
	}

	/* copy the last paging block */
	if (sc->num_of_pages_in_last_blk > 0) {
		memcpy(sc->fw_paging_db[idx].fw_paging_block.vaddr,
		    (const char *)image->fw_sect[sec_idx].fws_data + offset,
		    IWM_FW_PAGING_SIZE * sc->num_of_pages_in_last_blk);

	}

	return (0);
}

static int
iwm_alloc_fw_paging_mem(struct iwm_softc *sc, const struct iwm_fw_sects *image)
{
	int blk_idx = 0;
	int error, num_of_pages;

	if (sc->fw_paging_db[0].fw_paging_block.vaddr != NULL) {
		/* Device got reset, and we setup firmware paging again */
		return (0);
	}

	/* ensure IWM_BLOCK_2_EXP_SIZE is power of 2 of IWM_PAGING_BLOCK_SIZE */
#if (1 << IWM_BLOCK_2_EXP_SIZE) != IWM_PAGING_BLOCK_SIZE
#error IWM_BLOCK_2_EXP_SIZE must be power of 2 of IWM_PAGING_BLOCK_SIZE
#endif

	num_of_pages = image->paging_mem_size / IWM_FW_PAGING_SIZE;
	sc->num_of_paging_blk =
	    ((num_of_pages - 1) / IWM_NUM_OF_PAGE_PER_GROUP) + 1;

	sc->num_of_pages_in_last_blk =
		num_of_pages -
		IWM_NUM_OF_PAGE_PER_GROUP * (sc->num_of_paging_blk - 1);


	/* allocate block of 4Kbytes for paging CSS */
	error = iwm_dma_contig_alloc(sc,
	    &sc->fw_paging_db[blk_idx].fw_paging_block, IWM_FW_PAGING_SIZE,
	    4096);
	if (error) {
		/* free all the previous pages since we failed */
		iwm_free_fw_paging(sc);
		return (ENOMEM);
	}

	sc->fw_paging_db[blk_idx].fw_paging_size = IWM_FW_PAGING_SIZE;


	/*
	 * allocate blocks in dram.
	 * since that CSS allocated in fw_paging_db[0] loop start from index 1
	 */
	for (blk_idx = 1; blk_idx < sc->num_of_paging_blk + 1; blk_idx++) {
		/* allocate block of IWM_PAGING_BLOCK_SIZE (32K) */
		/* XXX Use iwm_dma_contig_alloc for allocating */
		error = iwm_dma_contig_alloc(sc,
		    &sc->fw_paging_db[blk_idx].fw_paging_block,
		    IWM_PAGING_BLOCK_SIZE, 4096);
		if (error) {
			/* free all the previous pages since we failed */
			iwm_free_fw_paging(sc);
			return (ENOMEM);
		}

		sc->fw_paging_db[blk_idx].fw_paging_size =
		    IWM_PAGING_BLOCK_SIZE;

	}

	return (0);
}

static int
iwm_save_fw_paging(struct iwm_softc *sc, const struct iwm_fw_sects *fw)
{
	int ret;

	ret = iwm_alloc_fw_paging_mem(sc, fw);
	if (ret)
		return (ret);

	return (iwm_fill_paging_mem(sc, fw));
}

/* send paging cmd to FW in case CPU2 has paging image */
static int
iwm_send_paging_cmd(struct iwm_softc *sc, const struct iwm_fw_sects *fw)
{
	int blk_idx;
	uint32_t dev_phy_addr;
	struct iwm_fw_paging_cmd fw_paging_cmd = {
		.flags =
			htole32(IWM_PAGING_CMD_IS_SECURED |
				IWM_PAGING_CMD_IS_ENABLED |
				(sc->num_of_pages_in_last_blk <<
				IWM_PAGING_CMD_NUM_OF_PAGES_IN_LAST_GRP_POS)),
		.block_size = htole32(IWM_BLOCK_2_EXP_SIZE),
		.block_num = htole32(sc->num_of_paging_blk),
	};

	/* loop for all paging blocks + CSS block */
	for (blk_idx = 0; blk_idx < sc->num_of_paging_blk + 1; blk_idx++) {
		dev_phy_addr = htole32(
		    sc->fw_paging_db[blk_idx].fw_paging_block.paddr >>
		    IWM_PAGE_2_EXP_SIZE);
		fw_paging_cmd.device_phy_addr[blk_idx] = dev_phy_addr;
		iwm_dma_sync(&sc->fw_paging_db[blk_idx].fw_paging_block,
		    DDI_DMA_SYNC_FORDEV);
	}

	return (iwm_send_cmd_pdu(sc, iwm_cmd_id(IWM_FW_PAGING_BLOCK_CMD,
	    IWM_LONG_GROUP, 0), 0, sizeof (fw_paging_cmd), &fw_paging_cmd));
}


/*
 * Bring the device up with the regular firmware (OpenBSD iwm_init_hw).
 * Called with sc_mtx held; firmware loads sleep on sc_fw_cv.
 */
static int
iwm_init_hw(struct iwm_softc *sc)
{
	int err, i, ac, qid;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	err = iwm_run_init_mvm_ucode(sc, 0);
	if (err)
		return (err);

	/* Should stop and start HW since INIT image just loaded. */
	iwm_stop_device(sc);
	err = iwm_start_hw(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not initialize hardware");
		return (err);
	}

	/* Restart, this time with the regular firmware */
	err = iwm_load_ucode_wait_alive(sc, IWM_UCODE_TYPE_REGULAR);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not load firmware");
		return (err);
	}

	if (!iwm_nic_lock(sc))
		return (EBUSY);

	err = iwm_send_tx_ant_cfg(sc, iwm_fw_valid_tx_ant(sc));
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not init tx ant config (error %d)", err);
		goto err;
	}

	err = iwm_send_phy_db_data(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not init phy db (error %d)", err);
		goto err;
	}

	err = iwm_send_phy_cfg_cmd(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not send phy config (error %d)", err);
		goto err;
	}

	err = iwm_send_bt_init_conf(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not init bt coex (error %d)", err);
		goto err;
	}

	if (isset(sc->sc_enabled_capa,
	    IWM_UCODE_TLV_CAPA_SOC_LATENCY_SUPPORT)) {
		err = iwm_send_soc_conf(sc);
		if (err)
			goto err;
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT)) {
		err = iwm_send_dqa_cmd(sc);
		if (err)
			goto err;
	}

	/* Add auxiliary station for scanning */
	err = iwm_add_aux_sta(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not add aux station (error %d)", err);
		goto err;
	}

	for (i = 0; i < IWM_NUM_PHY_CTX; i++) {
		/*
		 * The channel used here isn't relevant as it's
		 * going to be overwritten in the other flows.
		 * For now use the first channel we have.
		 */
		sc->sc_phyctxt[i].id = i;
		sc->sc_phyctxt[i].channel = iwm_default_chan(sc);
		err = iwm_phy_ctxt_cmd(sc, &sc->sc_phyctxt[i], 1, 1,
		    IWM_FW_CTXT_ACTION_ADD, 0, IWM_HTOP0_SCO_SCN, 0);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!could not add phy context %d (error %d)", i, err);
			goto err;
		}
	}

	/* Initialize tx backoffs to the minimum. */
	if (sc->sc_device_family == IWM_DEVICE_FAMILY_7000)
		iwm_tt_tx_backoff(sc, 0);

	err = iwm_config_ltr(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!PCIe LTR configuration failed (error %d)", err);
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_CT_KILL_BY_FW)) {
		err = iwm_send_temp_report_ths_cmd(sc);
		if (err)
			goto err;
	}

	err = iwm_power_update_device(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not send power command (error %d)", err);
		goto err;
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_LAR_SUPPORT)) {
		err = iwm_send_update_mcc_cmd(sc, "ZZ");
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!could not init LAR (error %d)", err);
			goto err;
		}
	}

	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_UMAC_SCAN)) {
		err = iwm_config_umac_scan(sc);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!could not configure scan (error %d)", err);
			goto err;
		}
	}

	/* Client mode only: one Tx queue per EDCA access category. */
	for (ac = 0; ac < EDCA_NUM_AC; ac++) {
		if (isset(sc->sc_enabled_capa,
		    IWM_UCODE_TLV_CAPA_DQA_SUPPORT))
			qid = ac + IWM_DQA_MIN_MGMT_QUEUE;
		else
			qid = ac;
		err = iwm_enable_txq(sc, IWM_STATION_ID, qid,
		    iwm_ac_to_tx_fifo[ac], 0, IWM_TID_NON_QOS, 0);
		if (err) {
			dev_err(sc->sc_dip, CE_WARN,
			    "!could not enable Tx queue %d (error %d)",
			    ac, err);
			goto err;
		}
	}

	err = iwm_disable_beacon_filter(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!could not disable beacon filter (error %d)", err);
		goto err;
	}

err:
	iwm_nic_unlock(sc);
	return (err);
}

/* Allow multicast from our BSSID. */
static int
iwm_allow_mcast(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in = (void *)ic->ic_bss;
	struct iwm_mcast_filter_cmd *cmd;
	size_t size;
	int err;

	size = roundup(sizeof (*cmd), 4);
	cmd = kmem_zalloc(size, KM_NOSLEEP);
	if (cmd == NULL)
		return (ENOMEM);
	cmd->filter_own = 1;
	cmd->port_id = 0;
	cmd->count = 0;
	cmd->pass_all = 1;
	IEEE80211_ADDR_COPY(cmd->bssid, in->in_macaddr);

	err = iwm_send_cmd_pdu(sc, IWM_MCAST_FILTER_CMD,
	    0, size, cmd);
	kmem_free(cmd, size);
	return (err);
}

/*
 * OpenBSD iwm_init().  Caller holds sc_rwlock as writer.  Unlike OpenBSD
 * this does not start a scan: on illumos, scans and connections are
 * requested through net80211 ioctls (see iwm_m_start()/iwm_m_setprop()).
 */
static int
iwm_init(struct iwm_softc *sc)
{
	int err, generation;

	ASSERT(RW_WRITE_HELD(&sc->sc_rwlock));
	mutex_enter(&sc->sc_mtx);
	generation = ++sc->sc_generation;

	err = iwm_preinit(sc);
	if (err)
		goto out;

	err = iwm_start_hw(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not initialize hardware");
		goto out;
	}

	err = iwm_init_hw(sc);
	if (err) {
		if (generation == sc->sc_generation)
			iwm_stop_device(sc);
		goto out;
	}

	ASSERT(sc->sc_task_refs == 0);
	sc->sc_flags &= ~IWM_FLAG_OACTIVE;
	sc->sc_flags |= IWM_FLAG_RUNNING;
out:
	mutex_exit(&sc->sc_mtx);
	return (err);
}

/*
 * OpenBSD iwm_stop().  Caller holds sc_rwlock as writer, not sc_mtx.
 */
static void
iwm_stop(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwm_node *in;
	mblk_t *rx;
	int i, oactive;

	ASSERT(RW_WRITE_HELD(&sc->sc_rwlock));
	/* The net80211 watchdog may call back into iwm_newstate(). */
	ieee80211_stop_watchdog(ic);

	mutex_enter(&sc->sc_mtx);
	sc->sc_flags |= IWM_FLAG_SHUTDOWN; /* Disallow new tasks. */

	/* Let any stale tasks finish up (OpenBSD refcnt_finalize). */
	while (sc->sc_task_refs > 0)
		cv_wait(&sc->sc_task_cv, &sc->sc_mtx);

	iwm_calib_stop(sc);
	iwm_led_blink_stop(sc);

	iwm_stop_device(sc);

	/* Reset soft state. */

	sc->sc_generation++;
	/* Wake anybody waiting for the firmware; they see the generation. */
	cv_broadcast(&sc->sc_cmd_cv);
	cv_broadcast(&sc->sc_fw_cv);
	cv_broadcast(&sc->sc_txq_cv);
	for (i = 0; i < nitems(sc->sc_cmd_resp_pkt); i++) {
		if (sc->sc_cmd_resp_pkt[i] != NULL)
			kmem_free(sc->sc_cmd_resp_pkt[i],
			    sc->sc_cmd_resp_len[i]);
		sc->sc_cmd_resp_pkt[i] = NULL;
		sc->sc_cmd_resp_len[i] = 0;
	}
	oactive = (sc->sc_flags & IWM_FLAG_OACTIVE) != 0;
	sc->sc_flags &= ~(IWM_FLAG_RUNNING | IWM_FLAG_OACTIVE);

	in = (void *)ic->ic_bss;
	if (in != NULL) {
		in->in_phyctxt = NULL;
		in->tid_disable_ampdu = 0xffff;
		in->tfd_queue_msk = 0;
		IEEE80211_ADDR_COPY(in->in_macaddr, iwm_etheranyaddr);
	}

	sc->sc_flags &= ~(IWM_FLAG_SCANNING | IWM_FLAG_BGSCAN);
	sc->sc_flags &= ~IWM_FLAG_MAC_ACTIVE;
	sc->sc_flags &= ~IWM_FLAG_BINDING_ACTIVE;
	sc->sc_flags &= ~IWM_FLAG_STA_ACTIVE;
	sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;
	sc->sc_flags &= ~IWM_FLAG_HW_ERR;
	sc->sc_flags &= ~IWM_FLAG_TXFLUSH;

	sc->sc_rx_ba_sessions = 0;
	sc->sc_rx_ba_tidmask = 0;
	sc->ba_rx.start_tidmask = 0;
	sc->ba_rx.stop_tidmask = 0;
	sc->tx_ba_queue_mask = 0;
	sc->ba_tx.start_tidmask = 0;
	sc->ba_tx.stop_tidmask = 0;

	sc->sc_endscan_pending = 0;
	sc->sc_bmiss_pending = 0;
	sc->sc_bar_pending = 0;
	sc->sc_tx_update = 0;
	rx = sc->sc_rx_head;
	sc->sc_rx_head = sc->sc_rx_tail = NULL;

	memset(sc->sc_tx_timer, 0, sizeof (sc->sc_tx_timer));
	mutex_exit(&sc->sc_mtx);

	while (rx != NULL) {
		mblk_t *next = rx->b_next;

		rx->b_next = NULL;
		freemsg(rx);
		rx = next;
	}

	/*
	 * net80211's own INIT transition (OpenBSD calls sc_newstate the same
	 * way).  It may transmit a deauth frame, which iwm_send() drops now
	 * that the device is down.
	 */
	(void) sc->sc_newstate(ic, IEEE80211_S_INIT, -1);
	mac_link_update(ic->ic_mach, LINK_STATE_DOWN);
	/* Unblock a flow-controlled client; m_tx now drops its frames. */
	if (oactive)
		mac_tx_update(ic->ic_mach);

	mutex_enter(&sc->sc_mtx);
	sc->ns_nstate = IEEE80211_S_INIT;
	sc->sc_flags &= ~IWM_FLAG_SHUTDOWN;
	mutex_exit(&sc->sc_mtx);
}

/*
 * Per-queue Tx watchdog, run once per second from ddi_periodic
 * (OpenBSD iwm_watchdog via if_timer).  Tx aggregation queues can get
 * "stuck" while other queues keep working; the Linux driver uses a
 * similar workaround.
 */
static void
iwm_watchdog(void *arg)
{
	struct iwm_softc *sc = arg;
	int i;

	mutex_enter(&sc->sc_mtx);
	if ((sc->sc_flags & IWM_FLAG_RUNNING) == 0 ||
	    (sc->sc_flags & (IWM_FLAG_SHUTDOWN | IWM_FLAG_SUSPENDED))) {
		mutex_exit(&sc->sc_mtx);
		return;
	}
	for (i = 0; i < nitems(sc->sc_tx_timer); i++) {
		if (sc->sc_tx_timer[i] > 0 && --sc->sc_tx_timer[i] == 0) {
			dev_err(sc->sc_dip, CE_WARN, "!device timeout "
			    "(Tx queue %d)", i);
			iwm_nic_error(sc);
			iwm_dump_driver_status(sc);
			iwm_add_task(sc, sc->sc_initq, IWM_TASK_INIT);
			sc->sc_oerrors++;
			break;
		}
	}
	mutex_exit(&sc->sc_mtx);
}

/*
 * net80211 ic_watchdog: drives its management-frame (auth/assoc) and node
 * inactivity timers.
 */
static void
iwm_ic_watchdog(void *arg)
{
	struct ieee80211com *ic = arg;

	ic->ic_watchdog_timer = 0;	/* this timeout has fired */
	ieee80211_watchdog(ic);
}

/*
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with uint32_t-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwm_error_event_table {
	uint32_t valid;		/* (nonzero) valid, (0) log is empty */
	uint32_t error_id;		/* type of error */
	uint32_t trm_hw_status0;	/* TRM HW status */
	uint32_t trm_hw_status1;	/* TRM HW status */
	uint32_t blink2;		/* branch link */
	uint32_t ilink1;		/* interrupt link */
	uint32_t ilink2;		/* interrupt link */
	uint32_t data1;		/* error-specific data */
	uint32_t data2;		/* error-specific data */
	uint32_t data3;		/* error-specific data */
	uint32_t bcon_time;		/* beacon timer */
	uint32_t tsf_low;		/* network timestamp function timer */
	uint32_t tsf_hi;		/* network timestamp function timer */
	uint32_t gp1;		/* GP1 timer register */
	uint32_t gp2;		/* GP2 timer register */
	uint32_t fw_rev_type;	/* firmware revision type */
	uint32_t major;		/* uCode version major */
	uint32_t minor;		/* uCode version minor */
	uint32_t hw_ver;		/* HW Silicon version */
	uint32_t brd_ver;		/* HW board version */
	uint32_t log_pc;		/* log program counter */
	uint32_t frame_ptr;		/* frame pointer */
	uint32_t stack_ptr;		/* stack pointer */
	uint32_t hcmd;		/* last host command header */
	uint32_t isr0;		/* LMPM_NIC_ISR0: rxtx_flag */
	uint32_t isr1;		/* LMPM_NIC_ISR1: host_flag */
	uint32_t isr2;		/* LMPM_NIC_ISR2: enc_flag */
	uint32_t isr3;		/* LMPM_NIC_ISR3: time_flag */
	uint32_t isr4;		/* LMPM_NIC_ISR4: wico interrupt */
	uint32_t last_cmd_id;	/* last HCMD id handled by the firmware */
	uint32_t wait_event;		/* wait event() caller address */
	uint32_t l2p_control;	/* L2pControlField */
	uint32_t l2p_duration;	/* L2pDurationField */
	uint32_t l2p_mhvalid;	/* L2pMhValidBits */
	uint32_t l2p_addr_match;	/* L2pAddrMatchStat */
	uint32_t lmpm_pmg_sel;	/* which clocks are on (LMPM_PMG_SEL) */
	uint32_t u_timestamp;	/* date and time of the compilation */
	uint32_t flow_handler;	/* FH read/write pointers, RX credit */
} __packed /* LOG_ERROR_TABLE_API_S_VER_3 */;

/*
 * UMAC error struct - relevant starting from family 8000 chip.
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with u32-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwm_umac_error_event_table {
	uint32_t valid;		/* (nonzero) valid, (0) log is empty */
	uint32_t error_id;	/* type of error */
	uint32_t blink1;	/* branch link */
	uint32_t blink2;	/* branch link */
	uint32_t ilink1;	/* interrupt link */
	uint32_t ilink2;	/* interrupt link */
	uint32_t data1;		/* error-specific data */
	uint32_t data2;		/* error-specific data */
	uint32_t data3;		/* error-specific data */
	uint32_t umac_major;
	uint32_t umac_minor;
	uint32_t frame_pointer;	/* core register 27 */
	uint32_t stack_pointer;	/* core register 28 */
	uint32_t cmd_header;	/* latest host cmd sent to UMAC */
	uint32_t nic_isr_pref;	/* ISR status register */
} __packed;

#define	ERROR_START_OFFSET	(1 * sizeof (uint32_t))
#define	ERROR_ELEM_SIZE		(7 * sizeof (uint32_t))

#define	IWM_ERRLOG(sc, fmt, val)					\
	dev_err((sc)->sc_dip, CE_NOTE, "!%08X | " fmt, (val))

static void
iwm_nic_umac_error(struct iwm_softc *sc)
{
	struct iwm_umac_error_event_table table;
	uint32_t base;

	base = sc->sc_uc.uc_umac_error_event_table;

	if (base < 0x800000) {
		dev_err(sc->sc_dip, CE_NOTE,
		    "!Invalid error log pointer 0x%08x", base);
		return;
	}

	if (iwm_read_mem(sc, base, &table, sizeof (table)/sizeof (uint32_t))) {
		dev_err(sc->sc_dip, CE_NOTE, "!reading errlog failed");
		return;
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		dev_err(sc->sc_dip, CE_NOTE, "!Start UMAC Error Log Dump:");
		dev_err(sc->sc_dip, CE_NOTE, "!Status: 0x%x, count: %d",
		    sc->sc_flags, table.valid);
	}

	dev_err(sc->sc_dip, CE_NOTE, "!0x%08X | %s", table.error_id,
	    iwm_desc_lookup(table.error_id));
	IWM_ERRLOG(sc, "umac branchlink1", table.blink1);
	IWM_ERRLOG(sc, "umac branchlink2", table.blink2);
	IWM_ERRLOG(sc, "umac interruptlink1", table.ilink1);
	IWM_ERRLOG(sc, "umac interruptlink2", table.ilink2);
	IWM_ERRLOG(sc, "umac data1", table.data1);
	IWM_ERRLOG(sc, "umac data2", table.data2);
	IWM_ERRLOG(sc, "umac data3", table.data3);
	IWM_ERRLOG(sc, "umac major", table.umac_major);
	IWM_ERRLOG(sc, "umac minor", table.umac_minor);
	IWM_ERRLOG(sc, "frame pointer", table.frame_pointer);
	IWM_ERRLOG(sc, "stack pointer", table.stack_pointer);
	IWM_ERRLOG(sc, "last host cmd", table.cmd_header);
	IWM_ERRLOG(sc, "isr status reg", table.nic_isr_pref);
}

#define	IWM_FW_SYSASSERT_CPU_MASK 0xf0000000
static const struct {
	const char *name;
	uint8_t num;
} advanced_lookup[] = {
	{ "NMI_INTERRUPT_WDG", 0x34 },
	{ "SYSASSERT", 0x35 },
	{ "UCODE_VERSION_MISMATCH", 0x37 },
	{ "BAD_COMMAND", 0x38 },
	{ "BAD_COMMAND", 0x39 },
	{ "NMI_INTERRUPT_DATA_ACTION_PT", 0x3C },
	{ "FATAL_ERROR", 0x3D },
	{ "NMI_TRM_HW_ERR", 0x46 },
	{ "NMI_INTERRUPT_TRM", 0x4C },
	{ "NMI_INTERRUPT_BREAK_POINT", 0x54 },
	{ "NMI_INTERRUPT_WDG_RXF_FULL", 0x5C },
	{ "NMI_INTERRUPT_WDG_NO_RBD_RXF_FULL", 0x64 },
	{ "NMI_INTERRUPT_HOST", 0x66 },
	{ "NMI_INTERRUPT_LMAC_FATAL", 0x70 },
	{ "NMI_INTERRUPT_UMAC_FATAL", 0x71 },
	{ "NMI_INTERRUPT_OTHER_LMAC_FATAL", 0x73 },
	{ "NMI_INTERRUPT_ACTION_PT", 0x7C },
	{ "NMI_INTERRUPT_UNKNOWN", 0x84 },
	{ "NMI_INTERRUPT_INST_ACTION_PT", 0x86 },
	{ "ADVANCED_SYSASSERT", 0 },
};

static const char *
iwm_desc_lookup(uint32_t num)
{
	int i;

	for (i = 0; i < nitems(advanced_lookup) - 1; i++)
		if (advanced_lookup[i].num ==
		    (num & ~IWM_FW_SYSASSERT_CPU_MASK))
			return (advanced_lookup[i].name);

	/* No entry matches 'num', so it is the last: ADVANCED_SYSASSERT */
	return (advanced_lookup[i].name);
}

/*
 * Dump the firmware error log to the system log (not the console).  Most
 * of it is only useful to Intel, but the error id names the failure.
 */
static void
iwm_nic_error(struct iwm_softc *sc)
{
	struct iwm_error_event_table table;
	uint32_t base;

	dev_err(sc->sc_dip, CE_NOTE, "!dumping device error log");
	base = sc->sc_uc.uc_error_event_table;
	if (base < 0x800000) {
		dev_err(sc->sc_dip, CE_NOTE,
		    "!Invalid error log pointer 0x%08x", base);
		return;
	}

	if (iwm_read_mem(sc, base, &table, sizeof (table)/sizeof (uint32_t))) {
		dev_err(sc->sc_dip, CE_NOTE, "!reading errlog failed");
		return;
	}

	if (!table.valid) {
		dev_err(sc->sc_dip, CE_NOTE, "!errlog not found, skipping");
		return;
	}

	if (ERROR_START_OFFSET <= table.valid * ERROR_ELEM_SIZE) {
		dev_err(sc->sc_dip, CE_NOTE, "!Start Error Log Dump:");
		dev_err(sc->sc_dip, CE_NOTE, "!Status: 0x%x, count: %d",
		    sc->sc_flags, table.valid);
	}

	dev_err(sc->sc_dip, CE_NOTE, "!0x%08X | %-28s", table.error_id,
	    iwm_desc_lookup(table.error_id));
	IWM_ERRLOG(sc, "trm_hw_status0", table.trm_hw_status0);
	IWM_ERRLOG(sc, "trm_hw_status1", table.trm_hw_status1);
	IWM_ERRLOG(sc, "branchlink2", table.blink2);
	IWM_ERRLOG(sc, "interruptlink1", table.ilink1);
	IWM_ERRLOG(sc, "interruptlink2", table.ilink2);
	IWM_ERRLOG(sc, "data1", table.data1);
	IWM_ERRLOG(sc, "data2", table.data2);
	IWM_ERRLOG(sc, "data3", table.data3);
	IWM_ERRLOG(sc, "beacon time", table.bcon_time);
	IWM_ERRLOG(sc, "tsf low", table.tsf_low);
	IWM_ERRLOG(sc, "tsf hi", table.tsf_hi);
	IWM_ERRLOG(sc, "time gp1", table.gp1);
	IWM_ERRLOG(sc, "time gp2", table.gp2);
	IWM_ERRLOG(sc, "uCode revision type", table.fw_rev_type);
	IWM_ERRLOG(sc, "uCode version major", table.major);
	IWM_ERRLOG(sc, "uCode version minor", table.minor);
	IWM_ERRLOG(sc, "hw version", table.hw_ver);
	IWM_ERRLOG(sc, "board version", table.brd_ver);
	IWM_ERRLOG(sc, "hcmd", table.hcmd);
	IWM_ERRLOG(sc, "isr0", table.isr0);
	IWM_ERRLOG(sc, "isr1", table.isr1);
	IWM_ERRLOG(sc, "isr2", table.isr2);
	IWM_ERRLOG(sc, "isr3", table.isr3);
	IWM_ERRLOG(sc, "isr4", table.isr4);
	IWM_ERRLOG(sc, "last cmd Id", table.last_cmd_id);
	IWM_ERRLOG(sc, "wait_event", table.wait_event);
	IWM_ERRLOG(sc, "l2p_control", table.l2p_control);
	IWM_ERRLOG(sc, "l2p_duration", table.l2p_duration);
	IWM_ERRLOG(sc, "l2p_mhvalid", table.l2p_mhvalid);
	IWM_ERRLOG(sc, "l2p_addr_match", table.l2p_addr_match);
	IWM_ERRLOG(sc, "lmpm_pmg_sel", table.lmpm_pmg_sel);
	IWM_ERRLOG(sc, "timestamp", table.u_timestamp);
	IWM_ERRLOG(sc, "flow_handler", table.flow_handler);

	if (sc->sc_uc.uc_umac_error_event_table)
		iwm_nic_umac_error(sc);
}

static void
iwm_dump_driver_status(struct iwm_softc *sc)
{
	int i;

	dev_err(sc->sc_dip, CE_NOTE, "!driver status:");
	for (i = 0; i < IWM_MAX_QUEUES; i++) {
		struct iwm_tx_ring *ring = &sc->txq[i];
		if (ring->queued == 0)
			continue;
		dev_err(sc->sc_dip, CE_NOTE, "!  tx ring %2d: qid=%-2d "
		    "cur=%-3d queued=%-3d", i, ring->qid, ring->cur,
		    ring->queued);
	}
	dev_err(sc->sc_dip, CE_NOTE, "!  rx ring: cur=%d", sc->rxq.cur);
	dev_err(sc->sc_dip, CE_NOTE, "!  802.11 state %s",
	    ieee80211_state_name[sc->sc_ic.ic_state]);
}

static int
iwm_rx_pkt_valid(struct iwm_rx_packet *pkt)
{
	int qid, idx, code;

	qid = pkt->hdr.qid & ~0x80;
	idx = pkt->hdr.idx;
	code = IWM_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);

	return (!(qid == 0 && idx == 0 && code == 0) &&
	    pkt->len_n_flags != htole32(IWM_FH_RSCSR_FRAME_INVALID));
}

/*
 * Notification payload of at least sizeof (*(_var)) bytes, else skip it
 * (OpenBSD's SYNC_RESP_STRUCT; it trusts the firmware's length).
 */
#define	IWM_RESP_STRUCT(_var, _pkt)					\
	(iwm_rx_packet_payload_len(_pkt) >= sizeof (*(_var)) ?		\
	    ((_var) = (void *)(_pkt)->data) != NULL : 0)

/*
 * Process one receive buffer, which may hold several packets
 * (OpenBSD iwm_rx_pkt).  Frames are copied out, so the buffer stays on
 * the ring.  Called with sc_mtx held from the interrupt handler.
 */
static void
iwm_rx_pkt(struct iwm_softc *sc, struct iwm_rx_data *data)
{
	struct iwm_rx_packet *pkt;
	uint32_t offset = 0, len;
	uint8_t *buf;
	const size_t minsz = sizeof (pkt->len_n_flags) + sizeof (pkt->hdr);
	int qid, idx, code, handled;

	iwm_dma_sync(&data->dma, DDI_DMA_SYNC_FORKERNEL);
	buf = data->dma.vaddr;

	while (offset + minsz < IWM_RBUF_SIZE) {
		pkt = (struct iwm_rx_packet *)(void *)(buf + offset);
		qid = pkt->hdr.qid;
		idx = pkt->hdr.idx;

		code = IWM_WIDE_ID(pkt->hdr.flags, pkt->hdr.code);

		if (!iwm_rx_pkt_valid(pkt))
			break;

		len = sizeof (pkt->len_n_flags) + iwm_rx_packet_len(pkt);
		if (len < minsz || len > (IWM_RBUF_SIZE - offset))
			break;

		handled = 1;
		switch (code) {
		case IWM_REPLY_RX_PHY_CMD:
			iwm_rx_rx_phy_cmd(sc, pkt, data);
			break;

		case IWM_REPLY_RX_MPDU_CMD:
			iwm_rx_mpdu(sc, pkt->data,
			    IWM_RBUF_SIZE - offset - minsz);
			break;

		case IWM_TX_CMD:
			iwm_rx_tx_cmd(sc, pkt, data);
			break;

		case IWM_BA_NOTIF:
			iwm_rx_compressed_ba(sc, pkt);
			break;

		case IWM_MISSED_BEACONS_NOTIFICATION:
			iwm_rx_bmiss(sc, pkt, data);
			break;

		case IWM_MFUART_LOAD_NOTIFICATION:
			break;

		case IWM_ALIVE: {
			struct iwm_alive_resp_v1 *resp1;
			struct iwm_alive_resp_v2 *resp2;
			struct iwm_alive_resp_v3 *resp3;
			size_t plen = iwm_rx_packet_payload_len(pkt);

			if (plen == sizeof (*resp1)) {
				resp1 = (void *)pkt->data;
				sc->sc_uc.uc_error_event_table
				    = le32toh(resp1->error_event_table_ptr);
				sc->sc_uc.uc_log_event_table
				    = le32toh(resp1->log_event_table_ptr);
				sc->sched_base = le32toh(resp1->scd_base_ptr);
				if (resp1->status == IWM_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
				else
					sc->sc_uc.uc_ok = 0;
			}

			if (plen == sizeof (*resp2)) {
				resp2 = (void *)pkt->data;
				sc->sc_uc.uc_error_event_table
				    = le32toh(resp2->error_event_table_ptr);
				sc->sc_uc.uc_log_event_table
				    = le32toh(resp2->log_event_table_ptr);
				sc->sched_base = le32toh(resp2->scd_base_ptr);
				sc->sc_uc.uc_umac_error_event_table
				    = le32toh(resp2->error_info_addr);
				if (resp2->status == IWM_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
				else
					sc->sc_uc.uc_ok = 0;
			}

			if (plen == sizeof (*resp3)) {
				resp3 = (void *)pkt->data;
				sc->sc_uc.uc_error_event_table
				    = le32toh(resp3->error_event_table_ptr);
				sc->sc_uc.uc_log_event_table
				    = le32toh(resp3->log_event_table_ptr);
				sc->sched_base = le32toh(resp3->scd_base_ptr);
				sc->sc_uc.uc_umac_error_event_table
				    = le32toh(resp3->error_info_addr);
				if (resp3->status == IWM_ALIVE_STATUS_OK)
					sc->sc_uc.uc_ok = 1;
				else
					sc->sc_uc.uc_ok = 0;
			}

			sc->sc_uc.uc_intr = 1;
			cv_broadcast(&sc->sc_fw_cv);
			break;
		}

		case IWM_CALIB_RES_NOTIF_PHY_DB: {
			struct iwm_calib_res_notif_phy_db *phy_db_notif;
			if (!IWM_RESP_STRUCT(phy_db_notif, pkt) ||
			    iwm_rx_packet_payload_len(pkt) <
			    sizeof (*phy_db_notif) +
			    le16toh(phy_db_notif->length))
				break;
			(void) iwm_phy_db_set_section(sc, phy_db_notif);
			sc->sc_init_complete |= IWM_CALIB_COMPLETE;
			cv_broadcast(&sc->sc_fw_cv);
			break;
		}

		case IWM_STATISTICS_NOTIFICATION: {
			struct iwm_notif_statistics *stats;
			if (!IWM_RESP_STRUCT(stats, pkt))
				break;
			memcpy(&sc->sc_stats, stats, sizeof (sc->sc_stats));
			sc->sc_noise = iwm_get_noise(&stats->rx.general);
			break;
		}

		case IWM_MCC_CHUB_UPDATE_CMD: {
			struct iwm_mcc_chub_notif *notif;
			if (!IWM_RESP_STRUCT(notif, pkt))
				break;
			iwm_mcc_update(sc, notif);
			break;
		}

		case IWM_DTS_MEASUREMENT_NOTIFICATION:
		case IWM_WIDE_ID(IWM_PHY_OPS_GROUP,
		    IWM_DTS_MEASUREMENT_NOTIF_WIDE):
		case IWM_WIDE_ID(IWM_PHY_OPS_GROUP,
		    IWM_TEMP_REPORTING_THRESHOLDS_CMD):
			break;

		case IWM_WIDE_ID(IWM_PHY_OPS_GROUP,
		    IWM_CT_KILL_NOTIFICATION): {
			struct iwm_ct_kill_notif *notif;
			if (!IWM_RESP_STRUCT(notif, pkt))
				break;
			dev_err(sc->sc_dip, CE_WARN, "device at critical "
			    "temperature (%u degC), stopping device",
			    le16toh(notif->temperature));
			sc->sc_flags |= IWM_FLAG_HW_ERR;
			iwm_add_task(sc, sc->sc_initq, IWM_TASK_INIT);
			break;
		}

		case IWM_ADD_STA_KEY:
		case IWM_PHY_CONFIGURATION_CMD:
		case IWM_TX_ANT_CONFIGURATION_CMD:
		case IWM_ADD_STA:
		case IWM_MAC_CONTEXT_CMD:
		case IWM_REPLY_SF_CFG_CMD:
		case IWM_POWER_TABLE_CMD:
		case IWM_LTR_CONFIG:
		case IWM_PHY_CONTEXT_CMD:
		case IWM_BINDING_CONTEXT_CMD:
		case IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_CFG_CMD):
		case IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_REQ_UMAC):
		case IWM_WIDE_ID(IWM_LONG_GROUP, IWM_SCAN_ABORT_UMAC):
		case IWM_SCAN_OFFLOAD_REQUEST_CMD:
		case IWM_SCAN_OFFLOAD_ABORT_CMD:
		case IWM_REPLY_BEACON_FILTERING_CMD:
		case IWM_MAC_PM_POWER_TABLE:
		case IWM_TIME_QUOTA_CMD:
		case IWM_REMOVE_STA:
		case IWM_TXPATH_FLUSH:
		case IWM_LQ_CMD:
		case IWM_WIDE_ID(IWM_LONG_GROUP,
		    IWM_FW_PAGING_BLOCK_CMD):
		case IWM_BT_CONFIG:
		case IWM_REPLY_THERMAL_MNG_BACKOFF:
		case IWM_NVM_ACCESS_CMD:
		case IWM_MCC_UPDATE_CMD:
		case IWM_TIME_EVENT_CMD: {
			size_t pkt_len;

			if (sc->sc_cmd_resp_pkt[idx] == NULL)
				break;

			pkt_len = sizeof (pkt->len_n_flags) +
			    iwm_rx_packet_len(pkt);

			if ((pkt->hdr.flags & IWM_CMD_FAILED_MSK) ||
			    pkt_len < sizeof (*pkt) ||
			    pkt_len > sc->sc_cmd_resp_len[idx]) {
				kmem_free(sc->sc_cmd_resp_pkt[idx],
				    sc->sc_cmd_resp_len[idx]);
				sc->sc_cmd_resp_pkt[idx] = NULL;
				break;
			}

			memcpy(sc->sc_cmd_resp_pkt[idx], pkt, pkt_len);
			break;
		}

		/* ignore */
		case IWM_PHY_DB_CMD:
			break;

		case IWM_INIT_COMPLETE_NOTIF:
			sc->sc_init_complete |= IWM_INIT_COMPLETE;
			cv_broadcast(&sc->sc_fw_cv);
			break;

		case IWM_SCAN_OFFLOAD_COMPLETE:
			break;

		case IWM_SCAN_ITERATION_COMPLETE:
		case IWM_SCAN_COMPLETE_UMAC:
		case IWM_SCAN_ITERATION_COMPLETE_UMAC:
			iwm_endscan(sc);
			break;

		case IWM_REPLY_ERROR: {
			struct iwm_error_resp *resp;
			if (!IWM_RESP_STRUCT(resp, pkt))
				break;
			dev_err(sc->sc_dip, CE_WARN, "!firmware error 0x%x, "
			    "cmd 0x%x", le32toh(resp->error_type),
			    resp->cmd_id);
			break;
		}

		case IWM_TIME_EVENT_NOTIFICATION: {
			struct iwm_time_event_notif *notif;
			uint32_t action;
			if (!IWM_RESP_STRUCT(notif, pkt))
				break;

			if (sc->sc_time_event_uid != le32toh(notif->unique_id))
				break;
			action = le32toh(notif->action);
			if (action & IWM_TE_V2_NOTIF_HOST_EVENT_END)
				sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;
			break;
		}

		case IWM_WIDE_ID(IWM_SYSTEM_GROUP,
		    IWM_FSEQ_VER_MISMATCH_NOTIFICATION):
			break;

		/*
		 * Firmware versions 21 and 22 generate some DEBUG_LOG_MSG
		 * messages. Just ignore them for now.
		 */
		case IWM_DEBUG_LOG_MSG:
			break;

		case IWM_MCAST_FILTER_CMD:
			break;

		case IWM_SCD_QUEUE_CFG:
			break;

		case IWM_WIDE_ID(IWM_DATA_PATH_GROUP, IWM_DQA_ENABLE_CMD):
			break;

		case IWM_WIDE_ID(IWM_SYSTEM_GROUP, IWM_SOC_CONFIGURATION_CMD):
			break;

		default:
			handled = 0;
			DPRINTF((CE_CONT, "!iwm: unhandled firmware response "
			    "0x%x/0x%x rx ring %d[%d]", code, pkt->len_n_flags,
			    (qid & ~0x80), idx));
			break;
		}

		/*
		 * uCode sets bit 0x80 when it originates the notification,
		 * i.e. when the notification is not a direct response to a
		 * command sent by the driver.
		 * For example, uCode issues IWM_REPLY_RX when it sends a
		 * received frame to the driver.
		 */
		if (handled && !(qid & (1 << 7))) {
			iwm_cmd_done(sc, qid, idx, code);
		}

		offset += roundup(len, IWM_FH_RSCSR_FRAME_ALIGN);
	}
}

static void
iwm_notif_intr(struct iwm_softc *sc)
{
	uint32_t wreg;
	uint16_t hw;
	int count;

	iwm_dma_sync(&sc->rxq.stat_dma, DDI_DMA_SYNC_FORKERNEL);

	count = IWM_RX_RING_COUNT;
	wreg = IWM_FH_RSCSR_CHNL0_WPTR;

	hw = le16toh(sc->rxq.stat->closed_rb_num) & 0xfff;
	hw &= (count - 1);
	while (sc->rxq.cur != hw) {
		struct iwm_rx_data *data = &sc->rxq.data[sc->rxq.cur];
		iwm_rx_pkt(sc, data);
		sc->rxq.cur = (sc->rxq.cur + 1) % count;
	}

	/*
	 * Tell the firmware what we have processed.
	 * Seems like the hardware gets upset unless we align the write by 8??
	 */
	hw = (hw == 0) ? count - 1 : hw - 1;
	IWM_WRITE(sc, wreg, hw & ~7);
}

/*
 * MSI/INTx interrupt handler (OpenBSD iwm_intr).  Received frames, Tx
 * restarts and net80211 notifications are collected under sc_mtx and
 * handed on after it is dropped.
 */
static uint_t
iwm_intr(caddr_t arg, caddr_t unused)
{
	struct iwm_softc *sc = (struct iwm_softc *)(void *)arg;
	struct ieee80211com *ic = &sc->sc_ic;
	mblk_t *rx;
	int rv = 0, txupd;
	uint32_t r1, r2 = 0;

	_NOTE(ARGUNUSED(unused))
	mutex_enter(&sc->sc_mtx);
	if (sc->sc_flags & IWM_FLAG_SUSPENDED) {
		mutex_exit(&sc->sc_mtx);
		return (DDI_INTR_UNCLAIMED);
	}

	IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);

	if (sc->sc_flags & IWM_FLAG_USE_ICT) {
		uint32_t *ict = sc->ict_dma.vaddr;
		uint32_t tmp;

		iwm_dma_sync(&sc->ict_dma, DDI_DMA_SYNC_FORKERNEL);
		tmp = le32toh(ict[sc->ict_cur]);
		if (!tmp)
			goto out_ena;

		/*
		 * ok, there was something.  keep plowing until we have all.
		 */
		r1 = r2 = 0;
		while (tmp) {
			r1 |= tmp;
			ict[sc->ict_cur] = 0;
			sc->ict_cur = (sc->ict_cur+1) % IWM_ICT_COUNT;
			tmp = le32toh(ict[sc->ict_cur]);
		}
		iwm_dma_sync(&sc->ict_dma, DDI_DMA_SYNC_FORDEV);

		/* this is where the fun begins.  don't ask */
		if (r1 == 0xffffffff)
			r1 = 0;

		/*
		 * Workaround for hardware bug where bits are falsely cleared
		 * when using interrupt coalescing.  Bit 15 should be set if
		 * bits 18 and 19 are set.
		 */
		if (r1 & 0xc0000)
			r1 |= 0x8000;

		r1 = (0xff & r1) | ((0xff00 & r1) << 16);
	} else {
		r1 = IWM_READ(sc, IWM_CSR_INT);
		r2 = IWM_READ(sc, IWM_CSR_FH_INT_STATUS);
	}
	if (r1 == 0 && r2 == 0) {
		goto out_ena;
	}
	if (r1 == 0xffffffff || (r1 & 0xfffffff0) == 0xa5a5a5a0)
		goto out;

	IWM_WRITE(sc, IWM_CSR_INT, r1 | ~sc->sc_intmask);

	if (r1 & IWM_CSR_INT_BIT_RF_KILL) {
		iwm_check_rfkill(sc);
		iwm_add_task(sc, sc->sc_initq, IWM_TASK_INIT);
		rv = 1;
		goto out_ena;
	}

	if (r1 & IWM_CSR_INT_BIT_SW_ERR) {
		dev_err(sc->sc_dip, CE_WARN, "!fatal firmware error");
		iwm_nic_error(sc);
		iwm_dump_driver_status(sc);
		iwm_add_task(sc, sc->sc_initq, IWM_TASK_INIT);
		rv = 1;
		goto out;
	}

	if (r1 & IWM_CSR_INT_BIT_HW_ERR) {
		dev_err(sc->sc_dip, CE_WARN,
		    "!hardware error, stopping device");
		if ((sc->sc_flags & IWM_FLAG_SHUTDOWN) == 0) {
			sc->sc_flags |= IWM_FLAG_HW_ERR;
			iwm_add_task(sc, sc->sc_initq, IWM_TASK_INIT);
		}
		rv = 1;
		goto out;
	}

	/* firmware chunk loaded */
	if (r1 & IWM_CSR_INT_BIT_FH_TX) {
		IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, IWM_CSR_FH_INT_TX_MASK);

		sc->sc_fw_chunk_done = 1;
		cv_broadcast(&sc->sc_fw_cv);
	}

	if (r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX |
	    IWM_CSR_INT_BIT_RX_PERIODIC)) {
		if (r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX)) {
			IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS,
			    IWM_CSR_FH_INT_RX_MASK);
		}
		if (r1 & IWM_CSR_INT_BIT_RX_PERIODIC) {
			IWM_WRITE(sc, IWM_CSR_INT, IWM_CSR_INT_BIT_RX_PERIODIC);
		}

		/* Disable periodic interrupt; we use it as just a one-shot. */
		IWM_WRITE_1(sc, IWM_CSR_INT_PERIODIC_REG,
		    IWM_CSR_INT_PERIODIC_DIS);

		/*
		 * Enable periodic interrupt in 8 msec only if we received
		 * real RX interrupt (instead of just periodic int), to catch
		 * any dangling Rx interrupt.  If it was just the periodic
		 * interrupt, there was no dangling Rx activity, and no need
		 * to extend the periodic interrupt; one-shot is enough.
		 */
		if (r1 & (IWM_CSR_INT_BIT_FH_RX | IWM_CSR_INT_BIT_SW_RX))
			IWM_WRITE_1(sc, IWM_CSR_INT_PERIODIC_REG,
			    IWM_CSR_INT_PERIODIC_ENA);

		iwm_notif_intr(sc);
	}

	rv = 1;

out_ena:
	iwm_restore_interrupts(sc);
out:
	/* Collect the work that must run without sc_mtx. */
	rx = sc->sc_rx_head;
	sc->sc_rx_head = sc->sc_rx_tail = NULL;
	txupd = sc->sc_tx_update;
	sc->sc_tx_update = 0;
	if (sc->sc_bmiss_pending || sc->sc_bar_pending ||
	    sc->sc_endscan_pending)
		iwm_add_task(sc, sc->sc_nswq, IWM_TASK_DEFER);
	mutex_exit(&sc->sc_mtx);

	if (rx != NULL)
		iwm_rx_deliver(sc, rx);
	if (txupd)
		mac_tx_update(ic->ic_mach);

	return (rv ? DDI_INTR_CLAIMED : DDI_INTR_UNCLAIMED);
}

/*
 * First-time hardware setup: run the INIT firmware to read the NVM (MAC
 * address, channels, capabilities).  OpenBSD iwm_preinit.  sc_mtx held.
 */
static int
iwm_preinit(struct iwm_softc *sc)
{
	int err;

	ASSERT(MUTEX_HELD(&sc->sc_mtx));
	err = iwm_prepare_card_hw(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not initialize hardware");
		return (err);
	}

	if (sc->attached)
		return (0);

	err = iwm_start_hw(sc);
	if (err) {
		dev_err(sc->sc_dip, CE_WARN, "!could not initialize hardware");
		return (err);
	}

	err = iwm_run_init_mvm_ucode(sc, 1);
	iwm_stop_device(sc);
	if (err)
		return (err);

	/* Print version info and MAC address on first successful fw load. */
	sc->attached = 1;
	dev_err(sc->sc_dip, CE_CONT, "!hw rev 0x%x, fw ver %s, address %s",
	    sc->sc_hw_rev & IWM_CSR_HW_REV_TYPE_MSK, sc->sc_fwver,
	    ieee80211_macaddr_sprintf(sc->sc_nvm.hw_addr));

	return (0);
}

/*
 * Restart the device after a firmware or hardware error, an RF-kill
 * change or a Tx timeout (OpenBSD iwm_init_task).
 */
static void
iwm_init_task(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int running, up, err = 0;

	rw_enter(&sc->sc_rwlock, RW_WRITER);
	mutex_enter(&sc->sc_mtx);
	iwm_task_done(sc, IWM_TASK_INIT);
	if (sc->sc_flags & (IWM_FLAG_SHUTDOWN | IWM_FLAG_SUSPENDED)) {
		mutex_exit(&sc->sc_mtx);
		rw_exit(&sc->sc_rwlock);
		return;
	}
	running = (sc->sc_flags & IWM_FLAG_RUNNING) != 0;
	if (!running)
		iwm_stop_device(sc);
	mutex_exit(&sc->sc_mtx);

	if (running)
		iwm_stop(sc);

	mutex_enter(&sc->sc_mtx);
	sc->sc_flags &= ~IWM_FLAG_HW_ERR;
	up = (sc->sc_flags & (IWM_FLAG_UP | IWM_FLAG_RUNNING)) == IWM_FLAG_UP;
	if (running)
		sc->sc_fw_restarts++;
	mutex_exit(&sc->sc_mtx);

	if (up)
		err = iwm_init(sc);
	rw_exit(&sc->sc_rwlock);

	/* Reconnect if the user had asked for a network. */
	if (up && err == 0) {
		(void) ieee80211_new_state(ic, ic->ic_des_esslen != 0 ?
		    IEEE80211_S_SCAN : IEEE80211_S_INIT, -1);
	}
}

/*
 * Transmit one 802.11 frame from net80211 (ic_xmit) or from mac_tx (type
 * DATA).  The mac_wifi plugin has already built the 802.11 header; for
 * data frames net80211 fills in sequence/QoS fields and encrypts.
 * Returns EAGAIN, with mp still owned by the caller, only for data frames
 * when the Tx queue is full; otherwise mp is consumed.
 */
static int
iwm_send(struct ieee80211com *ic, mblk_t *mp, uint8_t type)
{
	struct iwm_softc *sc = (struct iwm_softc *)ic;
	struct ieee80211_node *ni;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	mblk_t *m, *m0;
	size_t off;
	int err, ac = WME_AC_BE, isdata;

	isdata = ((type & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_DATA);

	mutex_enter(&sc->sc_mtx);
	if ((sc->sc_flags & IWM_FLAG_RUNNING) == 0 ||
	    (sc->sc_flags & (IWM_FLAG_SUSPENDED | IWM_FLAG_HW_ERR))) {
		mutex_exit(&sc->sc_mtx);
		freemsg(mp);
		return (ENXIO);
	}
	if (isdata && (sc->sc_flags & IWM_FLAG_TXFLUSH)) {
		/*
		 * Flushing happens while leaving RUN: drop rather than block
		 * the MAC client, since no Tx completion may follow.
		 */
		mutex_exit(&sc->sc_mtx);
		freemsg(mp);
		return (ENXIO);
	}
	if (isdata && sc->qfullmsk) {
		/* Retried after iwm_clear_oactive() calls mac_tx_update(). */
		sc->sc_flags |= IWM_FLAG_OACTIVE;
		sc->sc_noxmtbuf++;
		mutex_exit(&sc->sc_mtx);
		return (EAGAIN);
	}
	mutex_exit(&sc->sc_mtx);

	/* One contiguous copy with room for a software CCMP MIC. */
	m = allocb(msgdsize(mp) + 32, BPRI_MED);
	if (m == NULL) {
		mutex_enter(&sc->sc_mtx);
		sc->sc_noxmtbuf++;
		mutex_exit(&sc->sc_mtx);
		/*
		 * Drop: EAGAIN would need a later mac_tx_update(), and with an
		 * idle ring no Tx completion would ever send one.
		 */
		freemsg(mp);
		return (ENOMEM);
	}
	for (off = 0, m0 = mp; m0 != NULL; m0 = m0->b_cont) {
		size_t mblen = MBLKL(m0);

		bcopy(m0->b_rptr, m->b_rptr + off, mblen);
		off += mblen;
	}
	m->b_wptr += off;
	freemsg(mp);

	if (MBLKL(m) < sizeof (struct ieee80211_frame)) {
		freemsg(m);
		return (EINVAL);
	}
	wh = (struct ieee80211_frame *)m->b_rptr;
	ni = ieee80211_find_txnode(ic, wh->i_addr1);
	if (ni == NULL) {
		freemsg(m);
		mutex_enter(&sc->sc_mtx);
		sc->sc_oerrors++;
		mutex_exit(&sc->sc_mtx);
		return (EIO);
	}

	if (isdata)
		(void) ieee80211_encap(ic, m, ni);

	wh = (struct ieee80211_frame *)m->b_rptr;
	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		k = ieee80211_crypto_encap(ic, m);
		if (k == NULL) {
			freemsg(m);
			ieee80211_free_node(ni);
			mutex_enter(&sc->sc_mtx);
			sc->sc_oerrors++;
			mutex_exit(&sc->sc_mtx);
			return (EIO);
		}
		wh = (struct ieee80211_frame *)m->b_rptr;
	}

	/* Map the QoS TID to its access category. */
	if (isdata && (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_QOS)) {
		struct ieee80211_qosframe *qwh = (void *)wh;

		ac = TID_TO_WME_AC(qwh->i_qos[0] & IEEE80211_QOS_TID);
	}

	mutex_enter(&sc->sc_mtx);
	if ((sc->sc_flags & IWM_FLAG_RUNNING) == 0) {
		err = ENXIO;
	} else {
		err = iwm_tx(sc, m, ni, ac);
	}
	if (err == 0) {
		sc->sc_opackets++;
		sc->sc_obytes += off;
	} else {
		sc->sc_oerrors++;
	}
	mutex_exit(&sc->sc_mtx);

	if (err != 0)
		freemsg(m);
	ieee80211_free_node(ni);
	/* The frame is consumed even on error: it has been encapsulated. */
	return (err == 0 ? 0 : EIO);
}

/*
 * GLDv3 entry points.
 */
static int
iwm_m_start(void *arg)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int err;

	rw_enter(&sc->sc_rwlock, RW_WRITER);
	mutex_enter(&sc->sc_mtx);
	sc->sc_flags |= IWM_FLAG_UP;
	mutex_exit(&sc->sc_mtx);

	if (sc->sc_flags & IWM_FLAG_RUNNING)
		err = 0;
	else
		err = iwm_init(sc);

	mutex_enter(&sc->sc_mtx);
	if (err != 0 && (sc->sc_flags & IWM_FLAG_RFKILL)) {
		/*
		 * Radio switched off: let plumb succeed.  The RF-kill
		 * interrupt runs iwm_init_task() when it is switched on.
		 */
		dev_err(sc->sc_dip, CE_NOTE, "!radio is disabled by hardware "
		    "switch");
		err = 0;
	} else if (err != 0) {
		sc->sc_flags &= ~IWM_FLAG_UP;
	}
	mutex_exit(&sc->sc_mtx);
	rw_exit(&sc->sc_rwlock);

	if (err == 0 && (sc->sc_flags & IWM_FLAG_RUNNING))
		(void) ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	return (err);
}

static void
iwm_m_stop(void *arg)
{
	struct iwm_softc *sc = arg;

	rw_enter(&sc->sc_rwlock, RW_WRITER);
	mutex_enter(&sc->sc_mtx);
	sc->sc_flags &= ~IWM_FLAG_UP;
	mutex_exit(&sc->sc_mtx);
	if (sc->sc_flags & IWM_FLAG_RUNNING) {
		iwm_stop(sc);
	} else {
		mutex_enter(&sc->sc_mtx);
		iwm_stop_device(sc);
		mutex_exit(&sc->sc_mtx);
	}
	rw_exit(&sc->sc_rwlock);
}

static mblk_t *
iwm_m_tx(void *arg, mblk_t *mp)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	mblk_t *next;

	if ((sc->sc_flags & IWM_FLAG_RUNNING) == 0 ||
	    (sc->sc_flags & IWM_FLAG_SUSPENDED) ||
	    ic->ic_state != IEEE80211_S_RUN) {
		while (mp != NULL) {
			next = mp->b_next;
			mp->b_next = NULL;
			freemsg(mp);
			mp = next;
		}
		return (NULL);
	}

	while (mp != NULL) {
		next = mp->b_next;
		mp->b_next = NULL;
		if (iwm_send(ic, mp, IEEE80211_FC0_TYPE_DATA) == EAGAIN) {
			mp->b_next = next;
			break;
		}
		mp = next;
	}
	return (mp);
}

/*
 * A changed ESSID or key set needs a fresh connection: restart the device
 * and scan (the pattern every illumos net80211 driver uses).
 */
static void
iwm_netreset(struct iwm_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int err = 0;

	if (ic->ic_des_esslen == 0)
		return;
	rw_enter(&sc->sc_rwlock, RW_WRITER);
	if ((sc->sc_flags & IWM_FLAG_UP) == 0) {
		rw_exit(&sc->sc_rwlock);
		return;
	}
	if (sc->sc_flags & IWM_FLAG_RUNNING)
		iwm_stop(sc);
	err = iwm_init(sc);
	rw_exit(&sc->sc_rwlock);
	if (err == 0)
		(void) ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
}

static void
iwm_m_ioctl(void *arg, queue_t *wq, mblk_t *mp)
{
	struct iwm_softc *sc = arg;
	int err;

	err = ieee80211_ioctl(&sc->sc_ic, wq, mp);
	if (err == ENETRESET)
		iwm_netreset(sc);
}

static int
iwm_m_getprop(void *arg, const char *pr_name, mac_prop_id_t wldp_pr_num,
    uint_t wldp_length, void *wldp_buf)
{
	struct iwm_softc *sc = arg;

	return (ieee80211_getprop(&sc->sc_ic, pr_name, wldp_pr_num,
	    wldp_length, wldp_buf));
}

static void
iwm_m_propinfo(void *arg, const char *pr_name, mac_prop_id_t wldp_pr_num,
    mac_prop_info_handle_t prh)
{
	struct iwm_softc *sc = arg;

	ieee80211_propinfo(&sc->sc_ic, pr_name, wldp_pr_num, prh);
}

static int
iwm_m_setprop(void *arg, const char *pr_name, mac_prop_id_t wldp_pr_num,
    uint_t wldp_length, const void *wldp_buf)
{
	struct iwm_softc *sc = arg;
	int err;

	err = ieee80211_setprop(&sc->sc_ic, pr_name, wldp_pr_num,
	    wldp_length, wldp_buf);
	if (err == ENETRESET) {
		iwm_netreset(sc);
		err = 0;
	}
	return (err);
}

static int
iwm_m_stat(void *arg, uint_t stat, uint64_t *val)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct iwm_node *in;
	int rate;

	mutex_enter(&sc->sc_mtx);
	switch (stat) {
	case MAC_STAT_IFSPEED:
		ni = ic->ic_bss;
		in = (struct iwm_node *)ni;
		if (ni == NULL || ic->ic_state != IEEE80211_S_RUN) {
			*val = 0;
			break;
		}
		if (ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE) {
			rate = ic->ic_fixed_rate;
		} else if ((ni->in_flags & IEEE80211_NODE_HT) &&
		    in->in_txmcs >= 0 &&
		    in->in_txmcs < IWM_HT_RATESET_NUM_MCS) {
			struct iwm_ra_peer peer;
			const struct iwm_ht_rateset *rs;

			iwm_ra_peer(sc, in, &peer);
			rs = iwm_ra_get_ht_rateset(in->in_txmcs, peer.chan40,
			    peer.sgi);
			rate = rs->rates[in->in_txmcs % 8];
		} else if (ni->in_rates.ir_nrates > 0) {
			rate = ni->in_rates.ir_rates[ni->in_txrate] &
			    IEEE80211_RATE_VAL;
		} else {
			rate = 0;
		}
		/* 500 kbit/s units */
		*val = (uint64_t)rate * 500000ULL;
		break;
	case MAC_STAT_NOXMTBUF:
		*val = sc->sc_noxmtbuf;
		break;
	case MAC_STAT_IERRORS:
		*val = sc->sc_ierrors;
		break;
	case MAC_STAT_OERRORS:
		*val = sc->sc_oerrors;
		break;
	case MAC_STAT_RBYTES:
		*val = sc->sc_rbytes;
		break;
	case MAC_STAT_IPACKETS:
		*val = sc->sc_ipackets;
		break;
	case MAC_STAT_OBYTES:
		*val = sc->sc_obytes;
		break;
	case MAC_STAT_OPACKETS:
		*val = sc->sc_opackets;
		break;
	case WIFI_STAT_TX_FAILED:
		*val = sc->sc_oerrors;
		break;
	case WIFI_STAT_TX_RETRANS:
	case WIFI_STAT_FCS_ERRORS:
	case WIFI_STAT_WEP_ERRORS:
	case WIFI_STAT_TX_FRAGS:
	case WIFI_STAT_MCAST_TX:
	case WIFI_STAT_RTS_SUCCESS:
	case WIFI_STAT_RTS_FAILURE:
	case WIFI_STAT_ACK_FAILURE:
	case WIFI_STAT_RX_FRAGS:
	case WIFI_STAT_MCAST_RX:
	case WIFI_STAT_RX_DUPS:
		mutex_exit(&sc->sc_mtx);
		return (ieee80211_stat(ic, stat, val));
	default:
		mutex_exit(&sc->sc_mtx);
		return (ENOTSUP);
	}
	mutex_exit(&sc->sc_mtx);
	return (0);
}

/*
 * Changing the MAC address takes effect at the next (re)initialisation;
 * the firmware MAC context is built from ic_macaddr in iwm_auth().
 */
static int
iwm_m_unicst(void *arg, const uint8_t *macaddr)
{
	struct iwm_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	if (IEEE80211_ADDR_EQ(ic->ic_macaddr, macaddr))
		return (0);
	rw_enter(&sc->sc_rwlock, RW_WRITER);
	mutex_enter(&sc->sc_mtx);
	IEEE80211_ADDR_COPY(ic->ic_macaddr, macaddr);
	mutex_exit(&sc->sc_mtx);
	rw_exit(&sc->sc_rwlock);
	return (0);
}

/*
 * The firmware passes all multicast from our BSS (iwm_allow_mcast()), so
 * there is no filter to program.
 */
static int
iwm_m_multicst(void *arg, boolean_t add, const uint8_t *m)
{
	_NOTE(ARGUNUSED(arg, add, m))
	return (0);
}

static int
iwm_m_promisc(void *arg, boolean_t on)
{
	_NOTE(ARGUNUSED(arg, on))
	return (0);
}

static mac_callbacks_t iwm_m_callbacks = {
	.mc_callbacks	= MC_IOCTL | MC_SETPROP | MC_GETPROP | MC_PROPINFO,
	.mc_getstat	= iwm_m_stat,
	.mc_start	= iwm_m_start,
	.mc_stop	= iwm_m_stop,
	.mc_setpromisc	= iwm_m_promisc,
	.mc_multicst	= iwm_m_multicst,
	.mc_unicst	= iwm_m_unicst,
	.mc_tx		= iwm_m_tx,
	.mc_ioctl	= iwm_m_ioctl,
	.mc_setprop	= iwm_m_setprop,
	.mc_getprop	= iwm_m_getprop,
	.mc_propinfo	= iwm_m_propinfo
};

/*
 * Interrupt setup: MSI if available, else INTx.  MSI-X needs the IVAR
 * programming of OpenBSD's iwm_conf_msix_hw(), which is not ported.
 */
static void
iwm_intr_teardown(struct iwm_softc *sc)
{
	if (sc->sc_intr_htable[0] == NULL)
		return;
	if (sc->sc_intr_cap & DDI_INTR_FLAG_BLOCK)
		(void) ddi_intr_block_disable(sc->sc_intr_htable, 1);
	else
		(void) ddi_intr_disable(sc->sc_intr_htable[0]);
	(void) ddi_intr_remove_handler(sc->sc_intr_htable[0]);
	(void) ddi_intr_free(sc->sc_intr_htable[0]);
	sc->sc_intr_htable[0] = NULL;
}

static int
iwm_intr_add(struct iwm_softc *sc, int type)
{
	int navail, actual;

	if (ddi_intr_get_navail(sc->sc_dip, type, &navail) != DDI_SUCCESS ||
	    navail < 1)
		return (DDI_FAILURE);
	if (ddi_intr_alloc(sc->sc_dip, sc->sc_intr_htable, type, 0, 1,
	    &actual, DDI_INTR_ALLOC_STRICT) != DDI_SUCCESS || actual != 1) {
		sc->sc_intr_htable[0] = NULL;
		return (DDI_FAILURE);
	}
	if (ddi_intr_get_pri(sc->sc_intr_htable[0], &sc->sc_intr_pri) !=
	    DDI_SUCCESS ||
	    ddi_intr_get_cap(sc->sc_intr_htable[0], &sc->sc_intr_cap) !=
	    DDI_SUCCESS ||
	    ddi_intr_add_handler(sc->sc_intr_htable[0], iwm_intr,
	    (caddr_t)sc, NULL) != DDI_SUCCESS) {
		(void) ddi_intr_free(sc->sc_intr_htable[0]);
		sc->sc_intr_htable[0] = NULL;
		return (DDI_FAILURE);
	}
	sc->sc_intr_type = type;
	return (DDI_SUCCESS);
}

static int
iwm_intr_setup(struct iwm_softc *sc)
{
	int types;

	if (ddi_intr_get_supported_types(sc->sc_dip, &types) != DDI_SUCCESS)
		return (DDI_FAILURE);
	if ((types & DDI_INTR_TYPE_MSI) &&
	    iwm_intr_add(sc, DDI_INTR_TYPE_MSI) == DDI_SUCCESS)
		return (DDI_SUCCESS);
	if ((types & DDI_INTR_TYPE_FIXED) &&
	    iwm_intr_add(sc, DDI_INTR_TYPE_FIXED) == DDI_SUCCESS)
		return (DDI_SUCCESS);
	return (DDI_FAILURE);
}

static int
iwm_intr_enable(struct iwm_softc *sc)
{
	if (sc->sc_intr_cap & DDI_INTR_FLAG_BLOCK)
		return (ddi_intr_block_enable(sc->sc_intr_htable, 1));
	return (ddi_intr_enable(sc->sc_intr_htable[0]));
}

static int
iwm_pci_get_capability(ddi_acc_handle_t pcih, int cap, int *cap_off)
{
	uint8_t ptr;
	int n;

	if ((pci_config_get16(pcih, PCI_CONF_STAT) & PCI_STAT_CAP) == 0)
		return (DDI_FAILURE);
	for (n = 0, ptr = pci_config_get8(pcih, PCI_CONF_CAP_PTR);
	    ptr != 0 && ptr != 0xff && n < 48;
	    ptr = pci_config_get8(pcih, ptr + PCI_CAP_NEXT_PTR), n++) {
		if (pci_config_get8(pcih, ptr + PCI_CAP_ID) == cap) {
			*cap_off = ptr;
			return (DDI_SUCCESS);
		}
	}
	return (DDI_FAILURE);
}

/*
 * 8000-family silicon step (OpenBSD iwm_attach).  In the 8000 HW family
 * the format of the 4 bytes of CSR_HW_REV have changed, and now the
 * revision step also includes bit 0-1 (no more "dash" value).  To keep
 * hw_rev backwards compatible we store it in the old format.  sc_mtx held.
 */
static int
iwm_read_hw_rev(struct iwm_softc *sc)
{
	uint32_t hw_step;
	int err;

	sc->sc_hw_rev = IWM_READ(sc, IWM_CSR_HW_REV);
	if (sc->sc_device_family < IWM_DEVICE_FAMILY_8000)
		return (0);

	sc->sc_hw_rev = (sc->sc_hw_rev & 0xfff0) |
	    (IWM_CSR_HW_REV_STEP(sc->sc_hw_rev << 2) << 2);

	if (iwm_prepare_card_hw(sc) != 0) {
		dev_err(sc->sc_dip, CE_WARN, "!could not initialize hardware");
		return (EIO);
	}

	/*
	 * In order to recognize C step the driver should read the
	 * chip version id located at the AUX bus MISC address.
	 */
	IWM_SETBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
	DELAY(2);

	err = iwm_poll_bit(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
	    IWM_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
	    25000);
	if (!err) {
		dev_err(sc->sc_dip, CE_WARN, "!Failed to wake up the nic");
		return (EIO);
	}

	if (iwm_nic_lock(sc)) {
		hw_step = iwm_read_prph(sc, IWM_WFPM_CTRL_REG);
		hw_step |= IWM_ENABLE_WFPM;
		iwm_write_prph(sc, IWM_WFPM_CTRL_REG, hw_step);
		hw_step = iwm_read_prph(sc, IWM_AUX_MISC_REG);
		hw_step = (hw_step >> IWM_HW_STEP_LOCATION_BITS) & 0xF;
		if (hw_step == 0x3)
			sc->sc_hw_rev = (sc->sc_hw_rev & 0xFFFFFFF3) |
			    (IWM_SILICON_C_STEP << 2);
		iwm_nic_unlock(sc);
	} else {
		dev_err(sc->sc_dip, CE_WARN, "!Failed to lock the nic");
		return (EIO);
	}
	/* Leave the NIC as we found it; iwm_start_hw() resets it. */
	IWM_CLRBITS(sc, IWM_CSR_GP_CNTRL,
	    IWM_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
	return (0);
}

static void
iwm_phy_db_entry_free(struct iwm_phy_db_entry *entry)
{
	if (entry->data != NULL)
		kmem_free(entry->data, entry->size);
	entry->data = NULL;
	entry->size = 0;
}

/* Free the calibration results saved from the INIT firmware. */
static void
iwm_phy_db_free(struct iwm_softc *sc)
{
	struct iwm_phy_db *phy_db = &sc->sc_phy_db;
	int i;

	iwm_phy_db_entry_free(&phy_db->cfg);
	iwm_phy_db_entry_free(&phy_db->calib_nch);
	for (i = 0; i < IWM_NUM_PAPD_CH_GROUPS; i++)
		iwm_phy_db_entry_free(&phy_db->calib_ch_group_papd[i]);
	for (i = 0; i < IWM_NUM_TXP_CH_GROUPS; i++)
		iwm_phy_db_entry_free(&phy_db->calib_ch_group_txp[i]);
}

/* Release everything iwm_attach() set up.  Safe on a partial attach. */
static void
iwm_cleanup(struct iwm_softc *sc)
{
	int i;

	if (sc->sc_watchdog != NULL) {
		ddi_periodic_delete(sc->sc_watchdog);
		sc->sc_watchdog = NULL;
	}
	if (sc->sc_regh != NULL) {
		/* Make sure the device is quiet before tearing down. */
		IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);
		IWM_WRITE(sc, IWM_CSR_INT, ~0);
	}
	iwm_intr_teardown(sc);
	if (sc->sc_initq != NULL) {
		taskq_destroy(sc->sc_initq);
		sc->sc_initq = NULL;
	}
	if (sc->sc_nswq != NULL) {
		taskq_destroy(sc->sc_nswq);
		sc->sc_nswq = NULL;
	}
	if (sc->sc_fw.fw_rawdata != NULL)
		iwm_fw_info_free(&sc->sc_fw);
	iwm_free_fw_paging(sc);
	iwm_free_rx_ring(sc, &sc->rxq);
	for (i = 0; i < nitems(sc->txq); i++)
		iwm_free_tx_ring(sc, &sc->txq[i]);
	iwm_dma_contig_free(&sc->sched_dma);
	iwm_dma_contig_free(&sc->ict_dma);
	iwm_dma_contig_free(&sc->kw_dma);
	iwm_dma_contig_free(&sc->fw_dma);
	iwm_phy_db_free(sc);
	if (sc->sc_regh != NULL) {
		ddi_regs_map_free(&sc->sc_regh);
		sc->sc_regh = NULL;
	}
	if (sc->sc_pcih != NULL) {
		pci_config_teardown(&sc->sc_pcih);
		sc->sc_pcih = NULL;
	}
}

static int
iwm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct iwm_softc *sc;
	struct ieee80211com *ic;
	struct ieee80211_htrateset htrs;
	wifi_data_t wd = { 0 };
	mac_register_t *macp;
	char strbuf[32];
	uint16_t devid;
	int instance, err, i, ac, locks = 0;

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		sc = ddi_get_driver_private(dip);
		if (sc == NULL)
			return (DDI_FAILURE);
		/* OpenBSD iwm_resume: clear "PCI retry timeout" again. */
		if (pci_config_get8(sc->sc_pcih, 0x41) != 0)
			pci_config_put8(sc->sc_pcih, 0x41, 0);
		rw_enter(&sc->sc_rwlock, RW_WRITER);
		mutex_enter(&sc->sc_mtx);
		sc->sc_flags &= ~IWM_FLAG_SUSPENDED;
		mutex_exit(&sc->sc_mtx);
		err = 0;
		if (sc->sc_flags & IWM_FLAG_UP)
			err = iwm_init(sc);
		rw_exit(&sc->sc_rwlock);
		if ((sc->sc_flags & IWM_FLAG_UP) && err == 0) {
			ic = &sc->sc_ic;
			(void) ieee80211_new_state(ic, ic->ic_des_esslen != 0 ?
			    IEEE80211_S_SCAN : IEEE80211_S_INIT, -1);
		}
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	if (ddi_soft_state_zalloc(iwm_soft_state, instance) != DDI_SUCCESS)
		return (DDI_FAILURE);
	sc = ddi_get_soft_state(iwm_soft_state, instance);
	ddi_set_driver_private(dip, sc);
	sc->sc_dip = dip;
	ic = &sc->sc_ic;

	if (pci_config_setup(dip, &sc->sc_pcih) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!pci_config_setup() failed");
		sc->sc_pcih = NULL;
		goto fail;
	}
	if (iwm_pci_get_capability(sc->sc_pcih, PCI_CAP_ID_PCI_E,
	    &sc->sc_cap_off) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!PCIe capability structure not found!");
		goto fail;
	}

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	if (pci_config_get8(sc->sc_pcih, 0x41) != 0)
		pci_config_put8(sc->sc_pcih, 0x41, 0);

	devid = pci_config_get16(sc->sc_pcih, PCI_CONF_DEVID);
	for (i = 0; i < nitems(iwm_devices); i++) {
		if (iwm_devices[i].device == devid)
			break;
	}
	if (i == nitems(iwm_devices)) {
		dev_err(dip, CE_WARN, "!unknown adapter type 0x%04x", devid);
		goto fail;
	}
	sc->sc_fwname = iwm_devices[i].fwname;
	sc->sc_device_family = iwm_devices[i].family;
	sc->host_interrupt_operation_mode = 0;
	sc->sc_fwdmasegsz = IWM_FWDMASEGSZ_8000;
	sc->sc_nvm_max_section_size = 32768;
	sc->nvm_type = IWM_NVM_EXT;

	if (ddi_dev_regsize(dip, 1, &sc->sc_sz) != DDI_SUCCESS ||
	    ddi_regs_map_setup(dip, 1, &sc->sc_regs, 0, 0, &iwm_reg_accattr,
	    &sc->sc_regh) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!can't map mem space");
		sc->sc_regh = NULL;
		goto fail;
	}

	/* Clear and mask all interrupts before installing the handler. */
	IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);
	IWM_WRITE(sc, IWM_CSR_INT, 0xffffffff);
	IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, ~0);

	if (iwm_intr_setup(sc) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!can't establish interrupt");
		goto fail;
	}

	mutex_init(&sc->sc_mtx, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(sc->sc_intr_pri));
	rw_init(&sc->sc_rwlock, NULL, RW_DRIVER, NULL);
	cv_init(&sc->sc_cmd_cv, NULL, CV_DRIVER, NULL);
	cv_init(&sc->sc_fw_cv, NULL, CV_DRIVER, NULL);
	cv_init(&sc->sc_txq_cv, NULL, CV_DRIVER, NULL);
	cv_init(&sc->sc_task_cv, NULL, CV_DRIVER, NULL);
	locks = 1;

	mutex_enter(&sc->sc_mtx);
	err = iwm_read_hw_rev(sc);
	mutex_exit(&sc->sc_mtx);
	if (err != 0)
		goto fail;

	/*
	 * Allocate DMA memory for firmware transfers.
	 * Must be aligned on a 16-byte boundary.
	 */
	if (iwm_dma_contig_alloc(sc, &sc->fw_dma, sc->sc_fwdmasegsz, 16)) {
		dev_err(dip, CE_WARN, "!could not allocate memory for "
		    "firmware");
		goto fail;
	}

	/* Allocate "Keep Warm" page, used internally by the card. */
	if (iwm_dma_contig_alloc(sc, &sc->kw_dma, 4096, 4096)) {
		dev_err(dip, CE_WARN, "!could not allocate keep warm page");
		goto fail;
	}

	/* Allocate interrupt cause table (ICT). */
	if (iwm_dma_contig_alloc(sc, &sc->ict_dma, IWM_ICT_SIZE,
	    1 << IWM_ICT_PADDR_SHIFT)) {
		dev_err(dip, CE_WARN, "!could not allocate ICT table");
		goto fail;
	}

	/* TX scheduler rings must be aligned on a 1KB boundary. */
	if (iwm_dma_contig_alloc(sc, &sc->sched_dma,
	    nitems(sc->txq) * sizeof (struct iwm_agn_scd_bc_tbl), 1024)) {
		dev_err(dip, CE_WARN, "!could not allocate TX scheduler "
		    "rings");
		goto fail;
	}

	for (i = 0; i < nitems(sc->txq); i++) {
		if (iwm_alloc_tx_ring(sc, &sc->txq[i], i)) {
			dev_err(dip, CE_WARN, "!could not allocate TX ring %d",
			    i);
			goto fail;
		}
	}

	if (iwm_alloc_rx_ring(sc, &sc->rxq)) {
		dev_err(dip, CE_WARN, "!could not allocate RX ring");
		goto fail;
	}

	/*
	 * The INIT firmware only needs small commands; the command queue is
	 * queue 0 with DQA-capable firmware (all 8000-series images), which
	 * iwm_load_ucode_wait_alive() confirms.
	 */
	sc->cmdqid = IWM_DQA_CMD_QUEUE;
	if (iwm_alloc_txbufs(sc, &sc->txq[sc->cmdqid])) {
		dev_err(dip, CE_WARN, "!could not allocate command buffers");
		goto fail;
	}

	sc->sc_initq = taskq_create("iwm_init", 1, minclsyspri, 1, 1, 0);
	sc->sc_nswq = taskq_create("iwm_ns", 1, minclsyspri, 1, 1, 0);
	if (sc->sc_initq == NULL || sc->sc_nswq == NULL)
		goto fail;

	for (i = 0; i < nitems(sc->sc_phyctxt); i++) {
		sc->sc_phyctxt[i].id = i;
		sc->sc_phyctxt[i].sco = IWM_HTOP0_SCO_SCN;
	}
	sc->sc_amrr.amrr_min_success_threshold = IWM_AMRR_MIN_SUCCESS_THRESHOLD;
	sc->sc_amrr.amrr_max_success_threshold = IWM_AMRR_MAX_SUCCESS_THRESHOLD;

	if (iwm_intr_enable(sc) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!could not enable interrupt");
		goto fail;
	}

	/*
	 * Run the INIT firmware now: the MAC address and channel list must
	 * be known before net80211 and the MAC layer are attached.
	 */
	rw_enter(&sc->sc_rwlock, RW_WRITER);
	mutex_enter(&sc->sc_mtx);
	err = iwm_preinit(sc);
	mutex_exit(&sc->sc_mtx);
	rw_exit(&sc->sc_rwlock);
	if (err != 0) {
		dev_err(dip, CE_WARN, "could not load firmware %s (error %d); "
		    "install it in /kernel/firmware/iwm", sc->sc_fwname, err);
		goto fail;
	}

	/* Payload buffers for the queues the regular firmware will use. */
	err = iwm_alloc_txbufs(sc, &sc->txq[sc->cmdqid]);
	for (ac = 0; ac < EDCA_NUM_AC && err == 0; ac++) {
		int qid = isset(sc->sc_enabled_capa,
		    IWM_UCODE_TLV_CAPA_DQA_SUPPORT) ?
		    IWM_DQA_MIN_MGMT_QUEUE + ac : ac;
		err = iwm_alloc_txbufs(sc, &sc->txq[qid]);
	}
	if (isset(sc->sc_enabled_capa, IWM_UCODE_TLV_CAPA_DQA_SUPPORT)) {
		for (i = IWM_FIRST_AGG_TX_QUEUE;
		    i <= IWM_LAST_AGG_TX_QUEUE && err == 0; i++)
			err = iwm_alloc_txbufs(sc, &sc->txq[i]);
	}
	if (err != 0) {
		dev_err(dip, CE_WARN, "!could not allocate Tx buffers");
		goto fail;
	}

	/*
	 * net80211 setup (OpenBSD iwm_attach + iwm_preinit).
	 */
	IEEE80211_ADDR_COPY(ic->ic_macaddr, sc->sc_nvm.hw_addr);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_maxrssi = IWM_MAX_DBM - IWM_MIN_DBM;

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_WPA |		/* WPA/RSN */
	    IEEE80211_C_AES_CCM |	/* hardware CCMP for pairwise keys */
	    IEEE80211_C_WME |		/* QoS */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_SHPREAMBLE;	/* short preamble supported */

	ic->ic_sup_rates[IEEE80211_MODE_11B] = iwm_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = iwm_rateset_11g;
	/* not all hardware can do 5GHz band */
	if (sc->sc_nvm.sku_cap_band_52GHz_enable)
		ic->ic_sup_rates[IEEE80211_MODE_11A] = iwm_rateset_11a;

	if (sc->sc_nvm.sku_cap_11n_enable) {
		ic->ic_htcaps = IEEE80211_HTC_HT | IEEE80211_HTC_AMPDU |
		    IEEE80211_HTCAP_SHORTGI20 | IEEE80211_HTCAP_SHORTGI40 |
		    IEEE80211_HTCAP_CHWIDTH40 | IEEE80211_HTCAP_SMPS_OFF;
	}

	/* IBSS channel undefined for now. */
	ic->ic_ibss_chan = iwm_default_chan(sc);

	ic->ic_xmit = iwm_send;
	ic->ic_watchdog = iwm_ic_watchdog;
	ic->ic_set_shortslot = iwm_updateslot;
	ic->ic_wme.wme_update = iwm_updateedca;

	if (sc->sc_nvm.sku_cap_11n_enable) {
		iwm_setup_ht_rates(sc, &htrs);
		if (ieee80211_attach_mcs(ic, &htrs) != 0) {
			dev_err(dip, CE_WARN, "!bad HT rate set");
			goto fail;
		}
	} else {
		ieee80211_attach(ic);
	}
	sc->sc_flags |= IWM_FLAG_NET_ATTACHED;

	/* Our firmware takes 64k A-MPDUs with 4 usec spacing (OpenBSD). */
	ic->ic_ampdu_rxmax = IEEE80211_HTCAP_MAXRXAMPDU_64K;
	ic->ic_ampdu_density = IEEE80211_HTCAP_MPDUDENSITY_4;

	ieee80211_register_door(ic, ddi_driver_name(dip), instance);

	/* Override 802.11 state transition machine. */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = iwm_newstate;
	ic->ic_node_alloc = iwm_node_alloc;
	ic->ic_node_free = iwm_node_free;
	ic->ic_crypto.cs_key_set = iwm_set_key;
	ic->ic_crypto.cs_key_delete = iwm_delete_key;
	ic->ic_wme.wme_update = iwm_updateedca;

	/* BlockAck hooks; chain to net80211's defaults. */
	sc->sc_recv_action = ic->ic_recv_action;
	ic->ic_recv_action = iwm_recv_action;
	sc->sc_addba_response = ic->ic_addba_response;
	ic->ic_addba_response = iwm_addba_response;
	sc->sc_addba_stop = ic->ic_addba_stop;
	ic->ic_addba_stop = iwm_addba_stop;

	ieee80211_media_init(ic);
	ic->ic_def_txkey = 0;

	/*
	 * Register with the MAC layer.
	 */
	wd.wd_secalloc = WIFI_SEC_NONE;
	wd.wd_opmode = ic->ic_opmode;
	IEEE80211_ADDR_COPY(wd.wd_bssid, ic->ic_macaddr);

	if ((macp = mac_alloc(MAC_VERSION)) == NULL) {
		dev_err(dip, CE_WARN, "!mac_alloc() failed");
		goto fail;
	}
	macp->m_type_ident = MAC_PLUGIN_IDENT_WIFI;
	macp->m_driver = sc;
	macp->m_dip = dip;
	macp->m_src_addr = ic->ic_macaddr;
	macp->m_callbacks = &iwm_m_callbacks;
	macp->m_min_sdu = 0;
	macp->m_max_sdu = IEEE80211_MTU;
	macp->m_pdata = &wd;
	macp->m_pdata_size = sizeof (wd);
	err = mac_register(macp, &ic->ic_mach);
	mac_free(macp);
	if (err != 0) {
		dev_err(dip, CE_WARN, "!mac_register() failed (%d)", err);
		ic->ic_mach = NULL;
		goto fail;
	}

	(void) snprintf(strbuf, sizeof (strbuf), "iwm%d", instance);
	if (ddi_create_minor_node(dip, strbuf, S_IFCHR, instance + 1,
	    DDI_NT_NET_WIFI, 0) != DDI_SUCCESS) {
		dev_err(dip, CE_WARN, "!ddi_create_minor_node() failed");
		(void) mac_unregister(ic->ic_mach);
		ic->ic_mach = NULL;
		goto fail;
	}

	mac_link_update(ic->ic_mach, LINK_STATE_DOWN);

	sc->sc_watchdog = ddi_periodic_add(iwm_watchdog, sc, NANOSEC,
	    DDI_IPL_0);

	return (DDI_SUCCESS);

fail:
	/* No interrupt handler may run once net80211 is detached. */
	if (sc->sc_regh != NULL)
		IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);
	iwm_intr_teardown(sc);
	if (sc->sc_flags & IWM_FLAG_NET_ATTACHED)
		ieee80211_detach(ic);
	iwm_cleanup(sc);
	if (locks) {
		cv_destroy(&sc->sc_task_cv);
		cv_destroy(&sc->sc_txq_cv);
		cv_destroy(&sc->sc_fw_cv);
		cv_destroy(&sc->sc_cmd_cv);
		rw_destroy(&sc->sc_rwlock);
		mutex_destroy(&sc->sc_mtx);
	}
	ddi_set_driver_private(dip, NULL);
	ddi_soft_state_free(iwm_soft_state, instance);
	return (DDI_FAILURE);
}

static int
iwm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct iwm_softc *sc = ddi_get_driver_private(dip);
	struct ieee80211com *ic;

	if (sc == NULL)
		return (DDI_FAILURE);
	ic = &sc->sc_ic;

	switch (cmd) {
	case DDI_DETACH:
		break;
	case DDI_SUSPEND:
		rw_enter(&sc->sc_rwlock, RW_WRITER);
		if (sc->sc_flags & IWM_FLAG_RUNNING)
			iwm_stop(sc);
		mutex_enter(&sc->sc_mtx);
		sc->sc_flags |= IWM_FLAG_SUSPENDED;
		mutex_exit(&sc->sc_mtx);
		rw_exit(&sc->sc_rwlock);
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	if (mac_disable(ic->ic_mach) != 0)
		return (DDI_FAILURE);

	rw_enter(&sc->sc_rwlock, RW_WRITER);
	mutex_enter(&sc->sc_mtx);
	sc->sc_flags &= ~IWM_FLAG_UP;
	mutex_exit(&sc->sc_mtx);
	if (sc->sc_flags & IWM_FLAG_RUNNING)
		iwm_stop(sc);
	rw_exit(&sc->sc_rwlock);

	/* Wait for a pending restart; with UP clear it does nothing. */
	taskq_wait(sc->sc_initq);
	taskq_wait(sc->sc_nswq);

	/*
	 * On timeout the MAC stays disabled and the device stopped; a later
	 * detach retries (net80211 requires success before mac_unregister).
	 */
	if (ieee80211_wpa_quiesce(ic, ddi_get_lbolt() +
	    drv_usectohz(5000000)) != 0)
		return (DDI_FAILURE);

	/*
	 * Quiet the device and wait out any running interrupt handler
	 * before net80211 and the MAC handle go away: the handler delivers
	 * frames and calls mac_tx_update() after dropping sc_mtx.
	 */
	mutex_enter(&sc->sc_mtx);
	iwm_stop_device(sc);
	mutex_exit(&sc->sc_mtx);
	IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);
	iwm_intr_teardown(sc);

	ddi_remove_minor_node(dip, NULL);
	(void) mac_unregister(ic->ic_mach);
	ic->ic_mach = NULL;
	ieee80211_detach(ic);

	iwm_cleanup(sc);
	cv_destroy(&sc->sc_task_cv);
	cv_destroy(&sc->sc_txq_cv);
	cv_destroy(&sc->sc_fw_cv);
	cv_destroy(&sc->sc_cmd_cv);
	rw_destroy(&sc->sc_rwlock);
	mutex_destroy(&sc->sc_mtx);
	ddi_set_driver_private(dip, NULL);
	ddi_soft_state_free(iwm_soft_state, ddi_get_instance(dip));
	return (DDI_SUCCESS);
}

/*
 * quiesce(9E): stop all DMA and interrupts without blocking or locking,
 * for fast reboot.  A software reset halts the firmware and the DMA
 * engines.
 */
static int
iwm_quiesce(dev_info_t *dip)
{
	struct iwm_softc *sc = ddi_get_driver_private(dip);

	if (sc == NULL)
		return (DDI_FAILURE);
	if (sc->sc_regh == NULL)
		return (DDI_SUCCESS);
	IWM_WRITE(sc, IWM_CSR_INT_MASK, 0);
	IWM_WRITE(sc, IWM_CSR_INT, ~0);
	IWM_WRITE(sc, IWM_CSR_FH_INT_STATUS, ~0);
	IWM_SETBITS(sc, IWM_CSR_RESET, IWM_CSR_RESET_REG_FLAG_SW_RESET);
	return (DDI_SUCCESS);
}

DDI_DEFINE_STREAM_OPS(iwm_devops, nulldev, nulldev, iwm_attach, iwm_detach,
    nodev, NULL, D_MP, NULL, iwm_quiesce);

static struct modldrv iwm_modldrv = {
	&mod_driverops,
	"Intel Wireless 8260/8265 (iwm)",
	&iwm_devops
};

static struct modlinkage iwm_modlinkage = {
	MODREV_1,
	{ &iwm_modldrv, NULL }
};

int
_init(void)
{
	int err;

	err = ddi_soft_state_init(&iwm_soft_state, sizeof (struct iwm_softc),
	    1);
	if (err != 0)
		return (err);
	mac_init_ops(&iwm_devops, "iwm");
	err = mod_install(&iwm_modlinkage);
	if (err != 0) {
		mac_fini_ops(&iwm_devops);
		ddi_soft_state_fini(&iwm_soft_state);
	}
	return (err);
}

int
_fini(void)
{
	int err;

	err = mod_remove(&iwm_modlinkage);
	if (err == 0) {
		mac_fini_ops(&iwm_devops);
		ddi_soft_state_fini(&iwm_soft_state);
	}
	return (err);
}

int
_info(struct modinfo *mip)
{
	return (mod_info(&iwm_modlinkage, mip));
}
