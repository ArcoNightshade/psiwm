# iwm audit against OpenBSD / FreeBSD (Stage 0)

Method: read each driver area side by side with OpenBSD if_iwm.c (v1.419). Status values:
MATCH (equivalent to reference), DEVIATION (intentional or harmless), FINDING (defect or
missing feature; ID used in roadmap), TODO (not yet audited). Hardware effects of FINDINGs
are inferred from code and the references; none are confirmed on hardware yet.

## Audited areas

| Area | Ours | Reference | Status |
|---|---|---|---|
| APM init (`iwm_apm`) | if_iwmhw.c iwm_apm | iwm_apm_init | MATCH for family 8000 (no 7000-only PLL/OSC/APMG steps needed). |
| NIC config / HW_IF_CONFIG | iwm_transport_init | iwm_nic_config | MATCH. |
| RX init | iwm_transport_init | iwm_nic_rx_legacy_init | MATCH (same register sequence, WPTR=8). |
| TX init / keep-warm / SCD | iwm_transport_init | iwm_nic_tx_init | MATCH. |
| Post-ALIVE | iwm_post_alive | iwm_post_alive | DEVIATION: no ICT (interrupt cause table); direct CSR_INT read (D1). |
| FW upload, 8000 CPU1/CPU2 secure sections | iwm_upload | iwm_load_firmware_8000 / iwm_load_cpu_sections_8000 / iwm_firmware_load_chunk | MATCH (section status bits, separators, extended-address CHICK, 0xffff/0xffffffff finalize). Ours also checks queue/scheduler state after ALIVE. |
| PHY context cmd | iwm_association_phy | iwm_phy_ctxt_cmd_* | MATCH for 2.4 GHz HT20 single chain; band/width hardcoded (F9). |
| MAC context cmd | iwm_association_mac | iwm_mac_ctxt_cmd_* | MATCH on flag values (short preamble 1<<5, short slot 1<<4, TGG prot 1<<3, HT prot bits). DEVIATION: filter adds IN_BEACON (1<<6); DIS_DECRYPT bits for software CCMP. Raw numeric constants should become named (F10). |
| WME to FIFO mapping | iwm_wme_encode | iwm_ac_to_tx_fifo | MATCH (BE->1, BK->0, VI->2, VO->3, txop*32). |
| TX command | iwm_association_tx | iwm_tx / iwm_tx_fill_cmd | See F2, F3, F4, F5. |
| Interrupt and error handling | iwm_active_intr | iwm_intr | See F1. |

## Findings

| ID | Severity | Finding | Reference behaviour | Roadmap stage |
|---|---|---|---|---|
| F1 | High | HW_ERR/SW_ERR interrupt sets `r->error = EIO` and masks interrupts forever; no firmware error-table dump; RF_KILL not in `IWM_RUN_MASK` so RF-kill state changes are never seen. Any unrecognised cause bit (`causes & ~IWM_RUN_MASK`) also kills the device. | OpenBSD: RF_KILL -> `iwm_check_rfkill` + init_task restart; SW_ERR/HW_ERR -> log, optional `iwm_nic_error`/`iwm_nic_umac_error` dump, restart via init_task. Also rejects 0xa5a5a5a0 pattern as dead-device read. | 3 |
| F2 | High | Data TX always uses a fixed rate: legacy frames use the lowest shared basic rate; HT data uses MCS0 on one antenna. LQ command fills all 16 slots with one rate. Throughput is capped around 1-6 Mbit/s. | OpenBSD sets firmware rate table (LQ cmd) and drives rate selection from TX status (`iwm_ra`/MiRA); uses both valid antennas. | 4 (raise priority: needed for "functional") |
| F3 | Medium | TX command sets ACK flag (1<<3) unconditionally, including multicast/broadcast. | OpenBSD sets `IWM_TX_CMD_FLG_ACK` only for unicast. Group frames with ACK expected get retried by firmware. Check Linux `iwl_mvm_set_tx_cmd` too. | 2 |
| F4 | Low | `data_retry_limit` is 15 for data; OpenBSD uses IWM_LOW_RETRY_LIMIT for data and a larger mgmt limit. Verify values against reference headers. | iwm_tx_fill_cmd | 2 |
| F5 | Medium | Crypto is software CCMP via KCF in the TX/RX path. OpenBSD uses hardware CCMP for pairwise keys (key in TX cmd, `IWM_TX_CMD_SEC_CCM`, ADD_STA_KEY) and software only for group keys. | Performance and latency; also a larger attack surface in driver code. | 4 |
| F6 | Medium | Connection is run by a single taskq thread that blocks for the entire connection lifetime and delivers RX frames from the same thread. | OpenBSD handles RX in the interrupt/softnet path and state changes in short tasks. | 2 |
| F7 | Medium | No beacon-loss/missed-beacon handling, no bgscan/roaming, no statistics-driven RSSI/noise. | OpenBSD handles IWM_MISSED_BEACONS_NOTIFICATION, bgscan, calibration/statistics. | 3/4 |
| F8 | Medium | No SUSPEND/RESUME, no rfkill state in link status. | OpenBSD/FreeBSD restart device on resume via init path. | 3 |
| F9 | Low | PHY ctx fixed to 2.4 GHz/20 MHz/one chain (rxchain counts). | OpenBSD computes band, ctrl_pos, width, and static/dynamic chain counts. Needed for 5 GHz/HT40/MIMO. | 4 |
| F10 | Low | Many raw command IDs and flags (0x08, 0x28, 0x1c, 0x4e, 0xd0, 0x77, 0xa9, `(1U << 3)` etc.) rather than named constants. Maintainability risk and makes audit harder. | Named IWM_* constants in if_iwmreg.h. | 2 |
| F11 | Info | Device identity gate admits only 8086:24f3, subsystem 1130, rev 3a. | OpenBSD admits by product ID and selects firmware per product. | 1/6 |

Deviations:
- D1: ICT not used (OpenBSD and FreeBSD both use ICT in non-MSI-X mode). Direct CSR_INT
  read is valid for MSI but must be verified under interrupt coalescing (OpenBSD ICT path
  has a workaround for falsely cleared bits 18/19). Needs hardware test.

## Remaining audit queue (TODO)

1. NVM read, NVM parse (HW address, SKU, channel flags, LAR/MCC) vs iwm_nvm_init / iwm_parse_nvm_data.
2. TLV firmware parse (`if_iwmfw.c`) vs iwm_read_firmware (capabilities, API flags, section limits).
3. INIT firmware sequence, calibration (PHY_DB, CALIB_RES), PHY_CONFIG_CMD, tx antenna, BT coex, SF config.
4. Scan: UMAC scan request layout, dwell values, channel config, probe request building vs iwm_umac_scan.
5. Binding, ADD_STA, station removal, time events (session protection), power/MAC_PM and device power commands.
6. RX path: notification dispatch, RX descriptor and RSSI, RB replenish, closed_rb_num handling, RX buffer ownership.
7. TX completion: TX_CMD response status handling, queue reclaim, scheduler byte-count tables, queue flush on stop.
8. Aggregation (ADDBA/BA notification) vs iwm_ampdu_*.
9. Stop/teardown order (`iwm_device_stop`) vs iwm_stop_device including FH/SCD/APM stop and RF-kill handshake.
10. FreeBSD cross-check of everything above (if_iwm.c, if_iwm_pcie_trans.c, if_iwm_scan.c).
11. Linux iwlwifi/mvm cross-check for items where the BSD drivers differ.
12. Known bugs and errata: OpenBSD and FreeBSD commit logs since the donor commit; bug trackers.
