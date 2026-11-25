#!/bin/bash
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# Copyright 2026 lex0de <lex0de@tuta.com>
#
# Hardware test for iwm on illumos (docs/testing.md section 8), automated.
# Run as root on the machine with the card:
#
#	docs/tests/hwtest.sh            run everything (stops at first failure)
#	docs/tests/hwtest.sh build      build, check, install and modload only
#	                                (no card needed; fine in a plain VM)
#	docs/tests/hwtest.sh restore    put the original modules back
#
# Settings: docs/tests/hwtest.conf (copy hwtest.conf.example).
# Result: /var/tmp/iwm-hwtest-<time>.tar.gz -- send that file.
# The tarball contains logs and system details but not the WPA key.
#
# It replaces /kernel/drv/amd64/iwm and /kernel/misc/amd64/net80211 (the
# originals are kept as *.iwmtest-orig).  If net80211 is in use it asks for
# a reboot and continues when run again.

set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
uts=$root/usr/src/uts
conf=$here/hwtest.conf
state=/var/tmp/iwm-hwtest.state

DRV=/kernel/drv/amd64/iwm
MISC=/kernel/misc/amd64/net80211
FWDIR=/kernel/firmware/iwm
LINK=""

[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }

restore() {
	for f in $DRV $MISC; do
		if [ -f "$f.iwmtest-orig" ]; then
			cp -p "$f.iwmtest-orig" "$f" && echo "restored $f"
		fi
	done
	rm -f "$state"
	echo "Reboot to load the original modules."
}
mode=${1:-all}
case $mode in
restore) restore; exit 0 ;;
all|build) ;;
*) echo "usage: $0 [build|restore]"; exit 1 ;;
esac

OPEN_ESSID="" WPA_ESSID="" WPA_KEY="" PING_HOST="" IPERF_SERVER=""
IPERF_SECONDS=30 SOAK_SECONDS=300 CYCLES=10 BUILD=1
if [ -f "$conf" ]; then
	. "$conf"
else
	echo "note: $conf not found; only build/attach/scan will run"
fi

stamp=$(date +%Y%m%d-%H%M%S)
out=/var/tmp/iwm-hwtest-$stamp
mkdir -p "$out"
log=$out/report.txt
msgstart=$(wc -l < /var/adm/messages 2>/dev/null || echo 0)
secobj=""
keyfile=""

say() { echo "$*" | tee -a "$log"; }
run() {	# run a command, log it and its output
	echo "+ $*" >> "$log"
	"$@" >> "$log" 2>&1
}

collect() {
	{
		echo "== uname"; uname -a; cat /etc/release
		echo "== card"; prtconf -v 2>/dev/null |
		    grep -i -A2 'pciex8086,24f[34d]' | head -20
		echo "== modinfo"; modinfo | egrep 'iwm|net80211|mac_wifi'
		echo "== dladm"; dladm show-phys; dladm show-link
		[ -n "$LINK" ] && { dladm show-wifi "$LINK";
		    dladm show-link -s "$LINK"; dladm show-linkprop "$LINK"; }
		echo "== ipadm"; ipadm show-addr
		echo "== kstat"; kstat -m iwm 2>/dev/null
		echo "== msgbuf"; echo ::msgbuf | mdb -k 2>/dev/null | tail -200
		echo "== interrupts"; echo ::interrupts | mdb -k 2>/dev/null |
		    grep -i iwm
	} > "$out/system.txt" 2>&1
	tail -n +"$((msgstart + 1))" /var/adm/messages > "$out/messages.txt" \
	    2>/dev/null
}

cleanup_net() {
	[ -n "$LINK" ] || return
	ipadm delete-if "$LINK" >/dev/null 2>&1
	dladm disconnect-wifi "$LINK" >/dev/null 2>&1
	[ -n "$secobj" ] && dladm delete-secobj "$secobj" >/dev/null 2>&1
	[ -n "$keyfile" ] && rm -f "$keyfile"
}

finish() {
	rc=$1
	cleanup_net
	collect
	if [ "$rc" = 0 ]; then say "RESULT: all steps passed"
	else say "RESULT: FAILED at step $step"; fi
	tar -C /var/tmp -cf - "$(basename "$out")" | gzip > "$out.tar.gz"
	echo
	echo "Send this file: $out.tar.gz"
	exit "$rc"
}

fail() { say "FAIL: $*"; finish 1; }
pass() { say "ok: $*"; }
step=start

# Wait up to $1 seconds for command "$2..." to succeed.
waitfor() {
	local t=$1; shift
	while [ "$t" -gt 0 ]; do
		"$@" >/dev/null 2>&1 && return 0
		sleep 1; t=$((t - 1))
	done
	return 1
}

say "iwm hardware test ($mode) $stamp, tree $(cd "$root" && git log --oneline -1 2>/dev/null)"

