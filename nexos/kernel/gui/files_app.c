/* NexOS — kernel/gui/files_app.c | GUI File Manager | MIT License */
#include "files_app.h"
#include "wm.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../fs/vfs.h"
#include "../mm/heap.h"
#include "../kernel.h"
#include <stdint.h>
#include <stddef.h>

void *kmalloc(size_t size);
void  kfree(void *ptr);

static int fstrlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void fstrcpy(char *d, const char *s, int max) {
    int i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}

static void format_size(uint64_t sz, char *buf) {
    if (sz >= 1024 * 1024) {
        uint64_t mb = sz / (1024 * 1024);
        char t[8]; int ti = 0;
        if (!mb) { t[ti++] = '0'; } else while (mb) { t[ti++] = '0'+(int)(mb%10); mb/=10; }
        int bi = 0; while (ti > 0) buf[bi++] = t[--ti];
        buf[bi++] = 'M'; buf[bi] = 0;
    } else if (sz >= 1024) {
        uint64_t k = sz / 1024;
        char t[8]; int ti = 0;
        if (!k) { t[ti++] = '0'; } else while (k) { t[ti++] = '0'+(int)(k%10); k/=10; }
        int bi = 0; while (ti > 0) buf[bi++] = t[--ti];
        buf[bi++] = 'K'; buf[bi] = 0;
    } else {
        char t[8]; int ti = 0;
        uint64_t v = sz;
        if (!v) { t[ti++] = '0'; } else while (v) { t[ti++] = '0'+(int)(v%10); v/=10; }
        int bi = 0; while (ti > 0) buf[bi++] = t[--ti];
        buf[bi++] = 'B'; buf[bi] = 0;
    }
}

static void files_load_dir(files_app_t *f) {
    f->entry_count = 0; f->selected = 0; f->scroll = 0;
    vfs_node_t *dir = vfs_open(f->cwd, 0);
    if (!dir) return;
    vfs_dirent_t de;
    int idx = 0;
    while (idx < FILES_MAX_ENT) {
        if (vfs_readdir(dir, (uint32_t)idx, &de) != 0) break;
        fstrcpy(f->entry_names[f->entry_count], de.name, VFS_NAME_MAX);
        char full[256];
        int cl = fstrlen(f->cwd);
        fstrcpy(full, f->cwd, 250);
        if (cl > 0 && f->cwd[cl - 1] != '/') { full[cl] = '/'; full[cl + 1] = 0; }
        int fl = fstrlen(full);
        fstrcpy(full + fl, de.name, 250 - fl);
        vfs_stat_t st;
        if (vfs_stat(full, &st) == 0) {
            f->entry_types[f->entry_count] = st.type;
            f->entry_sizes[f->entry_count] = st.size;
        } else {
            f->entry_types[f->entry_count] = VFS_NODE_FILE;
            f->entry_sizes[f->entry_count] = 0;
        }
        f->entry_count++;
        idx++;
    }
    vfs_close(dir);
}

