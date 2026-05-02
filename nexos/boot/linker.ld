/* NexOS — boot/linker.ld | Kernel linker script | MIT License */

ENTRY(boot_start)

SECTIONS
{
    . = 0x100000;

    /* Exported to C so the PMM can precisely reserve the kernel image */
    kernel_start = .;

    .boot :
    {
        KEEP(*(.multiboot2))
    }

    .text ALIGN(4K) :
    {
        *(.text*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata*)
    }

    .data ALIGN(4K) :
    {
        *(.data*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss*)
    }

    /* Page-aligned end — covers all code, data, BSS (heap, bitmap, stacks,
       boot page tables).  The PMM uses this to avoid handing out kernel pages. */
    kernel_end = ALIGN(4096);

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.note.GNU-stack)
    }
}
