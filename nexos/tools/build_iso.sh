#!/bin/bash
# NexOS — tools/build_iso.sh | Assemble bootable ISO | MIT License

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT_DIR/build"
ISO_DIR="$BUILD_DIR/iso"
KERNEL="$BUILD_DIR/nexos.kernel"
ISO="$BUILD_DIR/nexos.iso"

echo "[build_iso.sh] Creating ISO structure..."
mkdir -p "$ISO_DIR/boot/grub"

# Copy kernel
cp "$KERNEL" "$ISO_DIR/boot/nexos.kernel"

# Copy GRUB config
cp "$ROOT_DIR/boot/grub/grub.cfg" "$ISO_DIR/boot/grub/grub.cfg"

# Build ISO with grub-mkrescue
echo "[build_iso.sh] Running grub-mkrescue..."
grub-mkrescue -o "$ISO" "$ISO_DIR" 2>/dev/null || \
    grub2-mkrescue -o "$ISO" "$ISO_DIR" 2>/dev/null

echo "[build_iso.sh] ISO created: $ISO"
ls -lh "$ISO"
