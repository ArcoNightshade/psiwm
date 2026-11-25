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
 * Prototypes for the static functions of if_iwm.c, in definition order.
 * Regenerate with docs/tests/genproto.py after adding or changing one.
 */

#ifndef _IF_IWM_PROTO_H
#define	_IF_IWM_PROTO_H

#ifdef __cplusplus
extern "C" {
#endif

static uint8_t iwm_lookup_cmd_ver(struct iwm_softc *sc, uint8_t grp,
    uint8_t cmd);
static int iwm_is_mimo_ht_plcp(uint8_t ht_plcp);
static int iwm_is_mimo_ht_mcs(int mcs);
static int iwm_store_cscheme(struct iwm_softc *sc, uint8_t *data, size_t dlen);
static int iwm_firmware_store_section(struct iwm_softc *sc,
    enum iwm_ucode_type type, uint8_t *data, size_t dlen);
static int iwm_set_default_calib(struct iwm_softc *sc, const void *data);
static void iwm_fw_info_free(struct iwm_fw_info *fw);
static void iwm_fw_version_str(char *buf, size_t bufsize, uint32_t major,
    uint32_t minor, uint32_t api);
static int iwm_read_firmware(struct iwm_softc *sc);
static uint32_t iwm_read_prph_unlocked(struct iwm_softc *sc, uint32_t addr);
static uint32_t iwm_read_prph(struct iwm_softc *sc, uint32_t addr);
static void iwm_write_prph_unlocked(struct iwm_softc *sc, uint32_t addr,
    uint32_t val);
static void iwm_write_prph(struct iwm_softc *sc, uint32_t addr, uint32_t val);
static int iwm_read_mem(struct iwm_softc *sc, uint32_t addr, void *buf,
    int dwords);
static int iwm_write_mem(struct iwm_softc *sc, uint32_t addr, const void *buf,
    int dwords);
static int iwm_write_mem32(struct iwm_softc *sc, uint32_t addr, uint32_t val);
static int iwm_poll_bit(struct iwm_softc *sc, int reg, uint32_t bits,
    uint32_t mask, int timo);
static int iwm_nic_lock(struct iwm_softc *sc);
static void iwm_nic_assert_locked(struct iwm_softc *sc);
static void iwm_nic_unlock(struct iwm_softc *sc);
static int iwm_set_bits_mask_prph(struct iwm_softc *sc, uint32_t reg,
    uint32_t bits, uint32_t mask);
static int iwm_set_bits_prph(struct iwm_softc *sc, uint32_t reg, uint32_t bits);
static int iwm_clear_bits_prph(struct iwm_softc *sc, uint32_t reg,
    uint32_t bits);
static int iwm_sleep(struct iwm_softc *sc, kcondvar_t *cv, int usec);
static int iwm_loadfirmware(struct iwm_softc *sc, const char *name,
    void **datap, size_t *sizep);
static int iwm_dma_contig_alloc(struct iwm_softc *sc, struct iwm_dma_info *dma,
    size_t size, size_t alignment);
static void iwm_dma_contig_free(struct iwm_dma_info *dma);
static void iwm_dma_sync(struct iwm_dma_info *dma, uint_t type);
static int iwm_alloc_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring);
static void iwm_disable_rx_dma(struct iwm_softc *sc);
static void iwm_reset_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring);
static void iwm_free_rx_ring(struct iwm_softc *sc, struct iwm_rx_ring *ring);
static int iwm_alloc_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring,
    int qid);
