/* NexOS — kernel/arch/x86_64/gdt.c | Global Descriptor Table | MIT License */
#include "gdt.h"
#include "../../kernel.h"

static gdt_entry_t gdt[7];
static gdtr_t gdtr;
tss_t tss;

extern void gdt_flush(uint64_t gdtr_ptr);
extern void tss_flush(void);

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    gdt[idx].base_low  = (base & 0xFFFF);
    gdt[idx].base_mid  = (base >> 16) & 0xFF;
    gdt[idx].base_high = (base >> 24) & 0xFF;
    gdt[idx].limit_low = (limit & 0xFFFF);
    gdt[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[idx].access    = access;
}

static void gdt_set_tss(int idx, uint64_t base, uint32_t limit) {
    /* TSS needs a 16-byte descriptor in 64-bit mode */
    uint64_t *tss_desc = (uint64_t *)&gdt[idx];
    tss_desc[0] = 0;
    tss_desc[1] = 0;

    tss_desc[0] |= (uint64_t)(limit & 0xFFFF);
    tss_desc[0] |= (uint64_t)(base & 0xFFFFFF) << 16;
    tss_desc[0] |= (uint64_t)0x89 << 40;
    tss_desc[0] |= (uint64_t)((limit >> 16) & 0xF) << 48;
    tss_desc[0] |= (uint64_t)((base >> 24) & 0xFF) << 56;
    tss_desc[1] = (base >> 32) & 0xFFFFFFFF;
}

void gdt_init(void) {
    /* Null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Kernel code segment: ring 0, 64-bit, execute/read */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);

    /* Kernel data segment: ring 0, read/write */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* User code segment: ring 3, 64-bit, execute/read */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xAF);

    /* User data segment: ring 3, read/write */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    /* TSS */
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    gdt_set_tss(5, tss_base, tss_limit);

    /* Set up GDTR */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)&gdt;

    gdt_flush((uint64_t)&gdtr);
    tss_flush();

    klog(LOG_INFO, "GDT initialized (ring 0/3, TSS loaded)");
}

/* Update TSS rsp0 — called on every process switch so syscalls land
   on the correct kernel stack for the current process. */
void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