# --- 0. preflight
step=preflight
if [ "$mode" = all ]; then
	prtconf -v 2>/dev/null | grep -qi 'pciex8086,24f[34d]' ||
	    fail "no Intel 8260/8265 (8086:24f3/24f4/24fd) found;" \
	    "use '$0 build' on a machine without the card"
fi
if svcs -H -o state network/physical:nwam 2>/dev/null | grep -q online; then
	say "warning: NWAM is online and may fight over the iwm link;" \
	    "consider: svcadm disable network/physical:nwam"
fi
for fw in iwm-8000C-36 iwm-8265-36; do
	[ -f "$FWDIR/$fw" ] || { mkdir -p "$FWDIR";
	    cp "$uts/common/io/iwm/fw-iw/$fw" "$FWDIR/$fw" ||
	    fail "cannot install firmware $fw"; }
done
pass "preflight (firmware in $FWDIR)"

# --- 1. build
step=build
if [ "$BUILD" = 1 ] && [ ! -f "$state" ]; then
	command -v dmake >/dev/null ||
	    fail "dmake not found: run from a bldenv shell, or set BUILD=0"
	# Modules are CTF-merged against genunix, so it must be built first.
	for d in genunix net80211 iwm; do
		say "building $d"
		(cd "$uts/intel/$d" && dmake -e all) > "$out/build-$d.log" 2>&1 ||
		    fail "build of $d (see build-$d.log)"
		[ "$d" = iwm ] || continue
		(cd "$uts/intel/$d" && dmake check) > "$out/check-$d.log" 2>&1 ||
		    say "warning: dmake check reported problems ($d)"
	done
	pass "build"
fi
# DEBUG builds (bldenv -d) go to debug64, non-DEBUG to obj64.
newdrv=$uts/intel/iwm/debug64/iwm
newmisc=$uts/intel/net80211/debug64/net80211
[ -f "$newdrv" ] || newdrv=$uts/intel/iwm/obj64/iwm
[ -f "$newmisc" ] || newmisc=$uts/intel/net80211/obj64/net80211
[ -f "$newdrv" ] && [ -f "$newmisc" ] || fail "built modules not found"

# --- 2. install (net80211 can only be replaced while unused)
step=install
for f in $DRV $MISC; do
	[ -f "$f" ] && [ ! -f "$f.iwmtest-orig" ] && cp -p "$f" "$f.iwmtest-orig"
done
if ! cmp -s "$newmisc" "$MISC"; then
	if modinfo | grep -qw net80211; then
		modunload -i "$(modinfo | awk '$6 == "iwm" {print $1}')" \
		    2>/dev/null
		modunload -i "$(modinfo | awk '$6 == "net80211" {print $1}')" \
		    2>/dev/null
	fi
	cp "$newmisc" "$MISC" || fail "cannot install net80211"
	if modinfo | grep -qw net80211; then
		cp "$newdrv" "$DRV"
		echo "installed" > "$state"
		say "net80211 is in use by another driver: new modules are" \
		    "installed.  Reboot, then run this script again."
		collect
		exit 0
	fi
fi
cp "$newdrv" "$DRV" || fail "cannot install iwm"
rm -f "$state"
pass "install"

# --- build mode: prove the module loads and all symbols resolve
if [ "$mode" = build ]; then
	step=modload
	elfdump -s "$DRV" 2>/dev/null | awk '$5 == "UNDEF" || $8 == "UNDEF"' \
	    > "$out/iwm-undef.txt"
	modinfo | grep -qw iwm && run modunload -i \
	    "$(modinfo | awk '$6 == "iwm" {print $1}')"
	run modload "$DRV" || fail "modload iwm (unresolved symbols?" \
	    "see messages.txt)"
	modinfo | grep -w iwm | tee -a "$log"
	modinfo | grep -qw iwm || fail "iwm not in modinfo after modload"
	run modunload -i "$(modinfo | awk '$6 == "iwm" {print $1}')"
	pass "modload/modunload (no device: attach not exercised)"
	finish 0
fi

# --- 3. attach
step=attach
if ! grep -q '^iwm ' /etc/name_to_major; then
	run add_drv -i '"pciex8086,24f3" "pciex8086,24f4" "pciex8086,24fd"' iwm ||
	    fail "add_drv"
else
	run modload "$DRV"
	run devfsadm -i iwm
fi
waitfor 30 sh -c "dladm show-phys -p -o link,device | grep -q ':iwm[0-9]'" ||
    fail "no iwm link after attach (driver did not attach)"
LINK=$(dladm show-phys -p -o link,device | awk -F: '$2 ~ /^iwm/ {print $1; exit}')
grep -q "fw ver" /var/adm/messages && tail -n +"$((msgstart + 1))" \
    /var/adm/messages | grep "fw ver" | tail -1 | tee -a "$log"
pass "attach (link $LINK)"

# --- 4. scan
step=scan
run ipadm delete-if "$LINK"
dladm scan-wifi "$LINK" > "$out/scan.txt" 2>&1 || fail "scan-wifi"
n=$(($(wc -l < "$out/scan.txt") - 1))
[ "$n" -gt 0 ] || fail "scan found no networks"
grep -q ' a ' "$out/scan.txt" && say "5 GHz networks seen"
pass "scan ($n networks)"

