/* NexOS — kernel/drivers/serial.h | Serial port driver | MIT License */
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *str);
void serial_printf(const char *fmt, ...);

#endif
