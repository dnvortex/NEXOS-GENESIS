/* NexOS — kernel/arch/x86_64/idt.c | Interrupt Descriptor Table | MIT License */
#include "idt.h"
#include "../../kernel.h"
#include "../../drivers/vga.h"
#include "../../drivers/serial.h"

static idt_entry_t idt[256];
static idtr_t idtr;

extern void idt_flush(uint64_t idtr_ptr);

/* ISR stubs defined in isr.asm */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

/* IRQ stubs */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

/* IRQ handler table */
static void (*irq_handlers[16])(registers_t *) = {0};

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = handler & 0xFFFF;
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].reserved    = 0;
}

void irq_install_handler(int irq, void (*handler)(registers_t *)) {
    irq_handlers[irq] = handler;
}

void irq_uninstall_handler(int irq) {
    irq_handlers[irq] = 0;
}

/* PIC remapping */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_remap(void) {
    /* Save masks */
    uint8_t m1 = io_inb(PIC1_DATA);
    uint8_t m2 = io_inb(PIC2_DATA);

    /* Initialize in cascade mode */
    io_outb(PIC1_CMD,  0x11); io_wait();
    io_outb(PIC2_CMD,  0x11); io_wait();

    /* Remap: IRQ0-7 → INT 32-39, IRQ8-15 → INT 40-47 */
    io_outb(PIC1_DATA, 0x20); io_wait();
    io_outb(PIC2_DATA, 0x28); io_wait();

    /* Cascade setup */
    io_outb(PIC1_DATA, 0x04); io_wait();
    io_outb(PIC2_DATA, 0x02); io_wait();

    /* 8086 mode */
    io_outb(PIC1_DATA, 0x01); io_wait();
    io_outb(PIC2_DATA, 0x01); io_wait();

    /* Restore masks */
    io_outb(PIC1_DATA, m1);
    io_outb(PIC2_DATA, m2);
}

static const char *exception_names[] = {
    "Division By Zero",          "Debug",
    "Non-Maskable Interrupt",    "Breakpoint",
    "Overflow",                  "Bound Range Exceeded",
    "Invalid Opcode",            "Device Not Available",
    "Double Fault",              "Coprocessor Segment Overrun",
    "Invalid TSS",               "Segment Not Present",
    "Stack-Segment Fault",       "General Protection Fault",
    "Page Fault",                "Reserved",
    "x87 Floating-Point",        "Alignment Check",
    "Machine Check",             "SIMD Floating-Point",
    "Virtualization",            "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection",      "VMM Communication",
    "Security",                  "Reserved"
};

void isr_handler(registers_t *regs) {
    uint64_t vec = regs->int_no;

    serial_printf("[EXCEPTION] #%llu %s | err=0x%llx\n",
        vec,
        (vec < 32) ? exception_names[vec] : "Unknown",
        regs->err_code);
    serial_printf("  RIP=0x%llx  CS=0x%llx  RFLAGS=0x%llx\n",
        regs->rip, regs->cs, regs->rflags);
    serial_printf("  RSP=0x%llx  SS=0x%llx\n", regs->rsp, regs->ss);
    serial_printf("  RAX=0x%llx  RBX=0x%llx  RCX=0x%llx  RDX=0x%llx\n",
        regs->rax, regs->rbx, regs->rcx, regs->rdx);
    serial_printf("  RSI=0x%llx  RDI=0x%llx  RBP=0x%llx\n",
        regs->rsi, regs->rdi, regs->rbp);
    serial_printf("  R8=0x%llx  R9=0x%llx  R10=0x%llx  R11=0x%llx\n",
        regs->r8, regs->r9, regs->r10, regs->r11);

    vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    vga_puts("\n\n*** KERNEL EXCEPTION ***\n");
    vga_puts(exception_names[vec < 32 ? vec : 0]);
    vga_puts(" — System Halted\n");

    cli();
    for (;;) hlt();
}

void irq_handler(registers_t *regs) {
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    if (irq_handlers[irq]) {
        irq_handlers[irq](regs);
    }

    /* Send EOI */
    if (irq >= 8) io_outb(PIC2_CMD, PIC_EOI);
    io_outb(PIC1_CMD, PIC_EOI);
}

void idt_init(void) {
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;

    /* CPU exceptions 0-31 */
    idt_set_gate(0,  (uint64_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint64_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint64_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint64_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint64_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint64_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint64_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint64_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint64_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint64_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E);

    /* IRQ handlers 32-47 */
    idt_set_gate(32, (uint64_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint64_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint64_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint64_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint64_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint64_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint64_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint64_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint64_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint64_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0x8E);

    /* Remap PIC */
    pic_remap();

    idt_flush((uint64_t)&idtr);

    klog(LOG_INFO, "IDT initialized (exceptions + IRQ0-15 remapped to INT 32-47)");
}
