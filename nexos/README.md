# NexOS

A custom x86_64 operating system written from scratch in C and Assembly (NASM).

## Features

### Kernel
- **Multiboot2** compliant bootloader entry (GRUB2)
- **GDT** with null, kernel code/data, user code/data (ring 3), and TSS segments
- **IDT** with all 256 vectors — CPU exceptions (0–31), PIC-remapped IRQs (32–47), INT 0x80 syscall gate
- **4-level paging** (PML4 → PDPT → PD → PT) — identity-maps first 4 MB
- **VMM** — `vmm_map`, `vmm_unmap`, `vmm_phys`, `vmm_create_address_space`, `vmm_switch_address_space`
- **PMM** — bitmap physical frame allocator, reads Multiboot2 memory map
- **Heap** — free-list `kmalloc` / `kfree` with splitting and coalescing
- **Ring 3 user mode** — per-process user stack, TSS rsp0 updated per switch, IRET via `enter_ring3`

### Drivers
| Driver | Details |
|--------|---------|
| VGA | 80×25 text mode, 16 colours, scrolling, cursor |
| Serial | COM1 @ 115200 baud — mirrors all kernel log output |
| PS/2 Keyboard | Scancode → ASCII, shift/caps/special keys, circular buffer |
| PIT | 8253/8254 @ 1000 Hz — `timer_sleep_ms`, `timer_get_ticks` |
| ATA PIO | Primary master/slave, IDENTIFY, 28-bit LBA read/write |
| PCI | Config-space bus enumeration — vendor/device/class logged |
| CMOS RTC | BCD → binary conversion, `rtc_get_time`, `rtc_time_to_string` |

### Filesystems
| FS | Mount | Description |
|----|-------|-------------|
| ramfs | `/` | In-memory heap-backed filesystem — files and directories |
| fat32 | `/mnt` | Full FAT32 + LFN, backed by ATA PIO reads |
| procfs | `/proc` | Dynamic virtual files: version, uptime, meminfo, per-PID status |

### Processes & Scheduling
- **PCB** — PID, PPID, name, state, ring-3 user stack, kernel stack (TSS rsp0), fd table, CWD
- **Round-robin scheduler** — 20 ms quantum, driven by PIT IRQ0
- **INT 0x80 syscall interface** — 15 system calls (see table below)

### Userspace
- **init** (PID 1) — populates `/etc`, `/dev`, tries to mount FAT32, launches nsh
- **nsh** — fully interactive shell with 25 built-ins
- **Minimal libc** — string.h, stdio.h, stdlib.h all via INT 0x80

### Shell (nsh) — 25 Built-in Commands
`ls` `cd` `pwd` `cat` `echo` `mkdir` `rm` `touch` `cp` `mv` `clear`
`help` `env` `export` `uname` `uptime` `ps` `kill` `free` `date`
`mount` `reboot` `halt` `exit` `logout`

**Shell features:**
- Pipe: `cmd1 | cmd2`
- Output redirection: `cmd > file`, `cmd >> file`
- Background jobs: `cmd &`
- Quoted arguments: `"hello world"`
- Escaped quotes: `\"` inside strings
- Unterminated quote detection: prints `nsh: unmatched quote`
- Environment variable expansion: `$VAR`, `$?`
- Command history (50 entries, ↑/↓ arrows)
- Tab completion (built-ins + VFS paths)
- Startup script: `/etc/nsh.rc`

---

## Prerequisites

| Tool | Purpose |
|------|---------|
| `nasm` | Assemble boot.asm, ISR stubs, gdt_flush, enter_ring3 |
| `gcc` | Compile kernel C (`x86_64-elf-gcc` preferred; system GCC fallback) |
| `ld` | Link kernel ELF |
| `grub-mkrescue` | Create bootable ISO |
| `xorriso` | ISO image backend for grub-mkrescue |
| `qemu-system-x86_64` | Run the OS in a VM |
| `python3` | USB installer script |
| `make` | Build orchestration |

Install on Ubuntu/Debian:
```bash
sudo apt install nasm gcc binutils grub-pc-bin grub-common xorriso \
     qemu-system-x86 python3 make
```

---

## Building

```bash
cd nexos
make all        # build kernel ELF + bootable ISO → build/nexos.iso
make kernel     # kernel ELF only → build/nexos.kernel
make iso        # ISO only (requires kernel)
make check      # syntax-check all C sources without linking
make clean      # remove build/
```

---

## Running in QEMU

```bash
make run
```

Launches QEMU with 256 MB RAM, VGA display, ATA disk image, and serial port forwarded to the host terminal.

---

## Debugging with GDB

```bash
make run-debug      # QEMU paused, GDB server on :1234
```

