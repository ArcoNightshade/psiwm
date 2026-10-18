# iwm (Intel 8000-series Wi-Fi) Roadmap

Target: functional, stable Wi-Fi on illumos (OmniOS) for the Intel 8000 series
cards, via the existing `iwm` driver in this tree.

Status of this document: written from a read of the repo only. Comparison against the
reference drivers (OpenBSD `if_iwm.c`, FreeBSD `if_iwm*`, Linux `iwlwifi/mvm`) has NOT
been done yet; items marked [VERIFY] are suspicions to be checked in Stage 0.

## 0. Scope and reference policy

Scope: the whole Intel 8000 series handled by the `iwm` driver family: 8260 (8086:24f3),
8265 (8086:24fd), 8275 and related SKUs, with per-SKU firmware (`iwm-8000C-36`,
`iwm-8265-36`). Other iwm-supported families (7260/7265/3160/3165/3168, 9000) are out of
scope unless added later as a post-Stage 6 extension; keep the cfg table family-aware so that
is possible.

Reference policy (applies to every stage):
- OpenBSD `sys/dev/pci/if_iwm.c` (and `if_iwmreg.h`, `if_iwmvar.h`) and FreeBSD
  `sys/dev/iwm/` are the primary references. Take heavy inspiration from them: when in doubt,
  match their command sequences, field values, timing, and workarounds, and check their commit
  logs and bug trackers for fixes newer than our donor snapshot.
- Linux iwlwifi/mvm is the tiebreaker for hardware behaviour the BSD drivers leave
  undocumented.
- Deviations from the BSD drivers must be justified in `docs/audit.md`.

## 1. Repository map

| Path (under `usr/src/`) | Purpose |
|---|---|
| `uts/common/io/iwm/if_iwm.c` (1.8k lines) | DDI glue: attach/detach/quiesce, PCI/BAR map, MSI, DMA helpers, GLDv3 (`mac`) callbacks, WiFi ioctls/props, public registration |
| `uts/common/io/iwm/if_iwmhw.c` (6.6k) | Everything else: NIC/APM/prph access, ring setup, firmware upload, INIT/REGULAR ucode lifecycle, NVM/LAR, scan, association commands (PHY ctx, MAC ctx, binding, STA, queues), TX/RX, CCMP, A-MPDU/BA, WME, interrupt processing, connection worker |
| `if_iwmfw.c/.h` | Firmware TLV parser |
| `if_iwmreg.h`, `if_iwmvar.h` | Register/command definitions (from OpenBSD), softc and ring structs |
| `fw-iw/iwm-8000C-36` | Firmware blob (8000C API 36 only) |
| `uts/intel/iwm/Makefile`, `uts/common/Makefile.{files,rules}`, `uts/intel/Makefile.intel` | Build wiring |
| `pkg/manifests/driver-network-iwm.p5m` | Package; currently has NO `driver` action/alias |
| `uts/common/io/net80211/*`, `sys/net80211.h` | Modified shared net80211 (HT, A-MSDU, input); touches other wifi drivers' behaviour |
| `cmd/cmd-inet/usr.lib/wpad/wpa.c` | wpad patched to disassociate on key-install failure |

The rest of the tree (~all of illumos/OmniOS) is unrelated baseline.

## 2. Current state (from code)

Working or implemented (untested on hardware by me; commit history claims it):
- Passive attach, INIT firmware, NVM parse, calibration, REGULAR firmware lifecycle.
- Passive and persistent MAC scanning feeding net80211 scan table.
- Open BSS and WPA2-PSK (native software CCMP; wpad performs 4-way handshake).
- HT20, TX A-MPDU for TID0 only, RX A-MSDU, WME.
- Heavy defensive style: validation of every notification, ownership/cleanup tracking.

Gaps and limitations found:
1. Hardware gate: `iwm_pci_map` rejects everything except 8086:24f3, subsystem 1130,
   revision 3a. Only one firmware (`iwm-8000C-36`) is packaged. No other 8000-family SKU
   (8265 needs `iwm-8265-36`) will attach.
2. Not installable: no `driver_aliases` entry, no `driver` action in the p5m, description says
   "wireless operation unavailable". Module name is not in `name_to_major`.
3. Opt-in only: connect support is off unless the `iwm-public-scan` driver property is set;
   default attach is passive. INIT/NVM and full-init are further properties
   (`iwm-init-nvm`, `iwm-full-init`). Debug property `iwm-attach-fail` is in production code.
