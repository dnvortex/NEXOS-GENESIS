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
    if (sz < 1024) {
        char t[8]; int ti = 0;
        uint64_t v = sz;
        if (v == 0) { t[ti++] = '0'; } else while (v) { t[ti++] = '0' + (int)(v % 10); v /= 10; }
        int bi = 0; while (ti > 0) buf[bi++] = t[--ti];
        buf[bi++] = 'B'; buf[bi] = 0;
    } else {
        uint64_t k = sz / 1024;
        char t[8]; int ti = 0;
        if (k == 0) { t[ti++] = '0'; } else while (k) { t[ti++] = '0' + (int)(k % 10); k /= 10; }
        int bi = 0; while (ti > 0) buf[bi++] = t[--ti];
        buf[bi++] = 'K'; buf[bi] = 0;
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
        /* stat to get type and size */
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

    /* breadcrumb */
    fb_fill_rect(wx, wy, ww, 36, COL_SURFACE0);
    font_puts(wx + FILES_PAD, wy + 10, f->cwd, COL_SUBTEXT, COL_SURFACE0);
    fb_fill_rect(wx, wy + 36, ww, 1, COL_SURFACE1);

    int list_y = wy + 37;
    int max_y  = win->y + win->h;
    for (int i = f->scroll; i < f->entry_count && list_y + FILES_ITEM_H <= max_y; i++) {
        uint32_t bg = (i == f->selected) ? COL_SURFACE1 : COL_MANTLE;
        fb_fill_rect(wx, list_y, ww, FILES_ITEM_H, bg);

        uint32_t icon_c = (f->entry_types[i] & VFS_NODE_DIR) ? COL_BLUE : COL_SUBTEXT;
        fb_fill_rounded_rect(wx + FILES_PAD, list_y + 4, 20, 20, 3, icon_c);
        char type_l[2] = { (f->entry_types[i] & VFS_NODE_DIR) ? 'D' : 'F', 0 };
        font_puts(wx + FILES_PAD + 6, list_y + 6, type_l, COL_BASE, icon_c);

        font_puts(wx + FILES_PAD + FILES_ICON_W + 8, list_y + 6,
                  f->entry_names[i], COL_TEXT, bg);

        if (!(f->entry_types[i] & VFS_NODE_DIR)) {
            char sz[16]; format_size(f->entry_sizes[i], sz);
            font_puts(wx + ww - 64, list_y + 6, sz, COL_SUBTEXT, bg);
        }
        list_y += FILES_ITEM_H;
    }
    /* fill remaining */
    if (list_y < max_y)
        fb_fill_rect(wx, list_y, ww, max_y - list_y, COL_MANTLE);
}

static void files_click(window_t *win, int cx, int cy, int btn) {
    (void)btn;
    files_app_t *f = (files_app_t *)win->userdata;
    if (!f) return;
    /* breadcrumb area — go up */
    if (cy < 36) {
        int cl = fstrlen(f->cwd);
        if (cl > 1) {
            /* pop last component */
            int i = cl - 1;
            while (i > 0 && f->cwd[i] != '/') i--;
            if (i == 0) { f->cwd[1] = 0; }
            else         { f->cwd[i] = 0; }
            files_load_dir(f);
        }
        return;
    }
    int item_y = cy - 37;
    int idx = f->scroll + item_y / FILES_ITEM_H;
    if (idx < 0 || idx >= f->entry_count) return;
    if (f->selected == idx) {
        /* double-click: enter dir */
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
