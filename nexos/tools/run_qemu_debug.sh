#!/bin/bash
# NexOS — tools/run_qemu_debug.sh | QEMU with GDB remote debug | MIT License

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO="$ROOT_DIR/build/nexos.iso"

echo "[QEMU-DEBUG] Starting NexOS with GDB server on port 1234..."
echo "[QEMU-DEBUG] Connect GDB with: target remote localhost:1234"
echo "[QEMU-DEBUG] Load symbols with: symbol-file build/nexos.kernel"

qemu-system-x86_64 \
    -machine q35 \
    -m 256M \
    -serial stdio \
    -display sdl \
    -cdrom "$ISO" \
    -boot d \
    -no-reboot \
    -s -S
