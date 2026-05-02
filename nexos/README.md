# NexOS — A Custom x86_64 Operating System

NexOS is a complete custom operating system built from scratch for x86_64 architecture. It is bootable in QEMU and VirtualBox, and can be installed to a USB drive for bare-metal use.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    GRUB 2 Bootloader                    │
│              (Multiboot2, El Torito ISO)                 │
└─────────────────────────┬───────────────────────────────┘
                           │
┌─────────────────────────▼───────────────────────────────┐
│                      NexOS Kernel                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐ │
│  │   GDT    │  │   IDT    │  │   PMM    │  │  VMM   │ │
│  │ 5 segs   │  │ 256 ints │  │  Bitmap  │  │ 4-lvl  │ │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘ │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐ │
│  │   Heap   │  │   PIT    │  │Keyboard  │  │  ATA   │ │
│  │ FreeList │  │ 1000Hz   │  │ PS/2 IRQ │  │  PIO   │ │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘ │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐ │
│  │   VFS    │  │  ramfs   │  │  FAT32   │  │  PCI   │ │
│  │ Abstract │  │ In-mem   │  │ R/W+LFN  │  │  Enum  │ │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘ │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐ │
│  │  Sched   │  │ Process  │  │ Syscalls │  │  RTC   │ │
│  │Round-rob │  │   PCB    │  │ INT 0x80 │  │  CMOS  │ │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘ │
└─────────────────────────┬───────────────────────────────┘
                           │
┌─────────────────────────▼───────────────────────────────┐
│                      Userspace                           │
│  ┌──────────────────────────────────────────────────┐   │
│  │              init (PID 1)                         │   │
│  │  mounts FAT32 · populates /dev · reads /etc      │   │
│  └────────────────────────┬─────────────────────────┘   │
│                            │                             │
│  ┌─────────────────────────▼────────────────────────┐   │
│  │         nsh — NexOS Shell                         │   │
│  │  25 built-ins · history · tab-complete · vars    │   │
│  └──────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────┐   │
│  │         Minimal libc (string/stdio/stdlib)        │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## Prerequisites

Install the following tools:

```bash
# Debian/Ubuntu
sudo apt install nasm gcc binutils grub-pc-bin grub-common \
     xorriso qemu-system-x86 python3 make

# Fedora/RHEL
sudo dnf install nasm gcc binutils grub2-tools xorriso \
     qemu-system-x86 python3 make

# macOS (Homebrew)
brew install nasm x86_64-elf-gcc x86_64-elf-binutils \
     xorriso qemu python3
```

### Cross-compiler (recommended)

The Makefile auto-detects `x86_64-elf-gcc`. If not found, falls back to system `gcc` with `-m64 -mcmodel=kernel`.

---

## Building

```bash
cd nexos

# Build everything (kernel + ISO)
make all

# Build only the kernel ELF
make kernel

# Build only the ISO (requires kernel built first)
make iso

# Clean all build artifacts
make clean
```

---

## Running in QEMU

```bash
# Build and boot in QEMU
make run

# Boot from disk image instead of ISO
DISPLAY_MODE=sdl ./tools/run_qemu.sh --disk

# Use VNC instead of SDL
DISPLAY_MODE=vnc make run
```

Manual QEMU command:
```bash
qemu-system-x86_64 \
  -machine q35 -m 256M \
  -cdrom build/nexos.iso \
  -serial stdio \
  -display sdl \
  -boot d
```

---

## GDB Debugging

```bash
# Terminal 1: launch QEMU with GDB server
make run-debug

# Terminal 2: connect GDB
gdb
(gdb) target remote localhost:1234
(gdb) symbol-file build/nexos.kernel
(gdb) continue
```

---

## Running in VirtualBox

1. Open VirtualBox → **New**
2. Name: `NexOS`, Type: `Other`, Version: `Other/Unknown (64-bit)`
3. RAM: **256 MB** or more
4. Storage: No hard disk needed for ISO-only boot
5. Settings → **Storage** → Add optical drive → Select `build/nexos.iso`
6. Settings → **System** → Motherboard:
   - Enable **I/O APIC**
   - Disable **EFI** (use legacy BIOS)
7. Settings → **Network** → NAT mode
8. Click **Start**

---

## Writing to USB (bare-metal install)

```bash
# Build ISO first
make all

# Launch installer (interactive, requires sudo for dd)
make install-usb
```

