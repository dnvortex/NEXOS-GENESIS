/* NexOS — kernel/gui/edit_app.c | GUI Text Editor | MIT License */
#include "edit_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/keyboard.h"
#include "../fs/vfs.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t sz);
void  kfree(void *p);

/* ── String / memory helpers ─────────────────────────────────────────────── */
static int elen(const char *s) { int n=0; while(s[n]) n++; return n; }
static void ecpy(char *d, const char *s, int max) {
    int i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static void emove(void *dst, const void *src, int n) {
    uint8_t *d=(uint8_t*)dst; const uint8_t *s=(const uint8_t*)src;
    if (d < s) for(int i=0;i<n;i++) d[i]=s[i];
    else for(int i=n-1;i>=0;i--) d[i]=s[i];
}
static void ezero(void *dst, int n) { uint8_t *d=(uint8_t*)dst; for(int i=0;i<n;i++) d[i]=0; }
static int emin(int a,int b){return a<b?a:b;}
static int emax(int a,int b){return a>b?a:b;}

/* ── Layout ──────────────────────────────────────────────────────────────── */
#define WIN_W      760
#define WIN_H      500
#define TOOLBAR_H   30
#define STATUS_H    20
#define LNUM_W      38   /* line number gutter width */
#define LINE_H      16   /* pixels per text line */

static int content_top(void)    { return TOOLBAR_H; }
static int content_h(window_t *w) { return w->h - WM_TITLEBAR_H - TOOLBAR_H - STATUS_H; }
static int visible_lines(window_t *w) { return content_h(w) / LINE_H; }
static int text_x(int wx)        { return wx + LNUM_W + 4; }
static int max_col(int ww)       { return (ww - LNUM_W - 8) / 8; }

/* ── Number helpers ──────────────────────────────────────────────────────── */
static void int_to_s(int v, char *buf) {
    char t[12]; int ti=0;
    if (!v) { t[ti++]='0'; } else while(v>0){t[ti++]='0'+v%10;v/=10;}
    int bi=0; while(ti>0) buf[bi++]=t[--ti]; buf[bi]=0;
}

/* ── File I/O ────────────────────────────────────────────────────────────── */
static void edit_load(edit_app_t *e, const char *path) {
    vfs_node_t *node = vfs_open(path, 0);
    if (!node) return;

    uint8_t buf[EDIT_LINE_CAP + 2];
    uint64_t off = 0;
    int row = 0, col = 0;
    ezero(e->lines, sizeof(e->lines));
    ezero(e->line_len, sizeof(e->line_len));
    e->num_lines = 1;

    while (row < EDIT_MAX_LINES) {
        uint32_t n = vfs_read(node, off, sizeof(buf)-1, buf);
        if (!n) break;
        for (uint32_t i = 0; i < n && row < EDIT_MAX_LINES; i++) {
            char c = (char)buf[i];
            if (c == '\n') {
                e->lines[row][col] = 0;
                e->line_len[row]   = col;
                row++; col=0;
                if (row >= e->num_lines) e->num_lines = row + 1;
                if (e->num_lines > EDIT_MAX_LINES) e->num_lines = EDIT_MAX_LINES;
            } else if (c != '\r' && col < EDIT_LINE_CAP-1) {
                e->lines[row][col++] = c;
            }
        }
        off += n;
        if (n < sizeof(buf)-1) break;
    }
    /* finish last line */
    e->lines[row][col] = 0;
    e->line_len[row] = col;
    if (row+1 > e->num_lines) e->num_lines = row+1;

    vfs_close(node);
    ecpy(e->filename, path, 256);
    e->has_file = 1;
    e->modified = 0;
    e->cur_col = e->cur_row = e->scroll_y = 0;
}

static void edit_save(edit_app_t *e) {
    vfs_create(e->filename, 0);
    vfs_node_t *node = vfs_open(e->filename, 0);
    if (!node) return;
    uint64_t off = 0;
    for (int i = 0; i < e->num_lines; i++) {
        uint32_t wlen = (uint32_t)e->line_len[i];
        if (wlen) vfs_write(node, off, wlen, (const uint8_t *)e->lines[i]);
        off += wlen;
        vfs_write(node, off, 1, (const uint8_t *)"\n");
        off++;
    }
    vfs_close(node);
    e->modified = 0;
}

/* ── Scroll adjust ───────────────────────────────────────────────────────── */
static void edit_fix_scroll(edit_app_t *e, window_t *w) {
    int vis = visible_lines(w);
    if (e->cur_row < e->scroll_y)             e->scroll_y = e->cur_row;
    if (e->cur_row >= e->scroll_y + vis)      e->scroll_y = e->cur_row - vis + 1;
    if (e->scroll_y < 0) e->scroll_y = 0;
}

/* ── Paint ───────────────────────────────────────────────────────────────── */
static void edit_paint(window_t *win) {
    edit_app_t *e = (edit_app_t *)win->userdata;
    if (!e) return;

    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;
    int vis = visible_lines(win);
    int mc  = max_col(ww);

    /* ── Toolbar ── */
    fb_fill_rect(wx, wy, ww, TOOLBAR_H, COL_SURFACE0);
    fb_fill_rect_blend(wx, wy, ww, 1, 0xFFFFFF, 12);

    /* Mode badge */
    const char *mode_s = (e->prompt != EPROMPT_NONE) ? "PROMPT" : "EDIT";
    uint32_t mode_col  = (e->prompt != EPROMPT_NONE) ? COL_YELLOW : COL_BLUE;
    fb_fill_rounded_rect(wx+6, wy+5, 52, 20, 4, COL_SURFACE1);
    font_puts(wx+8, wy+9, mode_s, mode_col, COL_SURFACE1);

    /* Filename */
    const char *fname = e->has_file ? e->filename : "Untitled";
    if (e->modified) {
        char mfname[260]; int mi=0;
        const char *p=fname; while(*p) mfname[mi++]=*p++;
        mfname[mi++]=' '; mfname[mi++]='*'; mfname[mi]=0;
        font_puts(wx+66, wy+9, mfname, COL_PEACH, COL_SURFACE0);
    } else {
        font_puts(wx+66, wy+9, fname, COL_TEXT, COL_SURFACE0);
    }

    /* Right: row/col */
    char pos[32]; int pi=0;
    const char *rlab="Ln:"; while(*rlab) pos[pi++]=*rlab++;
    char nb[12]; int_to_s(e->cur_row+1, nb); const char *p=nb; while(*p) pos[pi++]=*p++;
    pos[pi++]=' '; const char *clab="Col:"; while(*clab) pos[pi++]=*clab++;
    int_to_s(e->cur_col+1, nb); p=nb; while(*p) pos[pi++]=*p++;
    pos[pi]=0;
    font_puts(wx + ww - pi*8 - 8, wy+9, pos, COL_SUBTEXT, COL_SURFACE0);

    fb_fill_rect(wx, wy+TOOLBAR_H-1, ww, 1, COL_SURFACE1);

    /* ── Line number gutter ── */
    int ct = wy + content_top();
    fb_fill_rect(wx, ct, LNUM_W, content_h(win), COL_MANTLE);
    fb_fill_rect(wx+LNUM_W-1, ct, 1, content_h(win), COL_SURFACE1);

    /* ── Content ── */
    fb_fill_rect(wx+LNUM_W, ct, ww-LNUM_W, content_h(win), COL_BASE);

    for (int i = 0; i < vis && (e->scroll_y + i) < e->num_lines; i++) {
        int row = e->scroll_y + i;
        int ly  = ct + i * LINE_H;

        /* Line number */
        char lnum[8]; int_to_s(row+1, lnum);
        int lnw = elen(lnum) * 8;
        uint32_t lnum_col = (row == e->cur_row) ? COL_SUBTEXT : COL_OVERLAY0;
        font_puts(wx + LNUM_W - 4 - lnw, ly, lnum, lnum_col, COL_MANTLE);

        /* Row highlight for cursor row */
        if (row == e->cur_row)
            fb_fill_rect(wx+LNUM_W, ly, ww-LNUM_W, LINE_H, COL_SURFACE0);

        /* Text content — clipped to visible columns */
        const char *line = e->lines[row];
        int line_len = e->line_len[row];
        if (line_len > mc) line_len = mc;  /* clip display */
        if (line_len > 0)
            font_puts(text_x(wx), ly, line, COL_TEXT, COL_BASE);

        /* Cursor */
        if (row == e->cur_row) {
            int cur_x = text_x(wx) + emin(e->cur_col, mc) * 8;
            fb_fill_rect(cur_x, ly, 2, LINE_H, COL_BLUE);
        }
    }

    /* ── Status / Prompt bar ── */
    int sy = wy + TOOLBAR_H + content_h(win);
    fb_fill_rect(wx, sy, ww, STATUS_H, COL_SURFACE0);
    fb_fill_rect(wx, sy, ww, 1, COL_SURFACE1);

    if (e->prompt == EPROMPT_SAVE || e->prompt == EPROMPT_OPEN) {
        const char *plabel = (e->prompt == EPROMPT_SAVE) ? "Save as: " : "Open file: ";
        font_puts(wx+6, sy+2, plabel, COL_YELLOW, COL_SURFACE0);
        int pw = elen(plabel)*8;
        font_puts(wx+6+pw, sy+2, e->prompt_buf, COL_TEXT, COL_SURFACE0);
        /* Cursor in prompt */
        fb_fill_rect(wx+6+pw+e->prompt_len*8, sy+2, 1, 14, COL_BLUE);
    } else {
        font_puts(wx+6, sy+2,
                  "^S Save  ^O Open  ^N New  ^Q Quit  Arrows Move",
                  COL_OVERLAY0, COL_SURFACE0);
        /* Total lines */
        char tl[20]; int_to_s(e->num_lines, tl);
        int tll=elen(tl); char msg[24]; int mi=0;
        const char *p="Lines:"; while(*p) msg[mi++]=*p++;
        p=tl; while(*p) msg[mi++]=*p++; msg[mi]=0;
        font_puts(wx+ww-mi*8-6, sy+2, msg, COL_OVERLAY0, COL_SURFACE0);
    }
}

/* ── Key handling ────────────────────────────────────────────────────────── */
static void edit_key(window_t *win, char key) {
    edit_app_t *e = (edit_app_t *)win->userdata;
    if (!e) return;

    uint8_t uk = (uint8_t)key;

    /* ── Prompt mode ── */
    if (e->prompt != EPROMPT_NONE) {
        if (key == 27) { /* Esc */
            e->prompt = EPROMPT_NONE;
        } else if (key == '\n' || key == '\r') {
            e->prompt_buf[e->prompt_len] = 0;
            if (e->prompt == EPROMPT_SAVE) {
                ecpy(e->filename, e->prompt_buf, 256);
                e->has_file = 1;
                edit_save(e);
            } else {
                edit_load(e, e->prompt_buf);
            }
            e->prompt = EPROMPT_NONE;
        } else if ((key == '\b' || uk == 127) && e->prompt_len > 0) {
            e->prompt_buf[--e->prompt_len] = 0;
        } else if (key >= 32 && key < 127 && e->prompt_len < 254) {
            e->prompt_buf[e->prompt_len++] = key;
            e->prompt_buf[e->prompt_len]   = 0;
        }
        wm_invalidate(win); return;
    }

    /* ── Arrow keys ── */
    if (uk == (uint8_t)KEY_UP) {
        if (e->cur_row > 0) { e->cur_row--; e->cur_col = emin(e->cur_col, e->line_len[e->cur_row]); }
    } else if (uk == (uint8_t)KEY_DOWN) {
        if (e->cur_row < e->num_lines-1) { e->cur_row++; e->cur_col = emin(e->cur_col, e->line_len[e->cur_row]); }
    } else if (uk == (uint8_t)KEY_LEFT) {
        if (e->cur_col > 0) e->cur_col--;
        else if (e->cur_row > 0) { e->cur_row--; e->cur_col = e->line_len[e->cur_row]; }
    } else if (uk == (uint8_t)KEY_RIGHT) {
        if (e->cur_col < e->line_len[e->cur_row]) e->cur_col++;
        else if (e->cur_row < e->num_lines-1) { e->cur_row++; e->cur_col = 0; }
    } else if (uk == (uint8_t)KEY_HOME) {
        e->cur_col = 0;
    } else if (uk == (uint8_t)KEY_END) {
        e->cur_col = e->line_len[e->cur_row];
    } else if (uk == (uint8_t)KEY_PGUP) {
        e->cur_row = emax(0, e->cur_row - visible_lines(win));
        e->cur_col = emin(e->cur_col, e->line_len[e->cur_row]);
    } else if (uk == (uint8_t)KEY_PGDN) {
        e->cur_row = emin(e->num_lines-1, e->cur_row + visible_lines(win));
        e->cur_col = emin(e->cur_col, e->line_len[e->cur_row]);
    }
    /* ── Ctrl combos ── */
    else if (key == 19) { /* Ctrl+S — save */
        if (e->has_file) {
            edit_save(e);
        } else {
            e->prompt = EPROMPT_SAVE;
            e->prompt_len = 0; e->prompt_buf[0] = 0;
        }
    } else if (key == 15) { /* Ctrl+O — open */
        e->prompt = EPROMPT_OPEN;
        e->prompt_len = 0; e->prompt_buf[0] = 0;
    } else if (key == 14) { /* Ctrl+N — new */
        ezero(e->lines, sizeof(e->lines));
        ezero(e->line_len, sizeof(e->line_len));
        e->num_lines = 1;
        e->cur_col = e->cur_row = e->scroll_y = 0;
        e->has_file = 0; e->modified = 0;
        e->filename[0] = 0;
    }
    /* ── Backspace ── */
    else if (key == '\b' || uk == 127) {
        if (e->cur_col > 0) {
            int row = e->cur_row, col = e->cur_col;
            char *ln = e->lines[row];
            emove(&ln[col-1], &ln[col], e->line_len[row] - col);
            e->line_len[row]--;
            ln[e->line_len[row]] = 0;
            e->cur_col--;
            e->modified = 1;
        } else if (e->cur_row > 0) {
            /* Join with previous line */
            int prev = e->cur_row - 1;
            int plen = e->line_len[prev];
            int clen = e->line_len[e->cur_row];
            if (plen + clen < EDIT_LINE_CAP) {
                emove(&e->lines[prev][plen], e->lines[e->cur_row], clen+1);
                e->line_len[prev] = plen + clen;
                /* Remove current line */
                for (int i = e->cur_row; i < e->num_lines-1; i++) {
                    emove(e->lines[i], e->lines[i+1], EDIT_LINE_CAP);
                    e->line_len[i] = e->line_len[i+1];
                }
                e->num_lines--;
                e->cur_row = prev;
                e->cur_col = plen;
                e->modified = 1;
            }
        }
    } else if (uk == (uint8_t)KEY_DEL) {
        int row = e->cur_row;
        if (e->cur_col < e->line_len[row]) {
            char *ln = e->lines[row];
            emove(&ln[e->cur_col], &ln[e->cur_col+1], e->line_len[row]-e->cur_col);
            e->line_len[row]--;
            ln[e->line_len[row]] = 0;
            e->modified = 1;
        }
    }
    /* ── Enter ── */
    else if (key == '\n' || key == '\r') {
        if (e->num_lines < EDIT_MAX_LINES) {
            int row = e->cur_row, col = e->cur_col;
            /* Shift lines down */
            for (int i = e->num_lines; i > row+1; i--) {
                emove(e->lines[i], e->lines[i-1], EDIT_LINE_CAP);
                e->line_len[i] = e->line_len[i-1];
            }
            /* New line = tail of current */
            int rest = e->line_len[row] - col;
            emove(e->lines[row+1], &e->lines[row][col], rest);
            e->lines[row+1][rest] = 0; e->line_len[row+1] = rest;
            /* Truncate current */
            e->lines[row][col] = 0; e->line_len[row] = col;
            e->num_lines++;
            e->cur_row++; e->cur_col = 0;
            e->modified = 1;
        }
    }
    /* ── Tab ── */
    else if (key == '\t') {
        /* Insert 4 spaces */
        for (int s = 0; s < 4; s++) {
            int row = e->cur_row, col = e->cur_col;
            if (e->line_len[row] < EDIT_LINE_CAP-1) {
                char *ln = e->lines[row];
                emove(&ln[col+1], &ln[col], e->line_len[row]-col+1);
                ln[col] = ' ';
                e->line_len[row]++;
                e->cur_col++;
                e->modified = 1;
            }
        }
    }
    /* ── Printable char ── */
    else if (key >= 32 && key < 127) {
        int row = e->cur_row, col = e->cur_col;
        if (e->line_len[row] < EDIT_LINE_CAP-1) {
            char *ln = e->lines[row];
            emove(&ln[col+1], &ln[col], e->line_len[row]-col+1);
            ln[col] = key;
            e->line_len[row]++;
            e->cur_col++;
            e->modified = 1;
        }
    }

    edit_fix_scroll(e, win);
    wm_invalidate(win);
}

static void edit_close(window_t *win) {
    edit_app_t *e = (edit_app_t *)win->userdata;
    if (e) { kfree(e); win->userdata = NULL; }
    wm_close(win);
}

/* ── Constructor ─────────────────────────────────────────────────────────── */
edit_app_t *edit_create(int x, int y, const char *path) {
    window_t *win = wm_new(x, y, WIN_W, WIN_H, "Text Editor");
    if (!win) return NULL;

    edit_app_t *e = (edit_app_t *)kmalloc(sizeof(edit_app_t));
    if (!e) { wm_close(win); return NULL; }
    ezero(e, sizeof(edit_app_t));

    e->win       = win;
    e->num_lines = 1;
    e->prompt    = EPROMPT_NONE;

    win->on_paint = edit_paint;
    win->on_key   = edit_key;
    win->on_close = edit_close;
    win->userdata = e;

    if (path && path[0]) edit_load(e, path);
    return e;
}
