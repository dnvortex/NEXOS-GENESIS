/* NexOS — kernel/proc/process.h | Process Control Block | MIT License */
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "../fs/vfs.h"

#define MAX_PROCESSES   64
#define MAX_FDS         16
#define PROC_STACK_SIZE (8 * 1024)   /* 8KB per process kernel stack */
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
    uint8_t      *stack;
    uint64_t      stack_size;
    uint64_t      cr3;
    vfs_node_t   *fds[MAX_FDS];
    char          cwd[1024];
    int           exit_code;
    uint8_t       priority;
    uint32_t      time_slice_ms;
    uint32_t      time_used_ms;
    char          name[64];
} process_t;

void      proc_init(void);
process_t *proc_create(const char *name, void (*entry)(void), uint8_t priority);
void      proc_exit(int code);
process_t *proc_get_current(void);
process_t *proc_get_by_pid(uint32_t pid);
void      proc_kill(uint32_t pid);
int       proc_open_fd(process_t *proc, vfs_node_t *node);
void      proc_close_fd(process_t *proc, int fd);

extern process_t *processes[MAX_PROCESSES];
extern int        process_count;
extern process_t *current_process;

#endif
