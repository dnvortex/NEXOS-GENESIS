#!/bin/bash
# NexOS — tools/create_disk.sh | Create 64MB FAT32 disk image | MIT License
set -e

IMG="$(dirname "$0")/../build/nexos_disk.img"
MOUNT=/tmp/nexos_disk_mount

echo "[*] Creating 64MB FAT32 disk image..."
dd if=/dev/zero of="$IMG" bs=1M count=64 status=progress

echo "[*] Formatting as FAT32..."
if command -v mkfs.fat >/dev/null 2>&1; then
    mkfs.fat -F 32 -n "NEXOS" "$IMG"
elif command -v mkdosfs >/dev/null 2>&1; then
    mkdosfs -F 32 -n "NEXOS" "$IMG"
else
    echo "[!] Error: mkfs.fat / mkdosfs not found. Install dosfstools."
    exit 1
fi

echo "[*] Mounting and populating..."
mkdir -p "$MOUNT"
sudo mount -o loop "$IMG" "$MOUNT"

mkdir -p "$MOUNT/home/user"
mkdir -p "$MOUNT/var/log"
mkdir -p "$MOUNT/tmp"
echo "NexOS persistent disk" > "$MOUNT/README.txt"
echo "hostname=nexos"       > "$MOUNT/nexos.conf"
echo "This is /var/log on the FAT32 disk." > "$MOUNT/var/log/disk.log"

sudo umount "$MOUNT"
rmdir "$MOUNT"
echo "[*] Done: $IMG ($(du -h "$IMG" | cut -f1))"
