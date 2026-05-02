# NexOS — Project Reference

## What is this?
NexOS is a complete custom x86_64 operating system built from scratch in C,
NASM Assembly, and Python. It boots via Multiboot2, runs a real kernel in
long mode, and drops to a userspace shell (nsh) in ring 3.

## Build

```bash
cd nexos
make            # compile kernel + build ISO
make run        # launch in QEMU (ISO, RTL8139 networking)
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
- QEMU for x86_64 emulation (q35 machine, 256 MB RAM)
- Python 3 (USB installer)

## Kernel Subsystems (18 total)
1. **Multiboot2** — boot.asm parses tags, hands off to kernel_main
2. **GDT** — 64-bit flat, ring 0 + ring 3 segments + TSS
3. **IDT** — 48 ISR stubs, remapped PICs (IRQ → INT 0x20+)
4. **PMM** — bitmap allocator, 4 KB frames, 4 GB address space
5. **VMM** — 4-level paging, identity map first 32 MB + MMIO
6. **Heap** — singly-linked forward-coalesce allocator (8 MB @ 0x1200000)
7. **VFS** — vfs_node_t with function-pointer dispatch; mount table
8. **ramfs** — in-memory filesystem for /, /tmp, /dev, /etc, /proc
9. **FAT32** — read-only driver with LFN support; BPB validation + sig check
10. **procfs** — /proc/<pid>/status and /proc/meminfo via VFS callbacks
11. **Scheduler** — round-robin, process table (MAX 16), PROC_* states
12. **Syscall** — INT 0x80 gate, ring 3 → ring 0 dispatch
13. **PS/2 keyboard** — scancode-to-ASCII, arrow keys for history
14. **ATA PIO** — LBA28 read, dual-drive detection (master + slave)
15. **PIT** — 1000 Hz tick, timer_sleep_ms, uptime counter
16. **RTC** — CMOS real-time clock, rtc_time_to_string
17. **PCI** — bus 0 enumeration, config space read/write
18. **RTL8139** — polling NIC: TX (4 descriptors), RX (32 KB ring),
                  ARP reply, raw socket used by ping

## Userspace
- **init** (PID 1): sets up /etc, /dev, /tmp, /mnt; tries FAT32 on
  ATA master then slave; reaps zombies; launches nsh
- **nsh**: 39 built-in commands, 50-entry history, tab completion,
  env vars, pipes, output redirection, background jobs, quoted args

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

## Architecture Notes
- `sizeof(heap_block_t)` = 24 bytes (singly-linked: size + uint8_t free + next)
- Stack overflow fix: fat32_mount reads BPB into kmalloc'd 512-byte sector_buf
- RTL8139 uses polling (no IRQ handler wired); TX descriptor round-robin 0-3
- Physical address == virtual address for heap range (identity mapped)

## QEMU Invocation
```bash
# Standard (ISO, RTL8139, 256 MB):
qemu-system-x86_64 -machine q35 -m 256M -serial stdio \
  -cdrom build/nexos.iso -boot d \
  -netdev user,id=net0 -device rtl8139,netdev=net0

# With FAT32 disk (second IDE drive = ATA primary slave):
qemu-system-x86_64 -machine q35 -m 256M -serial stdio \
  -cdrom build/nexos.iso -boot d \
  -netdev user,id=net0 -device rtl8139,netdev=net0 \
  -drive file=build/nexos_disk.img,format=raw,if=ide,index=1,media=disk
```

## Tools
| Script | Purpose |
|---|---|
| `tools/build_iso.sh` | Assemble GRUB2 bootable ISO |
| `tools/run_qemu.sh` | Launch in QEMU (ISO or disk) |
| `tools/run_qemu_debug.sh` | Launch with GDB stub |
| `tools/create_disk.sh` | Create 64 MB FAT32 image (needs dosfstools) |
| `tools/install_usb.py` | Write ISO to USB (Linux / macOS / Windows) |

## Warnings (pre-existing, suppressed by -Wno-unused-function flag)
- `k_strlen` in fat32.c — internal helper never called externally
- `nsh_memset` in nsh.c — provided for potential future use