static int iwm_alloc_txbufs(struct iwm_softc *sc, struct iwm_tx_ring *ring);
static void iwm_free_txbufs(struct iwm_softc *sc, struct iwm_tx_ring *ring);
static void iwm_txd_release(struct iwm_softc *sc, struct iwm_tx_data *txd);
static void iwm_reset_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring);
static void iwm_free_tx_ring(struct iwm_softc *sc, struct iwm_tx_ring *ring);
static void iwm_enable_rfkill_int(struct iwm_softc *sc);
static int iwm_check_rfkill(struct iwm_softc *sc);
static void iwm_enable_interrupts(struct iwm_softc *sc);
static void iwm_enable_fwload_interrupt(struct iwm_softc *sc);
static void iwm_restore_interrupts(struct iwm_softc *sc);
static void iwm_disable_interrupts(struct iwm_softc *sc);
static void iwm_ict_reset(struct iwm_softc *sc);
static int iwm_set_hw_ready(struct iwm_softc *sc);
static int iwm_prepare_card_hw(struct iwm_softc *sc);
static void iwm_apm_config(struct iwm_softc *sc);
static int iwm_apm_init(struct iwm_softc *sc);
static void iwm_apm_stop(struct iwm_softc *sc);
static void iwm_init_msix_hw(struct iwm_softc *sc);
static void iwm_conf_msix_hw(struct iwm_softc *sc, int stopped);
static int iwm_clear_persistence_bit(struct iwm_softc *sc);
static int iwm_start_hw(struct iwm_softc *sc);
static void iwm_stop_device(struct iwm_softc *sc);
static void iwm_nic_config(struct iwm_softc *sc);
static int iwm_nic_rx_init(struct iwm_softc *sc);
static int iwm_nic_rx_legacy_init(struct iwm_softc *sc);
static int iwm_nic_tx_init(struct iwm_softc *sc);
static int iwm_nic_init(struct iwm_softc *sc);
static int iwm_enable_ac_txq(struct iwm_softc *sc, int qid, int fifo);
static int iwm_enable_txq(struct iwm_softc *sc, int sta_id, int qid, int fifo,
    int aggregate, uint8_t tid, uint16_t ssn);
static int iwm_disable_txq(struct iwm_softc *sc, int sta_id, int qid,
    uint8_t tid);
static int iwm_post_alive(struct iwm_softc *sc);
static struct iwm_phy_db_entry *iwm_phy_db_get_section(struct iwm_softc *sc,
    uint16_t type, uint16_t chg_id);
static int iwm_phy_db_set_section(struct iwm_softc *sc,
    struct iwm_calib_res_notif_phy_db *phy_db_notif);
static int iwm_is_valid_channel(uint16_t ch_id);
static uint8_t iwm_ch_id_to_ch_index(uint16_t ch_id);
static uint16_t iwm_channel_id_to_papd(uint16_t ch_id);
static uint16_t iwm_channel_id_to_txp(struct iwm_softc *sc, uint16_t ch_id);
static int iwm_phy_db_get_section_data(struct iwm_softc *sc, uint32_t type,
    uint8_t **data, uint16_t *size, uint16_t ch_id);
static int iwm_send_phy_db_cmd(struct iwm_softc *sc, uint16_t type,
    uint16_t length, void *data);
static int iwm_phy_db_send_all_channel_groups(struct iwm_softc *sc,
    uint16_t type, uint8_t max_ch_groups);
static int iwm_send_phy_db_data(struct iwm_softc *sc);
static int iwm_send_time_event_cmd(struct iwm_softc *sc,
    const struct iwm_time_event_cmd *cmd);
static void iwm_protect_session(struct iwm_softc *sc, struct iwm_node *in,
    uint32_t duration, uint32_t max_delay);
static void iwm_unprotect_session(struct iwm_softc *sc, struct iwm_node *in);
static int iwm_nvm_read_chunk(struct iwm_softc *sc, uint16_t section,
    uint16_t offset, uint16_t length, uint8_t *data, uint16_t *len);
static int iwm_nvm_read_section(struct iwm_softc *sc, uint16_t section,
    uint8_t *data, uint16_t *len, size_t max_len);
