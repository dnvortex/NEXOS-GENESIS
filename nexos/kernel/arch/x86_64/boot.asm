; NexOS — kernel/arch/x86_64/boot.asm | Multiboot2 + Long Mode Entry | MIT License
;
; Execution flow:
;   GRUB2 → boot_start (32-bit protected mode)
;     → enable A20 → build page tables → load GDT → enable PAE
;     → load CR3 → set EFER.LME → enable paging → far-jump to 64-bit
;   long_mode_entry (64-bit)
;     → set segment registers → set RSP → VGA sentinel → kernel_main

bits 32

; ─────────────────────────────────────────────────────────────────────────────
;  Multiboot2 header
; ─────────────────────────────────────────────────────────────────────────────
%define MB2_MAGIC   0xE85250D6
%define MB2_ARCH    0               ; i386 protected mode

section .multiboot2
align 8
mb2_header_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd mb2_header_end - mb2_header_start
    dd 0x100000000 - (MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header_start))

; End tag
align 8
    dw 0            ; type  = end
    dw 0            ; flags = 0
    dd 8            ; size  = 8
mb2_header_end:

; ─────────────────────────────────────────────────────────────────────────────
;  BSS: early page tables (page-aligned) + 16 KB stack
;  These live here so they are zero-initialised by the loader.
; ─────────────────────────────────────────────────────────────────────────────
section .bss
align 4096
boot_pml4:  resb 4096       ; Page-Map Level 4
boot_pdpt:  resb 4096       ; Page Directory Pointer Table
boot_pd:    resb 4096       ; Page Directory  (2 MB huge-page entries)

align 16
stack_bottom:
    resb 16384              ; 16 KB kernel stack
stack_top:

; ─────────────────────────────────────────────────────────────────────────────
;  Data: minimal GDT for 64-bit long mode
;  Descriptor layout (Intel Vol. 3A §3.4.5):
;    null  [0x00]: all zeros
;    code  [0x08]: 64-bit ring-0 code  (L=1, P=1, S=1, E=1, R=1)
;    data  [0x10]: ring-0 data         (P=1, S=1, W=1)
; ─────────────────────────────────────────────────────────────────────────────
section .data
align 8
gdt64:
    dq 0                                            ; null descriptor
.code_off: equ $ - gdt64                            ; = 0x08
    dq (1<<41)|(1<<43)|(1<<44)|(1<<47)|(1<<53)     ; 64-bit code, ring 0
.data_off: equ $ - gdt64                            ; = 0x10
    dq (1<<41)|(1<<44)|(1<<47)                      ; data, ring 0
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1   ; limit
    dd gdt64                    ; base (32-bit; kernel loads < 4 GB)

; ─────────────────────────────────────────────────────────────────────────────
;  32-bit entry — GRUB2 enters here with:
;    EAX = 0x36D76289  (Multiboot2 bootloader magic)
;    EBX = physical address of Multiboot2 info structure
;    Protected mode ON, paging OFF, interrupts OFF
; ─────────────────────────────────────────────────────────────────────────────
section .text
global boot_start
extern kernel_main

boot_start:
    cli                         ; ensure interrupts are off

    ; Preserve Multiboot2 arguments — System V AMD64 ABI first two args:
    ;   rdi = mb2_magic  (from eax)
    ;   rsi = mb2_info*  (from ebx)
    ; We cannot use rdi/rsi yet (32-bit mode), store in edi/esi for now;
    ; the zero-extension from 32→64 bit happens automatically.
    mov edi, eax                ; Multiboot2 magic
    mov esi, ebx                ; Multiboot2 info pointer

    ; ── 1. Enable A20 line via port 0x92 ─────────────────────────────────────
    in  al, 0x92
    or  al, 0x02                ; set bit 1 (A20 enable)
    and al, 0xFE                ; clear bit 0 (do not reset)
    out 0x92, al

    ; ── 2. Build early identity-map page tables ───────────────────────────────
    ;  PML4[0] → boot_pdpt  (present + writable)
    mov eax, boot_pdpt
    or  eax, 0x03
    mov [boot_pml4], eax

    ;  PDPT[0] → boot_pd   (present + writable)
    mov eax, boot_pd
    or  eax, 0x03
    mov [boot_pdpt], eax

    ;  PD[0]  → 0x000000, 2 MB huge page (present | writable | PS)
    mov dword [boot_pd + 0], 0x000083

    ;  PD[1]  → 0x200000, 2 MB huge page (present | writable | PS)
    mov dword [boot_pd + 8], 0x200083

    ; ── 3. Load GDT (must be done before enabling long mode) ─────────────────
    lgdt [gdt64_ptr]

    ; ── 4. Enable PAE (CR4 bit 5) ─────────────────────────────────────────────
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; ── 5. Load PML4 physical address into CR3 ────────────────────────────────
    mov eax, boot_pml4
    mov cr3, eax

    ; ── 6. Enable Long Mode (EFER.LME = bit 8 in MSR 0xC0000080) ─────────────
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; ── 7. Enable Paging + Protected Mode (CR0 bits 31 and 0) ────────────────
    mov eax, cr0
    or  eax, (1 << 31) | (1 << 0)
    mov cr0, eax

    ; ── 8. Far jump: flush pipeline, load CS with 64-bit code descriptor ──────
    ;  The label long_mode_entry is emitted with bits 64, so its offset
    ;  fits in 32 bits (kernel loads at 1 MB).
    jmp 0x08:long_mode_entry

; ─────────────────────────────────────────────────────────────────────────────
;  64-bit long-mode entry
; ─────────────────────────────────────────────────────────────────────────────
bits 64
long_mode_entry:
    ; ── 9. Load data segment selector into all segment registers ─────────────
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; ── 10. Set up 16-byte-aligned kernel stack ────────────────────────────────
    mov rsp, stack_top

    ; ── 11. VGA sentinel: write "OK" at top-left (0xB8000) ───────────────────
    ;  This proves we reached 64-bit mode before calling any C code.
    mov rax, 0xB8000
    mov word [rax + 0], 0x0F4F  ; 'O'  white on black
    mov word [rax + 2], 0x0F4B  ; 'K'

    ; ── 12. Call kernel_main(uint32_t mb2_magic, mb2_info_t *mb2_info) ─────────
    ;  edi already holds mb2_magic (zero-extended to rdi)
    ;  esi already holds mb2_info ptr (zero-extended to rsi)
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
