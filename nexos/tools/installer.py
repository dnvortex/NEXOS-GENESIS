#!/usr/bin/env python3
"""
NexOS Installer v1.0
Cross-platform installer — USB, QEMU, VirtualBox, VMware
Supports Linux · macOS · Windows
"""

import os, sys, subprocess, platform, shutil, time, textwrap, signal

# ── ANSI palette ──────────────────────────────────────────────────────────────
R  = "\033[0m"
B  = "\033[1m"
D  = "\033[2m"
CY = "\033[96m"
GR = "\033[92m"
YL = "\033[93m"
RD = "\033[91m"
MG = "\033[95m"
BL = "\033[94m"
WH = "\033[97m"
GY = "\033[90m"

def supports_color():
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

def c(code, text):
    return f"{code}{text}{R}" if supports_color() else text

# ── Layout helpers ────────────────────────────────────────────────────────────
WIDTH = 70

def line(ch="─", color=GY):
    return c(color, ch * WIDTH)

def box_top(title="", color=CY):
    if title:
        pad = WIDTH - len(title) - 4
        return c(color, f"╔══  {B}{title}{R}{color}  {'═' * pad}╗{R}")
    return c(color, "╔" + "═" * (WIDTH - 2) + "╗" + R)

def box_bot(color=CY):
    return c(color, "╚" + "═" * (WIDTH - 2) + "╝" + R)

def box_row(text="", color=CY, inner_color=WH):
    inner = f"{inner_color}{text}{R}"
    pad   = WIDTH - 2 - len(text)
    return c(color, "║") + f" {inner}" + " " * (pad - 1) + c(color, "║")

def banner():
    art = [
        r"  ███╗   ██╗███████╗██╗  ██╗ ██████╗ ███████╗",
        r"  ████╗  ██║██╔════╝╚██╗██╔╝██╔═══██╗██╔════╝",
        r"  ██╔██╗ ██║█████╗   ╚███╔╝ ██║   ██║███████╗",
        r"  ██║╚██╗██║██╔══╝   ██╔██╗ ██║   ██║╚════██║",
        r"  ██║ ╚████║███████╗██╔╝ ██╗╚██████╔╝███████║",
        r"  ╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝",
    ]
    print()
    for ln in art:
        print(c(CY, ln))
    subtitle = "I N S T A L L E R   v 1 . 0"
    print(c(GY, f"{'':>20}{subtitle}"))
    print()

def progress_bar(done, total, width=40, label=""):
    pct  = done / total if total else 1
    fill = int(pct * width)
    bar  = c(GR, "█" * fill) + c(GY, "░" * (width - fill))
    pcts = f"{int(pct*100):3d}%"
    print(f"\r  {label} [{bar}] {c(WH, pcts)}", end="", flush=True)

def ask(prompt, default=None):
    hint = f" [{default}]" if default else ""
    try:
        val = input(c(CY, f"  ▶  {prompt}{hint}: ")).strip()
    except (KeyboardInterrupt, EOFError):
        print(); abort()
    return val if val else default

def abort():
    print(c(YL, "\n  Aborted. No changes made.\n"))
    sys.exit(0)

def ok(msg):    print(c(GR, f"  ✔  {msg}"))
def warn(msg):  print(c(YL, f"  ⚠  {msg}"))
def err(msg):   print(c(RD, f"  ✖  {msg}"))
def info(msg):  print(c(WH, f"  ·  {msg}"))
def step(n, t): print(c(CY, f"\n  [{n}] ") + c(B, t))

# ── ISO / path resolution ─────────────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT  = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
ISO_PATH   = os.path.join(REPO_ROOT, "build", "nexos.iso")
BUILD_DIR  = os.path.join(REPO_ROOT, "build")
VMS_DIR    = os.path.join(REPO_ROOT, "build", "vms")

def require_iso():
    if not os.path.exists(ISO_PATH):
        err(f"ISO not found: {ISO_PATH}")
        info("Run  make iso  inside nexos/ first.")
        sys.exit(1)
    size_mb = os.path.getsize(ISO_PATH) // (1024 * 1024)
    ok(f"ISO found: {ISO_PATH}  ({size_mb} MB)")
    return ISO_PATH

# ── Drive detection ───────────────────────────────────────────────────────────
def detect_drives():
    os_name = platform.system()
    if os_name == "Linux":   return _drives_linux()
    if os_name == "Darwin":  return _drives_macos()
    if os_name == "Windows": return _drives_windows()
    return []