static uint8_t iwm_fw_valid_tx_ant(struct iwm_softc *sc);
static uint8_t iwm_fw_valid_rx_ant(struct iwm_softc *sc);
static int iwm_valid_siso_ant_rate_mask(struct iwm_softc *sc);
static void iwm_init_channel_map(struct iwm_softc *sc,
    const uint16_t * const nvm_ch_flags, const uint8_t *nvm_channels,
    int nchan);
static int iwm_mimo_enabled(struct iwm_softc *sc);
static void iwm_setup_ht_rates(struct iwm_softc *sc,
    struct ieee80211_htrateset *rs);
static int iwm_sta_rx_agg(struct iwm_softc *sc, struct ieee80211_node *ni,
    uint8_t tid, uint16_t ssn, uint16_t winsize, int timeout_val, int start);
static void iwm_delba(struct iwm_softc *sc, struct ieee80211_node *ni,
    uint8_t tid);
static void iwm_mac_ctxt_task(void *arg);
static void iwm_updateslot(struct ieee80211com *ic, int onoff);
static int iwm_updateedca(struct ieee80211com *ic);
static int iwm_sta_tx_agg(struct iwm_softc *sc, struct ieee80211_node *ni,
    uint8_t tid, uint16_t ssn, uint16_t winsize, int start);
static void iwm_ampdu_tx_refuse(struct iwm_softc *sc, struct ieee80211_node *ni,
    uint8_t tid);
static void iwm_ba_task(void *arg);
static void iwm_recv_action(struct ieee80211_node *ni, const uint8_t *frm,
    const uint8_t *efrm);
static int iwm_addba_response(struct ieee80211_node *ni,
    struct ieee80211_tx_ampdu *tap, int status, int baparamset, int batimeout);
static void iwm_addba_stop(struct ieee80211_node *ni,
    struct ieee80211_tx_ampdu *tap);
static void iwm_set_hw_address_8000(struct iwm_softc *sc,
    struct iwm_nvm_data *data, const uint16_t *mac_override,
    const uint16_t *nvm_hw);
static int iwm_parse_nvm_data(struct iwm_softc *sc, const uint16_t *nvm_hw,
    const uint16_t *nvm_sw, const uint16_t *nvm_calib,
    const uint16_t *mac_override, const uint16_t *phy_sku,
    const uint16_t *regulatory, int n_regulatory);
static int iwm_parse_nvm_sections(struct iwm_softc *sc,
    struct iwm_nvm_section *sections);
static int iwm_nvm_init(struct iwm_softc *sc);
static int iwm_firmware_load_sect(struct iwm_softc *sc, uint32_t dst_addr,
    const uint8_t *section, uint32_t byte_cnt);
static int iwm_firmware_load_chunk(struct iwm_softc *sc, uint32_t dst_addr,
    const uint8_t *chunk, uint32_t byte_cnt);
static int iwm_load_firmware_7000(struct iwm_softc *sc,
    enum iwm_ucode_type ucode_type);
static int iwm_load_cpu_sections_8000(struct iwm_softc *sc,
    struct iwm_fw_sects *fws, int cpu, int *first_ucode_section);
static int iwm_load_firmware_8000(struct iwm_softc *sc,
    enum iwm_ucode_type ucode_type);
static int iwm_load_firmware(struct iwm_softc *sc,
    enum iwm_ucode_type ucode_type);
static int iwm_start_fw(struct iwm_softc *sc, enum iwm_ucode_type ucode_type);
static int iwm_send_tx_ant_cfg(struct iwm_softc *sc, uint8_t valid_tx_ant);
static int iwm_send_phy_cfg_cmd(struct iwm_softc *sc);
static int iwm_send_dqa_cmd(struct iwm_softc *sc);
static int iwm_load_ucode_wait_alive(struct iwm_softc *sc,
    enum iwm_ucode_type ucode_type);
static int iwm_run_init_mvm_ucode(struct iwm_softc *sc, int justnvm);
static int iwm_config_ltr(struct iwm_softc *sc);
static int iwm_rx_addbuf(struct iwm_softc *sc, int size, int idx);
static int iwm_get_signal_strength(struct iwm_softc *sc,
    struct iwm_rx_phy_info *phy_info);
