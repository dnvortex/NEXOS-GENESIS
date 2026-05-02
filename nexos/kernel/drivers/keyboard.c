/* NexOS — kernel/drivers/keyboard.c | PS/2 keyboard via IRQ1 | MIT License */
#include "keyboard.h"
#include "../kernel.h"
#include "../arch/x86_64/idt.h"

void irq_install_handler(int irq, void (*handler)(registers_t *));
void irq_uninstall_handler(int irq);

#define KBD_DATA 0x60
#define KBD_STAT 0x64

/* Ring buffer */
#define KBD_BUF_SIZE 256
static char    kbd_buf[KBD_BUF_SIZE];
static uint8_t kbd_buf_head = 0;
static uint8_t kbd_buf_tail = 0;

/* Modifier state */
static int shift_down  = 0;
static int ctrl_down   = 0;
static int alt_down    = 0;
static int caps_lock   = 0;

/* Scan code set 1 → ASCII (unshifted) */
static const char sc_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char sc_to_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?',0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void kbd_handler(registers_t *regs) {
    UNUSED(regs);
    uint8_t sc = io_inb(KBD_DATA);
    int released = sc & 0x80;
    sc &= 0x7F;

    switch (sc) {
        case 0x2A: case 0x36: shift_down = !released; return;
        case 0x1D: ctrl_down = !released; return;
        case 0x38: alt_down  = !released; return;
        case 0x3A: if (!released) caps_lock = !caps_lock; return;
        default: break;
    }

    if (released) return;

    char ch = 0;
    if (shift_down)
        ch = sc_to_ascii_shift[sc];
    else
        ch = sc_to_ascii[sc];

    if (caps_lock && ch >= 'a' && ch <= 'z') ch -= 32;
    if (caps_lock && ch >= 'A' && ch <= 'Z' && shift_down) ch += 32;

    if (ch) {
        uint8_t next = (kbd_buf_tail + 1) % KBD_BUF_SIZE;
        if (next != kbd_buf_head) {
            kbd_buf[kbd_buf_tail] = ch;
            kbd_buf_tail = next;
        }
    }
    UNUSED(ctrl_down); UNUSED(alt_down);
}

void keyboard_init(void) {
    irq_install_handler(1, kbd_handler);
    klog(LOG_INFO, "Keyboard driver initialized");
}

int keyboard_available(void) {
    return kbd_buf_head != kbd_buf_tail;
}

char keyboard_getchar(void) {
    while (!keyboard_available()) { __asm__ volatile ("hlt"); }
    char ch = kbd_buf[kbd_buf_head];
    kbd_buf_head = (kbd_buf_head + 1) % KBD_BUF_SIZE;
    return ch;
}
