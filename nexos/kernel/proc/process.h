/* NexOS — kernel/proc/process.h | Process Control Block | MIT License */
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "../fs/vfs.h"

#define MAX_PROCESSES   64
#define MAX_FDS         16
#define PROC_STACK_SIZE (4096)       /* 1 page (4 KB) kernel stack per process */
#define MAX_ENV_VARS    64
#define MAX_ENV_LEN     256

typedef enum {
    PROC_RUNNING = 0,
    PROC_READY,
    PROC_BLOCKED,
    PROC_ZOMBIE,
    PROC_DEAD
} proc_state_t;

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cs,  ss;
    uint64_t cr3;
} cpu_context_t;

typedef struct process {
    uint32_t      pid;
    uint32_t      ppid;
    proc_state_t  state;
    cpu_context_t context;

    /* Kernel stack — used as rsp0 in TSS during syscall handling */
    uint8_t      *stack;
    uint64_t      stack_size;

    /* User-mode stack (one physical page, identity-mapped) */
    uint64_t      user_stack;      /* physical / virtual base */
    uint64_t      user_stack_top;  /* top (= base + PAGE_SIZE) */

    uint64_t      cr3;
    vfs_node_t   *fds[MAX_FDS];
    char          cwd[1024];
    int           exit_code;
    uint8_t       priority;
    uint32_t      time_slice_ms;
    uint32_t      time_used_ms;
    char          name[64];

    /* Per-fd file position (updated by read/write/lseek) */
    uint64_t      fd_offsets[MAX_FDS];

    /* Program break (sys_brk) — set to 0 until first use */
    uint64_t      brk;

    /* mmap allocation base — grows upward from first use */
    uint64_t      mmap_base;

    /* File creation mask (sys_umask) */
    uint32_t      umask;
} process_t;

void       proc_init(void);
process_t *proc_create(const char *name, void (*entry)(void), uint8_t priority);
void       proc_enter_ring3(process_t *proc);
void       proc_exit(int code);
process_t *proc_get_current(void);
process_t *proc_get_by_pid(uint32_t pid);
void       proc_kill(uint32_t pid);
void       proc_open_fd(process_t *proc, vfs_node_t *node);
void       proc_close_fd(process_t *proc, int fd);
int        proc_fork(void);
int        proc_exec(const char *path, char **argv);
int        proc_wait(uint32_t pid);

extern process_t *processes[MAX_PROCESSES];
extern int        process_count;
extern process_t *current_process;

#endif
