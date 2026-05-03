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
static int kbd_extended = 0;  /* set on 0xE0 prefix for arrow/nav keys */

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

    /* 0xE0 prefix → next byte is an extended (arrow/nav) scancode */
    if (sc == 0xE0) { kbd_extended = 1; return; }

    int released = sc & 0x80;
    sc &= 0x7F;

    /* Extended key dispatch (arrow keys, Delete, Home, End, PgUp, PgDn) */
    if (kbd_extended) {
        kbd_extended = 0;
        if (!released) {
            char ext_ch = 0;
            switch (sc) {
            case 0x48: ext_ch = (char)0x80; break; /* Up    → KEY_UP    */
            case 0x50: ext_ch = (char)0x81; break; /* Down  → KEY_DOWN  */
            case 0x4B: ext_ch = (char)0x82; break; /* Left  → KEY_LEFT  */
            case 0x4D: ext_ch = (char)0x83; break; /* Right → KEY_RIGHT */
            case 0x47: ext_ch = (char)0x84; break; /* Home  → KEY_HOME  */
            case 0x4F: ext_ch = (char)0x85; break; /* End   → KEY_END   */
            case 0x49: ext_ch = (char)0x86; break; /* PgUp  → KEY_PGUP  */
            case 0x51: ext_ch = (char)0x87; break; /* PgDn  → KEY_PGDN  */
            case 0x53: ext_ch = (char)0x88; break; /* Del   → KEY_DEL   */
            default:   break;
            }
            if (ext_ch) {
                uint8_t next = (kbd_buf_tail + 1) % KBD_BUF_SIZE;
                if (next != kbd_buf_head) {
                    kbd_buf[kbd_buf_tail] = ext_ch;
                    kbd_buf_tail = next;
                }
            }
        }
        return;
    }

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
        /* Encode Ctrl+letter as ASCII control codes 1-26 */
        if (ctrl_down) {
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 1);
            else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 1);
        }
        uint8_t next = (kbd_buf_tail + 1) % KBD_BUF_SIZE;
        if (next != kbd_buf_head) {
            kbd_buf[kbd_buf_tail] = ch;
            kbd_buf_tail = next;
        }
    }
    UNUSED(alt_down);
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
