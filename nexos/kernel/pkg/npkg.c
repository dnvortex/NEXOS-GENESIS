/* NexOS — kernel/pkg/npkg.c
 * NexOS Package Manager — install, remove, list .npkg packages.
 * MIT License */

#include "npkg.h"
#include "pkg_store.h"
#include "../fs/vfs.h"
#include "../mm/heap.h"
#include "../drivers/serial.h"
#include <stdint.h>

/* ── Limits ─────────────────────────────────────────────────────────────── */
#define NPKG_MAX_FILE_DATA   (128 * 1024)  /* 128 KB per file in package   */
#define NPKG_DB_LINE_MAX     320           /* max bytes per DB text line   */
#define NPKG_FILELIST_MAX    8192          /* max file list bytes per pkg  */

/* ── In-memory database ─────────────────────────────────────────────────── */
static npkg_installed_t db[NPKG_MAX_INSTALLED];
static int              db_count      = 0;
static int              db_dirty      = 0;
static int              npkg_inited   = 0;

/* ── String helpers (no stdlib) ─────────────────────────────────────────── */
static int npkg_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static void npkg_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static void npkg_strncpy(char *d, const char *s, int n) {
    int i = 0; while (i < n && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}
static int npkg_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void npkg_strcat(char *d, const char *s, int max) {
    int n = npkg_strlen(d);
    int i = 0;
    while (n + i < max - 1 && s[i]) { d[n + i] = s[i]; i++; }
    d[n + i] = 0;
}
static void npkg_itoa(int v, char *buf) {
    if (v == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[12]; int i = 0;
    if (v < 0) { buf[0]='-'; buf++; v = -v; }
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int j = 0; while (i > 0) buf[j++] = tmp[--i]; buf[j] = 0;
}
static void npkg_append_int(char *buf, int buf_size, int v) {
    char tmp[16]; npkg_itoa(v, tmp);
    npkg_strcat(buf, tmp, buf_size);
}
/* Return pointer past '\n' or to '\0' */
static const char *npkg_next_line(const char *p) {
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
    return p;
}
/* Copy up to '\n' or '\0' into out, return pointer to next line */
static const char *npkg_read_line(const char *p, char *out, int max) {
    int i = 0;
    while (*p && *p != '\n' && i < max - 1) out[i++] = *p++;
    out[i] = 0;
    if (*p == '\n') p++;
    return p;
}

/* ── VFS helpers ────────────────────────────────────────────────────────── */
static int npkg_vfs_exists(const char *path) {
    vfs_stat_t st;
    return vfs_stat(path, &st) == 0;
}

/* Extract parent directory from path into dir[] */
static void npkg_dirname(const char *path, char *dir, int max) {
    int last = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last = i;
    if (last <= 0) { dir[0] = '/'; dir[1] = 0; }
    else { npkg_strncpy(dir, path, last < max - 1 ? last : max - 2); dir[last] = 0; }
}

/* Create all components of path, ignoring errors on existing dirs */
static int npkg_mkdir_p(const char *path) {
    char tmp[512];
    int  len = npkg_strlen(path);
    for (int i = 1; i <= len; i++) {
        if (path[i] == '/' || path[i] == 0) {
            int n = i < 511 ? i : 511;
            npkg_strncpy(tmp, path, n);
            tmp[n] = 0;
            if (!npkg_vfs_exists(tmp))
                vfs_mkdir(tmp);
        }
    }
    return 0;
}

/* Write data to VFS file (creating it and its parent dirs) */
static int npkg_write_vfs_file(const char *path,
                                const uint8_t *data, uint32_t size) {
    /* Make parent directories */
    char parent[512];
    npkg_dirname(path, parent, 512);
    if (parent[0] == '/' && parent[1])   /* skip "/" itself */
        npkg_mkdir_p(parent);

    /* Create file if absent */
    if (!npkg_vfs_exists(path))
        vfs_create(path, 0);

    /* Write */
    vfs_node_t *n = vfs_open(path, 0);
    if (!n) return -1;
    uint32_t w = vfs_write(n, 0, size, data);
    vfs_close(n);
    return (w == size) ? 0 : -1;
}

/* Read entire file (up to max bytes) into heap-allocated buffer.
   Caller must kfree().  Returns NULL on error.  Sets *out_size. */
static uint8_t *npkg_read_vfs_file(const char *path, uint32_t *out_size) {
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0) return (void*)0;
    uint32_t   sz  = (st.size < NPKG_MAX_FILE_DATA) ? st.size : NPKG_MAX_FILE_DATA;
    uint8_t   *buf = (uint8_t*)kmalloc(sz + 1);
    if (!buf) return (void*)0;
    vfs_node_t *n  = vfs_open(path, 0);
    if (!n) { kfree(buf); return (void*)0; }
    uint32_t got = vfs_read(n, 0, sz, buf);
    vfs_close(n);
    buf[got] = 0;
    if (out_size) *out_size = got;
    return buf;
}

/* ── Database ────────────────────────────────────────────────────────────── */
static int npkg_db_find(const char *name) {
    for (int i = 0; i < db_count; i++)
        if (!npkg_strcmp(db[i].name, name)) return i;
    return -1;
}

static void npkg_db_add(const npkg_header_t *h) {
    if (db_count >= NPKG_MAX_INSTALLED) return;
    npkg_installed_t *e = &db[db_count++];
    npkg_strcpy(e->name,        h->name,        NPKG_NAME_MAX);
    npkg_strcpy(e->version,     h->version,     NPKG_VER_MAX);
    npkg_strcpy(e->description, h->description, NPKG_DESC_MAX);
    npkg_strcpy(e->author,      h->author,      NPKG_AUTH_MAX);
    e->file_count = (int)h->file_count;
    db_dirty = 1;
}

static void npkg_db_del(int idx) {
    for (int i = idx; i < db_count - 1; i++) db[i] = db[i + 1];
    db_count--;
    db_dirty = 1;
}

/* Persist DB to /var/lib/npkg/db (text, one package per line) */
static void npkg_db_save(void) {
    if (!db_dirty) return;
    /* Build text */
    uint32_t  cap = (uint32_t)db_count * NPKG_DB_LINE_MAX + 4;
    char     *buf = (char*)kmalloc(cap);
    if (!buf) return;
    int used = 0;
    for (int i = 0; i < db_count; i++) {
        npkg_installed_t *e = &db[i];
        /* line: "PKG <name>|<version>|<files>|<desc>\n" */
        if (used + NPKG_DB_LINE_MAX >= (int)cap) break;
        char *p = buf + used;
        npkg_strcpy(p, "PKG ", cap - used);
        npkg_strcat(p, e->name,        cap - used);
        npkg_strcat(p, "|",            cap - used);
        npkg_strcat(p, e->version,     cap - used);
        npkg_strcat(p, "|",            cap - used);
        char fc[8]; npkg_itoa(e->file_count, fc);
        npkg_strcat(p, fc,             cap - used);
        npkg_strcat(p, "|",            cap - used);
        npkg_strcat(p, e->description, cap - used);
        npkg_strcat(p, "\n",           cap - used);
        used += npkg_strlen(p);
    }
    npkg_write_vfs_file("/var/lib/npkg/db", (uint8_t*)buf, (uint32_t)used);
    kfree(buf);
    db_dirty = 0;
}

/* Load DB from /var/lib/npkg/db (best-effort, ignore parse errors) */
static void npkg_db_load(void) {
    uint32_t sz;
    uint8_t *raw = npkg_read_vfs_file("/var/lib/npkg/db", &sz);
    if (!raw) return;
    const char *p = (const char*)raw;
    char line[NPKG_DB_LINE_MAX];
    while (*p) {
        p = npkg_read_line(p, line, NPKG_DB_LINE_MAX);
        if (line[0] != 'P' || line[1] != 'K' || line[2] != 'G') continue;
        /* parse "PKG name|ver|fc|desc" */
        char *tok = line + 4;
        char  name[NPKG_NAME_MAX], ver[NPKG_VER_MAX];
        char  fc_s[8], desc[NPKG_DESC_MAX];
        /* name */
        int i = 0;
        while (tok[i] && tok[i] != '|' && i < NPKG_NAME_MAX-1) i++;
        npkg_strncpy(name, tok, i); tok += i + 1;
        /* version */
        i = 0;
        while (tok[i] && tok[i] != '|' && i < NPKG_VER_MAX-1) i++;
        npkg_strncpy(ver, tok, i); tok += i + 1;
        /* file count */
        i = 0;
        while (tok[i] && tok[i] != '|' && i < 7) i++;
        npkg_strncpy(fc_s, tok, i); tok += i + 1;
        /* desc */
        npkg_strcpy(desc, tok, NPKG_DESC_MAX);

        if (!name[0] || db_count >= NPKG_MAX_INSTALLED) continue;
        npkg_installed_t *e = &db[db_count++];
        npkg_strcpy(e->name,        name, NPKG_NAME_MAX);
        npkg_strcpy(e->version,     ver,  NPKG_VER_MAX);
        npkg_strcpy(e->description, desc, NPKG_DESC_MAX);
        e->author[0] = 0;
        e->file_count = 0;
        for (int k = 0; fc_s[k] >= '0' && fc_s[k] <= '9'; k++)
            e->file_count = e->file_count * 10 + (fc_s[k] - '0');
    }
    kfree(raw);
}

/* ── File list helpers (for removal) ────────────────────────────────────── */
static void npkg_filelist_path(const char *name, char *out, int max) {
    npkg_strcpy(out, "/var/lib/npkg/filelists/", max);
    npkg_strcat(out, name, max);
}

/* ── Public API ──────────────────────────────────────────────────────────── */
int npkg_init(void) {
    if (npkg_inited) return NPKG_OK;
    /* Create directory skeleton */
    npkg_mkdir_p("/var/lib/npkg/filelists");
    npkg_mkdir_p("/packages");
    npkg_mkdir_p("/usr/bin");
    npkg_mkdir_p("/usr/share");
    npkg_mkdir_p("/etc");
    /* Load persisted DB (empty on first boot — DB lives in RAM only) */
    npkg_db_load();
    npkg_inited = 1;
    serial_puts("[npkg] Package manager initialized\n");
    return NPKG_OK;
}

int npkg_install(const char *path) {
    if (!npkg_inited) npkg_init();

    /* ── 1. Open package file ── */
    vfs_node_t *n = vfs_open(path, 0);
    if (!n) return NPKG_ERR_NOTFOUND;

    /* ── 2. Read and validate header ── */
    npkg_header_t hdr;
    uint32_t got = vfs_read(n, 0, sizeof(hdr), (uint8_t*)&hdr);
    if (got < sizeof(hdr) || hdr.magic != NPKG_MAGIC ||
        hdr.format_version != NPKG_FORMAT_VER) {
        vfs_close(n);
        return NPKG_ERR_BADMAGIC;
    }
    if (hdr.file_count > NPKG_MAX_FILES) {
        vfs_close(n);
        return NPKG_ERR_BADPKG;
    }

    /* ── 3. Check not already installed ── */
    if (npkg_db_find(hdr.name) >= 0) {
        vfs_close(n);
        return NPKG_ERR_EXISTS;
    }

    /* ── 4. Install all files ── */
    uint64_t  offset   = sizeof(hdr);
    char     *filelist = (char*)kmalloc(NPKG_FILELIST_MAX);
    int       fl_used  = 0;
    if (!filelist) { vfs_close(n); return NPKG_ERR_NOMEM; }
    filelist[0] = 0;

    for (uint32_t i = 0; i < hdr.file_count; i++) {
        /* Read file entry header */
        npkg_file_entry_t entry;
        got = vfs_read(n, offset, sizeof(entry), (uint8_t*)&entry);
        offset += sizeof(entry);
        if (got < sizeof(entry)) break;

        /* Clamp size */
        if (entry.size > NPKG_MAX_FILE_DATA) entry.size = NPKG_MAX_FILE_DATA;

        /* Read file data */
        uint8_t *data = (uint8_t*)kmalloc(entry.size + 1);
        if (!data) { offset += entry.size; continue; }
        got  = vfs_read(n, offset, entry.size, data);
        offset += entry.size;
        data[got] = 0;

        /* Write to VFS */
        npkg_write_vfs_file(entry.path, data, got);
        kfree(data);

        /* Track in file list */
        int pl = npkg_strlen(entry.path);
        if (fl_used + pl + 2 < NPKG_FILELIST_MAX) {
            npkg_strcpy(filelist + fl_used, entry.path,
                        NPKG_FILELIST_MAX - fl_used);
            fl_used += pl;
            filelist[fl_used++] = '\n';
        }
    }
    vfs_close(n);

    /* ── 5. Save file list for removal ── */
    char fl_path[160];
    npkg_filelist_path(hdr.name, fl_path, 160);
    npkg_write_vfs_file(fl_path, (uint8_t*)filelist, (uint32_t)fl_used);
    kfree(filelist);

    /* ── 6. Record in DB ── */
    npkg_db_add(&hdr);
    npkg_db_save();

    serial_puts("[npkg] Installed: ");
    serial_puts(hdr.name);
    serial_puts(" ");
    serial_puts(hdr.version);
    serial_puts("\n");
    return NPKG_OK;
}

int npkg_remove(const char *name) {
    if (!npkg_inited) npkg_init();

    int idx = npkg_db_find(name);
    if (idx < 0) return NPKG_ERR_NOTFOUND;

    /* Delete installed files */
    char fl_path[160];
    npkg_filelist_path(name, fl_path, 160);
    uint32_t  sz;
    uint8_t  *raw = npkg_read_vfs_file(fl_path, &sz);
    if (raw) {
        const char *p = (const char*)raw;
        char line[512];
        while (*p) {
            p = npkg_read_line(p, line, 512);
            if (line[0]) vfs_unlink(line);
        }
        kfree(raw);
        vfs_unlink(fl_path);
    }

    npkg_db_del(idx);
    npkg_db_save();
    return NPKG_OK;
}

int npkg_list(char *buf, int buf_size) {
    if (!npkg_inited) npkg_init();
    if (db_count == 0) {
        npkg_strcpy(buf, "(no packages installed)\n", buf_size);
        return 0;
    }
    buf[0] = 0;
    for (int i = 0; i < db_count; i++) {
        npkg_strcat(buf, db[i].name,    buf_size);
        npkg_strcat(buf, " ",           buf_size);
        npkg_strcat(buf, db[i].version, buf_size);
        npkg_strcat(buf, "\n",          buf_size);
    }
    return db_count;
}

int npkg_info(const char *name, char *buf, int buf_size) {
    if (!npkg_inited) npkg_init();
    int idx = npkg_db_find(name);
    if (idx < 0) return NPKG_ERR_NOTFOUND;
    npkg_installed_t *e = &db[idx];
    buf[0] = 0;
    npkg_strcat(buf, "Name:        ", buf_size); npkg_strcat(buf, e->name,        buf_size); npkg_strcat(buf, "\n", buf_size);
    npkg_strcat(buf, "Version:     ", buf_size); npkg_strcat(buf, e->version,     buf_size); npkg_strcat(buf, "\n", buf_size);
    npkg_strcat(buf, "Description: ", buf_size); npkg_strcat(buf, e->description, buf_size); npkg_strcat(buf, "\n", buf_size);
    npkg_strcat(buf, "Author:      ", buf_size); npkg_strcat(buf, e->author,      buf_size); npkg_strcat(buf, "\n", buf_size);
    npkg_strcat(buf, "Files:       ", buf_size);
    npkg_append_int(buf, buf_size, e->file_count);
    npkg_strcat(buf, "\n", buf_size);
    return NPKG_OK;
}

int npkg_is_installed(const char *name) {
    return npkg_db_find(name) >= 0 ? 1 : 0;
}

/* Install all packages embedded in the compile-time store */
void npkg_init_store(void) {
    if (!npkg_inited) npkg_init();
    for (int i = 0; npkg_store[i].name != (void*)0; i++) {
        /* Write embedded bytes to /packages/<name> */
        char dst[128] = "/packages/";
        npkg_strcat(dst, npkg_store[i].name, 128);
        npkg_write_vfs_file(dst, npkg_store[i].data, npkg_store[i].size);

        /* Auto-install (skip if already installed) */
        npkg_install(dst);
    }
}
