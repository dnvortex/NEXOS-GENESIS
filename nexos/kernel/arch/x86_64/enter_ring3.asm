; NexOS — kernel/arch/x86_64/enter_ring3.asm | Drop to ring 3 via IRET | MIT License
bits 64

global enter_ring3

; void enter_ring3(uint64_t entry, uint64_t user_stack_top)
;   rdi = entry point (RIP for ring 3)
;   rsi = user stack top (RSP for ring 3)
;
; GDT selectors:
;   0x18 = user code  (index 3, RPL=0)  → 0x18 | 3 = 0x1B
;   0x20 = user data  (index 4, RPL=0)  → 0x20 | 3 = 0x23
enter_ring3:
    ; Set data segments to user data segment
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build IRET frame on the kernel stack:
    ;   [SS, RSP, RFLAGS, CS, RIP]
    push 0x23           ; SS  = user data segment | RPL 3
    push rsi            ; RSP = user stack top
    pushfq
    pop  rax
    or   rax, 0x200     ; ensure IF (interrupt enable) is set
    and  rax, ~0x3000   ; clear IOPL — ring 3 should not have raw I/O access
    push rax            ; RFLAGS
    push 0x1B           ; CS  = user code segment | RPL 3
    push rdi            ; RIP = entry point

    iretq