static void iwm_rx_rx_phy_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data);
static int iwm_get_noise(const struct iwm_statistics_rx_non_phy *stats);
static int iwm_rx_hwdecrypt(struct iwm_softc *sc, mblk_t *m,
    uint32_t rx_pkt_status);
static void iwm_rx_frame(struct iwm_softc *sc, mblk_t *m, int rssi,
    uint32_t tstamp);
static void iwm_rx_deliver(struct iwm_softc *sc, mblk_t *m);
static void iwm_rx_mpdu(struct iwm_softc *sc, void *pktdata, size_t maxlen);
static void iwm_ra_peer(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_ra_peer *p);
static void iwm_ra_choose(struct iwm_softc *sc, struct ieee80211_node *ni);
static void iwm_ra_add_stats(struct iwm_softc *sc, struct iwm_node *in, int mcs,
    uint32_t total, uint32_t fail);
static void iwm_ht_single_rate_control(struct iwm_softc *sc,
    struct ieee80211_node *ni, int txmcs, uint8_t failure_frame, int txfail);
static void iwm_rx_tx_cmd_single(struct iwm_softc *sc,
    struct iwm_rx_packet *pkt, struct iwm_node *in, int txmcs, int txrate);
static void iwm_txd_done(struct iwm_softc *sc, struct iwm_tx_data *txd);
static void iwm_txq_advance(struct iwm_softc *sc, struct iwm_tx_ring *ring,
    int idx);
static void iwm_ba_move_window(struct iwm_softc *sc, struct ieee80211_node *ni,
    int tid, uint16_t ssn);
static void iwm_ampdu_tx_done(struct iwm_softc *sc,
    struct iwm_cmd_header *cmd_hdr, struct iwm_node *in,
    struct iwm_tx_ring *txq, uint32_t initial_rate, uint8_t nframes,
    uint8_t failure_frame, uint16_t ssn, int status,
    struct iwm_agg_tx_status *agg_status);
static void iwm_rx_tx_cmd(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data);
static void iwm_clear_oactive(struct iwm_softc *sc, struct iwm_tx_ring *ring);
static void iwm_ampdu_rate_control(struct iwm_softc *sc,
    struct ieee80211_node *ni, struct iwm_tx_ring *txq, int tid, uint16_t seq,
    uint16_t ssn);
static void iwm_rx_compressed_ba(struct iwm_softc *sc,
    struct iwm_rx_packet *pkt);
static void iwm_rx_bmiss(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    struct iwm_rx_data *data);
static int iwm_binding_cmd(struct iwm_softc *sc, struct iwm_node *in,
    uint32_t action);
static void iwm_phy_ctxt_cmd_hdr(struct iwm_softc *sc,
    struct iwm_phy_ctxt *ctxt, struct iwm_phy_context_cmd *cmd, uint32_t action,
    uint32_t apply_time);
static void iwm_phy_ctxt_cmd_data(struct iwm_softc *sc,
    struct iwm_phy_context_cmd *cmd, struct ieee80211_channel *chan,
    uint8_t chains_static, uint8_t chains_dynamic, uint8_t sco,
    uint8_t vht_chan_width);
static int iwm_phy_ctxt_cmd_uhb(struct iwm_softc *sc, struct iwm_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action,
    uint32_t apply_time, uint8_t sco, uint8_t vht_chan_width);
static int iwm_phy_ctxt_cmd(struct iwm_softc *sc, struct iwm_phy_ctxt *ctxt,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t action,
    uint32_t apply_time, uint8_t sco, uint8_t vht_chan_width);
static int iwm_send_cmd(struct iwm_softc *sc, struct iwm_host_cmd *hcmd);
static int iwm_send_cmd_pdu(struct iwm_softc *sc, uint32_t id, uint32_t flags,
    uint16_t len, const void *data);
