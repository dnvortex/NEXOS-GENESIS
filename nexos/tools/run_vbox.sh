#!/bin/bash
# NexOS — tools/run_vbox.sh
# Quick VirtualBox launcher: imports and starts NexOS VM from the build ISO.
# MIT License

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
ISO="$ROOT_DIR/build/nexos.iso"
VM_NAME="NexOS"

if ! command -v VBoxManage &>/dev/null; then
    echo "[vbox] Error: VBoxManage not found."
    echo "[vbox] Install VirtualBox: https://www.virtualbox.org/wiki/Downloads"
    exit 1
fi

if [ ! -f "$ISO" ]; then
    echo "[vbox] Error: ISO not found at $ISO"
    echo "[vbox] Run 'make iso' first."
    exit 1
fi

# Remove stale VM if it exists (ignore errors)
VBoxManage unregistervm "$VM_NAME" --delete 2>/dev/null || true

echo "[vbox] Creating VM '$VM_NAME'..."
VBoxManage createvm --name "$VM_NAME" --ostype "Other_64" --register

echo "[vbox] Configuring hardware..."
VBoxManage modifyvm "$VM_NAME" \
    --memory 512 \
    --vram 16 \
    --cpus 2 \
    --nic1 nat \
    --nictype1 Am79C973 \
    --boot1 dvd \
    --boot2 disk \
    --boot3 none \
    --audio none \
    --usb off

echo "[vbox] Attaching ISO as CD-ROM..."
VBoxManage storagectl "$VM_NAME" --name "IDE" --add ide --controller PIIX4
VBoxManage storageattach "$VM_NAME" \
    --storagectl "IDE" \
    --port 1 --device 0 \
    --type dvddrive \
    --medium "$ISO"

echo "[vbox] Starting NexOS VM..."
VBoxManage startvm "$VM_NAME" --type gui
