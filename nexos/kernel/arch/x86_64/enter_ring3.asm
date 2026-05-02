; NexOS — kernel/arch/x86_64/enter_ring3.asm
; Ring-3 IRET launcher + kernel-thread launcher
; MIT License
bits 64

global enter_ring3
global enter_kernel_thread

; ──────────────────────────────────────────────────────────────────────────
; void enter_kernel_thread(uint64_t entry, uint64_t kernel_stack_top)
;   rdi = entry point  (C function pointer, never returns)
;   rsi = kernel stack top (already 16-byte aligned by caller)
;
; FIX 2c / FIX 4 — kernel-thread guard:
;   Instead of IRET with ring-3 selectors, simply switch to the process's
;   dedicated kernel stack and jump to the entry function.  No privilege
;   change takes place; the thread runs entirely at CPL=0.
;
;   The TSS rsp0 has already been set by proc_enter_ring3() in process.c
;   before calling this stub, so any future interrupt/syscall will land on
;   the correct kernel stack regardless of which stack we're on here.
; ──────────────────────────────────────────────────────────────────────────
enter_kernel_thread:
    ; Switch to the process's own kernel stack.
    and  rsi, ~0xF          ; ensure 16-byte alignment (should already be)
    mov  rsp, rsi           ; switch stack
    xor  rbp, rbp           ; mark bottom of call-frame chain

    ; Restore segment registers to kernel values (DS/ES/FS/GS may have been
    ; set to user-data selector 0x23 by a previous enter_ring3 call).
    mov  ax, 0x10           ; GDT_KERNEL_DATA
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; Jump to the C entry function — this never returns.
    ; (Using jmp instead of call so the function cannot accidentally ret
    ;  back to a dangling frame.)
    jmp  rdi

; ──────────────────────────────────────────────────────────────────────────
; void enter_ring3(uint64_t entry, uint64_t user_stack_top)
;   rdi = entry point (RIP for ring-3)
;   rsi = user stack top (RSP for ring-3)
;
; GDT selectors:
;   0x18 = user code  (index 3, RPL=0)  → 0x18 | 3 = 0x1B
;   0x20 = user data  (index 4, RPL=0)  → 0x20 | 3 = 0x23
;
; Guard: callers (proc_enter_ring3 in process.c) already check CS and
; dispatch to enter_kernel_thread instead when CS == GDT_KERNEL_CODE.
; This function therefore always performs the full ring-3 IRET.
; ──────────────────────────────────────────────────────────────────────────
enter_ring3:
    ; Set data segments to user data selector
    mov  ax, 0x23
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    ; Build IRET frame on the kernel stack:
    ;   [SS, RSP, RFLAGS, CS, RIP]   (top → bottom of push order)
    push 0x23           ; SS  = user data | RPL 3
    push rsi            ; RSP = user stack top
    pushfq
    pop  rax
    or   rax, 0x200     ; ensure IF is set
    and  rax, ~0x3000   ; clear IOPL — ring 3 has no raw I/O
    push rax            ; RFLAGS
    push 0x1B           ; CS  = user code | RPL 3
    push rdi            ; RIP = entry point

    iretq