In another terminal:
```bash
gdb build/nexos.kernel
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

---

## Running in VirtualBox

1. Create new VM: **Type** = Other, **Version** = Other/Unknown (64-bit)
2. RAM: 256 MB minimum
3. No hard disk needed for ISO boot
4. Mount `build/nexos.iso` as the optical drive
5. **Settings → System**: Enable I/O APIC, **disable EFI** (use legacy BIOS)
6. **Settings → Display**: VGA controller, 16 MB video memory
7. **Settings → Network**: NAT mode
8. Boot the VM

---

## Installing to USB

```bash
make install-usb        # runs tools/install_usb.py
# Follow the prompts — type YES to confirm drive wipe
```

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                         GRUB2                            │
│         (Multiboot2, loads build/nexos.kernel ELF)       │
└─────────────────────────┬────────────────────────────────┘
                          │ jmp kernel_main
                          ▼
┌──────────────────────────────────────────────────────────┐
│                    kernel_main()                         │
│                                                          │
│  serial → vga → GDT/TSS → IDT/ISRs                      │
│  → PMM (Multiboot2 mmap) → paging/VMM → heap            │
│  → PIT timer → PS/2 keyboard → ATA → PCI → RTC          │
│  → net_init (stub) → VFS/ramfs → procfs                  │
│  → proc_init → scheduler_init → syscall_init             │
└─────────────────────────┬────────────────────────────────┘
                          │ proc_enter_ring3(init)
                          │ TSS rsp0 = kernel stack top
                          │ IRET  CS=0x1B  SS=0x23  IF=1
                          ▼
┌──────────────────────────────────────────────────────────┐
│                   init_main()  [PID 1]                   │
│                                                          │
│  /etc  /dev  /proc  /tmp  /mnt (FAT32 if disk present)  │
│  writes: nexos.conf  passwd  motd  nsh.rc                │
└─────────────────────────┬────────────────────────────────┘
                          │ nsh_main()
                          ▼
┌──────────────────────────────────────────────────────────┐
│                    nsh  [shell]                          │
│                                                          │
│  sources /etc/nsh.rc                                     │
│  prompt → read_line (history, tab-complete)              │
│    → nsh_exec_line                                       │
│        ├─ pipe:       cmd1 | cmd2                        │
│        ├─ redirect:   cmd > file  /  cmd >> file         │
│        ├─ background: cmd &                              │
│        └─ built-in:   25 commands                        │
│                                                          │
│  syscall: INT 0x80 → ring 0 handler → IRET → ring 3     │
└──────────────────────────────────────────────────────────┘
```

---

## Syscall Table

| # | Name | rdi | rsi | rdx | Returns |
|---|------|-----|-----|-----|---------|
| 0 | `sys_read` | fd | buf | len | bytes read |
| 1 | `sys_write` | fd | buf | len | bytes written |
| 2 | `sys_open` | path | flags | — | fd or -1 |
| 3 | `sys_close` | fd | — | — | 0 or -1 |
| 4 | `sys_exit` | code | — | — | — |
| 5 | `sys_getpid` | — | — | — | PID |
| 6 | `sys_sleep` | ms | — | — | 0 |
| 7 | `sys_stat` | path | stat_buf | — | 0 or -1 |
| 8 | `sys_mkdir` | path | — | — | 0 or -1 |
| 9 | `sys_getcwd` | buf | size | — | 0 or -1 |
| 10 | `sys_chdir` | path | — | — | 0 or -1 |
| 11 | `sys_exec` | path | argv | — | — |
| 12 | `sys_fork` | — | — | — | child PID |
| 13 | `sys_wait` | pid | — | — | exit code |
| 14 | `sys_sbrk` | incr | — | — | new brk |

---

## File Layout

```
nexos/
├── Makefile
├── README.md
├── boot/
│   ├── linker.ld              Kernel linker script
│   └── grub/grub.cfg          GRUB2 menu entry
├── kernel/
│   ├── kernel.h / kernel.c    Entry, klog, kpanic, I/O helpers
│   ├── include/               Freestanding stdint/stddef/stdbool/stdarg
│   ├── arch/x86_64/
│   │   ├── boot.asm           Multiboot2 entry, long mode setup
│   │   ├── gdt.c / gdt.h      GDT + TSS, tss_set_rsp0()
│   │   ├── gdt_flush.asm      lgdt + ltr stubs
│   │   ├── idt.c / idt.h      IDT, PIC init, irq_install_handler
│   │   ├── isr.asm            ISR/IRQ stubs (pushes registers_t)
│   │   ├── paging.c / .h      4-level page tables, vmm_map_page
│   │   └── enter_ring3.asm    IRET into ring 3
│   ├── drivers/
│   │   ├── vga.c / .h         Text-mode video
│   │   ├── serial.c / .h      COM1 debug output
│   │   ├── keyboard.c / .h    PS/2 IRQ1, scancode map
│   │   ├── timer.c / .h       PIT 1000 Hz
│   │   ├── ata.c / .h         ATA PIO read/write
│   │   ├── pci.c / .h         PCI bus enumeration
│   │   └── rtc.c / .h         CMOS real-time clock
│   ├── mm/
│   │   ├── pmm.c / .h         Physical memory bitmap allocator
│   │   ├── heap.c / .h        Kernel heap (kmalloc/kfree)
│   │   └── vmm.c / .h         High-level VMM wrapper
│   ├── fs/
│   │   ├── vfs.c / .h         Virtual filesystem layer
│   │   ├── ramfs.c / .h       In-memory filesystem
│   │   ├── fat32.c / .h       FAT32 + LFN driver
│   │   └── procfs.c / .h      /proc virtual filesystem
│   ├── proc/
│   │   ├── process.c / .h     PCB, proc_create, proc_enter_ring3
│   │   ├── scheduler.c / .h   Round-robin scheduler
│   │   └── syscall.c / .h     INT 0x80 dispatch (15 calls)
│   └── net/
│       ├── net.h              net_interface, ip_packet structs
│       └── net.c              net_init() stub
├── userspace/
│   ├── init/init.c            PID 1 init process
│   ├── shell/nsh.c            Interactive shell
│   └── libc/                  Minimal C library stubs
└── tools/
    ├── build_iso.sh           grub-mkrescue wrapper
    ├── run_qemu.sh            QEMU launcher
    ├── run_qemu_debug.sh      QEMU + GDB stub
    └── install_usb.py         USB installer
```

---

## License

MIT — see individual source file headers.
