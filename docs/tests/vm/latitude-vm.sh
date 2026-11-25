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
# Run a Tribblix VM on a Linux host with the Intel 8260/8265 passed through
# (KVM + VFIO), for testing iwm.  Run as root on the host.
#
#   latitude-vm.sh check            IOMMU on? card found? alone in its group?
#   latitude-vm.sh install ISO      boot the installer (card stays with Linux)
#   latitude-vm.sh boot             boot the VM without the card (setup)
#   latitude-vm.sh run              give the card to the VM and boot it
#   latitude-vm.sh restore          give the card back to Linux
#
# The VM always has a user-mode NIC (no host network needed): ssh to it with
#   ssh -p 2222 root@127.0.0.1
# Its serial console is logged to $DIR/console.log (kernel panics land there
# once the guest console is set to ttya, see docs/testing.md).

set -eu
DIR=${VMDIR:-$HOME/iwm-vm}
DISK=$DIR/tribblix.qcow2
MEM=${VMMEM:-4096}
CPUS=${VMCPUS:-2}

[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
mkdir -p "$DIR"

card() {	# PCI address of the Intel 8260/8265
	lspci -Dnn | awk '/8086:24f[34d]/ {print $1; exit}'
}

check() {
	local bdf grp
	if ! grep -q -E 'intel_iommu=on|iommu=pt' /proc/cmdline; then
		echo "IOMMU: add 'intel_iommu=on iommu=pt' to the kernel command line"
		echo "       (GRUB_CMDLINE_LINUX in /etc/default/grub, then update-grub"
		echo "       or grub2-mkconfig) and enable VT-d in the BIOS, reboot."
	fi
	[ -d /sys/kernel/iommu_groups/0 ] || { echo "FAIL: no IOMMU groups"; exit 1; }
	bdf=$(card)
	[ -n "$bdf" ] || { echo "FAIL: no Intel 8260/8265 found"; exit 1; }
	grp=$(basename "$(readlink /sys/bus/pci/devices/$bdf/iommu_group)")
	echo "card $bdf ($(lspci -s "$bdf" | cut -d' ' -f2-)), IOMMU group $grp:"
	ls /sys/kernel/iommu_groups/"$grp"/devices
	if [ "$(ls /sys/kernel/iommu_groups/"$grp"/devices | wc -l)" -ne 1 ]; then
		echo "WARNING: other devices share the group; passthrough needs"
		echo "all of them (or an ACS override). Send me this output."
	else
		echo "ok: card is alone in its group"
	fi
	command -v qemu-system-x86_64 >/dev/null ||
	    echo "install QEMU: apt install qemu-system-x86 (Debian/Ubuntu)," \
	    "dnf install qemu-kvm (Fedora), pacman -S qemu-full (Arch)"
	[ -e /dev/kvm ] || echo "FAIL: /dev/kvm missing (enable VT-x in BIOS)"
}

qemu() {	# common QEMU command line; extra args appended
	exec qemu-system-x86_64 -enable-kvm -machine q35 -cpu host \
	    -smp "$CPUS" -m "$MEM" \
	    -drive file="$DISK",if=virtio,format=qcow2 \
	    -nic user,model=e1000,hostfwd=tcp:127.0.0.1:2222-:22 \
	    -serial file:"$DIR/console.log" \
	    "$@"
}

tovfio() {
	local bdf=$1 dev=/sys/bus/pci/devices/$1
	modprobe vfio-pci
	echo vfio-pci > "$dev/driver_override"
	[ -e "$dev/driver" ] && echo "$bdf" > "$dev/driver/unbind"
	echo "$bdf" > /sys/bus/pci/drivers_probe
	[ "$(basename "$(readlink "$dev/driver")")" = vfio-pci ] ||
	    { echo "FAIL: card did not bind to vfio-pci"; exit 1; }
	echo "card $bdf now belongs to the VM (Linux Wi-Fi is off)"
}

restore() {
	local bdf dev
	bdf=$(card); dev=/sys/bus/pci/devices/$bdf
	echo > "$dev/driver_override"
	[ -e "$dev/driver" ] && echo "$bdf" > "$dev/driver/unbind"
	echo "$bdf" > /sys/bus/pci/drivers_probe
	echo "card $bdf back with Linux driver: $(basename "$(readlink "$dev/driver" 2>/dev/null || echo none)")"
}

case ${1:-} in
check) check ;;
install)
	[ -f "${2:-}" ] || { echo "usage: $0 install tribblix.iso"; exit 1; }
	[ -f "$DISK" ] || qemu-img create -f qcow2 "$DISK" 30G
	qemu -cdrom "$2" -boot d ;;
boot)	qemu ;;
run)
	bdf=$(card); [ -n "$bdf" ] || { echo "no card"; exit 1; }
	tovfio "$bdf"
	trap restore EXIT
	qemu-system-x86_64 -enable-kvm -machine q35 -cpu host \
	    -smp "$CPUS" -m "$MEM" \
	    -drive file="$DISK",if=virtio,format=qcow2 \
	    -nic user,model=e1000,hostfwd=tcp:127.0.0.1:2222-:22 \
	    -serial file:"$DIR/console.log" \
	    -device vfio-pci,host="$bdf"
	;;
restore) restore ;;
*) sed -n '15,27p' "$0"; exit 1 ;;
esac
