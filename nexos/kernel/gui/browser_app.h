/* NexOS — kernel/gui/browser_app.h | GUI Web Browser | MIT License */
#pragma once
#include "wm.h"
#include <stdint.h>

#define BROWSER_URL_MAX   512
#define BROWSER_BUF_MAX   (24 * 1024)

typedef enum {
    BSTATE_IDLE,
    BSTATE_LOADING,
    BSTATE_DONE,
    BSTATE_ERROR
} browser_state_t;

typedef struct {
    window_t        *win;
    char             url[BROWSER_URL_MAX];
    int              url_len;
    char             text[BROWSER_BUF_MAX];
    int              text_len;
    int              scroll;
    int              line_count;
    browser_state_t  state;
    char             status[80];
} browser_app_t;

browser_app_t *browser_create(int x, int y);