4. Radio: 2.4 GHz only (11b/g), HT20, single spatial stream, no 5 GHz, no HT40, no VHT.
5. TX rate: fixed rate; the LQ command fills all 16 slots with one rate. No rate adaptation
   (iwm_rs/`rs_table` driven by TX status). Throughput will be poor and link quality fragile.
6. Error recovery: HW/SW error interrupts set `r->error` and mask interrupts permanently.
   No firmware-assert dump, no automatic restart, no RF-kill / CT-kill handling visible [VERIFY].
7. Power: station power management deliberately disabled ("do not enable station PM");
   no D3/suspend/resume (`iwm_detach` refuses everything but DETACH; no DDI_SUSPEND).
8. Connection model: one taskq thread is held for the entire connection lifetime, polling the
   RUN loop with `cv_timedwait`, and delivers RX from it. Lifecycle rollback code is large and
   hard to reason about; scalability and latency are suspect.
9. Missing mac callbacks: `m_stat` returns zeros (no real counters), `m_promisc` ENOTSUP,
   `m_multicst` is a no-op (no firmware multicast filter programming) [VERIFY], no
   `mc_getcapab`, no link speed reporting beyond 0.
10. Security: WPA2-PSK/CCMP only. No TKIP, WEP, WPA3/SAE, PMF (required PMF explicitly rejected),
    and the pairwise "none" cases return ENOTSUP. Software CCMP means no hardware key offload
    (`STA_KEY`) - correct but slow.
11. No background scanning, roaming, beacon-loss handling, or DTIM/keepalive tuning beyond one
    static power command.
12. Regulatory: LAR (MCC) path exists; channel/country policy unvalidated.
13. Tooling gaps: no man page, no kstats/DTrace SDT probes, no test plan, no cstyle/lint/hdrchk
    run evidence, `/* BEGIN CSTYLED */` on donor headers.
14. Process: commit message "SMACKDOWNgit status!" and a stray `.git-bir` file indicate cleanup
    needed before upstreaming. Shared net80211 changes affect all in-tree wifi drivers and need
    regression testing (iwn, iwh, ath, ral, etc.).

## 3. Stages

Each stage ends with: documentation updated (this file and `docs/` notes), cstyle/hdrchk/lint
clean, a recorded hardware test log, and a commit.

### Stage 0 - Audit against references (no behaviour change)
- Obtain reference sources (OpenBSD and FreeBSD first, per section 0): OpenBSD `sys/dev/pci/if_iwm.c`, `if_iwmreg.h`, `if_iwmvar.h`
  (the stated donor, pinned commit `0efabb06...`); FreeBSD `sys/dev/iwm/`; Linux
  `drivers/net/wireless/intel/iwlwifi/{pcie,mvm,fw}`. Record exact versions in
  `docs/references.md`.
- Diff our command structs, flags and sequences (ALIVE, post-alive, NVM access, PHY ctx, MAC
  ctx, binding, STA add, scan, TX cmd, LQ cmd, power, SF, BT coex) field by field against the
  donors. Produce `docs/audit.md` listing every deviation and justification.
- Search upstream errata and known bugs for 8260/8265 (firmware API 36 notifications, 8265 UMAC
  scan, LAR/MCC issues, PCIe ASPM/L1 and `CSR_GP_CNTRL` workarounds, 8000-family
  `IWM_CSR_HW_REV` step handling, rfkill/`OSC` quirks, firmware assert handling) and check each.
- Review items [VERIFY]: ring pointer/write-pointer updates and `wake`/nic-lock discipline,
  scheduler byte-count table updates, RX buffer replenish and `closed_rb_num`, DMA 36-bit
  limits, MSI vs legacy interrupt fallback, TX frame reclaim on firmware flush.
- Build a reproducible build + lint recipe for this tree (illumos-gate flow:
  `nightly`/`dmake` for `uts/intel/iwm`, `cstyle -pP`, `hdrchk`) and run it. Record results.

### Stage 1 - Make it installable and default-on for the one known card
- Add `driver_aliases` (`iwm "pciex8086,24f3"`), `name_to_major`, `driver_classes` if needed,
  p5m `driver` action; fix pkg summary/description; add copyright/exception-list entries.
- Remove `iwm-public-scan`/`iwm-init-nvm` gating: attach performs bounded INIT+NVM and registers
  the WiFi MAC by default. Keep one documented debug property set; drop `iwm-attach-fail` from
  non-DEBUG builds.
