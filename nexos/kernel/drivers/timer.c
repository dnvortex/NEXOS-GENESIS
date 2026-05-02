/* NexOS — kernel/drivers/timer.c | 8253/8254 PIT at 1000Hz | MIT License */
#include "timer.h"
#include "../kernel.h"
#include "../arch/x86_64/idt.h"

void irq_install_handler(int irq, void (*handler)(registers_t *));
void irq_uninstall_handler(int irq);

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_BASE_HZ 1193182

static volatile uint64_t ticks = 0;

static void timer_handler(registers_t *regs) {
    UNUSED(regs);
    ticks++;
}

void timer_init(uint32_t freq_hz) {
    uint32_t divisor = PIT_BASE_HZ / freq_hz;
    io_outb(PIT_CMD, 0x36);                      /* Channel 0, lobyte/hibyte, square wave */
    io_outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    io_outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
    irq_install_handler(0, timer_handler);
    klog(LOG_INFO, "PIT timer initialized at %u Hz", freq_hz);
}

void timer_sleep_ms(uint32_t ms) {
    uint64_t target = ticks + ms;
    while (ticks < target) { __asm__ volatile ("hlt"); }
}

uint64_t timer_get_ticks(void) {
    return ticks;
}

uint64_t timer_get_uptime_seconds(void) {
    return ticks / 1000;
}
