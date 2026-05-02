; NexOS — kernel/arch/x86_64/gdt_flush.asm | GDT/TSS reload stubs | MIT License

bits 64

global gdt_flush
global tss_flush

gdt_flush:
    lgdt [rdi]
    mov ax, 0x10        ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    pop rdi             ; return address
    mov rax, 0x08       ; kernel code selector
    push rax
    push rdi
    retfq

tss_flush:
    mov ax, 0x2B        ; TSS selector (index 5, RPL=3? No: 0x28 | 0x00 = 0x28)
    ltr ax
    ret
