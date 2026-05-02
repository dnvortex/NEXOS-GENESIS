/* NexOS — kernel/gui/term_app.h | GUI Terminal application | MIT License */
#pragma once
#include "wm.h"

#define TERM_COLS  90
#define TERM_ROWS  28
#define TERM_PAD    8

typedef struct {
    window_t *win;
    char buf[TERM_ROWS][TERM_COLS + 1];
    int col, row;
    char input[256];
    int input_len;
    uint32_t fg, bg;
    uint8_t dirty;
} term_app_t;

term_app_t *term_create(int x, int y);
void        term_puts(term_app_t *t, const char *s);
void        term_printf(term_app_t *t, const char *fmt, ...);
void        term_set_active(term_app_t *t);