def _drives_linux():
    drives = []
    try:
        out = subprocess.check_output(
            ["lsblk", "-o", "NAME,SIZE,LABEL,RM,TYPE,VENDOR", "--noheadings"],
            stderr=subprocess.DEVNULL, text=True)
        for ln in out.splitlines():
            p = ln.split()
            if len(p) >= 5 and p[3] == "1" and p[4] == "disk":
                name   = p[0].lstrip("─├└")
                label  = " ".join(p[5:]) if len(p) > 5 else p[2] if len(p) > 2 else ""
                drives.append({"path": f"/dev/{name}", "size": p[1], "label": label.strip()})
    except Exception:
        pass
    return drives

def _drives_macos():
    drives = []
    try:
        import plistlib
        raw = subprocess.check_output(
            ["diskutil", "list", "-plist", "external"],
            stderr=subprocess.DEVNULL)
        data = plistlib.loads(raw)
        for d in data.get("AllDisksAndPartitions", []):
            dev   = d.get("DeviceIdentifier", "")
            sz    = d.get("Size", 0)
            label = d.get("VolumeName", "")
            drives.append({"path": f"/dev/{dev}",
                           "size": f"{sz/(1024**3):.1f}G",
                           "label": label})
    except Exception:
        pass
    return drives

def _drives_windows():
    drives = []
    try:
        ps = (
            'Get-Disk | Where-Object {$_.BusType -eq "USB"} | '
            'ForEach-Object {'
            '$n=$_.DiskNumber; $s=$_.Size; $f=$_.FriendlyName;'
            'Write-Output "$n|$s|$f"}'
        )
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command", ps],
            stderr=subprocess.DEVNULL, text=True)
        for ln in out.splitlines():
            p = ln.strip().split("|")
            if len(p) < 2: continue
            sz = int(p[1]) if p[1].isdigit() else 0
            drives.append({"path": f"\\\\.\\PhysicalDrive{p[0]}",
                           "size": f"{sz/(1024**3):.1f}G",
                           "label": p[2] if len(p) > 2 else ""})
    except Exception:
        pass
    return drives

# ── Write ISO to drive ────────────────────────────────────────────────────────
def _write_unix(iso, target):
    if platform.system() == "Darwin":
        rdisk = target.replace("/dev/disk", "/dev/rdisk")
        subprocess.run(["diskutil", "unmountDisk", target], check=False,
                       capture_output=True)
        cmd = ["dd", f"if={iso}", f"of={rdisk}", "bs=4m"]
    else:
        cmd = ["dd", f"if={iso}", f"of={target}", "bs=4M", "conv=fsync",
               "status=progress"]
    print(c(GY, f"\n  Running: {' '.join(cmd)}\n"))
    ret = subprocess.call(cmd)
    subprocess.call(["sync"])
    return ret == 0

def _write_windows(iso, target):
    ps = (
        f'$s=[System.IO.File]::OpenRead("{iso}");'
        f'$d=[System.IO.File]::OpenWrite("{target}");'
        f'$b=New-Object byte[] (4*1024*1024);'
        f'while(($n=$s.Read($b,0,$b.Length))-gt 0){{$d.Write($b,0,$n)}};'
        f'$s.Close();$d.Flush();$d.Close()'
    )
    ret = subprocess.call(["powershell", "-NoProfile", "-Command", ps])
    return ret == 0

