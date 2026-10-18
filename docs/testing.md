# iwm build and hardware test procedure (Tribblix / illumos)

Nothing in docs/audit.md "Fix log" is verified until this procedure has been run.

## 0. Identify the card
    prtconf -pv | grep -B3 -A12 'pciex8086,24f'     # look for 24f3, 24f4, 24fd
    scanpci -v | grep -i -A4 'network'
Record: vendor/device (8086:xxxx), subsystem ID, revision, `uname -a`, `cat /etc/release`.
The driver currently admits only 8086:24f3, subsystem 8086:1130, revision 0x3a (finding F11).
Any other card needs Stage 1 gate changes first.

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
    add_drv -i '"pciex8086,24f3"' iwm        # only after the build is clean
    dmesg | tail -50 ; modinfo | grep iwm
Passive attach is the default. `iwm-public-scan=1` in /kernel/drv/iwm.conf enables
scan/connect. Keep a console open: a bug can panic the machine.

## 3. Collect after each test
    dmesg | grep -i iwm
    echo '::findleaks' | mdb -k          # after unload
    dladm show-wifi ; dladm scan-wifi

## 4. Regression checklist for the fixes made so far
- Connect to open AP; DHCP works (broadcast frames, F3).
- Connect to WPA2-PSK AP; ping; iperf3.
- Force a firmware error if possible (or note any "iwm firmware error id=" lines).
- dladm disconnect/connect cycles x20; modunload; no leaks.
