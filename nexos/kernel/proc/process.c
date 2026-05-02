/* NexOS — kernel/proc/process.c | Process management | MIT License */
#include "process.h"
#include "../kernel.h"
#include "../mm/heap.h"

process_t *processes[MAX_PROCESSES];
int        process_count = 0;
process_t *current_process = NULL;

static uint32_t next_pid = 1;

void proc_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) processes[i] = NULL;
    process_count = 0;
    klog(LOG_INFO, "Process manager initialized");
}

process_t *proc_create(const char *name, void (*entry)(void), uint8_t priority) {
    if (process_count >= MAX_PROCESSES) {
        klog(LOG_ERROR, "proc_create: max processes reached");
        return NULL;
    }

    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) return NULL;

    uint8_t *zp = (uint8_t *)proc;
    for (size_t i = 0; i < sizeof(process_t); i++) zp[i] = 0;

    proc->stack = (uint8_t *)kmalloc(PROC_STACK_SIZE);
    if (!proc->stack) { kfree(proc); return NULL; }

    proc->pid         = next_pid++;
    proc->ppid        = current_process ? current_process->pid : 0;
    proc->state       = PROC_READY;
    proc->priority    = priority;
    proc->time_slice_ms = 20;
    proc->stack_size  = PROC_STACK_SIZE;

    /* Copy name */
    int ni = 0;
    while (name[ni] && ni < 63) { proc->name[ni] = name[ni]; ni++; }
    proc->name[ni] = 0;

    /* Initial stack pointer */
    uint64_t stack_top = (uint64_t)(proc->stack + PROC_STACK_SIZE);
    stack_top &= ~0xF;  /* 16-byte align */

    /* Push entry address as return address */
    stack_top -= 8;
    *(uint64_t *)stack_top = (uint64_t)entry;

    /* Set up context */
    proc->context.rsp    = stack_top;
    proc->context.rip    = (uint64_t)entry;
    proc->context.rflags = 0x202;  /* IF set */
    proc->context.cs     = 0x08;
    proc->context.ss     = 0x10;

    /* Working directory */
    proc->cwd[0] = '/'; proc->cwd[1] = 0;

    /* Register */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!processes[i]) {
            processes[i] = proc;
            process_count++;
            break;
        }
    }

    klog(LOG_DEBUG, "Created process '%s' pid=%u", name, proc->pid);
    return proc;
}

void proc_exit(int code) {
    if (!current_process) return;
    current_process->exit_code = code;
    current_process->state = PROC_ZOMBIE;
    klog(LOG_DEBUG, "Process %u exited with code %d", current_process->pid, code);
    /* Scheduler will handle cleanup */
    for (;;) __asm__ volatile ("hlt");
}

process_t *proc_get_current(void) {
    return current_process;
}

process_t *proc_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i] && processes[i]->pid == pid) return processes[i];
    }
    return NULL;
}

void proc_kill(uint32_t pid) {
    process_t *p = proc_get_by_pid(pid);
    if (p) {
        p->state = PROC_ZOMBIE;
        klog(LOG_INFO, "Killed process %u (%s)", pid, p->name);
    }
}

int proc_open_fd(process_t *proc, vfs_node_t *node) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (!proc->fds[i]) {
            proc->fds[i] = node;
            return i;
        }
    }
    return -1;
}

void proc_close_fd(process_t *proc, int fd) {
    if (fd < 0 || fd >= MAX_FDS) return;
    proc->fds[fd] = NULL;
}