# ═════════════════════════════════════════════════════════════════════════════
#  OPTION 1 — Write to USB / Physical disk
# ═════════════════════════════════════════════════════════════════════════════
def opt_usb():
    print()
    print(box_top("Write NexOS to USB / Physical Disk"))
    print(box_row("This will make a bootable USB drive that installs NexOS on"))
    print(box_row("any x86-64 computer.  All data on the selected drive will"))
    print(box_row("be permanently erased."))
    print(box_bot())

    iso = require_iso()

    step(1, "Detecting USB drives...")
    drives = detect_drives()
    if not drives:
        warn("No removable USB drives detected.")
        info("Insert a USB drive (≥ 256 MB) and re-run the installer.")
        return

    print()
    for i, d in enumerate(drives, 1):
        lbl = f"  {d['label']}" if d["label"] else ""
        print(c(CY, f"    [{i}]") + f"  {d['path']:20s}  {d['size']:8s}{c(GY, lbl)}")
    print(c(GY, f"    [0]  Cancel"))
    print()

    choice = ask("Select drive number")
    if choice in (None, "0", ""):
        abort()
    try:
        idx = int(choice) - 1
        assert 0 <= idx < len(drives)
    except Exception:
        err("Invalid selection."); return

    target = drives[idx]["path"]
    print()
    print(c(RD, B + f"  !! WARNING: ALL DATA on {target} will be ERASED !!" + R))
    confirm = ask(f"Type  YES  to confirm write to {target}")
    if confirm != "YES":
        abort()

    step(2, f"Writing ISO to {target}  (this may take 1-3 minutes)...")
    ok_flag = _write_unix(iso, target) if platform.system() != "Windows" \
              else _write_windows(iso, target)

    print()
    if ok_flag:
        ok("USB drive written successfully!")
        print()
        info("How to boot NexOS from this USB drive:")
        info("  1. Insert USB into the target computer")
        info("  2. Restart and enter BIOS/UEFI  (Del / F2 / F12 on most boards)")
        info("  3. Set USB as first boot device")
        info("  4. Save & Exit — NexOS will boot automatically")
        info("")
        info("At the GRUB menu choose:")
        info("  'NexOS'          — run from USB (live mode)")
        info("  'Install NexOS'  — launch disk-installation wizard")
    else:
        err("Write failed. Make sure you have root/admin privileges.")
        info("On Linux/macOS try:  sudo python3 tools/installer.py")

# ═════════════════════════════════════════════════════════════════════════════
#  OPTION 2 — Quick QEMU launch
# ═════════════════════════════════════════════════════════════════════════════
def opt_qemu_run():
    print()
    print(box_top("Run NexOS in QEMU  (Quick Launch)"))

    iso = require_iso()
    qemu = shutil.which("qemu-system-x86_64")
    if not qemu:
        err("qemu-system-x86_64 not found in PATH.")
        info("Install QEMU:  sudo apt install qemu-system-x86  (Debian/Ubuntu)")
        info("               brew install qemu                  (macOS)")
        info("               https://www.qemu.org/download/     (Windows)")
        return

    mem = ask("RAM in MB", "256")
    display = "gtk" if shutil.which("gtk-launch") or platform.system() == "Darwin" else "sdl"

    kvm_args = ["-enable-kvm", "-cpu", "host"] \
        if os.path.exists("/dev/kvm") else ["-cpu", "qemu64"]

    cmd = [
        qemu, "-machine", "q35",
        "-m", mem,
        *kvm_args,
        "-cdrom", iso, "-boot", "d",
        "-netdev", "user,id=net0",
        "-device", "rtl8139,netdev=net0",
        "-display", display,
        "-serial", "stdio",
        "-no-reboot",
        "-vga", "virtio",
    ]

    print()
    info(f"Command: {' '.join(cmd)}")
    print()
    ok("Launching QEMU — close the window or press Ctrl+A X in the console to quit")
    print()
    subprocess.call(cmd)