static int iwm_send_cmd_status(struct iwm_softc *sc, struct iwm_host_cmd *cmd,
    uint32_t *status);
static int iwm_send_cmd_pdu_status(struct iwm_softc *sc, uint32_t id,
    uint16_t len, const void *data, uint32_t *status);
static void iwm_free_resp(struct iwm_softc *sc, struct iwm_host_cmd *hcmd);
static void iwm_cmd_done(struct iwm_softc *sc, int qid, int idx, int code);
static void iwm_update_sched(struct iwm_softc *sc, int qid, int idx,
    uint8_t sta_id, uint16_t len);
static void iwm_reset_sched(struct iwm_softc *sc, int qid, int idx,
    uint8_t sta_id);
static int iwm_min_basic_rate(struct ieee80211com *ic);
static uint8_t iwm_tx_fill_cmd(struct iwm_softc *sc, struct iwm_node *in,
    struct ieee80211_frame *wh, struct iwm_tx_cmd *tx);
static int iwm_tx(struct iwm_softc *sc, mblk_t *m, struct ieee80211_node *ni,
    int ac);
static int iwm_flush_tx_path(struct iwm_softc *sc, int tfd_queue_msk);
static int iwm_wait_tx_queues_empty(struct iwm_softc *sc);
static void iwm_led_enable(struct iwm_softc *sc);
static void iwm_led_disable(struct iwm_softc *sc);
static int iwm_led_is_enabled(struct iwm_softc *sc);
static void iwm_led_blink_timeout(void *arg);
static void iwm_led_blink_start(struct iwm_softc *sc);
static void iwm_led_blink_stop(struct iwm_softc *sc);
static int iwm_beacon_filter_send_cmd(struct iwm_softc *sc,
    struct iwm_beacon_filter_cmd *cmd);
static void iwm_beacon_filter_set_cqm_params(struct iwm_softc *sc,
    struct iwm_node *in, struct iwm_beacon_filter_cmd *cmd);
static int iwm_update_beacon_abort(struct iwm_softc *sc, struct iwm_node *in,
    int enable);
static void iwm_power_build_cmd(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_power_cmd *cmd);
static int iwm_power_mac_update_mode(struct iwm_softc *sc, struct iwm_node *in);
static int iwm_power_update_device(struct iwm_softc *sc);
static int iwm_disable_beacon_filter(struct iwm_softc *sc);
static int iwm_add_sta_cmd(struct iwm_softc *sc, struct iwm_node *in,
    int update);
static uint8_t iwm_node_sco(struct ieee80211_node *ni);
static int iwm_add_aux_sta(struct iwm_softc *sc);
static int iwm_drain_sta(struct iwm_softc *sc, struct iwm_node *in, int drain);
static int iwm_flush_sta(struct iwm_softc *sc, struct iwm_node *in);
static int iwm_rm_sta_cmd(struct iwm_softc *sc, struct iwm_node *in);
static uint16_t iwm_scan_rx_chain(struct iwm_softc *sc);
static uint32_t iwm_scan_rate_n_flags(struct iwm_softc *sc, int flags,
    int no_cck);
static uint8_t iwm_lmac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_lmac *chan, int n_ssids, int bgscan);
static uint8_t iwm_umac_scan_fill_channels(struct iwm_softc *sc,
    struct iwm_scan_channel_cfg_umac *chan, int n_ssids, int bgscan);
static int iwm_fill_probe_req_v1(struct iwm_softc *sc,
    struct iwm_scan_probe_req_v1 *preq1);
static uint8_t *iwm_add_rates(uint8_t *frm, const struct ieee80211_rateset *rs);
static uint8_t *iwm_add_xrates(uint8_t *frm,
    const struct ieee80211_rateset *rs);
static int iwm_fill_probe_req(struct iwm_softc *sc,
    struct iwm_scan_probe_req *preq);
