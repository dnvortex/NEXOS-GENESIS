# NexOS — Makefile | Top-level build system | MIT License

# Cross-compiler detection
ifneq ($(shell which x86_64-elf-gcc 2>/dev/null),)
    CC = x86_64-elf-gcc
    LD = x86_64-elf-ld
else
    CC = gcc
    LD = ld
endif

ASM    = nasm
PYTHON = python3

# Directories
ROOT    = .
BUILD   = build
KERNEL  = kernel
BOOT    = boot
USER    = userspace
TOOLS   = tools

# Output
KERNEL_ELF  = $(BUILD)/nexos.kernel
ISO_FILE    = $(BUILD)/nexos.iso

# Compiler flags for kernel
CFLAGS = -std=c11 -ffreestanding -fno-stack-protector -fno-pic \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -Wall -Wextra -O2 -m64 -mcmodel=kernel \
         -nostdlib -nostdinc \
         -I$(ROOT) -I$(KERNEL) -I$(KERNEL)/include \
         -Wno-unused-parameter -Wno-unused-variable

ASFLAGS = -f elf64

LDFLAGS = -T $(BOOT)/linker.ld -nostdlib -z noexecstack

# Source files
KERNEL_C_SOURCES = \
    $(KERNEL)/kernel.c \
    $(KERNEL)/arch/x86_64/gdt.c \
    $(KERNEL)/arch/x86_64/idt.c \
    $(KERNEL)/arch/x86_64/paging.c \
    $(KERNEL)/drivers/vga.c \
    $(KERNEL)/drivers/serial.c \
    $(KERNEL)/drivers/keyboard.c \
    $(KERNEL)/drivers/timer.c \
    $(KERNEL)/drivers/ata.c \
    $(KERNEL)/drivers/pci.c \
    $(KERNEL)/drivers/rtc.c \
    $(KERNEL)/mm/pmm.c \
    $(KERNEL)/mm/heap.c \
    $(KERNEL)/fs/vfs.c \
    $(KERNEL)/fs/ramfs.c \
    $(KERNEL)/fs/fat32.c \
    $(KERNEL)/proc/process.c \
    $(KERNEL)/proc/scheduler.c \
    $(KERNEL)/proc/syscall.c \
    $(KERNEL)/net/stub.c \
    $(USER)/init/init.c \
    $(USER)/shell/nsh.c

KERNEL_ASM_SOURCES = \
    $(KERNEL)/arch/x86_64/boot.asm \
    $(KERNEL)/arch/x86_64/gdt_flush.asm \
    $(KERNEL)/arch/x86_64/isr.asm

# Object files
KERNEL_C_OBJS   = $(patsubst %.c,   $(BUILD)/%.o, $(KERNEL_C_SOURCES))
KERNEL_ASM_OBJS = $(patsubst %.asm, $(BUILD)/%.asm.o, $(KERNEL_ASM_SOURCES))
ALL_OBJS = $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS)

# ---- Targets ----

.PHONY: all kernel iso clean run run-debug install-usb

all: kernel iso

kernel: $(KERNEL_ELF)

iso: kernel
        @chmod +x $(TOOLS)/build_iso.sh
        @$(TOOLS)/build_iso.sh

run: iso
        @chmod +x $(TOOLS)/run_qemu.sh
        @$(TOOLS)/run_qemu.sh

run-debug: iso
        @chmod +x $(TOOLS)/run_qemu_debug.sh
        @$(TOOLS)/run_qemu_debug.sh

install-usb: iso
        @$(PYTHON) $(TOOLS)/install_usb.py

clean:
        rm -rf $(BUILD)

# ---- Kernel ELF ----

$(KERNEL_ELF): $(ALL_OBJS) $(BOOT)/linker.ld
        @mkdir -p $(dir $@)
        @echo "[LD] $@"
        $(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)

# ---- Compile C ----

$(BUILD)/%.o: %.c
        @mkdir -p $(dir $@)
        @echo "[CC] $<"
        $(CC) $(CFLAGS) -c $< -o $@

# ---- Assemble ----

$(BUILD)/%.asm.o: %.asm
        @mkdir -p $(dir $@)
        @echo "[ASM] $<"
        $(ASM) $(ASFLAGS) $< -o $@

# ---- Dependencies ----

-include $(ALL_OBJS:.o=.d)