Or run directly:
```bash
sudo python3 tools/install_usb.py
```

The installer will:
1. Detect all removable USB drives
2. Show drive list with sizes and labels
3. Ask you to select the target
4. Require you to type `YES` to confirm (all data will be erased)
5. Write the ISO using `dd` and call `sync`

Then boot your machine and select the USB drive in your BIOS boot menu. Disable UEFI/Secure Boot and use legacy BIOS mode.

---

## Filesystem Layout

```
/
├── bin/             User binaries (nsh, init)
├── boot/grub/       GRUB configuration
├── dev/             Device nodes (stdin, stdout, stderr, null, zero, tty0)
├── etc/
│   ├── nexos.conf   System configuration (key=value)
│   ├── nsh.rc       Shell startup script
│   └── passwd       User list
├── home/user/       Default user home
├── lib/             Shared libraries (future)
├── mnt/             FAT32 disk mount point
├── proc/            Process info (future)
├── tmp/             Temporary files (ramfs)
└── var/log/         Log files
```

---

## Shell Commands (nsh)

| Command | Description |
|---------|-------------|
| `ls [path]` | List directory contents |
| `cd [path]` | Change directory |
| `pwd` | Print working directory |
| `cat <file>` | Display file contents |
| `echo [text]` | Print text (supports `$VAR`) |
| `mkdir <dir>` | Create directory |
| `rm <file>` | Delete file |
| `cp <src> <dst>` | Copy file |
| `mv <src> <dst>` | Move/rename file |
| `touch <file>` | Create empty file |
| `clear` | Clear screen |
| `env` | Show environment variables |
| `export KEY=VALUE` | Set environment variable |
| `uname` | Print OS name and version |
| `uptime` | Show system uptime |
| `ps` | List running processes |
| `kill <pid>` | Terminate a process |
| `free` | Show memory usage |
| `date` | Show current date/time (RTC) |
| `mount` | Show mounted filesystems |
| `reboot` | Reboot the system |
| `halt` | Halt the system |
| `help` | Show all commands |
| `exit` / `logout` | Exit the shell |

---

## Syscall Table

| Number | Name | Arguments | Description |
|--------|------|-----------|-------------|
| 0 | `sys_exit` | code | Terminate process |
| 1 | `sys_read` | fd, buf, len | Read from fd |
| 2 | `sys_write` | fd, buf, len | Write to fd |
| 3 | `sys_open` | path, flags | Open file |
| 4 | `sys_close` | fd | Close file descriptor |
| 5 | `sys_fork` | — | Create child process |
| 6 | `sys_exec` | path, argv | Replace process image |
| 7 | `sys_getpid` | — | Get process ID |
| 8 | `sys_sleep` | ms | Sleep for milliseconds |
| 9 | `sys_stat` | path, stat_buf | Get file info |
| 10 | `sys_mkdir` | path | Create directory |
| 11 | `sys_readdir` | fd, dirent | Read directory entry |
| 12 | `sys_getcwd` | buf, size | Get working directory |
| 13 | `sys_chdir` | path | Change directory |
| 14 | `sys_sbrk` | increment | Grow heap |

Syscalls use `INT 0x80`. Arguments in: `rax`=num, `rdi`=a1, `rsi`=a2, `rdx`=a3, `r10`=a4, `r8`=a5. Return in `rax`.

---

## Known Limitations & Future Roadmap

### Current Limitations (v0.1)
- No SMP (single CPU only)
- No UEFI boot (legacy BIOS only)
- No networking stack (stub only)
- No DMA for disk I/O (PIO polling only)
- No dynamic linking / shared libraries
- FAT32 write support is basic (no directory entry update for existing files)
- Scheduler runs processes cooperatively within the kernel (no ring 3 preemption yet)

### Roadmap
- [ ] Ring 3 user-mode processes with proper syscall entry
- [ ] Network stack (e1000 driver → TCP/IP)
- [ ] UEFI boot via GRUB EFI
- [ ] SMP support (APIC, CPU cores)
- [ ] ELF binary loader for external programs
- [ ] TTY layer and ANSI terminal emulation
- [ ] `/proc` virtual filesystem
- [ ] ext2/ext4 filesystem support
- [ ] NexOS package manager

---

## License

MIT License — Copyright (c) 2024 NexOS Project

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