static int iwm_lmac_scan(struct iwm_softc *sc, int bgscan);
static int iwm_config_umac_scan(struct iwm_softc *sc);
static int iwm_umac_scan_size(struct iwm_softc *sc);
static struct iwm_scan_umac_chan_param *iwm_get_scan_req_umac_chan_param(
    struct iwm_softc *sc, struct iwm_scan_req_umac *req);
static void *iwm_get_scan_req_umac_data(struct iwm_softc *sc,
    struct iwm_scan_req_umac *req);
static int iwm_umac_scan(struct iwm_softc *sc, int bgscan);
static void iwm_mcc_update(struct iwm_softc *sc,
    struct iwm_mcc_chub_notif *notif);
static uint8_t iwm_ridx2rate(struct ieee80211_rateset *rs, int ridx);
static int iwm_rval2ridx(int rval);
static void iwm_ack_rates(struct iwm_softc *sc, struct iwm_node *in,
    int *cck_rates, int *ofdm_rates);
static void iwm_mac_ctxt_cmd_common(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_ctx_cmd *cmd, uint32_t action);
static void iwm_mac_ctxt_cmd_fill_sta(struct iwm_softc *sc, struct iwm_node *in,
    struct iwm_mac_data_sta *sta, int assoc);
static int iwm_mac_ctxt_cmd(struct iwm_softc *sc, struct iwm_node *in,
    uint32_t action, int assoc);
static int iwm_update_quotas(struct iwm_softc *sc, struct iwm_node *in,
    int running);
static void iwm_add_task(struct iwm_softc *sc, taskq_t *tq, uint32_t task);
static void iwm_task_done(struct iwm_softc *sc, uint32_t task);
static void iwm_defer_task(void *arg);
static int iwm_scan(struct iwm_softc *sc);
static int iwm_umac_scan_abort(struct iwm_softc *sc);
static int iwm_lmac_scan_abort(struct iwm_softc *sc);
static int iwm_scan_abort(struct iwm_softc *sc);
static int iwm_phy_ctxt_update(struct iwm_softc *sc,
    struct iwm_phy_ctxt *phyctxt, struct ieee80211_channel *chan,
    uint8_t chains_static, uint8_t chains_dynamic, uint32_t apply_time,
    uint8_t sco, uint8_t vht_chan_width);
static int iwm_auth(struct iwm_softc *sc);
static int iwm_deauth(struct iwm_softc *sc);
static int iwm_run(struct iwm_softc *sc);
static int iwm_run_stop(struct iwm_softc *sc);
static struct ieee80211_channel *iwm_default_chan(struct iwm_softc *sc);
static struct ieee80211_node *iwm_node_alloc(struct ieee80211com *ic);
static void iwm_node_free(struct ieee80211_node *ni);
static int iwm_set_key(struct ieee80211com *ic, const struct ieee80211_key *ck,
    const uint8_t mac[IEEE80211_ADDR_LEN]);
static int iwm_delete_key(struct ieee80211com *ic,
    const struct ieee80211_key *k);
static void iwm_calib_timeout(void *arg);
static void iwm_calib_stop(struct iwm_softc *sc);
static int iwm_peer_has_mcs(struct ieee80211_node *ni, int mcs);
static void iwm_set_rate_table(struct iwm_node *in, struct iwm_lq_cmd *lqcmd);
static void iwm_setrates(struct iwm_node *in, int async);
static void iwm_newstate_task(void *psc);
static int iwm_newstate(struct ieee80211com *ic, enum ieee80211_state nstate,
    int arg);
static void iwm_endscan(struct iwm_softc *sc);
static void iwm_fill_sf_command(struct iwm_softc *sc,
    struct iwm_sf_cfg_cmd *sf_cmd, struct ieee80211_node *ni);
