/* NexOS — kernel/proc/syscall.c | INT 0x80 system call handler | MIT License */
#include "syscall.h"
#include "process.h"
#include "../kernel.h"
#include "../arch/x86_64/idt.h"
#include "../fs/vfs.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"

extern void syscall_stub(void);

/* Syscall dispatcher — called from INT 0x80 handler */
/* rax = syscall number, rdi/rsi/rdx/r10/r8/r9 = args */
/* Returns result in rax */
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5) {
    UNUSED(a4); UNUSED(a5);
    process_t *proc = proc_get_current();

    switch (num) {
    case SYS_EXIT:
        proc_exit((int)a1);
        return 0;

    case SYS_READ: {
        int fd = (int)a1;
        uint8_t *buf = (uint8_t *)(uintptr_t)a2;
        uint32_t len = (uint32_t)a3;
        if (!proc || fd < 0 || fd >= MAX_FDS) return (uint64_t)-1;
        if (fd == 0) {
            /* stdin: read from keyboard */
            uint32_t i = 0;
            while (i < len) {
                char ch = keyboard_getchar();
                buf[i++] = (uint8_t)ch;
                if (ch == '\n') break;
            }
            return i;
        }
        vfs_node_t *node = proc->fds[fd];
        if (!node) return (uint64_t)-1;
        return vfs_read(node, 0, len, buf);
    }

    case SYS_WRITE: {
        int fd = (int)a1;
        const uint8_t *buf = (const uint8_t *)(uintptr_t)a2;
        uint32_t len = (uint32_t)a3;
        if (fd == 1 || fd == 2) {
            for (uint32_t i = 0; i < len; i++) vga_putchar((char)buf[i]);
            return len;
        }
        if (!proc || fd < 0 || fd >= MAX_FDS) return (uint64_t)-1;
        vfs_node_t *node = proc->fds[fd];
        if (!node) return (uint64_t)-1;
        return vfs_write(node, 0, len, buf);
    }

    case SYS_OPEN: {
        const char *path = (const char *)(uintptr_t)a1;
        int flags = (int)a2;
        if (!proc) return (uint64_t)-1;
        vfs_node_t *node = vfs_open(path, flags);
        if (!node) return (uint64_t)-1;
        int fd = proc_open_fd(proc, node);
        return (fd < 0) ? (uint64_t)-1 : (uint64_t)fd;
    }

    case SYS_CLOSE: {
        int fd = (int)a1;
        if (!proc || fd < 0 || fd >= MAX_FDS) return (uint64_t)-1;
        proc_close_fd(proc, fd);
        return 0;
    }

    case SYS_GETPID:
        return proc ? proc->pid : 0;

    case SYS_SLEEP:
        timer_sleep_ms((uint32_t)a1);
        return 0;

    case SYS_STAT: {
        const char *path = (const char *)(uintptr_t)a1;
        vfs_stat_t *st   = (vfs_stat_t *)(uintptr_t)a2;
        return (uint64_t)vfs_stat(path, st);
    }

    case SYS_MKDIR: {
        const char *path = (const char *)(uintptr_t)a1;
        return (uint64_t)vfs_mkdir(path);
    }

    case SYS_GETCWD:
        if (!proc) return (uint64_t)-1;
        {
            char *buf = (char *)(uintptr_t)a1;
            uint32_t sz = (uint32_t)a2;
            uint32_t i = 0;
            while (i < sz - 1 && proc->cwd[i]) { buf[i] = proc->cwd[i]; i++; }
            buf[i] = 0;
        }
        return 0;

    case SYS_CHDIR: {
        const char *path = (const char *)(uintptr_t)a1;
        if (!proc) return (uint64_t)-1;
        vfs_node_t *node = vfs_open(path, 0);
        if (!node) return (uint64_t)-1;
        uint32_t i = 0;
        while (i < 1023 && path[i]) { proc->cwd[i] = path[i]; i++; }
        proc->cwd[i] = 0;
        return 0;
    }

    default:
        klog(LOG_WARN, "Unknown syscall %llu", num);
        return (uint64_t)-1;
    }
}

/* INT 0x80 handler stub */
static void syscall_handler(registers_t *regs) {
    uint64_t result = syscall_dispatch(
        regs->rax, regs->rdi, regs->rsi, regs->rdx,
        regs->r10, regs->r8
    );
    regs->rax = result;
}

void syscall_init(void) {
    idt_set_gate(0x80, (uint64_t)syscall_handler, 0x08, 0xEE); /* DPL=3 */
    klog(LOG_INFO, "Syscall interface initialized (INT 0x80)");
}
