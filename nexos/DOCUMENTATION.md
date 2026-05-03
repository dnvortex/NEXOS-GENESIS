# NexOS — Full Technical Documentation

**Version 0.1.0 · MIT License · x86_64**

---

## Table of Contents

1. [Overview](#1-overview)
2. [Project Structure](#2-project-structure)
3. [Building and Running](#3-building-and-running)
4. [Boot Process](#4-boot-process)
5. [Kernel Initialization Sequence](#5-kernel-initialization-sequence)
6. [Architecture Layer — x86_64](#6-architecture-layer--x86_64)
7. [Memory Management](#7-memory-management)
8. [Device Drivers](#8-device-drivers)
9. [Filesystem Layer](#9-filesystem-layer)
10. [Process Management](#10-process-management)
11. [System Call Interface](#11-system-call-interface)
12. [Networking Stack](#12-networking-stack)
13. [GUI Subsystem](#13-gui-subsystem)
14. [Framebuffer & Drawing API](#14-framebuffer--drawing-api)
15. [Font Renderer](#15-font-renderer)
16. [Animation Library](#16-animation-library)
17. [Built-in Applications](#17-built-in-applications)
18. [NexOS Shell (nsh)](#18-nexos-shell-nsh)
19. [Package Manager (npkg)](#19-package-manager-npkg)
20. [Installer](#20-installer)
21. [Colour Palette](#21-colour-palette)
22. [Kernel Logging](#22-kernel-logging)
23. [Known Limitations](#23-known-limitations)
24. [Bug Fix Log](#24-bug-fix-log)

---

## 1. Overview

NexOS is a monolithic, preemptive, single-address-space operating system for the **x86_64** architecture, written entirely from scratch in **C11**, **NASM assembly**, and **Python** (build tooling only). It boots via **GRUB2 Multiboot2**, runs in 64-bit long mode, and presents a fully hardware-accelerated **framebuffer GUI** without any third-party userspace, libc, or OS primitives.

Key properties:

| Property | Value |
|---|---|
| Target ISA | x86_64 (AMD64) |
| Boot protocol | GRUB2 Multiboot2 |
| Kernel model | Monolithic, single ring-0 address space |
| Memory model | Identity-mapped lower 32 MB (16 × 2 MB huge pages) + kernel at –2 GB higher half |
| FPU/SSE | Disabled throughout (`-mno-sse -mno-sse2`) — all arithmetic is integer-only |
| Compiler | GCC 14 · `-std=c11 -ffreestanding -O2 -mcmodel=kernel -nostdlib -nostdinc` |
| Assembler | NASM 2.16 |
| Framebuffer | 1024 × 768 × 32 bpp linear, requested from GRUB via Multiboot2 tag type 5 |
| Default theme | Catppuccin Mocha |

---

## 2. Project Structure

```
nexos/
├── boot/
│   └── linker.ld                  Kernel linker script
├── kernel/
│   ├── kernel.c                   Kernel entry point (kernel_main)
│   ├── kernel.h                   Shared definitions, klog, I/O helpers
│   ├── arch/x86_64/
│   │   ├── boot.asm               Multiboot2 header, A20, long-mode setup, PML4
│   │   ├── gdt.c / gdt.h          GDT + TSS (5 descriptors)
│   │   ├── idt.c / idt.h          IDT, ISR/IRQ stubs, CPU exception handlers
│   │   ├── isr.asm                Raw interrupt entry stubs (saves all GPRs)
│   │   ├── gdt_flush.asm          lgdt / far-jump to reload CS
│   │   ├── paging.c / paging.h    4-level paging, vmm_map_page, CR3
│   │   └── enter_ring3.asm        IRET-based ring-3 entry stub
│   ├── drivers/
│   │   ├── vga.c / vga.h          VGA text-mode 80×25 (used pre-FB)
│   │   ├── serial.c / serial.h    COM1 debug output (115200 baud)
│   │   ├── fb.c / fb.h            Linear framebuffer, drawing primitives
│   │   ├── font.c / font.h        8×16 bitmap font renderer
│   │   ├── console.c / console.h  Framebuffer scrolling text console
│   │   ├── keyboard.c / keyboard.h PS/2 keyboard, scancode set 1
│   │   ├── mouse.c / mouse.h      PS/2 mouse (3-byte packet, IRQ12)
│   │   ├── timer.c / timer.h      PIT 8253 at 1000 Hz
│   │   ├── rtc.c / rtc.h          CMOS real-time clock
│   │   ├── ata.c / ata.h          ATA/IDE PIO (primary master + slave)
│   │   ├── pci.c / pci.h          PCI type-1 config space scan
│   │   ├── rtl8139.c / rtl8139.h  RTL8139 NIC driver (TX/RX ring)
│   │   └── wifi.h                 Wi-Fi stub (scan/connect interface)
│   ├── mm/
│   │   ├── pmm.c / pmm.h          Physical memory manager (bitmap allocator)
│   │   ├── heap.c / heap.h        Kernel heap (free-list, kmalloc/kfree)
│   │   └── vmm.c / vmm.h          Virtual memory manager (high-level API)
│   ├── fs/
│   │   ├── vfs.c / vfs.h          VFS abstraction layer
│   │   ├── ramfs.c / ramfs.h      RAM filesystem (root /)
│   │   ├── fat32.c / fat32.h      FAT32 read/write on ATA
│   │   └── procfs.c / procfs.h    /proc pseudo-filesystem
│   ├── proc/
│   │   ├── process.c / process.h  PCB, fork/create, fd table
│   │   ├── scheduler.c            Round-robin scheduler
│   │   └── syscall.c / syscall.h  Linux x86_64 ABI syscall dispatcher
│   ├── net/
│   │   ├── net.c / net.h          Network subsystem init
│   │   ├── netif.c / netif.h      Network interface registry
│   │   ├── ethernet.c / ethernet.h Ethernet II framing
│   │   ├── arp.c / arp.h          ARP (request + cache)
│   │   ├── ip.c / ip.h            IPv4 (TX/RX, checksum)
│   │   ├── icmp.c / icmp.h        ICMP echo (ping)
│   │   ├── udp.c / udp.h          UDP (TX/RX)
│   │   ├── dns.c / dns.h          DNS A-record resolver
│   │   ├── tcp.c / tcp.h          TCP (connect, send, recv, close)
│   │   └── http.c / http.h        HTTP/1.0 GET client
│   ├── gui/
│   │   ├── gui.c / gui.h          Main GUI event loop
│   │   ├── wm.c / wm.h            Window manager
│   │   ├── desktop.c / desktop.h  Animated desktop background
│   │   ├── taskbar.c / taskbar.h  Bottom taskbar (40 px)
│   │   ├── launcher.c / launcher.h App launcher overlay
│   │   ├── notif.c / notif.h      Toast notification system
│   │   ├── anim.h                 Integer easing / colour helpers (header-only)
│   │   ├── term_app.c             Terminal emulator
│   │   ├── files_app.c            File manager
│   │   ├── sysinfo_app.c          System information
│   │   ├── browser_app.c          HTTP browser
│   │   ├── calc_app.c             Calculator
│   │   ├── clock_app.c            Analog/digital clock
│   │   ├── theme_app.c            Theme switcher (4 colour schemes)
│   │   ├── edit_app.c             Text editor
│   │   ├── viz_app.c              Audio/signal visualizer
│   │   ├── snake_app.c            Snake game
│   │   └── sysmon_app.c           System monitor (CPU/heap/PMM sparklines)
│   ├── pkg/
│   │   ├── npkg.c / npkg.h        Package manager (NXPK binary format)
│   │   └── pkg_store.c / pkg_store.h Built-in package store
│   └── installer/
│       └── installer.c / installer.h TUI disk installer
├── userspace/
│   ├── init/
│   │   └── init.c                 PID 1 — mounts, /bin stubs, GUI launch
│   ├── shell/
│   │   └── nsh.c                  NexOS shell (42 built-ins)
│   └── libc/
│       └── stdio.c                Minimal libc stubs (unused at runtime)
├── tools/
│   └── build_iso.sh               Wraps grub-mkrescue
└── Makefile                       Full build system
```

---

## 3. Building and Running

### Prerequisites

```
gcc >= 14          (x86_64-elf cross-compiler or native)
nasm >= 2.16
grub-mkrescue
xorriso
qemu-system-x86_64
python3            (Makefile tab-normalisation helper)
```

### Build

```bash
cd nexos
make -j$(nproc)
```

The Makefile compiles every `.c` and `.asm` source under `kernel/` and `userspace/`, links with `boot/linker.ld`, then calls `tools/build_iso.sh` to produce:

```
build/nexos.kernel   — ELF64 kernel binary
build/nexos.iso      — Bootable Multiboot2 ISO (~11 MB)
```

Compiler flags for all C files:

```
-std=c11 -ffreestanding -fno-stack-protector -fno-pic
-mno-red-zone -mno-mmx -mno-sse -mno-sse2
-Wall -Wextra -O2 -m64 -mcmodel=kernel
-nostdlib -nostdinc
-Wno-unused-parameter -Wno-unused-variable
```

### Run in QEMU

```bash
# With GUI (VGA virtio)
qemu-system-x86_64 \
  -machine q35 -m 256M \
  -cdrom build/nexos.iso -boot d \
  -serial stdio \
  -vga virtio \
  -cpu qemu64 \
  -netdev user,id=net0 -device rtl8139,netdev=net0

# Headless (serial log only)
qemu-system-x86_64 \
  -machine q35 -m 256M \
  -cdrom build/nexos.iso -boot d \
  -serial file:/tmp/nexos.log \
  -display none -no-reboot -vga virtio \
  -cpu qemu64 \
  -netdev user,id=net0 -device rtl8139,netdev=net0
```

### Trigger installer mode

Add `nexos.install` to the GRUB kernel command line. The installer TUI starts before the GUI.

---

## 4. Boot Process

```
GRUB2 (Multiboot2)
  │
  └── boot.asm  (32-bit protected mode entry)
        1. Validate Multiboot2 magic (0x36d76289 in EBX)
        2. Enable A20 line (BIOS fast-gate 0x92)
        3. Build 4-level page tables in BSS:
             PML4[0]  → PDPT    (identity: virt == phys for low 1 GB)
             PML4[511]→ PDPT    (higher half: virt 0xFFFF800000000000+)
             PDPT[0]  → PD      (two 2 MB huge-page entries → 0..4 MB)
             PD[0..15]→ huge    (16 × 2 MB = 32 MB identity map)
        4. Load minimal 3-entry GDT (null, code64, data64)
        5. Set CR3 = &boot_pml4
        6. Enable PAE (CR4.PAE), set EFER.LME, enable paging (CR0.PG)
        7. Far-jump to 64-bit code segment → long_mode_entry
        
  └── long_mode_entry  (64-bit)
        1. Reload all segment registers (DS/ES/FS/GS/SS = 0x10)
        2. Set RSP = stack_top  (16 KB BSS stack)
        3. Write 'K' sentinel to VGA text buffer (0xB8000) — proves long mode
        4. Call kernel_main(mb2_info_phys, mb2_magic)
```

Stack at boot: 16 KB static BSS region `stack_bottom .. stack_top`.

---

## 5. Kernel Initialization Sequence

`kernel_main()` in `kernel/kernel.c` runs the following sequence in order:

| Step | Subsystem | Notes |
|---|---|---|
| 1 | VGA text mode | 80×25 colour console for early diagnostics |
| 2 | Serial (COM1) | 115200 8N1; all `klog()` output mirrors here |
| 3 | GDT | 5 descriptors: null, kernel code/data, user code/data, TSS |
| 4 | IDT | 256 gates; CPU exceptions 0–21, IRQs 0–15 via PIC remap |
| 5 | PMM | Walks Multiboot2 memory map; marks kernel image as used |
| 6 | Paging | Re-initialises full kernel page tables; VMM ready |
| 7 | Heap | Fixed region `0x1200000..0x1A00000` (8 MB) |
| 8 | VMM | High-level virtual mapping API initialised |
| 9 | Timer (PIT) | Programmed to 1000 Hz; IRQ0 drives `timer_get_ticks()` |
| 10 | Keyboard | IRQ1 handler, scancode set 1, 256-byte circular buffer |
| 11 | RTC | Reads CMOS date/time |
| 12 | ATA | Probes primary master + slave via PIO identify |
| 13 | PCI | Type-1 config space scan (all 256 buses × 32 devices × 8 funcs) |
| 14 | Framebuffer | Address, pitch, width, height, bpp from Multiboot2 tag type 8 |
| 15 | Console | Framebuffer text console replaces VGA for further boot messages |
| 16 | VFS | Root filesystem created |
| 17 | ramfs | Mounted at `/`; creates `/etc`, `/bin`, `/dev`, `/var`, `/proc`, `/tmp` |
| 18 | procfs | Mounted at `/proc` |
| 19 | Network | RTL8139 detected via PCI, MAC read, netif registered, ARP/IP init |
| 20 | Processes | PCB table zeroed; PID 0 (idle) created |
| 21 | Syscalls | INT 0x80 handler registered in IDT |
| 22 | Installer? | If cmdline contains `nexos.install`, `installer_run()` is called |
| 23 | init (PID 1) | `proc_create("init", init_main, 1)` |
| 24 | Scheduler | Starts round-robin loop; idle task = `hlt` spin |

`init_main()` in `userspace/init/init.c`:

1. Logs heap free space.
2. Creates `/etc/hostname`, `/etc/os-release`, `/etc/motd`.
3. Populates `/bin` with 12 text-stub entries (one per app name).
4. Creates `/dev/null`, `/dev/zero`, `/dev/tty`.
5. Attempts FAT32 mount on ATA primary master (drive 0) then slave (drive 1).
6. Calls `gui_main()` → enters the GUI event loop.

---

## 6. Architecture Layer — x86_64

### 6.1 GDT (`kernel/arch/x86_64/gdt.c`)

Five descriptors at fixed selectors:

| Selector | Description |
|---|---|
| `0x00` | Null descriptor |
| `0x08` `GDT_KERNEL_CODE` | 64-bit ring-0 execute/read |
| `0x10` `GDT_KERNEL_DATA` | Ring-0 read/write |
| `0x18` `GDT_USER_CODE` | 64-bit ring-3 execute/read |
| `0x20` `GDT_USER_DATA` | Ring-3 read/write |
| `0x28` `GDT_TSS` | 64-bit TSS (16-byte system descriptor) |

The TSS `RSP0` field is updated by `tss_set_rsp0(rsp)` each time a process is scheduled, pointing to its kernel stack for syscall/interrupt entry.

### 6.2 IDT (`kernel/arch/x86_64/idt.c`)

- 256 IDT gates, all using selector `0x08`, DPL 0 (exceptions) or DPL 3 (INT 0x80 syscall gate).
- CPU exceptions 0–21 are handled with descriptive `kpanic()` messages.
- PIC remapped: master IRQs → vectors 32–39, slave IRQs → 40–47.
- IRQ0 (timer), IRQ1 (keyboard), IRQ4 (serial), IRQ12 (mouse), IRQ14/15 (ATA), IRQ11 (RTL8139) are all handled.
- `isr.asm` pushes all 15 GPRs onto the stack before calling the C handler, producing a `registers_t` struct.

### 6.3 Paging (`kernel/arch/x86_64/paging.c`)

- 4-level paging: PML4 → PDPT → PD → PT.
- Boot stub uses 2 MB huge pages (PD entries, bit 7 set) for the initial 32 MB identity map.
- `paging_init()` sets up the full kernel page tables and switches CR3.
- `vmm_map_page(pml4, virt, phys, flags)` allocates intermediate tables from PMM as needed.
- `VMM_FLAG_WRITE` (bit 1), `VMM_FLAG_USER` (bit 2) are the two active flag bits. `VMM_FLAG_NX` (bit 63) is defined but not enforced on current hardware.

---

## 7. Memory Management

### 7.1 Physical Memory Manager (`kernel/mm/pmm.c`)

Algorithm: **bitmap allocator** — one bit per 4 KB physical page.

```c
void     pmm_init(uint64_t mem_lower, uint64_t mem_upper);
void     pmm_init_region(uint64_t base, uint64_t size);   // mark free
void     pmm_deinit_region(uint64_t base, uint64_t size); // mark used
uint64_t pmm_alloc_page(void);   // returns physical address, or 0 on OOM
void     pmm_free_page(uint64_t addr);
uint64_t pmm_get_free_memory(void);   // bytes
uint64_t pmm_get_total_memory(void);  // bytes
uint64_t pmm_get_free_frames(void);   // page count
void     pmm_print_map(void);         // serial dump
```

The Multiboot2 memory map is walked: each entry with `mtype == 1` is passed to `pmm_init_region()`. Then `pmm_deinit_region()` re-marks the kernel image (`kernel_start..kernel_end`) and the heap window as used.

### 7.2 Kernel Heap (`kernel/mm/heap.c`)

Algorithm: **explicit free-list** with coalescing, best-fit allocation.

| Constant | Value |
|---|---|
| `HEAP_START` | `0x1200000` (physical 18 MB) |
| `HEAP_SIZE` | `8 MB` (`8U * 1024U * 1024U`) |

```c
void   heap_init(void *start, size_t size);
void  *kmalloc(size_t size);
void   kfree(void *ptr);
void  *krealloc(void *ptr, size_t size);
void  *kmalloc_aligned(size_t size, size_t align);  // align must be power-of-2
void   kfree_aligned(void *ptr);  // must be paired with kmalloc_aligned
size_t heap_free_space(void);     // walks free-list; returns total free bytes
```

**`kmalloc_aligned` contract:**  
Allocates `size + align + sizeof(void*)` bytes raw, then stores the original base pointer in the `sizeof(void*)` slot immediately preceding the aligned return address. The caller **must** call `kfree_aligned()` (not `kfree()`) to free the result. Mixing them will corrupt the heap.

### 7.3 Virtual Memory Manager (`kernel/mm/vmm.c`)

High-level wrapper over `paging.c`:

```c
void     vmm_init(void);
void     vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void     vmm_unmap(uint64_t virt);
uint64_t vmm_phys(uint64_t virt);
uint64_t vmm_create_address_space(void);     // allocates new PML4, copies kernel upper half
void     vmm_switch_address_space(uint64_t pml4_phys); // writes to CR3
```

---

## 8. Device Drivers

### 8.1 Serial (`kernel/drivers/serial.c`)

COM1 at I/O base `0x3F8`, 115200 baud, 8N1.  
Used exclusively for kernel debug output via `serial_printf()` and the `klog()` mirror. Not exposed to userspace.

### 8.2 VGA Text Mode (`kernel/drivers/vga.c`)

80×25 colour text console at `0xB8000`. Used during early boot before the framebuffer is initialised. `klog()` also writes to VGA during the early boot phase.

### 8.3 Programmable Interval Timer — PIT (`kernel/drivers/timer.c`)

8253 PIT programmed to **1000 Hz** (IRQ0). Provides:

```c
void     timer_init(uint32_t freq_hz);
void     timer_sleep_ms(uint32_t ms);   // busy-spins (interrupts enabled)
uint64_t timer_get_ticks(void);         // milliseconds since boot
uint64_t timer_get_uptime_seconds(void);
```

### 8.4 Real-Time Clock (`kernel/drivers/rtc.c`)

Reads CMOS via I/O ports `0x70`/`0x71`. BCD decoded automatically.

```c
void rtc_init(void);
void rtc_get_time(rtc_time_t *t);             // populates second/minute/hour/day/month/year
void rtc_time_to_string(char *buf, const rtc_time_t *t);  // "YYYY-MM-DD HH:MM:SS"
```

### 8.5 PS/2 Keyboard (`kernel/drivers/keyboard.c`)

- Scancode Set 1, IRQ1.
- 256-byte circular ring buffer (head/tail byte indices).
- Modifier state tracked: `shift_down`, `ctrl_down`, `alt_down`, `caps_lock`.
- Extended (0xE0-prefixed) scancodes produce `KEY_*` constants.
- **Both Left and Right** Ctrl/Alt track the same `ctrl_down` / `alt_down` flag.

```c
void keyboard_init(void);
char keyboard_getchar(void);      // blocks until a key is available
int  keyboard_available(void);    // non-blocking: 1 if buffer non-empty
```

Extended key constants (`char` values with high bit set):

| Constant | Value | Key |
|---|---|---|
| `KEY_UP` | `0x80` | ↑ Arrow |
| `KEY_DOWN` | `0x81` | ↓ Arrow |
| `KEY_LEFT` | `0x82` | ← Arrow |
| `KEY_RIGHT` | `0x83` | → Arrow |
| `KEY_HOME` | `0x84` | Home |
| `KEY_END` | `0x85` | End |
| `KEY_PGUP` | `0x86` | Page Up |
| `KEY_PGDN` | `0x87` | Page Down |
| `KEY_DEL` | `0x88` | Delete |

**Note:** Because these values have the high bit set, they are negative when interpreted as `signed char`. Always compare with explicit casts: `if (key == KEY_UP)` works because `KEY_UP` is itself a `(char)` cast.

### 8.6 PS/2 Mouse (`kernel/drivers/mouse.c`)

- 3-byte packet protocol, IRQ12.
- Outputs absolute screen position clamped to framebuffer bounds.
- Buttons: left click, right click.
- Mouse cursor drawn by the window manager.

### 8.7 ATA/IDE (`kernel/drivers/ata.c`)

PIO mode. Probes primary master (drive 0) and primary slave (drive 1) via IDENTIFY.

```c
void ata_init(void);
int  ata_read_sectors(int drive, uint32_t lba, uint8_t count, void *buf);
int  ata_write_sectors(int drive, uint32_t lba, uint8_t count, const void *buf);
```

Drive indices: `ATA_PRIMARY_MASTER = 0`, `ATA_PRIMARY_SLAVE = 1`.  
Returns 0 on success, –1 on error (timeout or status error bits).

### 8.8 PCI (`kernel/drivers/pci.c`)

Type-1 configuration space scan. Scans all 256 buses × 32 devices × 8 functions at boot. Devices are stored in a table accessible by vendor/device ID. Used by the RTL8139 driver to locate its I/O BAR.

### 8.9 RTL8139 NIC (`kernel/drivers/rtl8139.c`)

- Auto-detects via PCI scan (vendor `0x10EC`, device `0x8139`).
- Reads 6-byte MAC from I/O port.
- TX: 4 descriptor ring (each up to 1500 bytes).
- RX: 8 KB ring buffer; packets delivered to the Ethernet layer via callback.
- IRQ11 (or PCI-assigned IRQ).

### 8.10 Wi-Fi (`kernel/drivers/wifi.h`)

Stub interface. Provides `wifi_scan()`, `wifi_connect(ssid, passphrase)`, `wifi_disconnect()`, and `wifi_get_status()`. No hardware driver is implemented; the shell commands and GUI expose the API for future expansion.

---

## 9. Filesystem Layer

### 9.1 VFS (`kernel/fs/vfs.c`)

Thin abstraction layer. Every file, directory, device, and pseudo-file is a `vfs_node_t`:

```c
typedef struct vfs_node {
    char         name[256];
    uint32_t     type;      // VFS_NODE_FILE | DIR | CHARDEV | BLKDEV | PIPE | SYMLINK | MOUNT
    uint64_t     size;
    uint32_t     inode;
    void        *priv;      // filesystem-private data pointer
    // function pointers:
    vfs_read_fn    read;
    vfs_write_fn   write;
    vfs_open_fn    open;
    vfs_close_fn   close;
    vfs_finddir_fn finddir;
    vfs_readdir_fn readdir;
    vfs_mkdir_fn   mkdir;
    vfs_create_fn  create;
    vfs_unlink_fn  unlink;
} vfs_node_t;
```

Public API:

```c
vfs_node_t *vfs_open(const char *path, int flags);
void        vfs_close(vfs_node_t *node);
uint32_t    vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buf);
uint32_t    vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size, const uint8_t *buf);
int         vfs_readdir(vfs_node_t *node, uint32_t idx, vfs_dirent_t *dirent);
int         vfs_mkdir(const char *path);
int         vfs_create(const char *path, uint32_t flags);
int         vfs_stat(const char *path, vfs_stat_t *stat);
int         vfs_unlink(const char *path);
int         vfs_mount(const char *path, vfs_node_t *fs_root);
vfs_node_t *vfs_get_root(void);
```

Up to `VFS_MAX_MOUNTS = 16` simultaneous mount points. Path resolution is absolute; symlinks are not followed.

### 9.2 RAM Filesystem (`kernel/fs/ramfs.c`)

In-memory filesystem backing the root `/`. Nodes and file data are allocated from the kernel heap with `kmalloc`. Files are stored as contiguous byte buffers (no block structure). Supports create, mkdir, read, write, unlink, readdir.

### 9.3 FAT32 (`kernel/fs/fat32.c`)

Read/write FAT32 driver for ATA disks. Supports:
- BPB parsing (Volume Boot Record, FAT type detection).
- Cluster chain traversal.
- Directory entry read/write (8.3 short names; no LFN).
- File read, file write (existing files), directory listing.

Mounted by `init` at `/mnt/disk0` if ATA drive 0 has a valid FAT32 BPB. Drive 1 is tried at `/mnt/disk1` if drive 0 fails.

**In QEMU without a disk image,** FAT32 mount silently fails with `[ERROR] FAT32: failed to read BPB from drive N LBA 0`. This is expected and non-fatal; the system continues on ramfs alone.

### 9.4 Proc Filesystem (`kernel/fs/procfs.c`)

Mounted at `/proc`. Read-only pseudo-files synthesised on demand:

| Path | Contents |
|---|---|
| `/proc/cpuinfo` | Vendor string, model, bogomips |
| `/proc/meminfo` | PMM total/free, heap total/free |
| `/proc/uptime` | Seconds since boot |
| `/proc/version` | NexOS version string |
| `/proc/mounts` | Active VFS mount table |
| `/proc/<pid>/status` | PID, name, state, priority for each live process |

---

## 10. Process Management

### 10.1 Process Control Block (`kernel/proc/process.h`)

```c
typedef struct process {
    uint32_t      pid;
    uint32_t      ppid;
    proc_state_t  state;          // RUNNING | READY | BLOCKED | ZOMBIE | DEAD
    cpu_context_t context;        // all 15 GPRs + RIP, RFLAGS, CS, SS, CR3
    uint8_t      *stack;          // 4 KB kernel stack
    uint64_t      stack_size;
    uint64_t      user_stack;     // 1-page user stack base
    uint64_t      user_stack_top;
    uint64_t      cr3;            // per-process PML4 physical address
    vfs_node_t   *fds[16];        // open file descriptors
    char          cwd[1024];      // current working directory
    int           exit_code;
    uint8_t       priority;       // 0 (low) .. 255 (high)
    uint32_t      time_slice_ms;
    uint32_t      time_used_ms;
    char          name[64];
    uint64_t      fd_offsets[16]; // per-fd seek positions
    uint64_t      brk;            // program break for sys_brk
    uint64_t      mmap_base;      // base for anonymous mmap allocations
    uint32_t      umask;
} process_t;
```

Limits: `MAX_PROCESSES = 64`, `MAX_FDS = 16 per process`.

### 10.2 API

```c
void       proc_init(void);
process_t *proc_create(const char *name, void (*entry)(void), uint8_t priority);
void       proc_enter_ring3(process_t *proc);     // IRET to user mode
void       proc_exit(int code);                   // sets state = ZOMBIE
process_t *proc_get_current(void);
process_t *proc_get_by_pid(uint32_t pid);
void       proc_kill(uint32_t pid);
int        proc_open_fd(process_t *proc, vfs_node_t *node);  // returns fd index
void       proc_close_fd(process_t *proc, int fd);
```

### 10.3 Scheduler (`kernel/proc/scheduler.c`)

Simple **round-robin** scheduler. Called from the timer IRQ0 handler every tick. Iterates `processes[]`, finds the next `PROC_READY` entry, saves the current context, and restores the next. Each process gets a time slice proportional to its `priority` field.

---

## 11. System Call Interface

**ABI:** Linux x86_64 `INT 0x80`  
`rax` = syscall number  
`rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` = arguments 1–6  
`rax` on return = result (negative `uint64_t` = `-errno`)

NexOS implements the following Linux-compatible syscalls:

| Number | Name | Description |
|---|---|---|
| 0 | `read` | Read from fd |
| 1 | `write` | Write to fd |
| 2 | `open` | Open file by path |
| 3 | `close` | Close fd |
| 4 | `stat` | File status (144-byte Linux `struct stat`) |
| 5 | `fstat` | File status from fd |
| 8 | `lseek` | Set fd position (SEEK_SET/CUR/END) |
| 9 | `mmap` | Anonymous page allocation (MAP_ANONYMOUS) |
| 11 | `munmap` | Release mmap'd region |
| 12 | `brk` | Set program break |
| 16 | `ioctl` | `TIOCGWINSZ` (returns 1024×768); terminal ioctls no-op'd |
| 20 | `writev` | Scatter write |
| 21 | `access` | Check file accessibility |
| 22 | `pipe` | Not implemented (returns `ENOSYS`) |
| 32 | `dup` | Duplicate fd |
| 33 | `dup2` | Duplicate fd to specific slot |
| 39 | `getpid` | Returns current PID |
| 41 | `socket` | Returns `ENOSYS` |
| 56 | `clone` | Minimal clone (creates new process) |
| 57 | `fork` | Returns `ENOSYS` |
| 59 | `execve` | Returns `ENOSYS` |
| 60 | `exit` | Terminates current process |
| 61 | `wait4` | Wait for child PID |
| 63 | `uname` | Returns `struct utsname` with NexOS strings |
| 72 | `fcntl` | File flags (F_GETFL/F_SETFL, F_DUPFD) |
| 78 | `getdents` | Read directory entries |
| 80 | `chdir` | Change current directory |
| 83 | `mkdir` | Create directory |
| 84 | `rmdir` | Remove directory |
| 85 | `creat` | Create file |
| 87 | `unlink` | Delete file |
| 89 | `readlink` | Read symbolic link target |
| 96 | `gettimeofday` | Returns `struct timeval` from RTC |
| 102 | `getuid` | Returns 0 (root) |
| 104 | `getgid` | Returns 0 (root) |
| 107 | `geteuid` | Returns 0 |
| 108 | `getegid` | Returns 0 |
| 110 | `getppid` | Returns parent PID |
| 158 | `arch_prctl` | Sets FS base for TLS |
| 160 | `setrlimit` | Accepts but ignores |
| 161 | `getrlimit` | Returns stub limits |
| 186 | `gettid` | Returns current PID |
| 217 | `getdents64` | Directory listing (64-bit dirent) |
| 228 | `clock_gettime` | Returns `struct timespec` from timer/RTC |
| 231 | `exit_group` | Terminates process |
| 302 | `prlimit64` | Returns stub limits |

Errno constants follow the Linux ABI: `EPERM=1`, `ENOENT=2`, `EBADF=9`, `ENOMEM=12`, `ENOSYS=38`, etc.

---

## 12. Networking Stack

All layers are built on top of the RTL8139 driver. Packets flow:

```
RTL8139 RX IRQ
  └── ethernet_receive()
        └── arp_receive()      (type 0x0806)
        └── ip_receive()       (type 0x0800)
              └── icmp_receive()   (proto 1)
              └── udp_receive()    (proto 17)
              └── tcp_receive()    (proto 6)
```

### Network Interface Registry (`kernel/net/netif.c`)

```c
int      netif_register(const char *name, const uint8_t mac[6], int flags);
void     netif_set_up(const char *name);
int      netif_is_up(void);
netif_t *netif_get_default(void);
netif_t *netif_find(const char *name);
```

### ARP (`kernel/net/arp.c`)

Full ARP request/reply with a 16-entry cache. `arp_resolve(ip, mac_out)` sends an ARP request and blocks up to 500 ms for a reply.

### IPv4 (`kernel/net/ip.c`)

TX: `ip_send(proto, dst_ip, data, len)` — fills header, computes checksum, calls `ethernet_send`.  
RX: `ip_receive(frame, len)` — validates checksum, dispatches to ICMP/UDP/TCP.

IPv4 address is set at boot from DHCP (if available) or defaults to `10.0.2.15/24` (QEMU user-mode NAT address).

### ICMP (`kernel/net/icmp.c`)

Echo request/reply only. `icmp_ping(ip, timeout_ms)` returns round-trip time in ms or –1 on timeout.

### UDP (`kernel/net/udp.c`)

`udp_send(dst_ip, src_port, dst_port, data, len)`  
`udp_recv(buf, maxlen, src_ip_out, src_port_out, timeout_ms)` — polls for an incoming packet.

### DNS (`kernel/net/dns.c`)

A-record resolver over UDP port 53. Uses `8.8.8.8` as the default nameserver.

```c
int dns_resolve(const char *hostname, uint8_t ip_out[4]);
// Returns 0 on success, -1 on timeout/NXDOMAIN.
```

### TCP (`kernel/net/tcp.c`)

Minimal RFC 793 three-way handshake (SYN → SYN-ACK → ACK), data transfer, and FIN close. No congestion control. Single-connection at a time.

```c
int  tcp_connect(tcp_conn_t *conn, uint32_t ip, uint16_t port);
int  tcp_send(tcp_conn_t *conn, const uint8_t *data, uint16_t len);
int  tcp_recv(tcp_conn_t *conn, uint8_t *buf, uint16_t maxlen, uint32_t timeout_ms);
void tcp_close(tcp_conn_t *conn);
```

`tcp_conn_t` is caller-allocated (stack or heap). RX buffer is 8 KB.

### HTTP (`kernel/net/http.c`)

HTTP/1.0 GET client built on top of TCP + DNS.

```c
http_response_t *http_get(const char *url);   // url must start with "http://"
void             http_free(http_response_t *r);

typedef struct {
    int      status_code;
    uint32_t body_len;
    uint8_t *body;    // kmalloc'd — caller must call http_free()
} http_response_t;
```

---

## 13. GUI Subsystem

### 13.1 Main Loop (`kernel/gui/gui.c`)

`gui_main()` runs a soft real-time loop at approximately 30 fps (33 ms per frame):

```
loop:
  1. mouse_get_state()  → wm_handle_mouse() / wm_handle_mouse_release()
  2. keyboard_available() → wm_handle_key()
  3. taskbar_update() + taskbar_draw()
  4. launcher_tick() + launcher_draw()  (if visible)
  5. notif_tick() + notif_draw()
  6. wm_render_all()
  7. timer_sleep_ms(frame_remainder)
```

`fb_scene_dirty` flag forces a full `desktop_draw()` on the next frame when set.

### 13.2 Window Manager (`kernel/gui/wm.c`)

Supports up to `WM_MAX_WINDOWS = 16` windows. Windows are stored in a linked list ordered front-to-back.

```c
window_t *wm_new(int x, int y, int w, int h, const char *title);
void      wm_close(window_t *win);
void      wm_focus(window_t *win);
void      wm_raise(window_t *win);
void      wm_minimize(window_t *win);
void      wm_move(window_t *win, int x, int y);
void      wm_resize(window_t *win, int w, int h);
void      wm_invalidate(window_t *win);     // marks dirty for repaint
void      wm_render_all(void);             // composites all windows to FB
void      wm_handle_mouse(int x, int y, int left, int right);
void      wm_handle_mouse_release(int x, int y);
void      wm_handle_key(char key);
window_t *wm_focused(void);
void      wm_toggle_maximize(window_t *win);
int       wm_window_count(void);
window_t *wm_get_window(int idx);
```

**Window layout:**  
Each window has a 32 px titlebar (`WM_TITLEBAR_H = 32`) containing:
- Close button (red, `WM_BTN_R = 7` px radius)
- Minimise button (yellow)
- Maximise/restore button (green)

Windows are draggable by their titlebar. Maximise fills the screen above the taskbar.

**Callbacks** set on each `window_t`:
- `on_paint(win)` — called every frame when the window is dirty
- `on_click(win, x, y, btn)` — client-area click (coordinates relative to client top-left)
- `on_key(win, key)` — key pressed while window is focused
- `on_close(win)` — close button pressed

### 13.3 Desktop (`kernel/gui/desktop.c`)

Animated gradient background with floating particle dots. `desktop_set_phase(delta_ms)` advances the animation. `desktop_paint_rect(x, y, w, h)` repaints only an exposed region (used after window drag to avoid full redraws).

### 13.4 Taskbar (`kernel/gui/taskbar.c`)

40 px strip at the bottom of the screen (`TB_H = 40`). Contains:
- NexOS logo / launcher toggle button (left)
- Window button strip (one button per open window, shows title, click to focus/restore)
- System clock (right, updates every second via RTC)

### 13.5 Launcher (`kernel/gui/launcher.c`)

Full-screen app-launcher overlay. Triggered by clicking the taskbar logo or pressing the Super key equivalent.

- 11 apps arranged in a 3-column grid.
- Cards are 160 × 80 px with rounded corners, accent-colour icon, and name label.
- Mouse hover highlights cards; click spawns the app window.
- Keyboard: arrow keys navigate, Enter opens, Escape closes.
- Smooth slide-in/out animation (30 fps).

App grid layout (row × column):

| Row | Col 0 | Col 1 | Col 2 |
|---|---|---|---|
| 0 | Terminal | Files | System Info |
| 1 | Browser | Calculator | Clock |
| 2 | Theme | Text Editor | Visualizer |
| 3 | Snake | System Monitor | _(empty)_ |

### 13.6 Notification System (`kernel/gui/notif.c`)

Toast notifications slide in from the top-right corner. Up to `NOTIF_MAX = 4` simultaneous notifications.

```c
void notif_show(const char *title, const char *body, uint32_t ms);
// ms = display duration in milliseconds
```

---

## 14. Framebuffer & Drawing API

All drawing functions operate on the global `framebuffer_t fb` struct:

```c
typedef struct {
    uint32_t *addr;   // linear pixel buffer (BGRX / RGBX depending on GRUB)
    uint32_t  width;  // pixels (default 1024)
    uint32_t  height; // pixels (default 768)
    uint32_t  pitch;  // bytes per row
    uint8_t   bpp;    // bits per pixel (default 32)
    uint8_t   initialized;
} framebuffer_t;
```

### Drawing Primitives

```c
void     fb_put_pixel(int x, int y, uint32_t color);
uint32_t fb_get_pixel(int x, int y);
void     fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void     fb_fill_rect_blend(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
          // alpha: 0 = transparent, 255 = opaque; blends over existing pixels
void     fb_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void     fb_draw_rect_outline(int x, int y, int w, int h, uint32_t color, int thickness);
void     fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
          // Bresenham integer line algorithm
void     fb_draw_circle(int cx, int cy, int r, uint32_t color);
void     fb_fill_circle(int cx, int cy, int r, uint32_t color);
void     fb_clear(uint32_t color);
void     fb_scroll_up(int pixels, uint32_t bg_color);
void     fb_copy_rect(int sx, int sy, int dx, int dy, int w, int h);
          // memmove-based region copy (handles overlap)
void     fb_blur_rect(int x, int y, int w, int h, int radius);
          // In-place integer box-blur (radius 1–8) — frosted-glass effect
uint32_t fb_blend(uint32_t fg, uint32_t bg, uint8_t alpha);
          // Returns blended 32-bit colour; does NOT write to screen
```

All functions clip to framebuffer bounds. Negative or out-of-range coordinates are clamped silently. **No SSE/FPU** — all blending uses integer arithmetic with `>>8` shifts.

---

## 15. Font Renderer

8×16 bitmap font (256 ASCII glyphs, stored in a static array in `font.c`).

```c
void font_putchar(int x, int y, char c, uint32_t fg, uint32_t bg);
void font_puts(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void font_printf(int x, int y, uint32_t fg, uint32_t bg, const char *fmt, ...);
     // Supports: %s %d %u %x %c %% (no float)

void font_putchar2x(int x, int y, char c, uint32_t fg, uint32_t bg);
void font_puts2x(int x, int y, const char *s, uint32_t fg, uint32_t bg);
     // 2× scaled variants (16×32 pixels per glyph)

int  font_str_width(const char *s);    // returns pixel width at 1× scale
int  font_str_width2x(const char *s);  // returns pixel width at 2× scale
```

Glyph width is fixed at 8 pixels. `font_str_width(s)` = `strlen(s) * 8`.

---

## 16. Animation Library

Header-only (`kernel/gui/anim.h`). All functions are `static inline`. No FPU. Progress values use the range **[0, 256]** instead of [0.0, 1.0].

### Easing Functions

```c
int anim_ease_out_quad(int p);    // fast start → slow finish
int anim_ease_out_cubic(int p);   // stronger deceleration
int anim_ease_in_quad(int p);     // slow start → accelerate
int anim_ease_in_out(int p);      // smooth S-curve
int anim_ease_out_back(int p);    // slight overshoot then settle (pop-in)
```

### Interpolation

```c
int anim_clamp(int v, int lo, int hi);
int anim_lerp(int a, int b, int t);          // t in [0,256]
uint32_t anim_color_lerp(uint32_t a, uint32_t b, int t);
uint32_t anim_color_fade(uint32_t c, int alpha); // fade towards black
```

### Oscillator

```c
int anim_pingpong(uint32_t t, uint32_t period_ms);
// t = current time in ms (from timer_get_ticks())
// Returns 0→256→0 triangle wave over period_ms
```

---

## 17. Built-in Applications

All applications are compiled directly into the kernel image and run as kernel threads in ring 0. They create a `window_t` via `wm_new()` and register `on_paint`, `on_click`, `on_key`, and `on_close` callbacks.

### Terminal (`term_app.c`)

A scrollable VT100-like terminal emulator that pipes I/O through `nsh`. Supports:
- 80-column, variable-row scrolling text buffer.
- Cursor blink animation.
- ANSI colour codes (basic 16-colour subset).
- Backspace, Enter, arrow key history navigation.
- All nsh built-ins available.

### File Manager (`files_app.c`)

Two-pane file browser (directory tree left, file list right):
- Navigate with arrow keys or mouse click.
- Enter opens directories; Backspace goes up.
- Shows file size and type icon.
- New folder button, delete (Backspace on selected file).

### System Info (`sysinfo_app.c`)

Static display showing:
- OS name and version.
- CPU vendor and model string (from `cpuid`).
- Physical memory total and free (PMM).
- Heap used / total (heap free walk).
- Uptime (seconds from PIT).
- Kernel build flags summary.

### Browser (`browser_app.c`)

Minimal HTTP browser:
- Address bar at top; Enter fetches the URL via `http_get()`.
- Renders the raw HTTP response body as plain text (no HTML parser).
- Scrollable with arrow keys.
- Status bar shows HTTP status code and body length.

### Calculator (`calc_app.c`)

Desktop calculator with:
- Integer arithmetic: `+` `–` `×` `÷` `%`
- Parentheses, sign toggle (`±`), clear (`C`/`AC`).
- 18-digit display (64-bit integer result).
- Keyboard and mouse input.

### Clock (`clock_app.c`)

Dual-display clock:
- Analog face (integer-only trigonometry approximation for hand positions).
- Digital readout `HH:MM:SS` below.
- Date line `YYYY-MM-DD`.
- Updates every second from `rtc_get_time()`.

### Theme Switcher (`theme_app.c`)

Switches the global colour palette across 4 built-in themes:

| # | Name | Style |
|---|---|---|
| 0 | Catppuccin Mocha | Dark, warm purple |
| 1 | Catppuccin Latte | Light, warm |
| 2 | Gruvbox Dark | Retro amber/green |
| 3 | Nord | Arctic cool blue |

Applying a theme writes to the `col_*` global variables in `fb.c` immediately, and sets `fb_scene_dirty = 1` to force a full redraw on the next frame.

### Text Editor (`edit_app.c`)

Multi-file capable text editor:
- Up to `EDIT_MAX_LINES` lines, `EDIT_MAX_COLS` columns per line.
- Full cursor movement: arrow keys, Home, End, PgUp, PgDn.
- Insert/delete characters; Enter splits lines; Backspace merges lines.
- Keyboard shortcuts:
  - `Ctrl+S` — Save to current file path.
  - `Ctrl+O` — Open file (prompts for path).
  - `Ctrl+N` — New empty buffer.
- Syntax-aware line numbers in the gutter.
- Status bar shows filename, cursor position (line:col), and modified flag.

### Visualizer (`viz_app.c`)

Real-time signal visualizer with three rendering modes, cycled with Space:
- **Waveform** — oscilloscope-style sine wave animated by frame counter.
- **Spectrum** — 32-bar frequency spectrum (computed from integer sine approximations).
- **Heatmap** — 2D colour grid using the `heat_color()` palette (blue → cyan → yellow → red).

Frame counter displayed in the top-right corner. No audio hardware required — the signal is synthesised.

### Snake (`snake_app.c`)

Classic Snake on a 30 × 24 cell grid (each cell 16 × 16 px):
- Arrow keys control direction.
- Score tracked; food placed randomly (guaranteed-empty cell).
- **Win condition:** body length equals grid size (720 cells) → game over with win message instead of collision message.
- Speed increases every 5 points (frame delay reduces).
- New game: press Enter after game over.

### System Monitor (`sysmon_app.c`)

Live performance dashboard with sparkline history charts:

| Panel | Metric | Source |
|---|---|---|
| CPU | Uptime (proxy for load; true % requires PMC) | `timer_get_uptime_seconds()` |
| Kernel Heap | Used / total (8 MB) | `heap_free_space()` + `HEAP_SIZE` |
| Physical Memory | Free pages | `pmm_get_free_memory()` / `pmm_get_total_memory()` |

- History ring buffer: 60 samples (last 60 seconds at 1 Hz).
- Sparkline rendered as a filled area chart.
- Numeric readout and progress bar per panel.
- Total and free memory displayed as human-readable (KB/MB).
- Updates every 1000 ms.

---

## 18. NexOS Shell (nsh)

`nsh` is the system shell, compiled into the kernel and launched by the terminal app. It connects to `wm_handle_key` via `nsh_set_output(fn)` to write characters to the terminal window.

### Features

- **42 built-in commands** (no external binary loading)
- **50-entry command history** (↑/↓ keys)
- **Tab completion** (paths and command names)
- **Environment variables** (`export KEY=VALUE`, `$KEY` substitution)
- **Pipes** (`cmd1 | cmd2`)
- **Output redirection** (`>` overwrite, `>>` append)
- **Background jobs** (`cmd &`)
- **Quoted arguments** (`"hello world"`)

### Built-in Commands

#### File System

| Command | Usage | Description |
|---|---|---|
| `ls` | `ls [path]` | List directory contents |
| `cd` | `cd <path>` | Change directory |
| `pwd` | `pwd` | Print working directory |
| `cat` | `cat <file>` | Print file contents |
| `mkdir` | `mkdir <path>` | Create directory |
| `rm` | `rm <file>` | Delete file |
| `touch` | `touch <file>` | Create empty file or update timestamp |
| `cp` | `cp <src> <dst>` | Copy file |
| `mv` | `mv <src> <dst>` | Move/rename file |
| `hexdump` | `hexdump <file>` | Hex + ASCII dump |
| `wc` | `wc <file>` | Word/line/byte count |
| `head` | `head [-n N] <file>` | First N lines (default 10) |
| `tail` | `tail [-n N] <file>` | Last N lines (default 10) |
| `find` | `find <path> <name>` | Search for file by name |
| `stat` | `stat <path>` | File inode, size, type |
| `tree` | `tree [path]` | Recursive directory tree |

#### Environment & Shell

| Command | Usage | Description |
|---|---|---|
| `echo` | `echo [args...]` | Print arguments |
| `clear` | `clear` | Clear terminal |
| `help` | `help` | List all built-ins |
| `env` | `env` | Print all environment variables |
| `export` | `export KEY=VALUE` | Set environment variable |
| `history` | `history` | Show command history |
| `exit` / `logout` | `exit` | Exit shell session |

#### System Information

| Command | Usage | Description |
|---|---|---|
| `uname` | `uname [-a]` | OS name and version |
| `uptime` | `uptime` | Seconds since boot |
| `ps` | `ps` | List all processes |
| `kill` | `kill <pid>` | Terminate process |
| `free` | `free` | PMM and heap memory usage |
| `date` | `date` | Current date and time from RTC |
| `mount` | `mount` | Show active VFS mounts |
| `memmap` | `memmap` | PMM physical memory map |
| `top` | `top` | Live process CPU/memory table |
| `ver` | `ver` | NexOS version string |
| `which` | `which <cmd>` | Show if command is a built-in |

#### System Control

| Command | Usage | Description |
|---|---|---|
| `reboot` | `reboot` | Triple-fault reboot |
| `halt` | `halt` | HLT loop (power off in QEMU) |

#### Networking

| Command | Usage | Description |
|---|---|---|
| `ifconfig` | `ifconfig` | Show network interfaces, MAC, IP |
| `ping` | `ping <host>` | ICMP echo to hostname or IP |
| `dns` | `dns <hostname>` | Resolve hostname to IP |
| `wget` | `wget <url>` | HTTP GET and print body |
| `netstat` | `netstat` | Show active TCP connections |
| `wifi` | `wifi status\|scan\|connect\|disconnect` | Wi-Fi management |

#### Package Manager

| Command | Usage | Description |
|---|---|---|
| `npkg` | `npkg install <file>` | Install .npkg package |
| `npkg` | `npkg remove <name>` | Remove installed package |
| `npkg` | `npkg list` | List installed packages |
| `npkg` | `npkg info <name>` | Show package details |
| `npkg` | `npkg store` | List built-in package store |

#### GUI App Launchers (from terminal)

| Command | Aliases | Opens |
|---|---|---|
| `edit` | `nano`, `vi` | Text editor window |
| `viz` | `visualizer` | Visualizer window |
| `snake` | | Snake game window |
| `sysmon` | `htop` | System monitor window |
| `browser` | `www` | Browser window |
| `calc` | | Calculator window |
| `clock` | | Clock window |

---

## 19. Package Manager (npkg)

### Binary Package Format (`.npkg`)

Packages are binary files with a fixed 320-byte header followed by densely packed file entries:

```
[npkg_header_t — 320 bytes]
[npkg_file_entry_t — 260 bytes][raw file data — entry.size bytes]
[npkg_file_entry_t — 260 bytes][raw file data — entry.size bytes]
...
[install script — header.install_script_size bytes, may be 0]
```

**Header fields:**

| Field | Type | Description |
|---|---|---|
| `magic` | `uint32_t` | `0x4B50584E` ("NXPK" LE) |
| `format_version` | `uint16_t` | Currently `1` |
| `name` | `char[64]` | Package name |
| `version` | `char[32]` | Semantic version string |
| `description` | `char[128]` | Short description |
| `author` | `char[64]` | Author name |
| `file_count` | `uint32_t` | Number of file entries |
| `install_script_size` | `uint32_t` | Size of trailing install script (0 = none) |

Each `npkg_file_entry_t` (260 bytes) contains a 256-character absolute VFS target path and a 4-byte data size.

### API

```c
int  npkg_init(void);                               // sets up /var/lib/npkg/
int  npkg_install(const char *vfs_path);            // returns NPKG_* error code
int  npkg_remove(const char *name);
int  npkg_list(char *buf, int buf_size);
int  npkg_info(const char *name, char *buf, int buf_size);
int  npkg_is_installed(const char *name);
void npkg_init_store(void);                         // installs built-in packages
```

### Error Codes

| Code | Value | Meaning |
|---|---|---|
| `NPKG_OK` | 0 | Success |
| `NPKG_ERR_NOTFOUND` | 1 | Package not found |
| `NPKG_ERR_BADMAGIC` | 2 | Not a valid .npkg file |
| `NPKG_ERR_NOMEM` | 3 | Heap allocation failed |
| `NPKG_ERR_VFS` | 4 | VFS write error |
| `NPKG_ERR_EXISTS` | 5 | Already installed |
| `NPKG_ERR_BADPKG` | 6 | Malformed package data |

Limits: `NPKG_MAX_INSTALLED = 64`, `NPKG_MAX_FILES = 256 per package`.

---

## 20. Installer

The installer TUI is activated by passing `nexos.install` on the GRUB kernel command line. It runs before the GUI in `kernel_main()`.

The installer:
1. Probes all ATA drives.
2. Presents a text-mode menu to select the target drive.
3. Writes the NexOS bootloader and kernel image to the target drive.
4. Configures GRUB on the target.
5. Reboots on completion.

It is accessed by adding `nexos.install` to the GRUB boot entry `linux` line.

---

## 21. Colour Palette

The colour system uses **Catppuccin Mocha** as the default theme, with three alternative themes switchable at runtime. All colours are `uint32_t` in `0x00RRGGBB` format.

### Catppuccin Mocha (default)

| Macro | Colour | Use |
|---|---|---|
| `COL_BASE` | `#1E1E2E` | Window backgrounds, cards |
| `COL_MANTLE` | `#181825` | Desktop secondary |
| `COL_CRUST` | `#11111B` | Deepest background, shadows |
| `COL_SURFACE0` | `#313244` | Elevated surfaces, input fields |
| `COL_SURFACE1` | `#45475A` | Borders, separators |
| `COL_SURFACE2` | `#585B70` | Inactive elements |
| `COL_OVERLAY0` | `#6C7086` | Disabled text, placeholders |
| `COL_TEXT` | `#CDD6F4` | Primary text |
| `COL_SUBTEXT` | `#A6ADC8` | Secondary text, labels |
| `COL_BLUE` | `#89B4FA` | Links, highlights, focus rings |
| `COL_LAVENDER` | `#B4BEFE` | Accents |
| `COL_MAUVE` | `#CBA6F7` | Primary accent |
| `COL_RED` | `#F38BA8` | Errors, close button |
| `COL_PEACH` | `#FAB387` | Warnings |
| `COL_YELLOW` | `#F9E2AF` | Minimise button, alerts |
| `COL_GREEN` | `#A6E3A1` | Success, maximise button |
| `COL_TEAL` | `#94E2D5` | Info accents |
| `COL_SKY` | `#89DCEB` | Secondary info |

**Note:** There is no `COL_PINK` macro. Do not use it.

All `COL_*` macros expand to `extern uint32_t` variables (`col_base`, `col_text`, etc.) defined in `fb.c`. They change instantly when `theme_apply(id)` is called.

---

## 22. Kernel Logging

```c
void klog(log_level_t level, const char *fmt, ...);
void kpanic(const char *fmt, ...);
```

Output goes simultaneously to:
- **VGA text console** (colour-coded by level)
- **COM1 serial** (115200 baud) — format: `[TTTTTTTT][LEVEL] message\n`

Log levels:

| Level | Enum | Serial colour |
|---|---|---|
| `LOG_DEBUG` | 0 | Dark grey |
| `LOG_INFO` | 1 | Light grey |
| `LOG_WARN` | 2 | Yellow/brown |
| `LOG_ERROR` | 3 | Light red |
| `LOG_PANIC` | 4 | Light red + halt |

`kpanic()` logs at `LOG_PANIC`, then executes `cli; hlt` in an infinite loop.

Format specifiers supported in `klog`/`kpanic` (no libc):  
`%s` `%d` `%u` `%x` `%X` `%c` `%llu` `%lld` `%p` `%%`  
Width and zero-padding prefixes (e.g. `%02x`, `%8d`) are supported.

---

## 23. Known Limitations

| Area | Limitation |
|---|---|
| FPU/SSE | Entirely disabled. All arithmetic must be integer. Never enable SSE in kernel code — it will corrupt SIMD registers that GRUB may rely on. |
| FAT32 | No LFN (long filename) support. Only 8.3 short names. No FAT12/FAT16. |
| Networking | No DHCP client — IP address defaults to `10.0.2.15` for QEMU user-mode NAT. Real hardware needs DHCP implementation. |
| TCP | No congestion control, no windowing, no retransmission. Suitable for short HTTP requests only. |
| Processes | `fork()` and `execve()` return `ENOSYS`. All "processes" are kernel threads sharing the same address space. The per-process CR3 mechanism is wired up but address spaces are not populated for isolation. |
| Wi-Fi | Hardware driver not implemented. The `wifi` shell commands and API stubs are present for future expansion. |
| ATA write | Write support is implemented but FAT32 write is not fully tested on real hardware. |
| Disk images | Running in QEMU without providing a disk image (`-hda disk.img`) causes the expected `[ERROR] FAT32: failed to read BPB` messages on boot. This is non-fatal. |
| Memory limit | Maximum usable RAM: 256 MB (PMM bitmap sized accordingly). QEMU `-m 256M` matches this. |
| Heap | Fixed 8 MB window at physical `0x1200000–0x1A00000`. Cannot grow. Exhausting the heap causes `kmalloc` to return NULL. |
| VFS | No file locking. No permission model. All access is root (UID 0). |
| Display | Fixed 1024×768×32. No mode switching after boot. |

---

## 24. Bug Fix Log

Complete record of every static-analysis and runtime bug found and fixed (in development order):

| # | File | Bug | Severity | Fix |
|---|---|---|---|---|
| 1 | `keyboard.c` | `kbd_extended` flag never cleared after 0xE0 prefix without a following byte (IRQ timing edge case) | Medium | Initialised `kbd_extended = 0` in `keyboard_init`; cleared at top of each extended dispatch |
| 2 | `keyboard.c` | Right Ctrl (`0xE0 0x1D`) and Right Alt (`0xE0 0x38`) not tracked — `ctrl_down`/`alt_down` never set by right-side keys | Functional | Added `if (sc == 0x1D) { ctrl_down = !released; return; }` and matching Alt case at top of extended handler |
| 3 | `launcher.c` | Apps 8–10 drawn off-screen: second draw loop used `col = i - CARD_COLS` (indices 4, 5, 6) and hardcoded row-1 Y offset | Visual | Unified all three loops (draw, click, hover) to `row = i/CARD_COLS; col = i%CARD_COLS` |
| 4 | `edit_app.c` | Out-of-bounds write: after reading exactly `EDIT_MAX_LINES` lines the final `e->lines[row][col] = 0` wrote to `e->lines[EDIT_MAX_LINES]` (one past end) | Critical | Clamp `row = EDIT_MAX_LINES - 1` before the post-loop write |
| 5 | `viz_app.c` | `heat_color()`: no input guard — values `v > 255` in the `v >= 192` branch computed negative `uint32_t` colours (0xFF000000+ garbage) causing black screens | Medium | Clamp `v` to `[0, 255]` at top of function; guard `g` channel against underflow |
| 6 | `viz_app.c` | Misleading-indentation warning: `while(ti>0) fbuf[fi++]=t[--ti]; fbuf[fi]=0;` — null-terminator appeared visually loop-guarded | Warning | Added braces: `while(ti>0){ fbuf[fi++]=t[--ti]; }` |
| 7 | `sysmon_app.c` | `draw_sparkline()`: `% SYSMON_HISTORY` hardcoded in the ring index instead of `% hlen` parameter — wrong indices if ever called with a different ring size | Medium | Changed to `(pos + i) % hlen` |
| 8 | `sysmon_app.c` | `heap_total` hardcoded as `8ULL * 1024 * 1024` in two places — would silently show wrong percentages if `HEAP_SIZE` changed | Medium | Replaced both with `HEAP_SIZE` macro from `heap.h` |
| 9 | `snake_app.c` | `place_food()` on a full/near-full grid: busy-loop capped at 2000 tries then placed food on a snake cell, causing an immediate false self-collision and wrong game-over | Medium | Added win-condition check at entry; replaced cap with `SN_W*SN_H*4`; added guaranteed linear-scan fallback |
| 10 | `snake_app.c` | No win detection — a perfect game just produced a false self-collision | Functional | If `body_len >= SN_W * SN_H`, set `game_over = 1` with win message |
| 11 | `heap.c` | `kmalloc_aligned()` returned an offset pointer but `kfree()` on it computed wrong block header (`ptr - sizeof(header)` ≠ original allocation) — heap corruption on first free | Critical | Store original base pointer in `((void**)aligned)[-1]`; add `kfree_aligned()` that recovers it; declare in `heap.h` |
