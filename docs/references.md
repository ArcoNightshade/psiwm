# iwm reference sources

Reference policy: see roadmap.md section 0. OpenBSD and FreeBSD first, Linux iwlwifi
as tiebreaker. Reference sources are NOT stored in this repository; fetch them for audits.

| Source | Location | Version audited |
|---|---|---|
| OpenBSD if_iwm.c / if_iwmreg.h / if_iwmvar.h | https://raw.githubusercontent.com/openbsd/src/master/sys/dev/pci/ | `$OpenBSD: if_iwm.c,v 1.419 2025/12/01` (var.h v1.79) |
| OpenBSD pcidevs | same directory, `pcidevs` | master, fetched 2026-10-18 |
| FreeBSD iwm | https://raw.githubusercontent.com/freebsd/freebsd-src/main/sys/dev/iwm/ | main, fetched 2026-10-18 (files: if_iwm.c, if_iwm_pcie_trans.c, if_iwm_scan.c, if_iwmreg.h) |
| Linux iwlwifi | drivers/net/wireless/intel/iwlwifi/{pcie,mvm,fw} | not yet fetched |

In-tree donor: the driver headers state OpenBSD `0efabb06...` (BSD licence option) as donor.

## 8000-series PCI IDs (OpenBSD pcidevs)

| ID | Name | OpenBSD firmware |
|---|---|---|
| 8086:24f3 | AC 8260 | iwm-8000C-36 |
| 8086:24f4 | AC 8260 | iwm-8000C-36 |
| 8086:24fd | AC 8265 | iwm-8265-36 |

(OpenBSD also supports 3160/3165/3168/7260/7265/9260/9560; out of scope, see roadmap.)
