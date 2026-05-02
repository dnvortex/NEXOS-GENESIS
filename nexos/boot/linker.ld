/* NexOS — boot/linker.ld | Kernel linker script | MIT License */

ENTRY(boot_start)

SECTIONS
{
    . = 0x100000;

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

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.note.GNU-stack)
    }
}
