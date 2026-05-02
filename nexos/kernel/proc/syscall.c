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
#define EINTR    4
#define EBADF    9
#define ECHILD   10
#define ENOMEM   12
#define EFAULT   14
#define ENOTDIR  20
#define EINVAL   22
#define ENFILE   23
#define ENOTTY   25
#define ERANGE   34
#define ENOSYS   38

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
    UNUSED(a5);
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
        klog(LOG_DEBUG, "sys_execve() -> ENOSYS (not yet implemented)");
        RET_ERR(ENOSYS);

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