# Connect $1=essid [$2=secobj]; DHCP; ping.
connect() {
	local essid=$1 key=${2:-} gw
	ipadm delete-if "$LINK" >/dev/null 2>&1
	dladm disconnect-wifi "$LINK" >/dev/null 2>&1
	if [ -n "$key" ]; then
		run dladm connect-wifi -e "$essid" -k "$key" -T 60 "$LINK"
	else
		run dladm connect-wifi -e "$essid" -T 60 "$LINK"
	fi || return 1
	run ipadm create-ip "$LINK" || return 1
	run ipadm create-addr -T dhcp -w 60 "$LINK/v4" || return 1
	gw=${PING_HOST:-$(netstat -rn -f inet |
	    awk '$1 == "default" || $1 == "0.0.0.0" {print $2; exit}')}
	[ -n "$gw" ] || return 1
	PEER=$gw
	ping -s "$gw" 56 10 > "$out/ping-$essid.txt" 2>&1
	# Some replies must come back (illumos grep has no -o or \|).
	[ "$(loss "$out/ping-$essid.txt")" -lt 100 ] 2>/dev/null
}

# Packet loss percentage from illumos "ping -s" output.
loss() {
	sed -n 's/.* \([0-9][0-9]*\)% packet loss.*/\1/p' "$1" | tail -1
}

# --- 5. open network
step=open
if [ -n "$OPEN_ESSID" ]; then
	connect "$OPEN_ESSID" || fail "connect/DHCP/ping on open network"
	run dladm show-wifi "$LINK"
	pass "open network"
fi

# --- 6. WPA2
step=wpa
if [ -n "$WPA_ESSID" ]; then
	keyfile=$(mktemp /tmp/iwmkey.XXXXXX); chmod 600 "$keyfile"
	printf '%s\n' "$WPA_KEY" > "$keyfile"
	secobj=iwmtest$$
	dladm create-secobj -c wpa -f "$keyfile" "$secobj" >> "$log" 2>&1 ||
	    fail "create-secobj"
	rm -f "$keyfile"; keyfile=""
	connect "$WPA_ESSID" "$secobj" || fail "connect/DHCP/ping on WPA2"
	run dladm show-wifi "$LINK"
	pass "WPA2 network"
fi

connected=0
[ -n "$OPEN_ESSID$WPA_ESSID" ] && connected=1

# --- 7. throughput
step=iperf
if [ "$connected" = 1 ] && [ -n "$IPERF_SERVER" ]; then
	command -v iperf3 >/dev/null || fail "iperf3 not installed"
	iperf3 -c "$IPERF_SERVER" -t "$IPERF_SECONDS" > "$out/iperf-tx.txt" \
	    2>&1 || fail "iperf3 transmit"
	iperf3 -c "$IPERF_SERVER" -t "$IPERF_SECONDS" -R \
	    > "$out/iperf-rx.txt" 2>&1 || fail "iperf3 receive"
	say "tx: $(grep sender "$out/iperf-tx.txt" | tail -1)"
	say "rx: $(grep receiver "$out/iperf-rx.txt" | tail -1)"
	run dladm show-wifi "$LINK"
	pass "iperf3"
fi

# --- 8. soak
step=soak
if [ "$connected" = 1 ] && [ "$SOAK_SECONDS" -gt 0 ]; then
	ping -s -I 1 "$PEER" 56 "$SOAK_SECONDS" > "$out/soak.txt" 2>&1
	pct=$(loss "$out/soak.txt")
	say "soak: ${pct:-?}% loss over ${SOAK_SECONDS}s"
	[ -n "$pct" ] && [ "$pct" -le 5 ] || fail "soak loss ${pct:-?}%"
	pass "soak"
fi

# --- 9. reconnect cycles
step=cycles
if [ "$connected" = 1 ] && [ "$CYCLES" -gt 0 ]; then
	essid=${WPA_ESSID:-$OPEN_ESSID}
	key=""; [ -n "$WPA_ESSID" ] && key=$secobj
	i=1
	while [ "$i" -le "$CYCLES" ]; do
		connect "$essid" $key || fail "reconnect cycle $i"
		i=$((i + 1))
	done
	pass "$CYCLES reconnect cycles"
fi

# --- 10. unload and leaks
step=unload
cleanup_net
id=$(modinfo | awk '$6 == "iwm" {print $1}')
for i in 1 2 3; do
	run ifconfig "$LINK" unplumb
	run modunload -i "$id" && break
	sleep 2
done
modinfo | grep -qw iwm && say "warning: iwm did not unload"
echo ::findleaks | mdb -k > "$out/findleaks.txt" 2>&1
grep -qi iwm "$out/findleaks.txt" && fail "kernel leaks attributed to iwm"
run devfsadm -i iwm
pass "unload (leak check needs kmem_flags=0xf to be thorough)"

finish 0
