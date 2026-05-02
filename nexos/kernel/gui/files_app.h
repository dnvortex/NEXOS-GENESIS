/* NexOS — kernel/gui/files_app.h | GUI File Manager | MIT License */
#pragma once
#include "wm.h"
#include "../fs/vfs.h"

#define FILES_ITEM_H  28
#define FILES_PAD     12
#define FILES_ICON_W  24
#define FILES_MAX_ENT 64

typedef struct {
    window_t   *win;
    char        cwd[256];
    char        entry_names[FILES_MAX_ENT][VFS_NAME_MAX];
    uint32_t    entry_types[FILES_MAX_ENT];
    uint64_t    entry_sizes[FILES_MAX_ENT];
    int         entry_count;
    int         selected;
    int         scroll;
} files_app_t;

files_app_t *files_create(int x, int y);
