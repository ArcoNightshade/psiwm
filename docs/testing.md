# iwm build and hardware test procedure (Tribblix / illumos)

Nothing in docs/audit.md "Fix log" is verified until this procedure has been run.

## 0. Identify the card
    prtconf -pv | grep -B3 -A12 'pciex8086,24f'     # look for 24f3, 24f4, 24fd
    scanpci -v | grep -i -A4 'network'
Record: vendor/device (8086:xxxx), subsystem ID, revision, `uname -a`, `cat /etc/release`.
The driver now admits 8086:24f3, 24f4 and 24fd (8260 / 8265) by device ID only.

## 1. Build the module only
From the source root on the Tribblix machine (with the illumos build tools / closed
binaries set up as for the m41lx baseline):
    cd usr/src/uts/intel/iwm
    dmake -e all 2>&1 | tee /var/tmp/iwm-build.log
    dmake check 2>&1 | tee /var/tmp/iwm-check.log     # cstyle + hdrchk
Send both logs. Fix compiler errors/warnings before anything else.

## 2. Load without a driver alias (safe, passive)
    cp obj64/iwm /tmp/iwm; cp ../../../common/io/iwm/fw-iw/iwm-8000C-36 /kernel/firmware/iwm/
    modload /tmp/iwm
    add_drv -i '"pciex8086,24f3" "pciex8086,24f4" "pciex8086,24fd"' iwm   # after a clean build
Firmware files: iwm-8000C-36 (8260) and iwm-8265-36 (8265) into /kernel/firmware/iwm/.
    dmesg | tail -50 ; modinfo | grep iwm
The driver registers a WiFi link at attach; the firmware must already be installed or
attach fails with "could not load firmware". Keep a console open: a bug can panic the
machine. A DEBUG kernel (or `set kmem_flags=0xf` in /etc/system) catches memory misuse early.

## 3. Collect after each test
    dmesg | grep -i iwm
    echo '::findleaks' | mdb -k          # after unload
    dladm show-wifi ; dladm scan-wifi

## 4. Regression checklist for the fixes made so far
- Connect to open AP; DHCP works (broadcast frames, F3).
- Connect to WPA2-PSK AP; ping; iperf3.
- Force a firmware error if possible (or note any "iwm firmware error id=" lines).
- dladm disconnect/connect cycles x20; modunload; no leaks.

## 5. Additional checks for the 2026-10-19 changes
- Throughput: `iperf3` up/down on 2.4 GHz HT20; expect well above 6 Mbit/s (rate ladder, F2).
  Watch `dladm show-link -s iwm0` and the link speed in `dladm show-wifi`.
- Move away from / back to the AP: rate should fall and recover within a few seconds.
- Toggle the RF-kill switch (if the machine has one): expect "radio disabled by RF-kill"
  in dmesg, link down, and recovery on the next `dladm connect-wifi` after switching back.
- On a firmware error, dmesg should show "iwm firmware error id=..." (save it).
- 8265 only: confirm `iwm-8265-36` loads (dmesg firmware-open checkpoint).

## 6. Host tests (no illumos needed)
    docs/tests/run.sh
Checks that both packaged firmware images walk cleanly and advertise DQA and UMAC scan,
runs the MiRA/AMRR rate-control unit tests (docs/tests/ra.c, built from the driver
sources), type-checks the kernel sources against the tree headers, and checks that
if_iwm_proto.h matches if_iwm.c (regenerate with docs/tests/genproto.py).

## 7. Checks for the 2026-11-14 changes
- Power off the AP while connected: expect "iwm AP lost" within a few seconds and link down.
- After any firmware error, `dladm connect-wifi` again: expect "iwm runtime error=...; restarting"
  and a working connection, not a permanently failed driver.
- `man iwm` renders; `mandoc -Tlint usr/src/man/man4d/iwm.4d` is clean.

## 8. Checks for the OpenBSD re-port (2026-11-24)
Build only (no card, e.g. a plain VM): `docs/tests/hwtest.sh build` as root from a `bldenv`
shell builds, runs `dmake check`, installs and modloads the modules.
Automated: `cp docs/tests/hwtest.conf.example docs/tests/hwtest.conf`, fill in the networks,
then as root (from a `bldenv` shell if it should build) run `docs/tests/hwtest.sh`. It runs
the steps below, stops at the first failure and writes /var/tmp/iwm-hwtest-<time>.tar.gz to
send back. It replaces net80211 and iwm (originals kept; `hwtest.sh restore` puts them back)
and may ask for one reboot if net80211 is in use. Manual steps:
Sections 4, 5 and 7 described the previous driver; their log messages no longer exist.
Run in this order and stop at the first failure (send dmesg and the step number):
1. `modload`/attach: dmesg shows "hw rev 0x..., fw ver 36.ca7b901d.0, address ..."
   (logged with `!`, so check /var/adm/messages). `dladm show-phys` lists iwm0.
2. `dladm scan-wifi iwm0` lists 2.4 GHz and (if the card allows) 5 GHz networks.
3. Open AP: `dladm connect-wifi -e <essid> iwm0`, then DHCP; ping for 5 minutes.
4. WPA2-PSK AP: connect, DHCP, `iperf3 -c` both directions on 2.4 GHz HT20 and, if
   available, 5 GHz HT40. Expect tens of Mbit/s; MCS climbs within seconds.
5. Unplug/power off the AP: link goes down; power it back on and reconnect.
6. `ifconfig iwm0 unplumb` / plumb x20, `modunload -i <id>`, then `::findleaks`.
7. Suspend/resume if the machine supports it.
8. Any "fatal firmware error": the driver dumps the error log to /var/adm/messages
   (lines with "| "); send them. The device should restart and reconnect by itself.