static int iwm_sf_config(struct iwm_softc *sc, int new_state);
static int iwm_send_bt_init_conf(struct iwm_softc *sc);
static int iwm_send_soc_conf(struct iwm_softc *sc);
static int iwm_send_update_mcc_cmd(struct iwm_softc *sc, const char *alpha2);
static int iwm_send_temp_report_ths_cmd(struct iwm_softc *sc);
static void iwm_tt_tx_backoff(struct iwm_softc *sc, uint32_t backoff);
static void iwm_free_fw_paging(struct iwm_softc *sc);
static int iwm_fill_paging_mem(struct iwm_softc *sc,
    const struct iwm_fw_sects *image);
static int iwm_alloc_fw_paging_mem(struct iwm_softc *sc,
    const struct iwm_fw_sects *image);
static int iwm_save_fw_paging(struct iwm_softc *sc,
    const struct iwm_fw_sects *fw);
static int iwm_send_paging_cmd(struct iwm_softc *sc,
    const struct iwm_fw_sects *fw);
static int iwm_init_hw(struct iwm_softc *sc);
static int iwm_allow_mcast(struct iwm_softc *sc);
static int iwm_init(struct iwm_softc *sc);
static void iwm_stop(struct iwm_softc *sc);
static void iwm_watchdog(void *arg);
static void iwm_ic_watchdog(void *arg);
static void iwm_nic_umac_error(struct iwm_softc *sc);
static const char *iwm_desc_lookup(uint32_t num);
static void iwm_nic_error(struct iwm_softc *sc);
static void iwm_dump_driver_status(struct iwm_softc *sc);
static int iwm_rx_pkt_valid(struct iwm_rx_packet *pkt);
static void iwm_rx_pkt(struct iwm_softc *sc, struct iwm_rx_data *data);
static void iwm_notif_intr(struct iwm_softc *sc);
static uint_t iwm_intr(caddr_t arg, caddr_t unused);
static int iwm_preinit(struct iwm_softc *sc);
static void iwm_init_task(void *arg);
static int iwm_send(struct ieee80211com *ic, mblk_t *mp, uint8_t type);
static int iwm_m_start(void *arg);
static void iwm_m_stop(void *arg);
static mblk_t *iwm_m_tx(void *arg, mblk_t *mp);
static void iwm_netreset(struct iwm_softc *sc);
static void iwm_m_ioctl(void *arg, queue_t *wq, mblk_t *mp);
static int iwm_m_getprop(void *arg, const char *pr_name,
    mac_prop_id_t wldp_pr_num, uint_t wldp_length, void *wldp_buf);
static void iwm_m_propinfo(void *arg, const char *pr_name,
    mac_prop_id_t wldp_pr_num, mac_prop_info_handle_t prh);
static int iwm_m_setprop(void *arg, const char *pr_name,
    mac_prop_id_t wldp_pr_num, uint_t wldp_length, const void *wldp_buf);
static int iwm_m_stat(void *arg, uint_t stat, uint64_t *val);
static int iwm_m_unicst(void *arg, const uint8_t *macaddr);
static int iwm_m_multicst(void *arg, boolean_t add, const uint8_t *m);
static int iwm_m_promisc(void *arg, boolean_t on);
static void iwm_intr_teardown(struct iwm_softc *sc);
static int iwm_intr_add(struct iwm_softc *sc, int type);
static int iwm_intr_setup(struct iwm_softc *sc);
static int iwm_intr_enable(struct iwm_softc *sc);
static int iwm_pci_get_capability(ddi_acc_handle_t pcih, int cap, int *cap_off);
static int iwm_read_hw_rev(struct iwm_softc *sc);
static void iwm_phy_db_entry_free(struct iwm_phy_db_entry *entry);
static void iwm_phy_db_free(struct iwm_softc *sc);
static void iwm_cleanup(struct iwm_softc *sc);
static int iwm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int iwm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int iwm_quiesce(dev_info_t *dip);

#ifdef __cplusplus
}
#endif

#endif /* _IF_IWM_PROTO_H */
