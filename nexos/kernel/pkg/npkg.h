/* NexOS — kernel/pkg/npkg.h
 * NexOS Package Manager (npkg) — binary .npkg format + installer API
 * MIT License */
#pragma once
#include <stdint.h>
#include "pkg_store.h"

/* ── Binary package format (.npkg) ──────────────────────────────────────────
 *
 *  [Header   — 320 bytes, fixed]
 *  [File 0   — 260-byte entry + raw data]
 *  [File 1   — 260-byte entry + raw data]
 *  ...
 *  [Install script — npkg_header_t.install_script_size bytes, may be 0]
 *
 * An npkg_file_entry_t immediately precedes the raw file bytes on disk;
 * entries are NOT padded — they are densely packed after the header.
 * ──────────────────────────────────────────────────────────────────────── */

#define NPKG_MAGIC        0x4B50584E  /* "NXPK" little-endian */
#define NPKG_FORMAT_VER   1
#define NPKG_MAX_INSTALLED 64
#define NPKG_MAX_FILES    256         /* per package */
#define NPKG_NAME_MAX     64
#define NPKG_VER_MAX      32
#define NPKG_DESC_MAX     128
#define NPKG_AUTH_MAX     64
#define NPKG_PATH_MAX     256

/* On-disk package header (320 bytes) */
typedef struct {
    uint32_t magic;                        /* NPKG_MAGIC           */
    uint16_t format_version;               /* NPKG_FORMAT_VER      */
    uint16_t flags;                        /* reserved, set 0      */
    char     name[NPKG_NAME_MAX];          /* package name         */
    char     version[NPKG_VER_MAX];        /* semantic version     */
    char     description[NPKG_DESC_MAX];   /* short description    */
    char     author[NPKG_AUTH_MAX];        /* author / maintainer  */
    uint32_t file_count;                   /* number of files      */
    uint32_t install_script_size;          /* bytes; 0 = none      */
    uint8_t  reserved[16];
} __attribute__((packed)) npkg_header_t;

/* On-disk file entry (260 bytes) followed immediately by file data */
typedef struct {
    char     path[NPKG_PATH_MAX];   /* absolute target path in VFS */
    uint32_t size;                  /* byte count of raw data      */
} __attribute__((packed)) npkg_file_entry_t;

/* In-memory record of an installed package */
typedef struct {
    char name[NPKG_NAME_MAX];
    char version[NPKG_VER_MAX];
    char description[NPKG_DESC_MAX];
    char author[NPKG_AUTH_MAX];
    int  file_count;
} npkg_installed_t;

/* ── Error codes ─────────────────────────────────────────────────────────── */
#define NPKG_OK           0
#define NPKG_ERR_NOTFOUND  1
#define NPKG_ERR_BADMAGIC  2
#define NPKG_ERR_NOMEM     3
#define NPKG_ERR_VFS       4
#define NPKG_ERR_EXISTS    5
#define NPKG_ERR_BADPKG    6

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Call once after VFS is ready (sets up /var/lib/npkg/ etc.) */
int  npkg_init(void);

/* Install a .npkg file from the VFS.  Returns NPKG_* error code. */
int  npkg_install(const char *vfs_path);

/* Remove a previously-installed package by name. */
int  npkg_remove(const char *name);

/* Write a human-readable list of installed packages into buf. */
int  npkg_list(char *buf, int buf_size);

/* Write detailed info about a single package into buf. */
int  npkg_info(const char *name, char *buf, int buf_size);

/* Return 1 if the package is installed, 0 otherwise. */
int  npkg_is_installed(const char *name);

/* Install all packages embedded in pkg_store[] (called at boot). */
void npkg_init_store(void);
