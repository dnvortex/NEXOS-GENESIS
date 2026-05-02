#!/bin/bash
# NexOS — tools/run_qemu.sh | Launch NexOS in QEMU | MIT License

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO="$ROOT_DIR/build/nexos.iso"
DISK="$ROOT_DIR/build/nexos.img"

DISPLAY_MODE="${DISPLAY_MODE:-sdl}"
MEMORY="${MEMORY:-256}"

QEMU_CMD="qemu-system-x86_64"
QEMU_ARGS=(
    -machine q35
    -m ${MEMORY}M
    -serial stdio
    -display ${DISPLAY_MODE}
    -no-reboot
)

# Enable KVM if available
if [ -e /dev/kvm ]; then
    QEMU_ARGS+=(-enable-kvm -cpu host)
    echo "[QEMU] KVM acceleration enabled"
else
    QEMU_ARGS+=(-cpu qemu64)
    echo "[QEMU] KVM not available, using software emulation"
fi

# Boot source
if [ "$1" = "--disk" ] && [ -f "$DISK" ]; then
    echo "[QEMU] Booting from disk image: $DISK"
    QEMU_ARGS+=(-hda "$DISK")
else
    echo "[QEMU] Booting from ISO: $ISO"
    QEMU_ARGS+=(-cdrom "$ISO" -boot d)
fi

# Networking (stub)
QEMU_ARGS+=(-netdev user,id=net0 -device e1000,netdev=net0)

echo "[QEMU] Starting NexOS..."
"$QEMU_CMD" "${QEMU_ARGS[@]}"
