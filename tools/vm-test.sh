#!/bin/bash
#
# Startet WipeOS in QEMU zum Ausprobieren, ohne echte Hardware.
# Aufruf: tools/vm-test.sh out/WipeOS.img
# Es wird eine leere 2-GB-Testplatte angelegt, die gelöscht werden darf.
# Fenster schließen oder in der Oberfläche "Herunterfahren" beendet die VM.
#
set -e
IMG=${1:-out/WipeOS.img}
TMP=$(mktemp -d)
cp "$IMG" "$TMP/stick.img"
truncate -s 2G "$TMP/testplatte.img"
qemu-system-x86_64 -m 2048 -smp 2 $( [ -w /dev/kvm ] && echo -enable-kvm ) \
	-smbios type=1,manufacturer=LENOVO,product=21AH000AGE,version="ThinkPad T14 Gen 3",serial=TEST1234 \
	-device qemu-xhci \
	-drive if=none,id=stick,file="$TMP/stick.img",format=raw -device usb-storage,drive=stick,bootindex=0 \
	-device ahci,id=ahci -drive if=none,id=d1,file="$TMP/testplatte.img",format=raw \
	-device ide-hd,drive=d1,bus=ahci.0,model=TEST_HDD,serial=TESTSERIAL01 \
	-device usb-tablet -device usb-kbd -vga std
echo "Stick-Abbild mit Berichten: $TMP/stick.img"
