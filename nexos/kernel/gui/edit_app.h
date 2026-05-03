/* NexOS — kernel/gui/edit_app.h | GUI Text Editor | MIT License */
#pragma once
#include "wm.h"
#include <stdint.h>

#define EDIT_MAX_LINES  200
#define EDIT_LINE_CAP   200

typedef enum { EPROMPT_NONE, EPROMPT_SAVE, EPROMPT_OPEN } eprompt_t;

typedef struct {
    window_t   *win;
    char        lines[EDIT_MAX_LINES][EDIT_LINE_CAP];
    int         line_len[EDIT_MAX_LINES];
    int         num_lines;
    int         cur_col, cur_row;
    int         scroll_y;
    int         modified;
    char        filename[256];
    int         has_file;
    eprompt_t   prompt;
    char        prompt_buf[256];
    int         prompt_len;
} edit_app_t;

edit_app_t *edit_create(int x, int y, const char *path);
