/* NexOS — kernel/drivers/keyboard.h | PS/2 keyboard driver | MIT License */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
char keyboard_getchar(void);
int  keyboard_available(void);

#endif
