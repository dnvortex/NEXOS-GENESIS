/* NexOS — kernel/drivers/keyboard.h | PS/2 keyboard driver | MIT License */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
char keyboard_getchar(void);
int  keyboard_available(void);

/* Extended key codes (produced by 0xE0-prefixed PS/2 scancodes).
 * These arrive as char values with the high bit set (negative signed char). */
#define KEY_UP    ((char)0x80)
#define KEY_DOWN  ((char)0x81)
#define KEY_LEFT  ((char)0x82)
#define KEY_RIGHT ((char)0x83)
#define KEY_HOME  ((char)0x84)
#define KEY_END   ((char)0x85)
#define KEY_PGUP  ((char)0x86)
#define KEY_PGDN  ((char)0x87)
#define KEY_DEL   ((char)0x88)

#endif
