# NexOS — Project Reference

## What is this?
NexOS is a complete custom x86_64 operating system built from scratch in C,
NASM Assembly, and Python. It boots via Multiboot2, runs a real kernel in
long mode, hosts a full graphical desktop environment (GUI), and also ships
a userspace shell (nsh) as a fallback.

## Build

```bash
cd nexos
make            # compile kernel + build ISO
make run        # launch in QEMU (ISO, RTL8139 networking, USB tablet mouse)
make run-with-disk   # launch with 64 MB FAT32 disk image attached
make disk       # create build/nexos_disk.img (requires dosfstools)
make run-debug  # launch with GDB stub on localhost:1234
make install-usb     # write ISO to a USB drive (Linux/macOS/Windows)
make check      # syntax-only compile check (no object files)
make clean      # delete build/
```

## Toolchain (NixOS/Replit)
- gcc 14.3.0 x86_64-linux-gnu (freestanding, mcmodel=kernel)
- NASM 2.16.03 (elf64 output)
- GNU ld 2.44 (linker script: boot/linker.ld)
- QEMU for x86_64 emulation (q35 machine, 256 MB RAM, virtio VGA, USB tablet)
- Python 3 (USB installer)

## Kernel Subsystems (32 total)

### Base (18)
1. **Multiboot2** — boot.asm parses tags, framebuffer request tag (type=5), hands off to kernel_main
2. **GDT** — 64-bit flat, ring 0 + ring 3 segments + TSS
3. **IDT** — 48 ISR stubs, remapped PICs (IRQ → INT 0x20+)
4. **PMM** — bitmap allocator, 4 KB frames, 4 GB address space
5. **VMM** — 4-level paging, identity map first 32 MB + MMIO + FB pages
6. **Heap** — singly-linked forward-coalesce allocator (8 MB @ 0x1200000)
7. **VFS** — vfs_node_t with function-pointer dispatch; mount table
8. **ramfs** — in-memory filesystem for /, /tmp, /dev, /etc, /proc
9. **FAT32** — read-only driver with LFN support; BPB validation + sig check
10. **procfs** — /proc/<pid>/status and /proc/meminfo via VFS callbacks
11. **Scheduler** — round-robin, process table (MAX 16), PROC_* states
12. **Syscall** — INT 0x80 gate, ring 3 → ring 0 dispatch
13. **PS/2 keyboard** — scancode-to-ASCII, arrow keys, Ctrl+letter → control codes (1-26)
14. **ATA PIO** — LBA28 read, dual-drive detection (master + slave)
15. **PIT** — 1000 Hz tick, timer_sleep_ms, uptime counter
16. **RTC** — CMOS real-time clock, rtc_time_to_string
17. **PCI** — bus 0 enumeration, config space read/write
18. **RTL8139** — polling NIC: TX (4 descriptors), RX (32 KB ring), ARP reply

### GUI (14 new)
19. **fb** (kernel/drivers/fb.c) — linear framebuffer driver, 1024×768×32bpp; runtime-switchable Catppuccin palette via `col_*` globals; pixel, rect, line, circle, rounded-rect, blend, scroll, copy primitives
20. **font** (kernel/drivers/font.c) — complete IBM CP437 8×16 VGA bitmap font (256 glyphs); 1× and 2× renderers; font_printf
21. **console** (kernel/drivers/console.c) — framebuffer text console (128×48 chars); scroll, color, position; used by klog after FB init
22. **mouse** (kernel/drivers/mouse.c) — PS/2 mouse via IRQ12; hardware-sprite cursor with save/restore; 12×20 arrow bitmap
23. **wm** (kernel/gui/wm.c) — window manager; z-order stack (16 windows); titlebar, traffic-light buttons (close/min/max), drag, focus, maximize, paint/click/key callbacks
24. **desktop** (kernel/gui/desktop.c) — gradient background with dot grid and NexOS watermark
25. **taskbar** (kernel/gui/taskbar.c) — bottom bar; Apps button, window list, clock (RTC), free-RAM indicator
26. **launcher** (kernel/gui/launcher.c) — pop-up app launcher; 7 items with icons; shutdown/restart via ACPI/kbd
27. **term_app** (kernel/gui/term_app.c) — GUI terminal; 90×28 char buffer; routes commands to nsh via `nsh_set_output` hook; blinking cursor
28. **files_app** (kernel/gui/files_app.c) — file manager; VFS directory listing; navigate/open dirs; size display
29. **sysinfo_app** (kernel/gui/sysinfo_app.c) — system info window; uptime, RAM used/free bar, CPU info
30. **theme_app** (kernel/gui/theme_app.c) — theme switcher; 4 built-in themes (Catppuccin Mocha, Nord, Dracula, Gruvbox Dark); applies at runtime by swapping `col_*` globals
31. **notif** (kernel/gui/notif.c) — toast notification system; 4-slot queue; timed fade; shown bottom-right above taskbar
32. **gui** (kernel/gui/gui.c) — main 30fps event loop; keyboard shortcuts (Ctrl+T/F/I/W/Q); mouse routing; desktop+WM+taskbar+launcher+notif render pipeline