# ═════════════════════════════════════════════════════════════════════════════
#  OPTION 3 — Create persistent QEMU VM
# ═════════════════════════════════════════════════════════════════════════════
def opt_qemu_vm():
    print()
    print(box_top("Create Persistent QEMU VM"))
    print(box_row("Creates a raw disk image + launch script so NexOS persists"))
    print(box_row("across reboots when running in QEMU."))
    print(box_bot())

    iso = require_iso()
    os.makedirs(VMS_DIR, exist_ok=True)

    disk_gb = ask("Disk image size (GB)", "4")
    mem_mb  = ask("RAM (MB)", "512")
    vm_name = ask("VM name", "nexos-vm")

    disk_img   = os.path.join(VMS_DIR, f"{vm_name}.img")
    launch_sh  = os.path.join(VMS_DIR, f"run-{vm_name}.sh")
    launch_bat = os.path.join(VMS_DIR, f"run-{vm_name}.bat")

    step(1, f"Creating {disk_gb} GB raw disk image...")
    size_bytes = int(disk_gb) * 1024 * 1024 * 1024
    subprocess.run(["qemu-img", "create", "-f", "raw", disk_img, f"{disk_gb}G"],
                   check=False)
    if not os.path.exists(disk_img):
        # Fallback: create sparse file
        with open(disk_img, "wb") as f:
            f.seek(size_bytes - 1); f.write(b"\x00")
    ok(f"Disk image: {disk_img}")

    kvm_line = "-enable-kvm -cpu host \\\n" if os.path.exists("/dev/kvm") else "-cpu qemu64 \\\n"

    step(2, "Writing launch scripts...")

    # Shell script (Linux / macOS)
    sh = textwrap.dedent(f"""\
        #!/bin/bash
        # NexOS QEMU VM — generated by NexOS Installer
        ISO="{iso}"
        DISK="{disk_img}"
        FIRST_BOOT="${{FIRST_BOOT:-0}}"

        qemu-system-x86_64 \\
          -machine q35 \\
          -m {mem_mb} \\
          {kvm_line}  -drive file="$DISK",format=raw,if=ide,index=0,media=disk \\
          $([ "$FIRST_BOOT" = "1" ] && echo "-cdrom \\"$ISO\\" -boot dc" || echo "-boot c") \\
          -netdev user,id=net0 \\
          -device rtl8139,netdev=net0 \\
          -display gtk \\
          -serial stdio \\
          -vga virtio \\
          -no-reboot
    """)
    with open(launch_sh, "w") as f: f.write(sh)
    os.chmod(launch_sh, 0o755)

    # Batch file (Windows)
    bat = textwrap.dedent(f"""\
        @echo off
        REM NexOS QEMU VM — generated by NexOS Installer
        set ISO={iso}
        set DISK={disk_img}
        qemu-system-x86_64 ^
          -machine q35 ^
          -m {mem_mb} ^
          -cpu qemu64 ^
          -drive file="%DISK%",format=raw,if=ide,index=0,media=disk ^
          -netdev user,id=net0 ^
          -device rtl8139,netdev=net0 ^
          -display sdl ^
          -vga virtio ^
          -no-reboot
    """)
    with open(launch_bat, "w") as f: f.write(bat)

    ok(f"Launch script (Linux/macOS): {launch_sh}")
    ok(f"Launch script (Windows):     {launch_bat}")
    print()
    info("First-boot installation:")
    info(f"  FIRST_BOOT=1 bash {launch_sh}")
    info("  (boots from ISO → select 'Install NexOS' in GRUB)")
    info("")
    info("Subsequent boots:")
    info(f"  bash {launch_sh}")

# ═════════════════════════════════════════════════════════════════════════════
#  OPTION 4 — VirtualBox
# ═════════════════════════════════════════════════════════════════════════════
def opt_vbox():
    import uuid as _uuid
    print()
    print(box_top("Create VirtualBox VM"))
    print(box_row("Generates a .vbox config + VMDK pointer so you can import"))
    print(box_row("NexOS directly into VirtualBox."))
    print(box_bot())

    iso = require_iso()
    os.makedirs(VMS_DIR, exist_ok=True)

    vm_name  = ask("VM name", "NexOS")
    mem_mb   = ask("RAM (MB)", "512")
    disk_gb  = ask("Disk size (GB)", "8")
    vm_dir   = os.path.join(VMS_DIR, vm_name)
    os.makedirs(vm_dir, exist_ok=True)

    machine_uuid = str(_uuid.uuid4())
    hdd_uuid     = str(_uuid.uuid4())
    dvd_uuid     = str(_uuid.uuid4())
    vbox_path    = os.path.join(vm_dir, f"{vm_name}.vbox")
    vmdk_path    = os.path.join(vm_dir, f"{vm_name}.vmdk")

    step(1, "Creating virtual disk (VMDK)...")
    subprocess.run(
        ["VBoxManage", "createhd", "--filename", vmdk_path,
         "--size", str(int(disk_gb) * 1024), "--format", "VMDK"],
        check=False, capture_output=True)
    if not os.path.exists(vmdk_path):
        open(vmdk_path, "w").close()  # placeholder if VBoxManage absent
        warn("VBoxManage not found — created placeholder VMDK.")

    step(2, "Writing .vbox configuration...")
    vbox_xml = textwrap.dedent(f"""\
        <?xml version="1.0"?>
        <VirtualBox xmlns="http://www.virtualbox.org/" version="1.19-linux">
          <Machine uuid="{{{machine_uuid}}}" name="{vm_name}" OSType="Other_64"
                   snapshotFolder="Snapshots" lastStateChange="2025-01-01T00:00:00Z">
            <MediaRegistry>
              <HardDisks>
                <HardDisk uuid="{{{hdd_uuid}}}" location="{vmdk_path}"
                          format="VMDK" type="Normal"/>
              </HardDisks>
              <DVDImages>
                <Image uuid="{{{dvd_uuid}}}" location="{iso}"/>
              </DVDImages>
            </MediaRegistry>
            <Hardware>
              <CPU count="2"/>
              <Memory RAMSize="{mem_mb}"/>
              <Display VRAMSize="16"/>
              <AudioAdapter enabled="false"/>
              <Network>
                <Adapter slot="0" enabled="true" type="Am79C973"
                         MACAddress="AABBCCDDEEFF">
                  <NAT/>
                </Adapter>
              </Network>
            </Hardware>
            <StorageControllers>
              <StorageController name="IDE" type="PIIX4" PortCount="2">
                <AttachedDevice type="HardDisk" hotpluggable="false"
                                port="0" device="0">
                  <Image uuid="{{{hdd_uuid}}}"/>
                </AttachedDevice>
                <AttachedDevice passthrough="false" type="DVD"
                                port="1" device="0">
                  <Image uuid="{{{dvd_uuid}}}"/>
                </AttachedDevice>
              </StorageController>
            </StorageControllers>
          </Machine>
        </VirtualBox>
    """)
    with open(vbox_path, "w") as f: f.write(vbox_xml)
    ok(f"VirtualBox config: {vbox_path}")
    print()
    info("To import into VirtualBox:")
    info(f'  VBoxManage registervm "{vbox_path}"')
    info(f'  VBoxManage startvm "{vm_name}"')
    info("")
    info("Or: open VirtualBox → Machine → Add → browse to the .vbox file")
    info("")
    info("Boot order: DVD (ISO) first to run the in-OS installer,")
    info("then change to HDD for subsequent boots.")

