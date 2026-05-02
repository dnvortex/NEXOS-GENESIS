/* NexOS — kernel/gui/theme_app.h | Theme switcher window | MIT License */
#pragma once
#include "wm.h"

#define THEME_COUNT 4

typedef struct {
    const char *name;
    uint32_t base, mantle, crust;
    uint32_t surface0, surface1, surface2;
    uint32_t overlay0;
    uint32_t text, subtext;
    uint32_t blue, lavender, mauve;
    uint32_t red, peach, yellow, green;
    uint32_t teal, sky;
    /* 5 showcase dots */
    uint32_t dot[5];
} theme_def_t;

extern const theme_def_t g_themes[THEME_COUNT];
extern int g_active_theme;

void     theme_apply(int id);
window_t *theme_create(int x, int y);
