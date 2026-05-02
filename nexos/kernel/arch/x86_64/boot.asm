; NexOS — kernel/arch/x86_64/boot.asm | Multiboot2 entry point | MIT License

bits 32

MULTIBOOT2_MAGIC    equ 0xE85250D6
MULTIBOOT2_ARCH     equ 0           ; i386 protected mode

section .multiboot2
align 8
multiboot2_header_start:
    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH
    dd multiboot2_header_end - multiboot2_header_start
    dd -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + (multiboot2_header_end - multiboot2_header_start))

; Framebuffer tag — request 80x25 text mode
align 8
framebuffer_tag_start:
    dw 5                ; type: framebuffer
    dw 0                ; flags
    dd framebuffer_tag_end - framebuffer_tag_start
    dd 0                ; width (0 = no preference)
    dd 0                ; height
    dd 0                ; depth (0 = text mode)
framebuffer_tag_end:

; End tag
align 8
    dw 0                ; type: end
    dw 0                ; flags
    dd 8                ; size
multiboot2_header_end:

section .bss
align 16
stack_bottom:
    resb 16384          ; 16KB initial stack
stack_top:

section .text
global boot_start
extern kernel_main

boot_start:
    ; Set up stack
    mov esp, stack_top

    ; Clear direction flag
    cld

    ; Push multiboot2 info pointer and magic
    push 0              ; pad to 8 bytes
    push ebx            ; multiboot2 info pointer
    push 0
    push eax            ; multiboot2 magic

    ; Call kernel main
    call kernel_main

    ; Should never reach here
.hang:
    cli
    hlt
    jmp .hang
