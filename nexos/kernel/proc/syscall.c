/* NexOS — kernel/proc/syscall.c
 * POSIX-compatible syscall dispatcher (Linux x86_64 ABI)
 * INT 0x80:  rax=syscall#, rdi/rsi/rdx/r10/r8/r9 = args 1-6
 * Returns:   result in rax (negative = -errno on error)
 * MIT License */

#include "syscall.h"
#include "process.h"
#include "../kernel.h"
#include "../arch/x86_64/idt.h"
#include "../fs/vfs.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../drivers/rtc.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

/* ── Linux errno values ──────────────────────────────────────────────────── */
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define EEXIST   17
#define ENOTDIR  20
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define ENOTTY   25
#define EBADF    9
#define ECHILD   10
#define EAGAIN   11
#define ENOMEM   12
#define EFAULT   14
#define ENOTCONN 107
#define ERANGE   34
#define ENOSYS   38
#define EPIPE    32
#define ESPIPE   29
#define EROFS    30
#define EDEADLK  35
#define ENOLCK   37
#define ENOTEMPTY 39
#define ELOOP    40
#define ENOMSG   42
#define EIDRM    43
#define ENODATA  61
#define ENOSTR   60
#define ENOSR    63
#define EMLINK   31
#define EDOM     33
#define EOVERFLOW 75
#define ETIMEDOUT    110
#define ECONNRESET   104
#define ECONNREFUSED 111
#define EHOSTUNREACH 113
#define ENETUNREACH  101
#define EADDRINUSE   98
#define EADDRNOTAVAIL 99
#define EISCONN      106
#define ESHUTDOWN    108
#define EMSGSIZE     90
#define EPROTONOSUPPORT 93
#define EAFNOSUPPORT    97
#define EALREADY        114
#define ENAMETOOLONG    36

/* shorthand: return negative errno as uint64_t */
#define RET_ERR(e) return (uint64_t)(-(int64_t)(e))

/* ── Linux ABI structs ───────────────────────────────────────────────────── */

/* struct stat (144 bytes, matches Linux x86_64 ABI exactly) */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime_sec;
    uint64_t st_atime_nsec;
    int64_t  st_mtime_sec;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime_sec;
    uint64_t st_ctime_nsec;
    int64_t  __unused[3];
} __attribute__((packed)) linux_stat_t;

/* struct utsname (6 × 65 bytes) */
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} linux_utsname_t;

/* struct linux_dirent (getdents, syscall 78) — x86_64: unsigned long = 8 bytes */
typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    char     d_name[1]; /* variable; d_type byte lives at d_name[d_reclen-19] */
} __attribute__((packed)) linux_dirent_t;

/* struct linux_dirent64 (getdents64, syscall 217) */
typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[1]; /* variable */
} __attribute__((packed)) linux_dirent64_t;

/* struct timeval */
typedef struct {
    int64_t tv_sec;
    int64_t tv_usec;
} linux_timeval_t;

/* struct timespec */
typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} linux_timespec_t;

/* struct timezone (ignored in gettimeofday) */
typedef struct { int32_t tz_minuteswest; int32_t tz_dsttime; } linux_tz_t;

/* ioctl: TIOCGWINSZ window size */
typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } winsize_t;