## GUI Keyboard Shortcuts
| Shortcut | Action |
|---|---|
| Ctrl+T | Open new Terminal |
| Ctrl+F | Open new File Manager |
| Ctrl+I | Open System Info |
| Ctrl+W / Ctrl+Q | Close focused window |
| Escape | Close launcher menu |
| Right-click (desktop) | Open app launcher |

## GUI Themes
- **Catppuccin Mocha** (default) — dark purple-tinted
- **Nord** — cool arctic blues
- **Dracula** — classic dark purple
- **Gruvbox Dark** — warm earthy tones

## Userspace
- **init** (PID 1): sets up /etc, /dev, /tmp, /mnt; tries FAT32; launches `gui_main()`, falls back to `nsh_main()` if no framebuffer
- **nsh**: 39 built-in commands, 50-entry history, tab completion, env vars, pipes, redirection, background jobs; GUI-aware via `nsh_set_output()` hook

## Shell Commands (39)
**Core (25):** ls cd pwd cat echo mkdir rm cp mv touch clear help env
export uname uptime ps kill free date mount reboot halt exit logout

**New (14):** hexdump wc head tail find top history which stat tree
memmap ver ifconfig ping

## Key Memory Map
| Range | Purpose |
|---|---|
| 0x00000000 - 0x000FFFFF | BIOS / reserved (1 MB) |
| 0x00100000 - 0x011FFFFF | Kernel image + BSS + page tables + PMM bitmap |
| 0x01200000 - 0x019FFFFF | Heap (8 MB, kmalloc/kfree) |
| 0x01A00000 + | Free RAM (handed to VMM / heap on demand) |
| 0xE0000000+ | VESA framebuffer (MMIO, mapped by vmm_map after vmm_init) |

## Framebuffer Init Sequence
1. `boot.asm` includes MB2 framebuffer request tag (type=5, 1024×768×32)
2. `grub.cfg` sets `gfxmode=1024x768x32`, `gfxpayload=keep`, `terminal_output gfxterm`
3. `kernel_main()` after `vmm_init()`: scans MB2 tags for type=8 (fb info), maps all pages with `vmm_map(phys, phys, VMM_FLAG_WRITE)`, calls `fb_init()` then `console_init()`
4. `klog()` redirects from `vga_puts` → `console_puts` once `fb.initialized` is set
5. `init.c` calls `gui_main()` which initialises mouse/WM/taskbar/notif and enters the 30fps loop

## QEMU Invocation
```bash
# Standard GUI run (1024x768, USB tablet for accurate mouse):
qemu-system-x86_64 -machine q35 -m 256M -serial stdio \
  -vga virtio -usb -device usb-tablet \
  -cdrom build/nexos.iso -boot d \
  -netdev user,id=net0 -device rtl8139,netdev=net0

# With FAT32 disk:
NEXOS_USE_DISK=1 make run-with-disk
```

## Architecture Notes
- `sizeof(heap_block_t)` = 24 bytes (singly-linked: size + uint8_t free + next)
- Stack overflow fix: fat32_mount reads BPB into kmalloc'd 512-byte sector_buf
- RTL8139 uses polling (no IRQ handler wired); TX descriptor round-robin 0-3
- Physical address == virtual address for heap range (identity mapped)
- GUI `col_*` globals are `uint32_t` (not `#define`), enabling runtime theme switching
- `COL_*` macros are aliased to `col_*` globals — all existing code uses them unchanged

## Tools
| Script | Purpose |
|---|---|
| `tools/build_iso.sh` | Assemble GRUB2 bootable ISO |
| `tools/run_qemu.sh` | Launch in QEMU (ISO or disk, now with USB tablet + virtio VGA) |
| `tools/run_qemu_debug.sh` | Launch with GDB stub |
| `tools/create_disk.sh` | Create 64 MB FAT32 image (needs dosfstools) |
| `tools/install_usb.py` | Write ISO to USB (Linux / macOS / Windows) |