# ═════════════════════════════════════════════════════════════════════════════
#  OPTION 5 — VMware
# ═════════════════════════════════════════════════════════════════════════════
def opt_vmware():
    print()
    print(box_top("Create VMware VM"))
    print(box_row("Generates a .vmx configuration file for VMware Player,"))
    print(box_row("VMware Workstation, or VMware Fusion (macOS)."))
    print(box_bot())

    iso = require_iso()
    os.makedirs(VMS_DIR, exist_ok=True)

    vm_name = ask("VM name", "NexOS")
    mem_mb  = ask("RAM (MB)", "512")
    disk_gb = ask("Disk size (GB)", "8")
    vm_dir  = os.path.join(VMS_DIR, vm_name)
    os.makedirs(vm_dir, exist_ok=True)

    vmdk_path = os.path.join(vm_dir, f"{vm_name}.vmdk")
    vmx_path  = os.path.join(vm_dir, f"{vm_name}.vmx")

    step(1, "Creating virtual disk (VMDK flat descriptor)...")
    vmdk_descriptor = textwrap.dedent(f"""\
        # Disk DescriptorFile
        version=1
        encoding="UTF-8"
        CID=fffffffe
        parentCID=ffffffff
        createType="monolithicSparse"
        Extent description
        RW {int(disk_gb)*2097152} SPARSE "{vm_name}-flat.vmdk"
        ddb.virtualHWVersion = "14"
        ddb.geometry.cylinders = "{int(disk_gb)*1024}"
        ddb.geometry.heads = "255"
        ddb.geometry.sectors = "63"
        ddb.adapterType = "ide"
        ddb.toolsVersion = "0"
    """)
    with open(vmdk_path, "w") as f: f.write(vmdk_descriptor)
    ok(f"VMDK descriptor: {vmdk_path}")

    step(2, "Writing .vmx configuration...")
    vmx = textwrap.dedent(f"""\
        .encoding = "UTF-8"
        config.version = "8"
        virtualHW.version = "19"
        guestOS = "other-64"
        displayName = "{vm_name}"
        numvcpus = "2"
        cpuid.coresPerSocket = "1"
        memsize = "{mem_mb}"

        # IDE controller
        ide0:0.present = "TRUE"
        ide0:0.filename = "{vmdk_path}"
        ide0:0.deviceType = "disk"
        ide0:0.mode = "persistent"

        # CD-ROM (ISO)
        ide1:0.present = "TRUE"
        ide1:0.filename = "{iso}"
        ide1:0.deviceType = "cdrom-image"
        ide1:0.startConnected = "TRUE"

        # Network
        ethernet0.present = "TRUE"
        ethernet0.connectionType = "nat"
        ethernet0.virtualDev = "e1000"
        ethernet0.addressType = "generated"

        # Video
        svga.vramSize = "16777216"
        mks.enable3d = "FALSE"

        # BIOS
        bios.bootOrder = "cdrom,hdd"

        # Serial (for kernel debug output)
        serial0.present = "TRUE"
        serial0.fileType = "file"
        serial0.fileName = "{os.path.join(vm_dir, 'serial.log')}"
        serial0.tryNoRxLoss = "FALSE"
    """)
    with open(vmx_path, "w") as f: f.write(vmx)
    ok(f"VMware config:  {vmx_path}")
    print()
    info("To open in VMware:")
    info(f'  vmplayer "{vmx_path}"          (VMware Player)')
    info(f'  vmrun start "{vmx_path}"       (command line)')
    info("")
    info("Or: double-click the .vmx file in your file manager.")
    info("")
    info("First boot will load the ISO — select 'Install NexOS' in GRUB")
    info("to install to the virtual disk.")