- Relax the identity gate to vendor/device only (subsystem/revision become cfg-table lookups
  with per-SKU fixups, as in OpenBSD's `iwm_devices`/`iwm_cfg` tables).
- Verify: `modload`, attach, `dladm show-wifi`, `dladm scan-wifi`, clean `modunload`,
  attach/detach loops, `pkg` install on a test boot environment.

### Stage 2 - Reliable open and WPA2 connection (baseline "it works")
- Hardware-test open and WPA2-PSK (CCMP) on 2.4 GHz: connect, DHCP, ping, iperf, 24 h soak.
- Fix defects found in Stage 0 audit and testing. Prioritise: TX/RX ring correctness, command
  timeouts, leaks (kmem/DMA, `::findleaks`, `::kmastat`), lock ordering (`crypto_lock ->
  ic_genlock -> lock`), `DDI_DEFER`/quiesce safety.
- Replace per-connection permanently blocked worker with an event-driven state machine (or at
  least bound its blocking and move RX delivery off the control path) once behaviour is pinned
  by tests.
- Implement real `m_stat` counters (packets, bytes, errors, link speed), `m_multicst`
  with firmware multicast filter, `m_promisc` if firmware supports it, link up/down reporting.
- Add `mc_getcapab` as needed and `dladm` property parity with iwn/ath.

### Stage 3 - Robustness and recovery
- Firmware error path: read and log error table / assert log, stop cleanly, restart REGULAR image
  and re-associate automatically (OpenBSD `iwm_nic_error` / restart flow). Rate-limit restarts.
- RF-kill (HW switch via CSR_GP_CNTRL) and CT-kill handling; report via link state.
- Beacon loss / missed beacons notification, disassociation, reconnection to same ESSID.
- DDI_SUSPEND/RESUME: save state, stop firmware, restore on resume (reassociate).
- Fault injection: keep the `iwm-attach-fail` style hooks only in DEBUG builds; add tests for
  command timeout, DMA failure and interrupt storm paths.
- Add kstats and SDT probes (command send/response, scan, state changes, FW errors).

### Stage 4 - Performance and radio capability
- Rate adaptation: implement LQ command driven rate scaling (OpenBSD `iwm_ra`/MiRA or Linux
  `rs`), using TX status/BA notifications.
- Extend TX aggregation beyond TID0 (all TIDs/ACs), RX BA reorder in driver or net80211.
- 5 GHz channels (11a), HT40 where regulatory allows, short GI, STBC/LDPC if supported, second
  antenna (MIMO 2x2 on 8260/8265), then VHT80 (8265 supports 11ac).
- Hardware key offload (ADD_STA_KEY) for CCMP/TKIP/GCMP instead of software crypto.
- Station power save (device + beacon filtering + proper DTIM handling), BT coex config
  audit (8260 has BT coex; verify `BT_COEX_*` commands match donor).
- Background scan and roaming.
- Benchmark: iperf3 TCP up/down vs. OpenBSD/Linux on same AP; track in `docs/perf.md`.

### Stage 5 - Security and protocol completeness
- WPA2 enterprise (works through wpad if EAPOL path is clean), TKIP/WEP legacy (optional),
  802.11w PMF, WPA3-SAE (requires wpad work).
- Regulatory: validate LAR/MCC behaviour per country; document channel policy.
- Review all attacker-reachable parsing (RX frames, TLVs, firmware notifications) with fuzzing
  where possible (userland harness around the parsers).

### Stage 6 - Other 8000-family SKUs and upstream hygiene
- Add 8265/8275/other 8000 PCI IDs and firmware (`iwm-8265-36`, 8000C variants), per-SKU cfg
  table. Obtain and package firmware with the correct redistribution licence.
- Man page `iwm(4D)`, release notes, test report template.
- Clean history for upstream: squash/clean commit messages, remove `.git-bir`, document and
  test the net80211 changes against other wifi drivers, run full `nightly` with `-C`
  (cstyle, hdrchk, lint, `wsdiff`), `pkglint`, and `ctfmerge` checks.

## 4. Documentation rules for all stages
- Every function that talks to hardware cites the donor function/command it follows.
- Every deviation from the donors is recorded in `docs/audit.md` with reason.
- Every bug found is recorded with commit, root cause and test.
- This roadmap is updated at the end of each stage.