/* ── Internal helpers ────────────────────────────────────────────────────── */
static void sc_memzero(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p; for (size_t i = 0; i < n; i++) b[i] = 0;
}
static void sc_strcpy(char *d, const char *s, size_t max) {
    size_t i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static size_t sc_strlen(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}

/* ── at_resolve: resolve (dirfd, path) → absolute path ──────────────────── *
 * Handles AT_FDCWD (-100) and absolute paths.  For relative paths with a    *
 * real dirfd we fall back to CWD (VFS nodes do not store full paths).       */
#define AT_FDCWD_VALUE (-100)
static int at_resolve(process_t *proc, int dirfd, const char *path,
                      char *out, size_t outsz) {
    if (!path) return -1;
    /* Absolute path: ignore dirfd entirely */
    if (path[0] == '/') { sc_strcpy(out, path, outsz); return 0; }
    /* Pick base directory string */
    const char *base;
    if (dirfd == AT_FDCWD_VALUE) {
        base = (proc && proc->cwd[0]) ? proc->cwd : "/";
    } else if (proc && dirfd >= 0 && dirfd < MAX_FDS && proc->fds[dirfd]) {
        base = (proc->cwd[0]) ? proc->cwd : "/"; /* node has no stored path */
    } else {
        return -1;
    }
    /* Join: base + "/" + path */
    size_t blen = sc_strlen(base);
    size_t plen = sc_strlen(path);
    if (blen + 1 + plen + 1 > outsz) return -1;
    for (size_t i = 0; i < blen; i++) out[i] = base[i];
    if (blen > 0 && out[blen - 1] != '/') { out[blen] = '/'; blen++; }
    for (size_t i = 0; i <= plen; i++) out[blen + i] = path[i];
    return 0;
}

/* Map VFS node type to Linux st_mode file-type bits */
static uint32_t vfs_type_to_mode(uint32_t vtype) {
    if (vtype & VFS_NODE_DIR)     return 0x4000 | 0x1ED; /* dir  0755 */
    if (vtype & VFS_NODE_CHARDEV) return 0x2000 | 0x1B6; /* chr  0666 */
    if (vtype & VFS_NODE_BLKDEV)  return 0x6000 | 0x1B6; /* blk  0666 */
    if (vtype & VFS_NODE_PIPE)    return 0x1000 | 0x1B6; /* fifo 0666 */
    if (vtype & VFS_NODE_SYMLINK) return 0xA000 | 0x1FF; /* lnk  0777 */
    return 0x8000 | 0x1A4;                                /* reg  0644 */
}

/* Map VFS node type to Linux d_type */
static uint8_t vfs_type_to_dtype(uint32_t vtype) {
    if (vtype & VFS_NODE_DIR)     return 4;  /* DT_DIR  */
    if (vtype & VFS_NODE_CHARDEV) return 2;  /* DT_CHR  */
    if (vtype & VFS_NODE_BLKDEV)  return 6;  /* DT_BLK  */
    if (vtype & VFS_NODE_PIPE)    return 1;  /* DT_FIFO */
    if (vtype & VFS_NODE_SYMLINK) return 10; /* DT_LNK  */
    return 8;                                /* DT_REG  */
}

/* Fill a linux_stat_t from a vfs_node_t (no extra VFS call needed) */
static void fill_stat_from_node(linux_stat_t *ls, const vfs_node_t *node) {
    sc_memzero(ls, sizeof(*ls));
    ls->st_dev      = 1;
    ls->st_ino      = node->inode ? node->inode : 1;
    ls->st_nlink    = 1;
    ls->st_mode     = vfs_type_to_mode(node->type);
    ls->st_size     = (int64_t)node->size;
    ls->st_blksize  = 512;
    ls->st_blocks   = (int64_t)((node->size + 511) / 512);
}

/* Fill a linux_stat_t from a vfs_stat_t path lookup */
static void fill_stat_from_vstat(linux_stat_t *ls, const vfs_stat_t *vs) {
    sc_memzero(ls, sizeof(*ls));
    ls->st_dev     = 1;
    ls->st_ino     = vs->inode;
    ls->st_nlink   = vs->nlinks ? vs->nlinks : 1;
    ls->st_mode    = vfs_type_to_mode(vs->type);
    ls->st_size    = (int64_t)vs->size;
    ls->st_blksize = 512;
    ls->st_blocks  = (int64_t)((vs->size + 511) / 512);
}

/* ── Pipe implementation ─────────────────────────────────────────────────── */
#define PIPE_BUF_SIZE 4096

typedef struct {
    uint8_t  buf[PIPE_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t  write_closed;
    uint8_t  read_closed;
} pipe_buf_t;

static uint32_t pipe_read_fn(vfs_node_t *node, uint64_t off,
                              uint32_t len, uint8_t *buf) {
    (void)off;
    pipe_buf_t *p = (pipe_buf_t *)node->priv;
    if (!p) return 0;
    uint32_t n = 0;
    while (n < len && p->count > 0) {
        buf[n++] = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_BUF_SIZE;
        p->count--;
    }
    return n;
}

static uint32_t pipe_write_fn(vfs_node_t *node, uint64_t off,
                               uint32_t len, const uint8_t *buf) {
    (void)off;
    pipe_buf_t *p = (pipe_buf_t *)node->priv;
    if (!p) return 0;
    uint32_t n = 0;
    while (n < len && p->count < PIPE_BUF_SIZE) {
        p->buf[p->tail] = buf[n++];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
        p->count++;
    }
    return n;
}

/* ── Brk / mmap virtual-address pool ────────────────────────────────────── */
/* Lazy-init the process break at a safe region above 512 MB             */
#define PROC_BRK_BASE    0x20000000ULL   /* 512 MB — user heap starts here */
#define PROC_MMAP_BASE   0x40000000ULL   /* 1 GB  — mmap region starts here */
#ifndef PAGE_SIZE
#define PAGE_SIZE        0x1000ULL
#endif

static uint64_t proc_get_brk(process_t *proc) {
    if (proc->brk == 0) proc->brk = PROC_BRK_BASE;
    return proc->brk;
}

static uint64_t proc_get_mmap_base(process_t *proc) {
    if (proc->mmap_base == 0) proc->mmap_base = PROC_MMAP_BASE;
    return proc->mmap_base;
}

/* Map [virt, virt+len) backed by fresh PMM pages; returns 0 on success */
static int map_anon_pages(uint64_t virt, uint64_t len) {
    uint64_t cur = virt & ~(PAGE_SIZE - 1);
    uint64_t end = (virt + len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    while (cur < end) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return -1;
        vmm_map(cur, phys, VMM_FLAG_WRITE | VMM_FLAG_USER);
        cur += PAGE_SIZE;
    }
    return 0;
}

/* ── Main dispatcher ─────────────────────────────────────────────────────── */
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5) {
    process_t *proc = proc_get_current();

    switch ((int)num) {

    /* ── 0: read ─────────────────────────────────────────────────────────── */
    case SYS_READ: {
        int      fd  = (int)a1;
        uint8_t *buf = (uint8_t *)(uintptr_t)a2;
        uint32_t len = (uint32_t)a3;
        klog(LOG_DEBUG, "sys_read(fd=%d, len=%u)", fd, len);
        if (!buf || len == 0)          RET_ERR(EINVAL);
        if (fd == 0) {
            /* stdin — block-read from keyboard until newline */
            uint32_t i = 0;
            while (i < len) {
                char ch = keyboard_getchar();
                buf[i++] = (uint8_t)ch;
                if (ch == '\n') break;
            }
            return i;
        }
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[fd];
        if (!node) RET_ERR(EBADF);
        uint32_t n = vfs_read(node, proc->fd_offsets[fd], len, buf);
        proc->fd_offsets[fd] += n;
        return n;
    }

    /* ── 1: write ────────────────────────────────────────────────────────── */
    case SYS_WRITE: {
        int            fd  = (int)a1;
        const uint8_t *buf = (const uint8_t *)(uintptr_t)a2;
        uint32_t       len = (uint32_t)a3;
        klog(LOG_DEBUG, "sys_write(fd=%d, len=%u)", fd, len);
        if (!buf) RET_ERR(EFAULT);
        if (fd == 1 || fd == 2) {
            for (uint32_t i = 0; i < len; i++) vga_putchar((char)buf[i]);
            return len;
        }
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[fd];
        if (!node) RET_ERR(EBADF);
        uint32_t n = vfs_write(node, proc->fd_offsets[fd], len, buf);
        proc->fd_offsets[fd] += n;
        return n;
    }

    /* ── 2: open ─────────────────────────────────────────────────────────── */
    case SYS_OPEN: {
        const char *path  = (const char *)(uintptr_t)a1;
        int         flags = (int)a2;
        klog(LOG_DEBUG, "sys_open(\"%s\", flags=0x%x)", path ? path : "?", flags);
        if (!path)  RET_ERR(EFAULT);
        if (!proc)  RET_ERR(ENOMEM);
        vfs_node_t *node = vfs_open(path, flags);
        if (!node)  RET_ERR(ENOENT);
        int fd = proc_open_fd(proc, node);
        if (fd < 0) { vfs_close(node); RET_ERR(ENFILE); }
        proc->fd_offsets[fd] = 0;
        return (uint64_t)fd;
    }

    /* ── 3: close ────────────────────────────────────────────────────────── */
    case SYS_CLOSE: {
        int fd = (int)a1;
        klog(LOG_DEBUG, "sys_close(fd=%d)", fd);
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        if (!proc->fds[fd]) RET_ERR(EBADF);
        proc_close_fd(proc, fd);
        proc->fd_offsets[fd] = 0;
        return 0;
    }

    /* ── 4: stat ─────────────────────────────────────────────────────────── */
    case SYS_STAT: {
        const char   *path = (const char *)(uintptr_t)a1;
        linux_stat_t *ls   = (linux_stat_t *)(uintptr_t)a2;
        klog(LOG_DEBUG, "sys_stat(\"%s\")", path ? path : "?");
        if (!path || !ls) RET_ERR(EFAULT);
        vfs_stat_t vs;
        if (vfs_stat(path, &vs) != 0) RET_ERR(ENOENT);
        fill_stat_from_vstat(ls, &vs);
        return 0;
    }

    /* ── 5: fstat ────────────────────────────────────────────────────────── */
    case SYS_FSTAT: {
        int           fd = (int)a1;
        linux_stat_t *ls = (linux_stat_t *)(uintptr_t)a2;
        klog(LOG_DEBUG, "sys_fstat(fd=%d)", fd);
        if (!ls) RET_ERR(EFAULT);
        /* Special-case stdin/stdout/stderr — report as char device */
        if (fd >= 0 && fd <= 2) {
            sc_memzero(ls, sizeof(*ls));
            ls->st_dev  = 1;
            ls->st_ino  = (uint64_t)(fd + 1);
            ls->st_mode = 0x2000 | 0x1B6; /* S_IFCHR | 0666 */
            ls->st_nlink = 1;
            return 0;
        }
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[fd];
        if (!node) RET_ERR(EBADF);
        fill_stat_from_node(ls, node);
        return 0;
    }

    /* ── 8: lseek ────────────────────────────────────────────────────────── */
    case SYS_LSEEK: {
        int      fd     = (int)a1;
        int64_t  offset = (int64_t)a2;
        int      whence = (int)a3;
        klog(LOG_DEBUG, "sys_lseek(fd=%d, off=%lld, whence=%d)", fd, offset, whence);
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[fd];
        if (!node) RET_ERR(EBADF);
        uint64_t new_off;
        switch (whence) {
        case 0: /* SEEK_SET */ new_off = (uint64_t)offset;                       break;
        case 1: /* SEEK_CUR */ new_off = proc->fd_offsets[fd] + (uint64_t)offset; break;
        case 2: /* SEEK_END */ new_off = node->size + (uint64_t)offset;            break;
        default: RET_ERR(EINVAL);
        }
        proc->fd_offsets[fd] = new_off;
        return new_off;
    }

    /* ── 9: mmap ─────────────────────────────────────────────────────────── */
    case SYS_MMAP: {
        uint64_t addr   = a1;
        uint64_t length = a2;
        /* int prot  = (int)a3; int flags = (int)a4; fd and offset ignored (anon) */
        klog(LOG_DEBUG, "sys_mmap(addr=%llx, len=%llx, prot=%d, flags=%d)",
             addr, length, (int)a3, (int)a4);
        if (length == 0 || !proc) RET_ERR(EINVAL);
        length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t virt = (addr != 0) ? addr : proc_get_mmap_base(proc);
        if (map_anon_pages(virt, length) != 0) RET_ERR(ENOMEM);
        if (addr == 0) proc->mmap_base = virt + length;
        klog(LOG_DEBUG, "sys_mmap -> %llx", virt);
        return virt;
    }

    /* ── 10: mprotect ────────────────────────────────────────────────────── */
    case SYS_MPROTECT: {
        klog(LOG_DEBUG, "sys_mprotect(addr=%llx, len=%llx, prot=%d)", a1, a2, (int)a3);
        /* Stub — NexOS doesn't enforce per-page protection yet */
        return 0;
    }

    /* ── 11: munmap ──────────────────────────────────────────────────────── */
    case SYS_MUNMAP: {
        uint64_t addr   = a1;
        uint64_t length = a2;
        klog(LOG_DEBUG, "sys_munmap(addr=%llx, len=%llx)", addr, length);
        /* Unmap pages; don't free physical memory (no tracking yet) */
        uint64_t cur = addr & ~(PAGE_SIZE - 1);
        uint64_t end = (addr + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        while (cur < end) { vmm_unmap(cur); cur += PAGE_SIZE; }
        return 0;
    }

    /* ── 12: brk ─────────────────────────────────────────────────────────── */
    case SYS_BRK: {
        uint64_t new_brk = a1;
        if (!proc) RET_ERR(ENOMEM);
        uint64_t cur_brk = proc_get_brk(proc);
        klog(LOG_DEBUG, "sys_brk(new=%llx, cur=%llx)", new_brk, cur_brk);
        if (new_brk == 0) return cur_brk;          /* query */
        if (new_brk > cur_brk) {
            if (map_anon_pages(cur_brk, new_brk - cur_brk) != 0)
                return cur_brk;                    /* fail → return old brk */
        }
        proc->brk = new_brk;
        return proc->brk;
    }

    /* ── 16: ioctl ───────────────────────────────────────────────────────── */
    case SYS_IOCTL: {
        int           fd  = (int)a1;
        unsigned long req = (unsigned long)a2;
        void         *arg = (void *)(uintptr_t)a3;
        klog(LOG_DEBUG, "sys_ioctl(fd=%d, req=0x%lx)", fd, req);
        if (fd >= 0 && fd <= 2) {          /* stdin/stdout/stderr are terminal-like */
            switch (req) {
            case 0x5401: case 0x5402: case 0x5403: /* TCGETS/TCSETS/TCSETSW */
            case 0x5404: case 0x5409: case 0x540A: /* TCSETSF/TCSBRK/TCXONC */
                return 0;                  /* pretend terminal config succeeded */
            case 0x5413: {                 /* TIOCGWINSZ */
                winsize_t *ws = (winsize_t *)arg;
                if (ws) { ws->ws_row = 25; ws->ws_col = 80;
                          ws->ws_xpixel = 640; ws->ws_ypixel = 400; }
                return 0;
            }
            case 0x80045430:               /* TIOCGPGRP */
                if (arg) *(int *)arg = proc ? (int)proc->pid : 1;
                return 0;
            }
        }
        RET_ERR(ENOTTY);
    }

    /* ── 21: access ──────────────────────────────────────────────────────── */
    case SYS_ACCESS: {
        const char *path = (const char *)(uintptr_t)a1;
        int         mode = (int)a2;
        klog(LOG_DEBUG, "sys_access(\"%s\", mode=%d)", path ? path : "?", mode);
        if (!path) RET_ERR(EFAULT);
        vfs_node_t *node = vfs_open(path, 0);
        if (!node) RET_ERR(ENOENT);
        vfs_close(node);
        return 0;   /* F_OK and all permission checks pass — NexOS has no ACLs */
    }

    /* ── 22: pipe ────────────────────────────────────────────────────────── */
    case SYS_PIPE: {
        int *pipefd = (int *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_pipe(pipefd=%p)", (void *)pipefd);
        if (!proc || !pipefd) RET_ERR(EFAULT);
        pipe_buf_t *pbuf = (pipe_buf_t *)kmalloc(sizeof(pipe_buf_t));
        if (!pbuf) RET_ERR(ENOMEM);
        sc_memzero(pbuf, sizeof(*pbuf));
        vfs_node_t *rnode = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
        vfs_node_t *wnode = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
        if (!rnode || !wnode) {
            kfree(pbuf);
            if (rnode) kfree(rnode);
            if (wnode) kfree(wnode);
            RET_ERR(ENOMEM);
        }
        sc_memzero(rnode, sizeof(*rnode)); sc_memzero(wnode, sizeof(*wnode));
        rnode->type = VFS_NODE_PIPE; rnode->read  = pipe_read_fn;  rnode->priv = pbuf;
        wnode->type = VFS_NODE_PIPE; wnode->write = pipe_write_fn; wnode->priv = pbuf;
        int rfd = proc_open_fd(proc, rnode);
        int wfd = proc_open_fd(proc, wnode);
        if (rfd < 0 || wfd < 0) {
            if (rfd >= 0) proc_close_fd(proc, rfd);
            if (wfd >= 0) proc_close_fd(proc, wfd);
            kfree(rnode); kfree(wnode); kfree(pbuf);
            RET_ERR(ENFILE);
        }
        pipefd[0] = rfd; pipefd[1] = wfd;
        klog(LOG_DEBUG, "sys_pipe -> [%d, %d]", rfd, wfd);
        return 0;
    }

    /* ── 23: select ──────────────────────────────────────────────────────── */
    case SYS_SELECT: {
        /* Stub — always report fd 0 (stdin) as readable if nfds > 0 */
        klog(LOG_DEBUG, "sys_select(nfds=%d) -> stub 0", (int)a1);
        return 0;
    }

    /* ── 32: dup ─────────────────────────────────────────────────────────── */
    case SYS_DUP: {
        int fd = (int)a1;
        klog(LOG_DEBUG, "sys_dup(fd=%d)", fd);
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[fd];
        if (!node) RET_ERR(EBADF);
        int newfd = proc_open_fd(proc, node);
        if (newfd < 0) RET_ERR(ENFILE);
        proc->fd_offsets[newfd] = proc->fd_offsets[fd];
        return (uint64_t)newfd;
    }

    /* ── 33: dup2 ────────────────────────────────────────────────────────── */
    case SYS_DUP2: {
        int oldfd = (int)a1;
        int newfd = (int)a2;
        klog(LOG_DEBUG, "sys_dup2(old=%d, new=%d)", oldfd, newfd);
        if (!proc) RET_ERR(EBADF);
        if (oldfd < 0 || oldfd >= MAX_FDS) RET_ERR(EBADF);
        if (newfd < 0 || newfd >= MAX_FDS) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[oldfd];
        if (!node) RET_ERR(EBADF);
        if (oldfd == newfd) return (uint64_t)newfd;
        if (proc->fds[newfd]) proc_close_fd(proc, newfd);
        proc->fds[newfd] = node;
        proc->fd_offsets[newfd] = proc->fd_offsets[oldfd];
        return (uint64_t)newfd;
    }

    /* ── 39: getpid ──────────────────────────────────────────────────────── */
    case SYS_GETPID:
        klog(LOG_DEBUG, "sys_getpid() -> %u", proc ? proc->pid : 0);
        return proc ? (uint64_t)proc->pid : 1;

    /* ── 57: fork ────────────────────────────────────────────────────────── */
    case SYS_FORK:
        klog(LOG_DEBUG, "sys_fork() -> ENOSYS (not yet implemented)");
        RET_ERR(ENOSYS);

    /* ── 59: execve ──────────────────────────────────────────────────────── */
    case SYS_EXECVE:
        klog(LOG_DEBUG, "sys_execve(path=%s)", (char *)a1);
        return proc_exec((char*)a1, (char**)a2);

    /* ── 60: exit ────────────────────────────────────────────────────────── */
    case SYS_EXIT:
        klog(LOG_DEBUG, "sys_exit(code=%d)", (int)a1);
        proc_exit((int)a1);
        return 0; /* unreachable */

    /* ── 61: wait4 ───────────────────────────────────────────────────────── */
    case SYS_WAIT4: {
        int      wpid    = (int)a1;
        int     *wstatus = (int *)(uintptr_t)a2;
        klog(LOG_DEBUG, "sys_wait4(pid=%d)", wpid);
        /* Scan for zombie child of current process */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t *p = processes[i];
            if (!p) continue;
            if (p->state != PROC_ZOMBIE) continue;
            if (!proc || p->ppid != proc->pid) continue;
            if (wpid > 0 && (int)p->pid != wpid) continue;
            int collected_pid = (int)p->pid;
            if (wstatus) *wstatus = (p->exit_code & 0xFF) << 8; /* WEXITSTATUS */
            /* Clean up zombie */
            processes[i] = NULL;
            process_count--;
            kfree(p->stack);
            kfree(p);
            return (uint64_t)collected_pid;
        }
        RET_ERR(ECHILD);
    }

    /* ── 63: uname ───────────────────────────────────────────────────────── */
    case SYS_UNAME: {
        linux_utsname_t *ut = (linux_utsname_t *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_uname(%p)", (void *)ut);
        if (!ut) RET_ERR(EFAULT);
        sc_memzero(ut, sizeof(*ut));
        sc_strcpy(ut->sysname,    "NexOS",    65);
        sc_strcpy(ut->nodename,   "nexos",    65);
        sc_strcpy(ut->release,    "0.1.0",    65);
        sc_strcpy(ut->version,    "#1 SMP",   65);
        sc_strcpy(ut->machine,    "x86_64",   65);
        sc_strcpy(ut->domainname, "(none)",   65);
        return 0;
    }

    /* ── 78: getdents ────────────────────────────────────────────────────── */
    case SYS_GETDENTS: {
        int      fd    = (int)a1;
        uint8_t *buf   = (uint8_t *)(uintptr_t)a2;
        uint32_t count = (uint32_t)a3;
        klog(LOG_DEBUG, "sys_getdents(fd=%d, count=%u)", fd, count);
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        vfs_node_t *dir = proc->fds[fd];
        if (!dir || !(dir->type & VFS_NODE_DIR)) RET_ERR(ENOTDIR);
        uint32_t pos = 0;
        uint32_t idx = (uint32_t)proc->fd_offsets[fd];
        vfs_dirent_t de;
        while (vfs_readdir(dir, idx, &de) == 0) {
            size_t namelen = sc_strlen(de.name);
            /* linux_dirent: ino(8) + off(8) + reclen(2) + name + \0 + d_type */
            uint16_t reclen = (uint16_t)((18 + namelen + 2 + 7) & ~7u);
            if (pos + reclen > count) break;
            linux_dirent_t *ent = (linux_dirent_t *)(buf + pos);
            ent->d_ino    = de.inode;
            ent->d_off    = (int64_t)(idx + 1);
            ent->d_reclen = reclen;
            for (size_t k = 0; k < namelen; k++) ent->d_name[k] = de.name[k];
            ent->d_name[namelen] = 0;
            /* d_type byte at end of record (before alignment padding) */
            buf[pos + 18 + namelen + 1] = 0; /* DT_UNKNOWN — no node type here */
            pos += reclen;
            idx++;
        }
        proc->fd_offsets[fd] = idx;
        return pos;
    }

    /* ── 79: getcwd ──────────────────────────────────────────────────────── */
    case SYS_GETCWD: {
        char    *buf  = (char *)(uintptr_t)a1;
        uint32_t size = (uint32_t)a2;
        klog(LOG_DEBUG, "sys_getcwd(size=%u)", size);
        if (!proc || !buf || size == 0) RET_ERR(EFAULT);
        size_t cwdlen = sc_strlen(proc->cwd);
        if (cwdlen + 1 > (size_t)size) RET_ERR(ERANGE);
        sc_strcpy(buf, proc->cwd, (size_t)size);
        return (uint64_t)(uintptr_t)buf;
    }

    /* ── 80: chdir ───────────────────────────────────────────────────────── */
    case SYS_CHDIR: {
        const char *path = (const char *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_chdir(\"%s\")", path ? path : "?");
        if (!proc || !path) RET_ERR(EFAULT);
        vfs_node_t *node = vfs_open(path, 0);
        if (!node) RET_ERR(ENOENT);
        if (!(node->type & VFS_NODE_DIR)) { vfs_close(node); RET_ERR(ENOTDIR); }
        vfs_close(node);
        sc_strcpy(proc->cwd, path, sizeof(proc->cwd));
        return 0;
    }

    /* ── 82: rename ──────────────────────────────────────────────────────── */
    case SYS_RENAME: {
        const char *oldpath = (const char *)(uintptr_t)a1;
        const char *newpath = (const char *)(uintptr_t)a2;
        klog(LOG_DEBUG, "sys_rename(\"%s\", \"%s\")",
             oldpath ? oldpath : "?", newpath ? newpath : "?");
        if (!oldpath || !newpath) RET_ERR(EFAULT);
        /* Read source into kernel buffer, create dest, delete source */
        vfs_node_t *src = vfs_open(oldpath, 0);
        if (!src) RET_ERR(ENOENT);
        uint64_t sz = src->size;
        if (sz > 0) {
            uint8_t *tmp = (uint8_t *)kmalloc((size_t)sz + 1);
            if (!tmp) { vfs_close(src); RET_ERR(ENOMEM); }
            vfs_read(src, 0, (uint32_t)sz, tmp);
            vfs_close(src);
            vfs_create(newpath, 0);
            vfs_node_t *dst = vfs_open(newpath, 1);
            if (dst) { vfs_write(dst, 0, (uint32_t)sz, tmp); vfs_close(dst); }
            kfree(tmp);
        } else {
            vfs_close(src);
            vfs_create(newpath, 0);
        }
        vfs_unlink(oldpath);
        return 0;
    }

    /* ── 83: mkdir ───────────────────────────────────────────────────────── */
    case SYS_MKDIR: {
        const char *path = (const char *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_mkdir(\"%s\")", path ? path : "?");
        if (!path) RET_ERR(EFAULT);
        return (uint64_t)vfs_mkdir(path);
    }

    /* ── 84: rmdir ───────────────────────────────────────────────────────── */
    case SYS_RMDIR: {
        const char *path = (const char *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_rmdir(\"%s\")", path ? path : "?");
        if (!path) RET_ERR(EFAULT);
        vfs_node_t *node = vfs_open(path, 0);
        if (!node) RET_ERR(ENOENT);
        if (!(node->type & VFS_NODE_DIR)) { vfs_close(node); RET_ERR(ENOTDIR); }
        vfs_close(node);
        int r = vfs_unlink(path);
        return r == 0 ? 0 : (uint64_t)(-ENOENT);
    }

    /* ── 87: unlink ──────────────────────────────────────────────────────── */
    case SYS_UNLINK: {
        const char *path = (const char *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_unlink(\"%s\")", path ? path : "?");
        if (!path) RET_ERR(EFAULT);
        int r = vfs_unlink(path);
        return r == 0 ? 0 : (uint64_t)(-ENOENT);
    }

    /* ── 89: readlink ────────────────────────────────────────────────────── */
    case SYS_READLINK: {
        const char *path = (const char *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_readlink(\"%s\") -> EINVAL (no symlinks)", path ? path : "?");
        (void)path;
        RET_ERR(EINVAL); /* NexOS has no symlinks */
    }

    /* ── 90: chmod ───────────────────────────────────────────────────────── */
    case SYS_CHMOD: {
        const char *path = (const char *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_chmod(\"%s\", mode=%o) -> stub 0",
             path ? path : "?", (unsigned)a2);
        return 0; /* NexOS has no permissions model — silently succeed */
    }

    /* ── 92: chown ───────────────────────────────────────────────────────── */
    case SYS_CHOWN: {
        const char *path = (const char *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_chown(\"%s\", uid=%u, gid=%u) -> stub 0",
             path ? path : "?", (unsigned)a2, (unsigned)a3);
        return 0; /* stub — NexOS has no ownership model */
    }

    /* ── 95: umask ───────────────────────────────────────────────────────── */
    case SYS_UMASK: {
        uint32_t new_mask = (uint32_t)a1;
        klog(LOG_DEBUG, "sys_umask(mask=%03o)", new_mask);
        if (!proc) return 0022;
        uint32_t old = proc->umask ? proc->umask : 0022;
        proc->umask = new_mask & 0777;
        return old;
    }

    /* ── 96: gettimeofday ────────────────────────────────────────────────── */
    case SYS_GETTIMEOFDAY: {
        linux_timeval_t *tv = (linux_timeval_t *)(uintptr_t)a1;
        klog(LOG_DEBUG, "sys_gettimeofday(%p)", (void *)tv);
        if (tv) {
            rtc_time_t t;
            rtc_get_time(&t);
            /* Compute seconds since Unix epoch (1970-01-01).
               Simple approximation: use uptime from boot + a fixed base. */
            uint64_t uptime = timer_get_uptime_seconds();
            /* RTC year → seconds: rough formula (ignore leap seconds) */
            uint32_t y = t.year;
            uint64_t epoch = 0;
            if (y >= 1970) {
                uint32_t dy = y - 1970;
                epoch = (uint64_t)dy * 365 * 86400
                      + (uint64_t)(dy / 4) * 86400;       /* leap years */
                const uint16_t mdays[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
                uint8_t m = t.month > 0 ? t.month - 1 : 0;
                if (m > 11) m = 11;
                epoch += (uint64_t)mdays[m] * 86400;
                epoch += (uint64_t)(t.day > 0 ? t.day - 1 : 0) * 86400;
                epoch += (uint64_t)t.hour * 3600;
                epoch += (uint64_t)t.minute * 60;
                epoch += (uint64_t)t.second;
            } else {
                epoch = uptime;
            }
            tv->tv_sec  = (int64_t)epoch;
            tv->tv_usec = (int64_t)((timer_get_ticks() % 1000) * 1000);
        }
        return 0;
    }

    /* ── 102: getuid ─────────────────────────────────────────────────────── */
    case SYS_GETUID:
        klog(LOG_DEBUG, "sys_getuid() -> 0 (root)");
        return 0;

    /* ── 104: getgid ─────────────────────────────────────────────────────── */
    case SYS_GETGID:
        klog(LOG_DEBUG, "sys_getgid() -> 0 (root)");
        return 0;

    /* ── 107: geteuid ────────────────────────────────────────────────────── */
    case SYS_GETEUID:
        klog(LOG_DEBUG, "sys_geteuid() -> 0 (root)");
        return 0;

    /* ── 108: getegid ────────────────────────────────────────────────────── */
    case SYS_GETEGID:
        klog(LOG_DEBUG, "sys_getegid() -> 0 (root)");
        return 0;

    /* ── 110: getppid ────────────────────────────────────────────────────── */
    case SYS_GETPPID:
        klog(LOG_DEBUG, "sys_getppid() -> %u", proc ? proc->ppid : 0);
        return proc ? (uint64_t)proc->ppid : 0;

    /* ── 112: setsid ─────────────────────────────────────────────────────── */
    case SYS_SETSID: {
        uint32_t pid = proc ? proc->pid : 1;
        klog(LOG_DEBUG, "sys_setsid() -> %u (stub)", pid);
        return (uint64_t)pid; /* stub — return own pid as session id */
    }

    /* ── 115: getgroups ──────────────────────────────────────────────────── */
    case SYS_GETGROUPS: {
        int size = (int)a1;
        klog(LOG_DEBUG, "sys_getgroups(size=%d) -> 0", size);
        /* NexOS has no supplementary groups */
        return 0;
    }

    /* ── 217: getdents64 ─────────────────────────────────────────────────── */
    case SYS_GETDENTS64: {
        int      fd    = (int)a1;
        uint8_t *buf   = (uint8_t *)(uintptr_t)a2;
        uint32_t count = (uint32_t)a3;
        klog(LOG_DEBUG, "sys_getdents64(fd=%d, count=%u)", fd, count);
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        vfs_node_t *dir = proc->fds[fd];
        if (!dir || !(dir->type & VFS_NODE_DIR)) RET_ERR(ENOTDIR);
        uint32_t pos = 0;
        uint32_t idx = (uint32_t)proc->fd_offsets[fd];
        vfs_dirent_t de;
        while (vfs_readdir(dir, idx, &de) == 0) {
            size_t namelen = sc_strlen(de.name);
            /* linux_dirent64: ino(8)+off(8)+reclen(2)+type(1)+name+\0, aligned 8 */
            uint16_t reclen = (uint16_t)((19 + namelen + 1 + 7) & ~7u);
            if (pos + reclen > count) break;
            linux_dirent64_t *ent = (linux_dirent64_t *)(buf + pos);
            ent->d_ino    = de.inode;
            ent->d_off    = (int64_t)(idx + 1);
            ent->d_reclen = reclen;
            ent->d_type   = 0; /* DT_UNKNOWN (no vfs_node to query type from) */
            for (size_t k = 0; k < namelen; k++) ent->d_name[k] = de.name[k];
            ent->d_name[namelen] = 0;
            pos += reclen;
            idx++;
        }
        proc->fd_offsets[fd] = idx;
        return pos;
    }

    /* ── 228: clock_gettime ──────────────────────────────────────────────── */
    case SYS_CLOCK_GETTIME: {
        int              clk_id = (int)a1;
        linux_timespec_t *tp    = (linux_timespec_t *)(uintptr_t)a2;
        klog(LOG_DEBUG, "sys_clock_gettime(clk=%d)", clk_id);
        if (!tp) RET_ERR(EFAULT);
        if (clk_id == 1 /* CLOCK_MONOTONIC */ || clk_id == 4 /* CLOCK_MONOTONIC_RAW */) {
            uint64_t sec  = timer_get_uptime_seconds();
            uint64_t tick = timer_get_ticks();
            tp->tv_sec  = (int64_t)sec;
            tp->tv_nsec = (int64_t)((tick % 1000) * 1000000LL);
        } else {
            /* CLOCK_REALTIME (0) and others: use RTC */
            rtc_time_t t;
            rtc_get_time(&t);
            uint32_t y = t.year;
            int64_t epoch = 0;
            if (y >= 1970) {
                uint32_t dy = y - 1970;
                epoch = (int64_t)dy * 365 * 86400 + (int64_t)(dy / 4) * 86400;
                const uint16_t mdays[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
                uint8_t m = t.month > 0 ? t.month - 1 : 0;
                if (m > 11) m = 11;
                epoch += (int64_t)mdays[m] * 86400;
                epoch += (int64_t)(t.day > 0 ? t.day - 1 : 0) * 86400;
                epoch += (int64_t)t.hour * 3600;
                epoch += (int64_t)t.minute * 60;
                epoch += (int64_t)t.second;
            }
            tp->tv_sec  = epoch;
            tp->tv_nsec = (int64_t)((timer_get_ticks() % 1000) * 1000000LL);
        }
        return 0;
    }

    /* ── 300: NexOS sleep (ms) ───────────────────────────────────────────── */
    case SYS_SLEEP:
        klog(LOG_DEBUG, "sys_sleep(%u ms)", (uint32_t)a1);
        timer_sleep_ms((uint32_t)a1);
        return 0;

    /* ════════════════════════════════════════════════════════════════════════
     * ADDITIONAL POSIX / LINUX SYSCALLS
     * ════════════════════════════════════════════════════════════════════════ */

    /* ── 13/14/15: rt_sigaction / rt_sigprocmask / rt_sigreturn ─────────── */
    case SYS_RT_SIGACTION:
    case SYS_RT_SIGPROCMASK:
    case SYS_RT_SIGRETURN:
        return 0;   /* no signal delivery yet — safe stub */

    /* ── 17: pread64(fd, buf, count, offset) ────────────────────────────── */
    case SYS_PREAD64: {
        int fd = (int)a1;
        void *buf = (void *)a2;
        size_t cnt = (size_t)a3;
        uint64_t off = a4;
        process_t *proc = proc_get_current();
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[fd];
        uint32_t n = vfs_read(node, off, (uint32_t)cnt, (uint8_t *)buf);
        return (uint64_t)(int64_t)(int32_t)n;
    }

    /* ── 18: pwrite64(fd, buf, count, offset) ───────────────────────────── */
    case SYS_PWRITE64: {
        int fd = (int)a1;
        const void *buf = (const void *)a2;
        size_t cnt = (size_t)a3;
        uint64_t off = a4;
        process_t *proc = proc_get_current();
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[fd];
        uint32_t n = vfs_write(node, off, (uint32_t)cnt, (const uint8_t *)buf);
        return (uint64_t)(int64_t)(int32_t)n;
    }

    /* ── 19: readv(fd, iovec[], iovcnt) ─────────────────────────────────── */
    case SYS_READV: {
        typedef struct { void *base; uint64_t len; } iovec_t;
        int fd = (int)a1;
        const iovec_t *iov = (const iovec_t *)a2;
        int iovcnt = (int)a3;
        process_t *proc = proc_get_current();
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        uint64_t total = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (!iov[i].base || !iov[i].len) continue;
            if (fd == 0) {
                /* stdin: read one char at a time */
                uint8_t *b = (uint8_t *)iov[i].base;
                for (uint64_t j = 0; j < iov[i].len; j++) {
                    char c = keyboard_getchar();
                    b[j] = (uint8_t)c;
                    if (c == '\n') { total += j + 1; goto readv_done; }
                }
                total += iov[i].len;
            } else {
                if (!proc->fds[fd]) { RET_ERR(EBADF); }
                vfs_node_t *node = proc->fds[fd];
                uint32_t n = vfs_read(node, proc->fd_offsets[fd],
                                      (uint32_t)iov[i].len,
                                      (uint8_t *)iov[i].base);
                proc->fd_offsets[fd] += n;
                total += n;
            }
        }
        readv_done:
        return total;
    }

    /* ── 20: writev(fd, iovec[], iovcnt) ────────────────────────────────── */
    case SYS_WRITEV: {
        typedef struct { const void *base; uint64_t len; } iovecw_t;
        int fd = (int)a1;
        const iovecw_t *iov = (const iovecw_t *)a2;
        int iovcnt = (int)a3;
        process_t *proc = proc_get_current();
        uint64_t total = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (!iov[i].base || !iov[i].len) continue;
            const uint8_t *src = (const uint8_t *)iov[i].base;
            uint32_t len = (uint32_t)iov[i].len;
            if (fd == 1 || fd == 2) {
                for (uint32_t j = 0; j < len; j++) vga_putchar(src[j]);
                total += len;
            } else if (proc && fd >= 0 && fd < MAX_FDS && proc->fds[fd]) {
                uint32_t n = vfs_write(proc->fds[fd], proc->fd_offsets[fd],
                                       len, src);
                proc->fd_offsets[fd] += n;
                total += n;
            } else { RET_ERR(EBADF); }
        }
        return total;
    }

    /* ── 24: sched_yield() ───────────────────────────────────────────────── */
    case SYS_SCHED_YIELD:
        return 0;

    /* ── 28: madvise(addr, len, advice) ─────────────────────────────────── */
    case SYS_MADVISE:
        return 0;   /* no-op stub: no demand paging yet */

    /* ── 35: nanosleep(req*, rem*) ──────────────────────────────────────── */
    case SYS_NANOSLEEP: {
        linux_timespec_t *req = (linux_timespec_t *)a1;
        if (!req) RET_ERR(EFAULT);
        uint32_t ms = (uint32_t)(req->tv_sec * 1000 +
                                  req->tv_nsec / 1000000LL);
        if (ms > 0) timer_sleep_ms(ms);
        return 0;
    }

    /* ── 36: getitimer() ────────────────────────────────────────────────── */
    case SYS_GETITIMER:
        return 0;

    /* ── 40: sendfile(out_fd, in_fd, offset*, count) ────────────────────── */
    case SYS_SENDFILE: {
        int out_fd = (int)a1, in_fd = (int)a2;
        uint64_t cnt = a4;
        process_t *proc = proc_get_current();
        if (!proc) RET_ERR(EBADF);
        if (in_fd  < 0 || in_fd  >= MAX_FDS || !proc->fds[in_fd])  RET_ERR(EBADF);
        if (out_fd < 0 || out_fd >= MAX_FDS || !proc->fds[out_fd]) RET_ERR(EBADF);
        uint8_t tmp[512]; uint64_t total = 0;
        while (total < cnt) {
            uint32_t chunk = (uint32_t)(cnt - total);
            if (chunk > 512) chunk = 512;
            uint32_t n = vfs_read(proc->fds[in_fd],
                                   proc->fd_offsets[in_fd], chunk, tmp);
            if (!n) break;
            proc->fd_offsets[in_fd] += n;
            vfs_write(proc->fds[out_fd], proc->fd_offsets[out_fd], n, tmp);
            proc->fd_offsets[out_fd] += n;
            total += n;
        }
        return total;
    }

    /* ── 41-50: socket family stubs ─────────────────────────────────────── */
    case SYS_SOCKET:
    case SYS_CONNECT:
    case SYS_ACCEPT:
    case SYS_SENDTO:
    case SYS_RECVFROM:
    case SYS_BIND:
    case SYS_LISTEN:
        RET_ERR(ENOSYS);   /* networking via syscalls not yet wired */

    /* ── 56: clone() ────────────────────────────────────────────────────── */
    case SYS_CLONE:
        RET_ERR(ENOSYS);

    /* ── 62: kill(pid, sig) ─────────────────────────────────────────────── */
    case SYS_KILL: {
        int sig = (int)a2;
        if (sig == 0) return 0;   /* probe-only, no signal delivery yet */
        return 0;
    }

    /* ── 72: fcntl(fd, cmd, arg) ────────────────────────────────────────── */
    case SYS_FCNTL: {
        int fd  = (int)a1;
        int cmd = (int)a2;
        uint64_t arg = a3;
        process_t *proc = proc_get_current();
        if (!proc || fd < 0 || fd >= MAX_FDS) RET_ERR(EBADF);
        #define F_DUPFD    0
        #define F_GETFD    1
        #define F_SETFD    2
        #define F_GETFL    3
        #define F_SETFL    4
        #define O_RDWR     2
        switch (cmd) {
        case F_GETFD: return 0;
        case F_SETFD: return 0;
        case F_GETFL: return O_RDWR;
        case F_SETFL: return 0;
        case F_DUPFD: {
            int minfd = (int)arg;
            if (minfd < 0) minfd = 0;
            for (int nfd = minfd; nfd < MAX_FDS; nfd++) {
                if (!proc->fds[nfd]) {
                    proc->fds[nfd]        = proc->fds[fd];
                    proc->fd_offsets[nfd] = proc->fd_offsets[fd];
                    return (uint64_t)(int64_t)nfd;
                }
            }
            RET_ERR(ENFILE);
        }
        default: return 0;
        }
    }

    /* ── 77: ftruncate(fd, length) ──────────────────────────────────────── */
    case SYS_FTRUNCATE:
        return 0;   /* VFS has no truncate yet — stub succeeds */

    /* ── 97: getrlimit(resource, rlim*) ────────────────────────────────── */
    case SYS_GETRLIMIT: {
        typedef struct { uint64_t rlim_cur, rlim_max; } rlimit_t;
        rlimit_t *rl = (rlimit_t *)a2;
        if (!rl) RET_ERR(EFAULT);
        rl->rlim_cur = 0xFFFFFFFFFFFFFFFFULL;  /* RLIM_INFINITY */
        rl->rlim_max = 0xFFFFFFFFFFFFFFFFULL;
        int res = (int)a1;
        if (res == 7 /* RLIMIT_NOFILE */) {
            rl->rlim_cur = MAX_FDS;
            rl->rlim_max = MAX_FDS;
        }
        return 0;
    }

    /* ── 99: sysinfo(info*) ─────────────────────────────────────────────── */
    case SYS_SYSINFO: {
        typedef struct {
            int64_t  uptime;
            uint64_t loads[3];
            uint64_t totalram, freeram, sharedram, bufferram;
            uint64_t totalswap, freeswap;
            uint16_t procs;
            uint64_t totalhigh, freehigh;
            uint32_t mem_unit;
            char     _f[20 - 2 * sizeof(uint64_t) - sizeof(uint32_t)];
        } __attribute__((packed)) sysinfo_t;
        sysinfo_t *si = (sysinfo_t *)a1;
        if (!si) RET_ERR(EFAULT);
        sc_memzero(si, sizeof(*si));
        si->uptime   = (int64_t)timer_get_uptime_seconds();
        si->totalram = (uint64_t)pmm_get_free_frames() * 2 * 4096ULL;
        si->freeram  = (uint64_t)pmm_get_free_frames()  * 4096ULL;
        si->mem_unit = 1;
        si->procs    = 1;
        return 0;
    }

    /* ── 131: sigaltstack ───────────────────────────────────────────────── */
    case SYS_SIGALTSTACK:
        return 0;

    /* ── 157: prctl(option, arg2 …) ────────────────────────────────────── */
    case SYS_PRCTL: {
        int opt = (int)a1;
        if (opt == 15 /* PR_SET_NAME */) return 0;
        if (opt == 16 /* PR_GET_NAME */) {
            char *name = (char *)a2;
            if (name) sc_strcpy(name, "nexos", 16);
            return 0;
        }
        return 0;
    }

    /* ── 158: arch_prctl(code, addr) ────────────────────────────────────── */
    case SYS_ARCH_PRCTL: {
        int     code = (int)a1;
        uint64_t addr = a2;
        #define ARCH_SET_GS 0x1001
        #define ARCH_SET_FS 0x1002
        #define ARCH_GET_FS 0x1003
        #define ARCH_GET_GS 0x1004
        if (code == ARCH_SET_FS) {
            __asm__ volatile("wrmsr"
                :: "c"(0xC0000100U),
                   "a"((uint32_t)addr),
                   "d"((uint32_t)(addr >> 32)));
            return 0;
        }
        if (code == ARCH_SET_GS) {
            __asm__ volatile("wrmsr"
                :: "c"(0xC0000101U),
                   "a"((uint32_t)addr),
                   "d"((uint32_t)(addr >> 32)));
            return 0;
        }
        if (code == ARCH_GET_FS) {
            uint32_t lo, hi;
            __asm__ volatile("rdmsr"
                : "=a"(lo), "=d"(hi) : "c"(0xC0000100U));
            *(uint64_t *)addr = ((uint64_t)hi << 32) | lo;
            return 0;
        }
        if (code == ARCH_GET_GS) {
            uint32_t lo, hi;
            __asm__ volatile("rdmsr"
                : "=a"(lo), "=d"(hi) : "c"(0xC0000101U));
            *(uint64_t *)addr = ((uint64_t)hi << 32) | lo;
            return 0;
        }
        RET_ERR(EINVAL);
    }

    /* ── 202: futex(uaddr, op, val, …) ─────────────────────────────────── */
    case SYS_FUTEX: {
        int op = (int)a2 & ~128; /* strip FUTEX_PRIVATE_FLAG */
        if (op == 1 /* FUTEX_WAKE */) return 0;   /* no threads → 0 woken */
        if (op == 0 /* FUTEX_WAIT */) return 0;   /* not blocking (single-threaded) */
        return 0;
    }

    /* ── 218: set_tid_address(tidptr) ───────────────────────────────────── */
    case SYS_SET_TID_ADDRESS: {
        process_t *proc = proc_get_current();
        return proc ? (uint64_t)(uint32_t)proc->pid : 1;
    }

    /* ── 231: exit_group(status) ────────────────────────────────────────── */
    case SYS_EXIT_GROUP: {
        process_t *proc = proc_get_current();
        if (proc) proc_exit((int)a1);
        return 0;
    }

    /* ── 273: set_robust_list ───────────────────────────────────────────── */
    case SYS_SET_ROBUST_LIST:
        return 0;

    /* ═══════════════════════════════════════════════════════════════════════
     *  LEVEL 1 / LEVEL 2 LINUX ABI  — ~110 additional syscalls
     * ═══════════════════════════════════════════════════════════════════════ */

    /* ── 6: lstat (no real symlinks, same as stat) ───────────────────────── */
    case SYS_LSTAT: {
        const char   *path = (const char *)(uintptr_t)a1;
        linux_stat_t *ls   = (linux_stat_t *)(uintptr_t)a2;
        if (!path || !ls) RET_ERR(EFAULT);
        vfs_stat_t vs;
        if (vfs_stat(path, &vs) != 0) RET_ERR(ENOENT);
        fill_stat_from_vstat(ls, &vs);
        return 0;
    }

    /* ── 7: poll ─────────────────────────────────────────────────────────── */
    case SYS_POLL: {
        typedef struct { int fd; short events; short revents; } pollfd_t;
        pollfd_t *pfds = (pollfd_t *)(uintptr_t)a1;
        int nfds = (int)a2;
        /* int timeout = (int)a3; */
        if (!pfds || nfds <= 0) return 0;
        int ready = 0;
        for (int i = 0; i < nfds; i++) {
            pfds[i].revents = 0;
            if (proc && pfds[i].fd >= 0 && pfds[i].fd < MAX_FDS
                && proc->fds[pfds[i].fd]) {
                if (pfds[i].events & 0x01) { pfds[i].revents |= 0x01; ready++; }
                if (pfds[i].events & 0x04) { pfds[i].revents |= 0x04; ready++; }
            }
        }
        return ready;
    }

    /* ── 25: mremap — alloc new region, copy ────────────────────────────── */
    case SYS_MREMAP: {
        uint64_t old_addr = a1;
        uint64_t old_size = a2;
        uint64_t new_size = a3;
        if (!proc) RET_ERR(ENOMEM);
        new_size = (new_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint64_t new_addr = proc_get_mmap_base(proc);
        if (map_anon_pages(new_addr, new_size) != 0) RET_ERR(ENOMEM);
        proc->mmap_base = new_addr + new_size;
        uint64_t copy = (old_size < new_size) ? old_size : new_size;
        uint8_t *src = (uint8_t *)(uintptr_t)old_addr;
        uint8_t *dst = (uint8_t *)(uintptr_t)new_addr;
        for (uint64_t i = 0; i < copy; i++) dst[i] = src[i];
        return new_addr;
    }

    /* ── 26: msync — no-op ───────────────────────────────────────────────── */
    case SYS_MSYNC:
        return 0;

    /* ── 27: mincore — all pages resident ───────────────────────────────── */
    case SYS_MINCORE: {
        uint64_t len = a2;
        uint8_t *vec = (uint8_t *)(uintptr_t)a3;
        if (!vec) RET_ERR(EFAULT);
        uint64_t npages = (len + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t i = 0; i < npages; i++) vec[i] = 1;
        return 0;
    }

    /* ── 29-31: SysV shm — ENOSYS ────────────────────────────────────────── */
    case SYS_SHMGET: case SYS_SHMAT: case SYS_SHMCTL:
        RET_ERR(ENOSYS);

    /* ── 34: pause — instant fake-EINTR ─────────────────────────────────── */
    case SYS_PAUSE:
        RET_ERR(EINTR);

    /* ── 37: alarm — no SIGALRM delivery, return 0 ──────────────────────── */
    case SYS_ALARM:
        return 0;

    /* ── 38: setitimer — stub ────────────────────────────────────────────── */
    case SYS_SETITIMER:
        return 0;

    /* ── 46-47: sendmsg / recvmsg — ENOSYS ──────────────────────────────── */
    case SYS_SENDMSG: case SYS_RECVMSG:
        RET_ERR(ENOSYS);

    /* ── 48: shutdown ────────────────────────────────────────────────────── */
    case SYS_SHUTDOWN:
        return 0;

    /* ── 51-52: getsockname / getpeername ───────────────────────────────── */
    case SYS_GETSOCKNAME: case SYS_GETPEERNAME:
        RET_ERR(ENOTCONN);

    /* ── 53: socketpair ──────────────────────────────────────────────────── */
    case SYS_SOCKETPAIR:
        RET_ERR(ENOSYS);

    /* ── 54-55: setsockopt / getsockopt — accept silently ───────────────── */
    case SYS_SETSOCKOPT: case SYS_GETSOCKOPT:
        return 0;

    /* ── 58: vfork — ENOSYS (no real fork yet) ───────────────────────────── */
    case SYS_VFORK:
        RET_ERR(ENOSYS);

    /* ── 64-71: SysV IPC — ENOSYS ────────────────────────────────────────── */
    case SYS_SEMGET: case SYS_SEMOP:  case SYS_SEMCTL:
    case SYS_SHMDT:
    case SYS_MSGGET: case SYS_MSGSND: case SYS_MSGRCV: case SYS_MSGCTL:
        RET_ERR(ENOSYS);

    /* ── 73: flock — no-op (single process) ─────────────────────────────── */
    case SYS_FLOCK:
        return 0;

    /* ── 74: fsync / 75: fdatasync — no-op ──────────────────────────────── */
    case SYS_FSYNC: case SYS_FDATASYNC: {
        int fd = (int)a1;
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        return 0;
    }

    /* ── 76: truncate ────────────────────────────────────────────────────── */
    case SYS_TRUNCATE: {
        const char *path = (const char *)(uintptr_t)a1;
        int64_t     len  = (int64_t)a2;
        if (!path) RET_ERR(EFAULT);
        vfs_node_t *node = vfs_open(path, 0);
        if (!node) RET_ERR(ENOENT);
        node->size = (len > 0) ? (uint64_t)len : 0;
        vfs_close(node);
        return 0;
    }

    /* ── 81: fchdir ──────────────────────────────────────────────────────── */
    case SYS_FCHDIR: {
        int fd = (int)a1;
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        vfs_node_t *node = proc->fds[fd];
        if (!(node->type & VFS_NODE_DIR)) RET_ERR(ENOTDIR);
        if (node->name[0]) sc_strcpy(proc->cwd, node->name, sizeof(proc->cwd));
        return 0;
    }

    /* ── 85: creat(path,mode) = open(O_CREAT|O_WRONLY|O_TRUNC) ──────────── */
    case SYS_CREAT: {
        const char *path = (const char *)(uintptr_t)a1;
        if (!path || !proc) RET_ERR(EFAULT);
        vfs_node_t *node = vfs_open(path, 0x241);
        if (!node) {
            vfs_create(path, 0);
            node = vfs_open(path, 0x241);
        }
        if (!node) RET_ERR(ENOENT);
        int fd = proc_open_fd(proc, node);
        if (fd < 0) { vfs_close(node); RET_ERR(ENFILE); }
        proc->fd_offsets[fd] = 0;
        return (uint64_t)fd;
    }

    /* ── 86: link — no hard links ────────────────────────────────────────── */
    case SYS_LINK:
        RET_ERR(EPERM);

    /* ── 88: symlink — no symlinks in VFS ───────────────────────────────── */
    case SYS_SYMLINK:
        RET_ERR(EPERM);

    /* ── 91: fchmod — no permission model ───────────────────────────────── */
    case SYS_FCHMOD:
        return 0;

    /* ── 93: fchown / 94: lchown — we're always root ────────────────────── */
    case SYS_FCHOWN: case SYS_LCHOWN:
        return 0;

    /* ── 98: getrusage ───────────────────────────────────────────────────── */
    case SYS_GETRUSAGE: {
        uint64_t *ru = (uint64_t *)(uintptr_t)a2;
        if (ru) { for (int i = 0; i < 18; i++) ru[i] = 0; }
        return 0;
    }

    /* ── 100: times ──────────────────────────────────────────────────────── */
    case SYS_TIMES: {
        uint64_t *tms = (uint64_t *)(uintptr_t)a1;
        if (tms) { tms[0] = tms[1] = tms[2] = tms[3] = 0; }
        return 0;
    }

    /* ── 101: ptrace — deny ───────────────────────────────────────────────── */
    case SYS_PTRACE:
        RET_ERR(EPERM);

    /* ── 103: syslog — no-op ──────────────────────────────────────────────── */
    case SYS_SYSLOG:
        return 0;

    /* ── UID/GID setters — we're always UID 0, accept silently ─────────── */
    case SYS_SETUID:    case SYS_SETGID:
    case SYS_SETREUID:  case SYS_SETREGID:
    case SYS_SETGROUPS:
    case SYS_SETRESUID: case SYS_SETRESGID:
    case SYS_SETFSUID:  case SYS_SETFSGID:
        return 0;

    /* ── 109: setpgid ────────────────────────────────────────────────────── */
    case SYS_SETPGID:
        return 0;

    /* ── 111: getpgrp ────────────────────────────────────────────────────── */
    case SYS_GETPGRP:
        return proc ? (uint64_t)proc->pid : 1;

    /* ── 118: getresuid ──────────────────────────────────────────────────── */
    case SYS_GETRESUID: {
        uint32_t *r = (uint32_t *)(uintptr_t)a1;
        uint32_t *e = (uint32_t *)(uintptr_t)a2;
        uint32_t *s = (uint32_t *)(uintptr_t)a3;
        if (r) *r = 0; if (e) *e = 0; if (s) *s = 0;
        return 0;
    }

    /* ── 120: getresgid ──────────────────────────────────────────────────── */
    case SYS_GETRESGID: {
        uint32_t *r = (uint32_t *)(uintptr_t)a1;
        uint32_t *e = (uint32_t *)(uintptr_t)a2;
        uint32_t *s = (uint32_t *)(uintptr_t)a3;
        if (r) *r = 0; if (e) *e = 0; if (s) *s = 0;
        return 0;
    }

    /* ── 121: getpgid ────────────────────────────────────────────────────── */
    case SYS_GETPGID:
        return proc ? (uint64_t)proc->pid : 1;

    /* ── 124: getsid ─────────────────────────────────────────────────────── */
    case SYS_GETSID:
        return proc ? (uint64_t)proc->pid : 1;

    /* ── 125: capget — full root caps ────────────────────────────────────── */
    case SYS_CAPGET: {
        uint32_t *d = (uint32_t *)(uintptr_t)a2;
        if (d) {
            for (int i = 0; i < 6; i++) d[i] = 0xFFFFFFFFu;
        }
        return 0;
    }

    /* ── 126: capset — accept ────────────────────────────────────────────── */
    case SYS_CAPSET:
        return 0;

    /* ── 127-130: more signal syscalls ───────────────────────────────────── */
    case SYS_RT_SIGPENDING:
        if (a1) *(uint64_t *)(uintptr_t)a1 = 0;
        return 0;
    case SYS_RT_SIGTIMEDWAIT:
        RET_ERR(EINTR);
    case SYS_RT_SIGQUEUEINFO:
        return 0;
    case SYS_RT_SIGSUSPEND:
        RET_ERR(EINTR);

    /* ── 132: utime — accept silently ────────────────────────────────────── */
    case SYS_UTIME:
        return 0;

    /* ── 133: mknod ──────────────────────────────────────────────────────── */
    case SYS_MKNOD: {
        const char *path = (const char *)(uintptr_t)a1;
        if (!path) RET_ERR(EFAULT);
        vfs_create(path, 0);
        return 0;
    }

    /* ── 135: personality — PER_LINUX ───────────────────────────────────── */
    case SYS_PERSONALITY:
        return 0;

    /* ── 137: statfs ─────────────────────────────────────────────────────── */
    case SYS_STATFS: {
        /* struct statfs layout: f_type, f_bsize, f_blocks, f_bfree,
           f_bavail, f_files, f_ffree, f_fsid(2×u64), f_namelen ... */
        uint64_t *sf = (uint64_t *)(uintptr_t)a2;
        if (!sf) RET_ERR(EFAULT);
        for (int i = 0; i < 15; i++) sf[i] = 0;
        sf[0] = 0xEF53;        /* EXT2_SUPER_MAGIC   */
        sf[1] = 4096;          /* f_bsize             */
        sf[2] = 1024*1024;     /* f_blocks (4 GB)     */
        sf[3] = 512*1024;      /* f_bfree             */
        sf[4] = 512*1024;      /* f_bavail            */
        sf[5] = 65536;         /* f_files             */
        sf[6] = 32768;         /* f_ffree             */
        sf[8] = 255;           /* f_namelen (at [8] after 2×u64 fsid) */
        return 0;
    }

    /* ── 138: fstatfs ────────────────────────────────────────────────────── */
    case SYS_FSTATFS: {
        uint64_t *sf = (uint64_t *)(uintptr_t)a2;
        if (!sf) RET_ERR(EFAULT);
        for (int i = 0; i < 15; i++) sf[i] = 0;
        sf[0] = 0xEF53; sf[1] = 4096; sf[2] = 1024*1024;
        sf[3] = 512*1024; sf[4] = 512*1024;
        sf[5] = 65536;   sf[6] = 32768; sf[8] = 255;
        return 0;
    }

    /* ── 140-141: getpriority / setpriority ──────────────────────────────── */
    case SYS_GETPRIORITY: return 0;
    case SYS_SETPRIORITY: return 0;

    /* ── 142-148: POSIX scheduler calls — all succeed ───────────────────── */
    case SYS_SCHED_SETPARAM:   case SYS_SCHED_GETPARAM:
    case SYS_SCHED_SETSCHEDULER: case SYS_SCHED_GETSCHEDULER:
        return 0;
    case SYS_SCHED_GET_PRIORITY_MAX: return 99;
    case SYS_SCHED_GET_PRIORITY_MIN: return 1;
    case SYS_SCHED_RR_GET_INTERVAL: {
        linux_timespec_t *ts = (linux_timespec_t *)(uintptr_t)a2;
        if (ts) { ts->tv_sec = 0; ts->tv_nsec = 10000000LL; }
        return 0;
    }

    /* ── 149-152: mlock family — no-op ───────────────────────────────────── */
    case SYS_MLOCK: case SYS_MUNLOCK:
    case SYS_MLOCKALL: case SYS_MUNLOCKALL:
        return 0;

    /* ── 160: setrlimit — accept ─────────────────────────────────────────── */
    case SYS_SETRLIMIT:
        return 0;

    /* ── 161: chroot ─────────────────────────────────────────────────────── */
    case SYS_CHROOT: {
        const char *path = (const char *)(uintptr_t)a1;
        if (!path) RET_ERR(EFAULT);
        if (proc) sc_strcpy(proc->cwd, path, sizeof(proc->cwd));
        return 0;
    }

    /* ── 162: sync / 163: acct — no-op ──────────────────────────────────── */
    case SYS_SYNC: case SYS_ACCT:
        return 0;

    /* ── 169: reboot ─────────────────────────────────────────────────────── */
    case SYS_REBOOT: {
        /* QEMU ACPI shutdown via port 0x604, else keyboard controller reset */
        __asm__ volatile("outw %0, %1" :: "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
        __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0xFE),    "Nd"((uint8_t)0x64));
        return 0;
    }

    /* ── 170: sethostname — accept ───────────────────────────────────────── */
    case SYS_SETHOSTNAME:
        return 0;

    /* ── 186: gettid — return PID (single-threaded) ─────────────────────── */
    case SYS_GETTID:
        return proc ? (uint64_t)proc->pid : 1;

    /* ── 200: tkill ──────────────────────────────────────────────────────── */
    case SYS_TKILL: {
        int tid = (int)a1;
        int sig = (int)a2;
        if (sig == 9 || sig == 15) proc_kill((uint32_t)tid);
        return 0;
    }

    /* ── 201: time(t) ────────────────────────────────────────────────────── */
    case SYS_TIME: {
        /* Approximate: ticks since boot / 1000 = seconds since boot */
        uint64_t t = timer_get_ticks() / 1000;
        if (a1) *(uint64_t *)(uintptr_t)a1 = t;
        return t;
    }

    /* ── 203-204: sched affinity — single CPU ────────────────────────────── */
    case SYS_SCHED_SETAFFINITY:
        return 0;
    case SYS_SCHED_GETAFFINITY: {
        uint64_t *mask = (uint64_t *)(uintptr_t)a3;
        if (mask) *mask = 1;
        return 0;
    }

    /* ── 213 / 291: epoll_create / epoll_create1 — dummy fd ─────────────── */
    case SYS_EPOLL_CREATE: case SYS_EPOLL_CREATE1: {
        if (!proc) RET_ERR(ENOMEM);
        vfs_node_t *node = vfs_open("/dev/null", 0);
        if (!node) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, node);
        if (fd < 0) { vfs_close(node); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 229: clock_getres — 1 ms resolution ────────────────────────────── */
    case SYS_CLOCK_GETRES: {
        linux_timespec_t *ts = (linux_timespec_t *)(uintptr_t)a2;
        if (ts) { ts->tv_sec = 0; ts->tv_nsec = 1000000LL; }
        return 0;
    }

    /* ── 230: clock_nanosleep ────────────────────────────────────────────── */
    case SYS_CLOCK_NANOSLEEP: {
        linux_timespec_t *rqtp = (linux_timespec_t *)(uintptr_t)a3;
        if (rqtp) {
            uint64_t ms = (uint64_t)rqtp->tv_sec * 1000
                        + (uint64_t)rqtp->tv_nsec / 1000000ULL;
            if (ms > 0) timer_sleep_ms((uint32_t)ms);
        }
        return 0;
    }

    /* ── 232: epoll_wait — no events ────────────────────────────────────── */
    case SYS_EPOLL_WAIT:
        return 0;

    /* ── 233: epoll_ctl — accept ─────────────────────────────────────────── */
    case SYS_EPOLL_CTL:
        return 0;

    /* ── 234: tgkill ─────────────────────────────────────────────────────── */
    case SYS_TGKILL: {
        int tid = (int)a2;
        int sig = (int)a3;
        if (sig == 9 || sig == 15) proc_kill((uint32_t)tid);
        return 0;
    }

    /* ── 235: utimes — accept ────────────────────────────────────────────── */
    case SYS_UTIMES:
        return 0;

    /* ── 257: openat ─────────────────────────────────────────────────────── */
    case SYS_OPENAT: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        int         flags = (int)a3;
        if (!path || !proc) RET_ERR(EFAULT);
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        vfs_node_t *node = vfs_open(rp, flags);
        if (!node && (flags & 0x40)) {          /* O_CREAT */
            vfs_create(rp, 0);
            node = vfs_open(rp, flags);
        }
        if (!node) RET_ERR(ENOENT);
        int fd = proc_open_fd(proc, node);
        if (fd < 0) { vfs_close(node); RET_ERR(ENFILE); }
        proc->fd_offsets[fd] = 0;
        return (uint64_t)fd;
    }

    /* ── 258: mkdirat ────────────────────────────────────────────────────── */
    case SYS_MKDIRAT: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        if (!path) RET_ERR(EFAULT);
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        return (vfs_mkdir(rp) == 0) ? 0 : (uint64_t)(-EEXIST);
    }

    /* ── 259: mknodat ────────────────────────────────────────────────────── */
    case SYS_MKNODAT: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        if (!path) RET_ERR(EFAULT);
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        vfs_create(rp, 0);
        return 0;
    }

    /* ── 260: fchownat — no-op ───────────────────────────────────────────── */
    case SYS_FCHOWNAT:
        return 0;

    /* ── 262: newfstatat ─────────────────────────────────────────────────── */
    case SYS_NEWFSTATAT: {
        int           dirfd = (int)a1;
        const char   *path  = (const char *)(uintptr_t)a2;
        linux_stat_t *ls    = (linux_stat_t *)(uintptr_t)a3;
        /* int flags = (int)a4; */
        if (!ls) RET_ERR(EFAULT);
        /* AT_EMPTY_PATH (0x1000): fstat on dirfd itself */
        if (!path || path[0] == '\0') {
            if (proc && dirfd >= 0 && dirfd < MAX_FDS && proc->fds[dirfd]) {
                fill_stat_from_node(ls, proc->fds[dirfd]);
                return 0;
            }
            RET_ERR(EBADF);
        }
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        vfs_stat_t vs;
        if (vfs_stat(rp, &vs) != 0) RET_ERR(ENOENT);
        fill_stat_from_vstat(ls, &vs);
        return 0;
    }

    /* ── 263: unlinkat ───────────────────────────────────────────────────── */
    case SYS_UNLINKAT: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        int         flags = (int)a3;
        if (!path) RET_ERR(EFAULT);
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        if (flags & 0x200) {           /* AT_REMOVEDIR */
            vfs_node_t *dn = vfs_open(rp, 0);
            if (!dn) RET_ERR(ENOENT);
            if (!(dn->type & VFS_NODE_DIR)) { vfs_close(dn); RET_ERR(ENOTDIR); }
            vfs_close(dn);
        }
        return (vfs_unlink(rp) == 0) ? 0 : (uint64_t)(-ENOENT);
    }

    /* ── 264: renameat ───────────────────────────────────────────────────── */
    case SYS_RENAMEAT: {
        int         olddirfd = (int)a1;
        const char *oldpath  = (const char *)(uintptr_t)a2;
        int         newdirfd = (int)a3;
        const char *newpath  = (const char *)(uintptr_t)a4;
        if (!oldpath || !newpath) RET_ERR(EFAULT);
        char op[512], np[512];
        if (at_resolve(proc, olddirfd, oldpath, op, sizeof(op)) < 0) RET_ERR(ENOENT);
        if (at_resolve(proc, newdirfd, newpath, np, sizeof(np)) < 0) RET_ERR(ENOENT);
        /* Implement via copy + delete (matching SYS_RENAME behaviour) */
        return syscall_dispatch(SYS_RENAME,
                                (uint64_t)(uintptr_t)op,
                                (uint64_t)(uintptr_t)np,
                                0, 0, 0);
    }

    /* ── 265: linkat — no hard links ─────────────────────────────────────── */
    case SYS_LINKAT:
        RET_ERR(EPERM);

    /* ── 266: symlinkat — no symlinks ────────────────────────────────────── */
    case SYS_SYMLINKAT:
        RET_ERR(EPERM);

    /* ── 267: readlinkat ─────────────────────────────────────────────────── */
    case SYS_READLINKAT: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        char       *buf   = (char *)(uintptr_t)a3;
        size_t      bufsz = (size_t)a4;
        if (!path || !buf) RET_ERR(EFAULT);
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        size_t n = sc_strlen(rp);
        if (n > bufsz) n = bufsz;
        for (size_t i = 0; i < n; i++) buf[i] = rp[i];
        return (int64_t)n;
    }

    /* ── 268: fchmodat — no-op ───────────────────────────────────────────── */
    case SYS_FCHMODAT:
        return 0;

    /* ── 269: faccessat ──────────────────────────────────────────────────── */
    case SYS_FACCESSAT: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        if (!path) RET_ERR(EFAULT);
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        vfs_node_t *node = vfs_open(rp, 0);
        if (!node) RET_ERR(ENOENT);
        vfs_close(node);
        return 0;
    }

    /* ── 270: pselect6 — instant poll, no blocking ───────────────────────── */
    case SYS_PSELECT6:
        return 0;

    /* ── 271: ppoll ──────────────────────────────────────────────────────── */
    case SYS_PPOLL: {
        typedef struct { int fd; short events; short revents; } pollfd_t;
        pollfd_t *pfds = (pollfd_t *)(uintptr_t)a1;
        int nfds = (int)a2;
        if (!pfds || nfds <= 0) return 0;
        int ready = 0;
        for (int i = 0; i < nfds; i++) {
            pfds[i].revents = 0;
            if (proc && pfds[i].fd >= 0 && pfds[i].fd < MAX_FDS
                && proc->fds[pfds[i].fd]) {
                if (pfds[i].events & 0x01) { pfds[i].revents |= 0x01; ready++; }
                if (pfds[i].events & 0x04) { pfds[i].revents |= 0x04; ready++; }
            }
        }
        return ready;
    }

    /* ── 272: unshare — no-op ────────────────────────────────────────────── */
    case SYS_UNSHARE:
        return 0;

    /* ── 280: utimensat — accept ─────────────────────────────────────────── */
    case SYS_UTIMENSAT:
        return 0;

    /* ── 285: fallocate — extend file size ───────────────────────────────── */
    case SYS_FALLOCATE: {
        int     fd  = (int)a1;
        int64_t off = (int64_t)a2;
        int64_t len = (int64_t)a3;
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        uint64_t new_end = (uint64_t)(off + len);
        if (proc->fds[fd]->size < new_end) proc->fds[fd]->size = new_end;
        return 0;
    }

    /* ── 290: eventfd2 — return dummy fd ─────────────────────────────────── */
    case SYS_EVENTFD2: {
        if (!proc) RET_ERR(ENOMEM);
        vfs_node_t *node = vfs_open("/dev/null", 0);
        if (!node) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, node);
        if (fd < 0) { vfs_close(node); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 292: dup3 ───────────────────────────────────────────────────────── */
    case SYS_DUP3: {
        int oldfd = (int)a1;
        int newfd = (int)a2;
        /* int flags = (int)a3; O_CLOEXEC — ignore for now */
        if (!proc || oldfd < 0 || oldfd >= MAX_FDS || !proc->fds[oldfd])
            RET_ERR(EBADF);
        if (newfd < 0 || newfd >= MAX_FDS) RET_ERR(EBADF);
        if (oldfd == newfd) RET_ERR(EINVAL);
        if (proc->fds[newfd]) proc_close_fd(proc, newfd);
        proc->fds[newfd]        = proc->fds[oldfd];
        proc->fd_offsets[newfd] = proc->fd_offsets[oldfd];
        return (uint64_t)newfd;
    }

    /* ── 293: pipe2 — delegate to pipe (ignore flags) ────────────────────── */
    case SYS_PIPE2:
        return syscall_dispatch(SYS_PIPE, a1, 0, 0, 0, 0);

    /* ── 302: prlimit64 ──────────────────────────────────────────────────── */
    case SYS_PRLIMIT64: {
        /* pid=a1, resource=a2, new_limit=a3(ignored), old_limit=a4 */
        struct rlim64 { uint64_t cur; uint64_t max; };
        struct rlim64 *old = (struct rlim64 *)(uintptr_t)a4;
        if (old) {
            switch ((int)a2) {
            case 3:  /* RLIMIT_STACK   */
                old->cur = 8ULL * 1024 * 1024;
                old->max = 0xFFFFFFFFFFFFFFFFULL;
                break;
            case 7:  /* RLIMIT_NOFILE  */
                old->cur = MAX_FDS;
                old->max = MAX_FDS;
                break;
            case 9:  /* RLIMIT_MEMLOCK */
                old->cur = 64 * 1024;
                old->max = 64 * 1024;
                break;
            default:
                old->cur = 0xFFFFFFFFFFFFFFFFULL;
                old->max = 0xFFFFFFFFFFFFFFFFULL;
                break;
            }
        }
        return 0;
    }

    /* ── 318: getrandom — RDTSC-based entropy ────────────────────────────── */
    case SYS_GETRANDOM: {
        uint8_t *buf   = (uint8_t *)(uintptr_t)a1;
        size_t   count = (size_t)a2;
        if (!buf) RET_ERR(EFAULT);
        for (size_t i = 0; i < count; i++) {
            uint32_t lo, hi;
            __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
            uint64_t tsc = ((uint64_t)hi << 32) | lo;
            buf[i] = (uint8_t)((tsc ^ (tsc >> 17) ^ ((uint64_t)i * 0x9E3779B9ULL)) & 0xFF);
        }
        return (int64_t)count;
    }

    /* ── 322: execveat — resolve path, delegate to execve ────────────────── */
    case SYS_EXECVEAT: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        if (!path) RET_ERR(EFAULT);
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        return syscall_dispatch(SYS_EXECVE,
                                (uint64_t)(uintptr_t)rp,
                                a3, a4, 0, 0);
    }

    /* ── 332: statx — fill from VFS stat ─────────────────────────────────── */
    case SYS_STATX: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        /* int flags = (int)a3;  uint32_t mask = (uint32_t)a4; */
        uint8_t    *sx    = (uint8_t *)(uintptr_t)a5;
        if (!sx) RET_ERR(EFAULT);
        for (int i = 0; i < 256; i++) sx[i] = 0;
        linux_stat_t ls;
        if (!path || path[0] == '\0') {
            if (proc && dirfd >= 0 && dirfd < MAX_FDS && proc->fds[dirfd])
                fill_stat_from_node(&ls, proc->fds[dirfd]);
            else
                RET_ERR(EBADF);
        } else {
            char rp[512];
            if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
            vfs_stat_t vs;
            if (vfs_stat(rp, &vs) != 0) RET_ERR(ENOENT);
            fill_stat_from_vstat(&ls, &vs);
        }
        /* statx layout: stx_mask(u32), stx_blksize(u32), stx_attributes(u64),
           stx_nlink(u32), stx_uid(u32), stx_gid(u32), stx_mode(u16), pad(u16),
           stx_ino(u64), stx_size(u64), stx_blocks(u64) ... */
        uint32_t *u32 = (uint32_t *)sx;
        uint64_t *u64 = (uint64_t *)sx;
        u32[0] = 0x7FFu;                        /* stx_mask: all valid */
        u32[1] = (uint32_t)ls.st_blksize;       /* stx_blksize */
        /* u64[1] = stx_attributes = 0 */
        u32[4] = (uint32_t)ls.st_nlink;         /* stx_nlink */
        u32[5] = 0;                              /* stx_uid */
        u32[6] = 0;                              /* stx_gid */
        *(uint16_t *)&u32[7] = (uint16_t)ls.st_mode; /* stx_mode */
        u64[4] = (uint64_t)ls.st_ino;           /* stx_ino */
        u64[5] = (uint64_t)ls.st_size;          /* stx_size */
        u64[6] = (uint64_t)ls.st_blocks;        /* stx_blocks */
        return 0;
    }

    /* ════════════════════════════════════════════════════════════════════════
     * COMPLETE LINUX x86_64 SYSCALL TABLE — remaining entries 134–462
     * ════════════════════════════════════════════════════════════════════════ */

    /* ── 134: uselib — obsolete, always ENOSYS ───────────────────────────── */
    case SYS_USELIB:
        RET_ERR(ENOSYS);

    /* ── 136: ustat — obsolete, use statfs instead ───────────────────────── */
    case SYS_USTAT:
        RET_ERR(ENOSYS);

    /* ── 139: sysfs — filesystem type info (no loadable fs) ─────────────── */
    case SYS_SYSFS_SC:
        RET_ERR(ENOSYS);

    /* ── 153: vhangup — no TTY hangup, succeed silently ─────────────────── */
    case SYS_VHANGUP:
        return 0;

    /* ── 154: modify_ldt — LDT not used in 64-bit NexOS ─────────────────── */
    case SYS_MODIFY_LDT:
        RET_ERR(ENOSYS);

    /* ── 155: pivot_root — no namespaces ─────────────────────────────────── */
    case SYS_PIVOT_ROOT:
        RET_ERR(ENOSYS);

    /* ── 156: _sysctl — deprecated, use /proc/sys ────────────────────────── */
    case SYS__SYSCTL:
        RET_ERR(ENOSYS);

    /* ── 159: adjtimex — no NTP PLL, succeed silently ───────────────────── */
    case SYS_ADJTIMEX:
        return 0;

    /* ── 164: settimeofday — accept, ignore ──────────────────────────────── */
    case SYS_SETTIMEOFDAY:
        return 0;

    /* ── 165: mount ──────────────────────────────────────────────────────── */
    case SYS_MOUNT:
        return 0;   /* NexOS VFS auto-mounts; silently succeed */

    /* ── 166: umount2 ────────────────────────────────────────────────────── */
    case SYS_UMOUNT2:
        return 0;

    /* ── 167: swapon ─────────────────────────────────────────────────────── */
    case SYS_SWAPON:
        RET_ERR(ENOSYS);

    /* ── 168: swapoff ────────────────────────────────────────────────────── */
    case SYS_SWAPOFF:
        RET_ERR(ENOSYS);

    /* ── 171: setdomainname — accept ─────────────────────────────────────── */
    case SYS_SETDOMAINNAME:
        return 0;

    /* ── 172: iopl — ring-0 already ─────────────────────────────────────── */
    case SYS_IOPL:
        return 0;

    /* ── 173: ioperm — allow all ports (we're ring 0) ───────────────────── */
    case SYS_IOPERM:
        return 0;

    /* ── 174–178: module management — no runtime loading ─────────────────── */
    case SYS_CREATE_MODULE:
    case SYS_GET_KERNEL_SYMS:
    case SYS_QUERY_MODULE:
        RET_ERR(ENOSYS);

    /* ── 175: init_module / 176: delete_module ───────────────────────────── */
    case SYS_INIT_MODULE:
    case SYS_DELETE_MODULE:
        RET_ERR(ENOSYS);

    /* ── 179: quotactl — no disk quotas ──────────────────────────────────── */
    case SYS_QUOTACTL:
        RET_ERR(ENOSYS);

    /* ── 180–185: obsolete / unavailable ─────────────────────────────────── */
    case SYS_NFSSERVCTL:
    case SYS_GETPMSG:
    case SYS_PUTPMSG:
    case SYS_AFS_SYSCALL:
    case SYS_TUXCALL:
    case SYS_SECURITY:
        RET_ERR(ENOSYS);

    /* ── 187: readahead — no page cache, succeed as no-op ───────────────── */
    case SYS_READAHEAD:
        return 0;

    /* ── 188–190: setxattr / lsetxattr / fsetxattr — no xattr support ───── */
    case SYS_SETXATTR:
    case SYS_LSETXATTR:
    case SYS_FSETXATTR:
        return 0;   /* silently discard extended attributes */

    /* ── 191–193: getxattr / lgetxattr / fgetxattr ───────────────────────── */
    case SYS_GETXATTR:
    case SYS_LGETXATTR:
    case SYS_FGETXATTR:
        RET_ERR(ENODATA);   /* attribute does not exist */

    /* ── 194–196: listxattr / llistxattr / flistxattr ────────────────────── */
    case SYS_LISTXATTR:
    case SYS_LLISTXATTR:
    case SYS_FLISTXATTR:
        return 0;   /* empty attribute list */

    /* ── 197–199: removexattr / lremovexattr / fremovexattr ─────────────── */
    case SYS_REMOVEXATTR:
    case SYS_LREMOVEXATTR:
    case SYS_FREMOVEXATTR:
        return 0;

    /* ── 205: set_thread_area — no 32-bit TLS segments ──────────────────── */
    case SYS_SET_THREAD_AREA:
        return 0;

    /* ── 206–210: Linux AIO — ENOSYS (use io_uring instead) ─────────────── */
    case SYS_IO_SETUP:
    case SYS_IO_DESTROY:
    case SYS_IO_GETEVENTS:
    case SYS_IO_SUBMIT:
    case SYS_IO_CANCEL:
        RET_ERR(ENOSYS);

    /* ── 211: get_thread_area — not used in 64-bit ───────────────────────── */
    case SYS_GET_THREAD_AREA:
        RET_ERR(ENOSYS);

    /* ── 212: lookup_dcookie — not used ──────────────────────────────────── */
    case SYS_LOOKUP_DCOOKIE:
        RET_ERR(ENOSYS);

    /* ── 214–215: old epoll_ctl/wait — use 233/232 ───────────────────────── */
    case SYS_EPOLL_CTL_OLD:
    case SYS_EPOLL_WAIT_OLD:
        RET_ERR(ENOSYS);

    /* ── 216: remap_file_pages — deprecated, no-op ───────────────────────── */
    case SYS_REMAP_FILE_PAGES:
        return 0;

    /* ── 219: restart_syscall — no signals to restart after ─────────────── */
    case SYS_RESTART_SYSCALL:
        return 0;

    /* ── 220: semtimedop — SysV sem with timeout ─────────────────────────── */
    case SYS_SEMTIMEDOP:
        RET_ERR(ENOSYS);

    /* ── 221: fadvise64 — no page cache to advise ────────────────────────── */
    case SYS_FADVISE64:
        return 0;

    /* ── 222–226: POSIX per-process timers (stub counter) ────────────────── */
    case SYS_TIMER_CREATE: {
        /* Write a fake timer ID (0) to the caller's pointer */
        int *tidp = (int *)(uintptr_t)a2;
        if (tidp) *tidp = 0;
        return 0;
    }
    case SYS_TIMER_SETTIME:
    case SYS_TIMER_GETTIME:
    case SYS_TIMER_DELETE:
        return 0;

    case SYS_TIMER_GETOVERRUN:
        return 0;   /* 0 overruns */

    /* ── 227: clock_settime — accept, ignore ─────────────────────────────── */
    case SYS_CLOCK_SETTIME:
        return 0;

    /* ── 236: vserver — never implemented ────────────────────────────────── */
    case SYS_VSERVER:
        RET_ERR(ENOSYS);

    /* ── 237–239: NUMA memory policy — single-node, no-op ───────────────── */
    case SYS_MBIND:
    case SYS_SET_MEMPOLICY:
    case SYS_GET_MEMPOLICY:
        return 0;

    /* ── 240–245: POSIX message queues ───────────────────────────────────── */
    case SYS_MQ_OPEN:
    case SYS_MQ_UNLINK:
    case SYS_MQ_TIMEDSEND:
    case SYS_MQ_TIMEDRECEIVE:
    case SYS_MQ_NOTIFY:
    case SYS_MQ_GETSETATTR:
        RET_ERR(ENOSYS);

    /* ── 246: kexec_load ─────────────────────────────────────────────────── */
    case SYS_KEXEC_LOAD:
        RET_ERR(ENOSYS);

    /* ── 247: waitid ─────────────────────────────────────────────────────── */
    case SYS_WAITID: {
        int idtype = (int)a1;
        /* id=a2, siginfo*=a3, options=a4 */
        (void)idtype;
        /* Scan for any zombie */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            process_t *p = processes[i];
            if (!p || p->state != PROC_ZOMBIE) continue;
            if (!proc || p->ppid != proc->pid) continue;
            int cpid = (int)p->pid;
            processes[i] = NULL;
            process_count--;
            kfree(p->stack);
            kfree(p);
            return (uint64_t)cpid;
        }
        RET_ERR(ECHILD);
    }

    /* ── 248–250: kernel keyring ─────────────────────────────────────────── */
    case SYS_ADD_KEY:
    case SYS_REQUEST_KEY:
    case SYS_KEYCTL:
        RET_ERR(ENOSYS);

    /* ── 251–252: I/O priority ───────────────────────────────────────────── */
    case SYS_IOPRIO_SET:
    case SYS_IOPRIO_GET:
        return 0;

    /* ── 253: inotify_init — return a dummy fd ───────────────────────────── */
    case SYS_INOTIFY_INIT: {
        if (!proc) RET_ERR(ENOMEM);
        vfs_node_t *n = vfs_open("/dev/null", 0);
        if (!n) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, n);
        if (fd < 0) { vfs_close(n); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 254: inotify_add_watch — return watch descriptor 1 ─────────────── */
    case SYS_INOTIFY_ADD_WATCH:
        return 1;   /* wd=1, events never fire (no inotify engine) */

    /* ── 255: inotify_rm_watch ───────────────────────────────────────────── */
    case SYS_INOTIFY_RM_WATCH:
        return 0;

    /* ── 256: migrate_pages — single NUMA node, trivially succeed ────────── */
    case SYS_MIGRATE_PAGES:
        return 0;

    /* ── 261: futimesat — update file timestamps (stub) ─────────────────── */
    case SYS_FUTIMESAT:
        return 0;

    /* ── 274: get_robust_list — no futex robust lists ───────────────────── */
    case SYS_GET_ROBUST_LIST: {
        /* head_ptr=a2, len_ptr=a3 */
        uint64_t *hp = (uint64_t *)(uintptr_t)a2;
        uint64_t *lp = (uint64_t *)(uintptr_t)a3;
        if (hp) *hp = 0;
        if (lp) *lp = 0;
        return 0;
    }

    /* ── 275: splice(fd_in, off_in, fd_out, off_out, len, flags) ─────────── */
    case SYS_SPLICE: {
        int      in_fd  = (int)a1;
        int      out_fd = (int)a3;
        uint64_t len    = a5;
        if (!proc) RET_ERR(EBADF);
        if (in_fd  < 0 || in_fd  >= MAX_FDS || !proc->fds[in_fd])  RET_ERR(EBADF);
        if (out_fd < 0 || out_fd >= MAX_FDS || !proc->fds[out_fd]) RET_ERR(EBADF);
        uint8_t buf[512];
        uint64_t total = 0;
        while (total < len) {
            uint32_t chunk = (uint32_t)(len - total);
            if (chunk > 512) chunk = 512;
            uint32_t n = vfs_read(proc->fds[in_fd],
                                   proc->fd_offsets[in_fd], chunk, buf);
            if (!n) break;
            proc->fd_offsets[in_fd] += n;
            vfs_write(proc->fds[out_fd], proc->fd_offsets[out_fd], n, buf);
            proc->fd_offsets[out_fd] += n;
            total += n;
        }
        return total;
    }

    /* ── 276: tee — pipe mirroring, no pipe backend ──────────────────────── */
    case SYS_TEE:
        RET_ERR(ENOSYS);

    /* ── 277: sync_file_range — VFS has no dirty tracking ───────────────── */
    case SYS_SYNC_FILE_RANGE:
        return 0;

    /* ── 278: vmsplice — no pipe backend ────────────────────────────────── */
    case SYS_VMSPLICE:
        RET_ERR(ENOSYS);

    /* ── 279: move_pages — single NUMA node ──────────────────────────────── */
    case SYS_MOVE_PAGES:
        return 0;

    /* ── 281: epoll_pwait — same as epoll_wait (no events) ──────────────── */
    case SYS_EPOLL_PWAIT:
        return 0;

    /* ── 282: signalfd — dummy fd (no signal delivery) ───────────────────── */
    case SYS_SIGNALFD: {
        if (!proc) RET_ERR(ENOMEM);
        /* reuse existing fd if one was passed (a1 >= 0) */
        int oldfd = (int)a1;
        if (oldfd >= 0 && oldfd < MAX_FDS && proc->fds[oldfd])
            return (uint64_t)oldfd;
        vfs_node_t *n = vfs_open("/dev/null", 0);
        if (!n) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, n);
        if (fd < 0) { vfs_close(n); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 283: timerfd_create — dummy fd ──────────────────────────────────── */
    case SYS_TIMERFD_CREATE: {
        if (!proc) RET_ERR(ENOMEM);
        vfs_node_t *n = vfs_open("/dev/null", 0);
        if (!n) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, n);
        if (fd < 0) { vfs_close(n); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 284: eventfd — dummy fd ─────────────────────────────────────────── */
    case SYS_EVENTFD: {
        if (!proc) RET_ERR(ENOMEM);
        vfs_node_t *n = vfs_open("/dev/null", 0);
        if (!n) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, n);
        if (fd < 0) { vfs_close(n); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 286: timerfd_settime ────────────────────────────────────────────── */
    case SYS_TIMERFD_SETTIME:
        return 0;

    /* ── 287: timerfd_gettime ────────────────────────────────────────────── */
    case SYS_TIMERFD_GETTIME: {
        linux_timespec_t *ts = (linux_timespec_t *)(uintptr_t)a2;
        if (ts) { ts->tv_sec = 0; ts->tv_nsec = 0; }
        return 0;
    }

    /* ── 288: accept4 — same as accept, ignore flags ─────────────────────── */
    case SYS_ACCEPT4:
        RET_ERR(ENOSYS);

    /* ── 289: signalfd4 — same as signalfd ───────────────────────────────── */
    case SYS_SIGNALFD4: {
        if (!proc) RET_ERR(ENOMEM);
        int oldfd = (int)a1;
        if (oldfd >= 0 && oldfd < MAX_FDS && proc->fds[oldfd])
            return (uint64_t)oldfd;
        vfs_node_t *n = vfs_open("/dev/null", 0);
        if (!n) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, n);
        if (fd < 0) { vfs_close(n); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 294: inotify_init1 — same as inotify_init ───────────────────────── */
    case SYS_INOTIFY_INIT1: {
        if (!proc) RET_ERR(ENOMEM);
        vfs_node_t *n = vfs_open("/dev/null", 0);
        if (!n) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, n);
        if (fd < 0) { vfs_close(n); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 295: preadv(fd, iov, iovcnt, offset_lo, offset_hi) ─────────────── */
    case SYS_PREADV: {
        typedef struct { void *base; uint64_t len; } iovec_t;
        int             fd     = (int)a1;
        const iovec_t  *iov    = (const iovec_t *)(uintptr_t)a2;
        int             iovcnt = (int)a3;
        uint64_t        off    = a4;
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        uint64_t total = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (!iov[i].base || !iov[i].len) continue;
            uint32_t n = vfs_read(proc->fds[fd], off,
                                   (uint32_t)iov[i].len,
                                   (uint8_t *)iov[i].base);
            off   += n;
            total += n;
        }
        return total;
    }

    /* ── 296: pwritev(fd, iov, iovcnt, offset_lo, offset_hi) ────────────── */
    case SYS_PWRITEV: {
        typedef struct { const void *base; uint64_t len; } iovecw_t;
        int              fd     = (int)a1;
        const iovecw_t  *iov    = (const iovecw_t *)(uintptr_t)a2;
        int              iovcnt = (int)a3;
        uint64_t         off    = a4;
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        uint64_t total = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (!iov[i].base || !iov[i].len) continue;
            uint32_t n = vfs_write(proc->fds[fd], off,
                                    (uint32_t)iov[i].len,
                                    (const uint8_t *)iov[i].base);
            off   += n;
            total += n;
        }
        return total;
    }

    /* ── 297: rt_tgsigqueueinfo ──────────────────────────────────────────── */
    case SYS_RT_TGSIGQUEUEINFO:
        return 0;

    /* ── 298: perf_event_open — no performance counters ─────────────────── */
    case SYS_PERF_EVENT_OPEN:
        RET_ERR(ENOSYS);

    /* ── 299: recvmmsg — multiple messages, no socket backend ───────────── */
    case SYS_RECVMMSG:
        RET_ERR(ENOSYS);

    /* ── 301: fanotify_mark ──────────────────────────────────────────────── */
    case SYS_FANOTIFY_MARK:
        RET_ERR(ENOSYS);

    /* ── 303: name_to_handle_at / 304: open_by_handle_at ─────────────────── */
    case SYS_NAME_TO_HANDLE_AT:
    case SYS_OPEN_BY_HANDLE_AT:
        RET_ERR(ENOSYS);

    /* ── 305: clock_adjtime ──────────────────────────────────────────────── */
    case SYS_CLOCK_ADJTIME:
        return 0;

    /* ── 306: syncfs — sync a single filesystem ──────────────────────────── */
    case SYS_SYNCFS:
        return 0;

    /* ── 307: sendmmsg — multiple UDP messages, no socket backend ────────── */
    case SYS_SENDMMSG:
        RET_ERR(ENOSYS);

    /* ── 308: setns — no namespaces ──────────────────────────────────────── */
    case SYS_SETNS:
        RET_ERR(ENOSYS);

    /* ── 309: getcpu(cpu*, node*, tcache*) — always CPU 0, node 0 ────────── */
    case SYS_GETCPU: {
        uint32_t *cpu  = (uint32_t *)(uintptr_t)a1;
        uint32_t *node = (uint32_t *)(uintptr_t)a2;
        if (cpu)  *cpu  = 0;
        if (node) *node = 0;
        return 0;
    }

    /* ── 310–311: process_vm_readv / process_vm_writev ───────────────────── */
    case SYS_PROCESS_VM_READV:
    case SYS_PROCESS_VM_WRITEV:
        RET_ERR(ENOSYS);

    /* ── 312: kcmp — no process isolation ───────────────────────────────── */
    case SYS_KCMP:
        RET_ERR(ENOSYS);

    /* ── 313: finit_module — no runtime module loading ───────────────────── */
    case SYS_FINIT_MODULE:
        RET_ERR(ENOSYS);

    /* ── 314–315: extended scheduler attributes ──────────────────────────── */
    case SYS_SCHED_SETATTR:
        return 0;
    case SYS_SCHED_GETATTR: {
        /* struct sched_attr: u32 size, u32 sched_policy, u64 flags, ... */
        uint32_t *sa = (uint32_t *)(uintptr_t)a2;
        if (sa) {
            for (int i = 0; i < 14; i++) sa[i] = 0;
            sa[0] = 56;   /* size of struct sched_attr */
        }
        return 0;
    }

    /* ── 316: renameat2 — delegate to renameat (ignore flags) ────────────── */
    case SYS_RENAMEAT2:
        return syscall_dispatch(SYS_RENAMEAT, a1, a2, a3, a4, 0);

    /* ── 317: seccomp — return 0 (no restrictions) ───────────────────────── */
    case SYS_SECCOMP:
        return 0;

    /* ── 319: memfd_create — anonymous in-memory file ────────────────────── */
    case SYS_MEMFD_CREATE: {
        if (!proc) RET_ERR(ENOMEM);
        /* Create a transient VFS node under /tmp */
        const char *name = (const char *)(uintptr_t)a1;
        char path[128];
        if (!name) name = "memfd";
        sc_strcpy(path, "/tmp/memfd_", sizeof(path));
        /* Append a simple counter */
        static uint32_t mfd_seq = 0;
        uint32_t seq = mfd_seq++;
        uint32_t pos = (uint32_t)sc_strlen(path);
        if (pos < 120) {
            /* Append decimal seq */
            if (seq == 0) { path[pos++] = '0'; }
            else {
                char tmp[12]; int tl = 0;
                uint32_t v = seq;
                while (v) { tmp[tl++] = (char)('0' + v % 10); v /= 10; }
                for (int ii = tl - 1; ii >= 0; ii--)
                    if (pos < 120) path[pos++] = tmp[ii];
            }
            path[pos] = '\0';
        }
        vfs_create(path, 0);
        vfs_node_t *n = vfs_open(path, 0);
        if (!n) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, n);
        if (fd < 0) { vfs_close(n); RET_ERR(EMFILE); }
        proc->fd_offsets[fd] = 0;
        return (uint64_t)fd;
    }

    /* ── 320: kexec_file_load ────────────────────────────────────────────── */
    case SYS_KEXEC_FILE_LOAD:
        RET_ERR(ENOSYS);

    /* ── 321: bpf ────────────────────────────────────────────────────────── */
    case SYS_BPF:
        RET_ERR(ENOSYS);

    /* ── 323: userfaultfd ────────────────────────────────────────────────── */
    case SYS_USERFAULTFD:
        RET_ERR(ENOSYS);

    /* ── 324: membarrier — memory barrier (no SMP, always OK) ───────────── */
    case SYS_MEMBARRIER:
        return 0;

    /* ── 325: mlock2 ─────────────────────────────────────────────────────── */
    case SYS_MLOCK2:
        return 0;

    /* ── 326: copy_file_range ────────────────────────────────────────────── */
    case SYS_COPY_FILE_RANGE: {
        int      fd_in  = (int)a1;
        int      fd_out = (int)a3;
        uint64_t len    = a5;
        if (!proc) RET_ERR(EBADF);
        if (fd_in  < 0 || fd_in  >= MAX_FDS || !proc->fds[fd_in])  RET_ERR(EBADF);
        if (fd_out < 0 || fd_out >= MAX_FDS || !proc->fds[fd_out]) RET_ERR(EBADF);
        uint8_t  buf[512];
        uint64_t total = 0;
        while (total < len) {
            uint32_t chunk = (uint32_t)(len - total);
            if (chunk > 512) chunk = 512;
            uint32_t n = vfs_read(proc->fds[fd_in],
                                   proc->fd_offsets[fd_in], chunk, buf);
            if (!n) break;
            proc->fd_offsets[fd_in] += n;
            vfs_write(proc->fds[fd_out], proc->fd_offsets[fd_out], n, buf);
            proc->fd_offsets[fd_out] += n;
            total += n;
        }
        return total;
    }

    /* ── 327: preadv2(fd, iov, iovcnt, offset, flags) ───────────────────── */
    case SYS_PREADV2: {
        typedef struct { void *base; uint64_t len; } iovec_t;
        int             fd     = (int)a1;
        const iovec_t  *iov    = (const iovec_t *)(uintptr_t)a2;
        int             iovcnt = (int)a3;
        uint64_t        off    = a4;
        /* a5 = flags, ignored */
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        uint64_t total = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (!iov[i].base || !iov[i].len) continue;
            uint32_t n = vfs_read(proc->fds[fd], off,
                                   (uint32_t)iov[i].len,
                                   (uint8_t *)iov[i].base);
            off   += n;
            total += n;
        }
        return total;
    }

    /* ── 328: pwritev2(fd, iov, iovcnt, offset, flags) ──────────────────── */
    case SYS_PWRITEV2: {
        typedef struct { const void *base; uint64_t len; } iovecw_t;
        int              fd     = (int)a1;
        const iovecw_t  *iov    = (const iovecw_t *)(uintptr_t)a2;
        int              iovcnt = (int)a3;
        uint64_t         off    = a4;
        if (!proc || fd < 0 || fd >= MAX_FDS || !proc->fds[fd]) RET_ERR(EBADF);
        uint64_t total = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (!iov[i].base || !iov[i].len) continue;
            uint32_t n = vfs_write(proc->fds[fd], off,
                                    (uint32_t)iov[i].len,
                                    (const uint8_t *)iov[i].base);
            off   += n;
            total += n;
        }
        return total;
    }

    /* ── 329: pkey_mprotect — memory-protection keys, no-op ─────────────── */
    case SYS_PKEY_MPROTECT:
        return 0;

    /* ── 330: pkey_alloc — return -ENOSPC (no pkeys available) ──────────── */
    case SYS_PKEY_ALLOC:
        RET_ERR(ENOSYS);

    /* ── 331: pkey_free ──────────────────────────────────────────────────── */
    case SYS_PKEY_FREE:
        return 0;

    /* ── 333: io_pgetevents ──────────────────────────────────────────────── */
    case SYS_IO_PGETEVENTS:
        RET_ERR(ENOSYS);

    /* ── 334: rseq (restartable sequences) ───────────────────────────────── */
    case SYS_RSEQ:
        return 0;   /* accepted — no actual rseq engine needed */

    /* ════════════════════════════════════════════════════════════════════════
     * Linux 5.1+ syscalls (new-style numbering: 424–462)
     * ════════════════════════════════════════════════════════════════════════ */

    /* ── 424: pidfd_send_signal ──────────────────────────────────────────── */
    case SYS_PIDFD_SEND_SIGNAL: {
        /* pidfd=a1, sig=a2 — map to kill semantics */
        int sig = (int)a2;
        if (sig == 9 || sig == 15) proc_kill((uint32_t)a1);
        return 0;
    }

    /* ── 425–427: io_uring — not implemented ─────────────────────────────── */
    case SYS_IO_URING_SETUP:
    case SYS_IO_URING_ENTER:
    case SYS_IO_URING_REGISTER:
        RET_ERR(ENOSYS);

    /* ── 428–433: new VFS mount API — ENOSYS ─────────────────────────────── */
    case SYS_OPEN_TREE:
    case SYS_MOVE_MOUNT_SC:
    case SYS_FSOPEN:
    case SYS_FSCONFIG:
    case SYS_FSMOUNT:
    case SYS_FSPICK:
        RET_ERR(ENOSYS);

    /* ── 434: pidfd_open(pid, flags) — dummy fd ──────────────────────────── */
    case SYS_PIDFD_OPEN: {
        if (!proc) RET_ERR(ENOMEM);
        vfs_node_t *n = vfs_open("/dev/null", 0);
        if (!n) RET_ERR(ENOMEM);
        int fd = proc_open_fd(proc, n);
        if (fd < 0) { vfs_close(n); RET_ERR(EMFILE); }
        return (uint64_t)fd;
    }

    /* ── 435: clone3 — ENOSYS ────────────────────────────────────────────── */
    case SYS_CLONE3:
        RET_ERR(ENOSYS);

    /* ── 436: close_range(first, last, flags) ────────────────────────────── */
    case SYS_CLOSE_RANGE: {
        uint32_t first = (uint32_t)a1;
        uint32_t last  = (uint32_t)a2;
        if (!proc) RET_ERR(EBADF);
        if (last >= (uint32_t)MAX_FDS) last = (uint32_t)MAX_FDS - 1;
        for (uint32_t f = first; f <= last; f++) {
            if ((int)f < MAX_FDS && proc->fds[f])
                proc_close_fd(proc, (int)f);
        }
        return 0;
    }

    /* ── 437: openat2(dirfd, path, open_how*, size) ──────────────────────── */
    case SYS_OPENAT2: {
        int         dirfd = (int)a1;
        const char *path  = (const char *)(uintptr_t)a2;
        /* open_how struct: u64 flags, u64 mode, u64 resolve */
        uint64_t   *how   = (uint64_t *)(uintptr_t)a3;
        int         oflags = how ? (int)how[0] : 0;
        if (!path || !proc) RET_ERR(EFAULT);
        char rp[512];
        if (at_resolve(proc, dirfd, path, rp, sizeof(rp)) < 0) RET_ERR(ENOENT);
        vfs_node_t *node = vfs_open(rp, oflags);
        if (!node && (oflags & 0x40)) {
            vfs_create(rp, 0);
            node = vfs_open(rp, oflags);
        }
        if (!node) RET_ERR(ENOENT);
        int fd = proc_open_fd(proc, node);
        if (fd < 0) { vfs_close(node); RET_ERR(ENFILE); }
        proc->fd_offsets[fd] = 0;
        return (uint64_t)fd;
    }

    /* ── 438: pidfd_getfd — ENOSYS ───────────────────────────────────────── */
    case SYS_PIDFD_GETFD:
        RET_ERR(ENOSYS);

    /* ── 439: faccessat2 — delegate to faccessat ─────────────────────────── */
    case SYS_FACCESSAT2:
        return syscall_dispatch(SYS_FACCESSAT, a1, a2, a3, 0, 0);

    /* ── 440: process_madvise ────────────────────────────────────────────── */
    case SYS_PROCESS_MADVISE:
        return 0;

    /* ── 441: epoll_pwait2 — same as epoll_wait ──────────────────────────── */
    case SYS_EPOLL_PWAIT2:
        return 0;

    /* ── 442: mount_setattr ──────────────────────────────────────────────── */
    case SYS_MOUNT_SETATTR:
        RET_ERR(ENOSYS);

    /* ── 443: quotactl_fd ────────────────────────────────────────────────── */
    case SYS_QUOTACTL_FD:
        RET_ERR(ENOSYS);

    /* ── 444–446: Landlock LSM ───────────────────────────────────────────── */
    case SYS_LANDLOCK_CREATE_RULESET:
    case SYS_LANDLOCK_ADD_RULE:
    case SYS_LANDLOCK_RESTRICT_SELF:
        RET_ERR(ENOSYS);

    /* ── 447: memfd_secret ───────────────────────────────────────────────── */
    case SYS_MEMFD_SECRET:
        RET_ERR(ENOSYS);

    /* ── 448: process_mrelease ───────────────────────────────────────────── */
    case SYS_PROCESS_MRELEASE:
        RET_ERR(ENOSYS);

    /* ── 449: futex_waitv ────────────────────────────────────────────────── */
    case SYS_FUTEX_WAITV:
        RET_ERR(ENOSYS);

    /* ── 450: set_mempolicy_home_node ────────────────────────────────────── */
    case SYS_SET_MEMPOLICY_HOME_NODE:
        return 0;

    /* ── 451: cachestat ──────────────────────────────────────────────────── */
    case SYS_CACHESTAT:
        RET_ERR(ENOSYS);

    /* ── 452: fchmodat2 — delegate to fchmodat ───────────────────────────── */
    case SYS_FCHMODAT2:
        return syscall_dispatch(SYS_FCHMODAT, a1, a2, a3, 0, 0);

    /* ── 453: map_shadow_stack ───────────────────────────────────────────── */
    case SYS_MAP_SHADOW_STACK:
        RET_ERR(ENOSYS);

    /* ── 454–456: futex_wake / futex_wait / futex_requeue ────────────────── */
    case SYS_FUTEX_WAKE:
    case SYS_FUTEX_WAIT_SC:
    case SYS_FUTEX_REQUEUE:
        return 0;   /* single-threaded — trivially done */

    /* ── 457–458: statmount / listmount ──────────────────────────────────── */
    case SYS_STATMOUNT:
    case SYS_LISTMOUNT:
        RET_ERR(ENOSYS);

    /* ── 459–461: LSM attribute syscalls ─────────────────────────────────── */
    case SYS_LSM_GET_SELF_ATTR:
    case SYS_LSM_SET_SELF_ATTR:
    case SYS_LSM_LIST_MODULES:
        RET_ERR(ENOSYS);

    /* ── 462: mseal (memory sealing) ────────────────────────────────────── */
    case SYS_MSEAL:
        return 0;   /* no-op: sealing accepted, not enforced */

    /* ── Unknown ─────────────────────────────────────────────────────────── */
    default:
        klog(LOG_WARN, "syscall: unknown number %llu (a1=%llx)", num, a1);
        RET_ERR(ENOSYS);
    }
}

/* ── INT 0x80 entry point ────────────────────────────────────────────────── */
static void syscall_handler(registers_t *regs) {
    uint64_t result = syscall_dispatch(
        regs->rax,  /* syscall number */
        regs->rdi,  /* arg1 */
        regs->rsi,  /* arg2 */
        regs->rdx,  /* arg3 */
        regs->r10,  /* arg4 (Linux uses r10 for arg4, not rcx) */
        regs->r8    /* arg5 */
    );
    regs->rax = result;
}

void syscall_init(void) {
    idt_set_gate(0x80, (uint64_t)syscall_handler, 0x08, 0xEE); /* DPL=3, trap gate */
    klog(LOG_INFO, "Syscall: INT 0x80 registered, Linux x86_64 ABI (%d syscalls)",
         SYS_CLOCK_GETTIME);
}