# ═════════════════════════════════════════════════════════════════════════════
#  OPTION 6 — System info & requirements check
# ═════════════════════════════════════════════════════════════════════════════
def opt_sysinfo():
    print()
    print(box_top("System Check"))
    checks = {
        "qemu-system-x86_64": ("QEMU x86_64",       "required for VM options"),
        "qemu-img":            ("qemu-img",           "required for disk creation"),
        "VBoxManage":          ("VirtualBox CLI",     "optional, for VirtualBox import"),
        "grub-mkrescue":       ("grub-mkrescue",      "required to rebuild ISO"),
        "nasm":                ("NASM assembler",     "required to build kernel"),
        "gcc":                 ("GCC compiler",       "required to build kernel"),
        "dd":                  ("dd",                 "required for USB writing"),
        "lsblk":               ("lsblk",              "Linux drive detection"),
        "diskutil":            ("diskutil",           "macOS drive detection"),
    }
    for cmd, (name, note) in checks.items():
        found = shutil.which(cmd) is not None
        sym   = c(GR, "✔") if found else c(RD, "✖")
        nclr  = WH if found else GY
        print(f"  {sym}  {c(nclr, name):35s}  {c(GY, note)}")
    print()
    info(f"Platform:  {platform.system()} {platform.machine()}")
    info(f"Python:    {sys.version.split()[0]}")
    info(f"Repo root: {REPO_ROOT}")
    if os.path.exists(ISO_PATH):
        size = os.path.getsize(ISO_PATH) // (1024*1024)
        ok(f"ISO:       {ISO_PATH}  ({size} MB)")
    else:
        warn(f"ISO not built yet — run  make iso  in {REPO_ROOT}")

# ═════════════════════════════════════════════════════════════════════════════
#  MAIN MENU
# ═════════════════════════════════════════════════════════════════════════════
MENU = [
    ("1", "Write to USB / Physical Disk",
          "Burn NexOS to a USB stick — boot on any x86-64 PC"),
    ("2", "Run in QEMU  (Quick Launch)",
          "Start NexOS right now in a QEMU window"),
    ("3", "Create Persistent QEMU VM",
          "Build a disk image + launch script for QEMU"),
    ("4", "Create VirtualBox VM",
          "Generate a .vbox file ready to import"),
    ("5", "Create VMware VM",
          "Generate a .vmx file for VMware Player/Workstation/Fusion"),
    ("6", "System Requirements Check",
          "Verify tools are installed"),
    ("0", "Exit", ""),
]

HANDLERS = {"1": opt_usb, "2": opt_qemu_run, "3": opt_qemu_vm,
            "4": opt_vbox, "5": opt_vmware, "6": opt_sysinfo}

def main_menu():
    while True:
        if supports_color():
            os.system("clear" if platform.system() != "Windows" else "cls")
        banner()
        print(line())
        for key, title, desc in MENU:
            key_s  = c(CY, B + f"  [{key}]" + R)
            title_s = c(WH, f"  {title:35s}")
            desc_s  = c(GY, desc) if desc else ""
            print(f"{key_s}{title_s}{desc_s}")
        print(line())
        print()
        choice = ask("Select option").strip()
        if choice == "0":
            print(c(GR, "\n  Goodbye!\n"))
            sys.exit(0)
        handler = HANDLERS.get(choice)
        if handler:
            handler()
            print()
            input(c(GY, "  Press Enter to return to the menu..."))
        else:
            warn("Invalid option — choose 0-6")
            time.sleep(0.8)

def main():
    signal.signal(signal.SIGINT, lambda *_: abort())
    if "--help" in sys.argv or "-h" in sys.argv:
        print("NexOS Installer v1.0")
        print("Usage:  python3 installer.py")
        print("Options passed as first arg:")
        for k, t, _ in MENU[:-1]:
            print(f"  {k}  {t}")
        sys.exit(0)
    if len(sys.argv) > 1 and sys.argv[1] in HANDLERS:
        HANDLERS[sys.argv[1]]()
        sys.exit(0)
    main_menu()

if __name__ == "__main__":
    main()