static void files_paint(window_t *win) {
    files_app_t *f = (files_app_t *)win->userdata;
    if (!f) return;
    int wx = win->x, wy = win->y + WM_TITLEBAR_H;
    int ww = win->w;
    int client_h = win->h - WM_TITLEBAR_H;

    /* ── Toolbar / breadcrumb strip ── */
    fb_fill_rect(wx, wy, ww, 38, COL_SURFACE0);
    fb_fill_rect_blend(wx, wy, ww, 1, 0xFFFFFF, 16);   /* top rim */

    /* "Up" arrow button */
    fb_fill_rounded_rect(wx + 8, wy + 7, 24, 24, 5, COL_SURFACE1);
    font_puts(wx + 14, wy + 12, "^", COL_SUBTEXT, COL_SURFACE1);

    /* Path breadcrumb pill */
    int pb_x = wx + 40, pb_w = ww - 48;
    fb_fill_rounded_rect(pb_x, wy + 7, pb_w, 24, 6, COL_BASE);
    fb_draw_rect_outline(pb_x, wy + 7, pb_w, 24, COL_SURFACE1, 1);
    font_puts(pb_x + 10, wy + 13, f->cwd, COL_SUBTEXT, COL_BASE);

    /* Bottom divider */
    fb_fill_rect(wx, wy + 38, ww, 1, COL_SURFACE1);

    /* ── Status bar at bottom ── */
    int sb_y = wy + client_h - 22;
    fb_fill_rect(wx, sb_y, ww, 22, COL_SURFACE0);
    fb_fill_rect(wx, sb_y, ww, 1, COL_SURFACE1);
    char count_buf[16];
    {
        int n = f->entry_count, ti = 0; char t[8];
        if (!n) { t[ti++] = '0'; } else while (n) { t[ti++] = '0' + n % 10; n /= 10; }
        int bi = 0; while (ti > 0) count_buf[bi++] = t[--ti]; count_buf[bi] = 0;
    }
    font_puts(wx + 10, sb_y + 3, count_buf, COL_SUBTEXT, COL_SURFACE0);
    font_puts(wx + 10 + fstrlen(count_buf) * 8 + 4, sb_y + 3,
              "items", COL_OVERLAY0, COL_SURFACE0);

    /* ── File list ── */
    int list_y = wy + 39;
    int max_y  = sb_y - 1;

    if (f->entry_count == 0) {
        fb_fill_rect(wx, list_y, ww, max_y - list_y, COL_MANTLE);
        font_puts(wx + ww/2 - 32, list_y + 40, "Empty folder",
                  COL_OVERLAY0, COL_MANTLE);
    }

    for (int i = f->scroll; i < f->entry_count && list_y + FILES_ITEM_H <= max_y; i++) {
        int is_dir = (f->entry_types[i] & VFS_NODE_DIR);
        int is_sel = (i == f->selected);

        /* Row background: alternating + selection highlight */
        uint32_t row_bg = is_sel ? COL_SURFACE1
                        : ((i & 1) ? COL_MANTLE
                                   : fb_blend(COL_MANTLE, COL_SURFACE0, 40));
        fb_fill_rect(wx, list_y, ww, FILES_ITEM_H, row_bg);

        /* Selected row: left accent bar */
        if (is_sel)
            fb_fill_rect(wx, list_y, 3, FILES_ITEM_H, COL_BLUE);

        /* File/folder icon — circle with letter */
        uint32_t icon_col = is_dir ? COL_YELLOW : COL_BLUE;
        int ic_x = wx + FILES_PAD + (is_sel ? 4 : 1);
        fb_fill_circle(ic_x + 10, list_y + FILES_ITEM_H/2, 10, icon_col);
        /* Specular on icon */
        fb_fill_circle(ic_x + 7,  list_y + FILES_ITEM_H/2 - 3, 4,
                       fb_blend(0xFFFFFF, icon_col, 120));
        /* Icon letter */
        char ic[2] = { is_dir ? 'D' : 'F', 0 };
        font_puts(ic_x + 6, list_y + FILES_ITEM_H/2 - 7, ic, COL_BASE, icon_col);

        /* Filename */
        uint32_t name_col = is_sel ? COL_TEXT : (is_dir ? COL_TEXT : COL_SUBTEXT);
        font_puts(wx + FILES_PAD + FILES_ICON_W + 10, list_y + 6,
                  f->entry_names[i], name_col, row_bg);

        /* Size (right-aligned, files only) */
        if (!is_dir) {
            char sz[16]; format_size(f->entry_sizes[i], sz);
            int sz_x = wx + ww - fstrlen(sz) * 8 - 12;
            font_puts(sz_x, list_y + 6, sz, COL_OVERLAY0, row_bg);
        } else {
            font_puts(wx + ww - 40, list_y + 6, "dir", COL_OVERLAY0, row_bg);
        }

        /* Bottom hairline separator */
        fb_fill_rect(wx, list_y + FILES_ITEM_H - 1, ww, 1, COL_SURFACE0);

        list_y += FILES_ITEM_H;
    }

    /* Fill remaining space */
    if (list_y < max_y)
        fb_fill_rect(wx, list_y, ww, max_y - list_y, COL_MANTLE);
}

static void files_click(window_t *win, int cx, int cy, int btn) {
    (void)btn;
    files_app_t *f = (files_app_t *)win->userdata;
    if (!f) return;

    /* "Up" button */
    if (cy < 38 && cx >= 8 && cx < 32) {
        int cl = fstrlen(f->cwd);
        if (cl > 1) {
            int i = cl - 1;
            while (i > 0 && f->cwd[i] != '/') i--;
            if (i == 0) f->cwd[1] = 0; else f->cwd[i] = 0;
            files_load_dir(f);
        }
        return;
    }
    if (cy < 39) return;

    int item_y = cy - 39;
    int idx = f->scroll + item_y / FILES_ITEM_H;
    if (idx < 0 || idx >= f->entry_count) return;

    if (f->selected == idx) {
        if (f->entry_types[idx] & VFS_NODE_DIR) {
            int cl = fstrlen(f->cwd);
            char npath[256];
            fstrcpy(npath, f->cwd, 250);
            if (cl > 1) { npath[cl] = '/'; npath[cl + 1] = 0; cl++; }
            fstrcpy(npath + cl, f->entry_names[idx], 250 - cl);
            fstrcpy(f->cwd, npath, 256);
            files_load_dir(f);
        }
    } else {
        f->selected = idx;
    }
    (void)cx;
}

static void files_close(window_t *win) {
    files_app_t *f = (files_app_t *)win->userdata;
    if (f) { kfree(f); win->userdata = NULL; }
    wm_close(win);
}

files_app_t *files_create(int x, int y) {
    window_t *win = wm_new(x, y, 460, 480, "Files");
    if (!win) return NULL;
    files_app_t *f = (files_app_t *)kmalloc(sizeof(files_app_t));
    if (!f) { wm_close(win); return NULL; }
    for (int i = 0; i < (int)sizeof(files_app_t); i++) ((uint8_t *)f)[i] = 0;

    f->win = win;
    f->cwd[0] = '/'; f->cwd[1] = 0;
    files_load_dir(f);

    win->on_paint = files_paint;
    win->on_click = files_click;
    win->on_close = files_close;
    win->userdata = f;
    return f;
}
