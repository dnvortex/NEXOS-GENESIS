#!/bin/bash
# NexOS — tools/run_vmware.sh
# Generates a VMware .vmx config and launches NexOS in VMware Player.
# MIT License

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
ISO="$ROOT_DIR/build/nexos.iso"
VM_DIR="$ROOT_DIR/build/vms/NexOS-VMware"
VMX="$VM_DIR/nexos.vmx"
VMDK="$VM_DIR/nexos.vmdk"
DISK_GB="${DISK_GB:-8}"
MEM_MB="${MEM_MB:-512}"

if [ ! -f "$ISO" ]; then
    echo "[vmware] Error: ISO not found at $ISO"
    echo "[vmware] Run 'make iso' first."
    exit 1
fi

mkdir -p "$VM_DIR"

echo "[vmware] Creating ${DISK_GB}GB virtual disk descriptor..."
cat > "$VMDK" <<VMDK_EOF
# Disk DescriptorFile
version=1
encoding="UTF-8"
CID=fffffffe
parentCID=ffffffff
createType="monolithicSparse"
RW $((DISK_GB * 2097152)) SPARSE "nexos-flat.vmdk"
ddb.virtualHWVersion = "14"
ddb.geometry.cylinders = "$((DISK_GB * 1024))"
ddb.geometry.heads = "255"
ddb.geometry.sectors = "63"
ddb.adapterType = "ide"
VMDK_EOF

echo "[vmware] Writing .vmx configuration..."
cat > "$VMX" <<VMX_EOF
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "19"
guestOS = "other-64"
displayName = "NexOS"
numvcpus = "2"
cpuid.coresPerSocket = "1"
memsize = "$MEM_MB"

ide0:0.present = "TRUE"
ide0:0.filename = "$VMDK"
ide0:0.deviceType = "disk"
ide0:0.mode = "persistent"

ide1:0.present = "TRUE"
ide1:0.filename = "$ISO"
ide1:0.deviceType = "cdrom-image"
ide1:0.startConnected = "TRUE"

ethernet0.present = "TRUE"
ethernet0.connectionType = "nat"
ethernet0.virtualDev = "e1000"
ethernet0.addressType = "generated"

svga.vramSize = "16777216"
mks.enable3d = "FALSE"

bios.bootOrder = "cdrom,hdd"

serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "$VM_DIR/serial.log"
serial0.tryNoRxLoss = "FALSE"
VMX_EOF

echo "[vmware] VMX: $VMX"
echo ""

# Try to launch
for player in vmplayer vmware "vmware-workstation"; do
    if command -v "$player" &>/dev/null; then
        echo "[vmware] Launching with $player..."
        "$player" "$VMX" &
        exit 0
    fi
done

echo "[vmware] VMware Player not found in PATH."
echo "[vmware] Open the VM manually:"
echo "           $VMX"
echo ""
echo "[vmware] Download VMware Player (free):"
echo "           https://www.vmware.com/products/workstation-player.html"
