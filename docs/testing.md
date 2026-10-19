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
Public WiFi registration is now the default. `iwm-public-scan=0` in /kernel/drv/iwm.conf
returns to passive attach. Keep a console open: a bug can panic the machine.

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
