/* NexOS — kernel/proc/process.c | Process management + ring-3 support | MIT License */
#include "process.h"
#include "../kernel.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/paging.h"

process_t *processes[MAX_PROCESSES];
int        process_count = 0;
process_t *current_process = NULL;

static uint32_t next_pid = 1;

/* Defined in enter_ring3.asm */
extern void enter_ring3(uint64_t entry, uint64_t user_stack_top);

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

    /* Zero the PCB */
    uint8_t *zp = (uint8_t *)proc;
    for (size_t i = 0; i < sizeof(process_t); i++) zp[i] = 0;

    /*
     * Kernel stack — 1 page (4 KB).
     * Used as rsp0 in the TSS so that ring-3 → kernel transitions
     * (INT 0x80 syscalls, exceptions) land on a valid kernel stack.
     */
    proc->stack = (uint8_t *)kmalloc(PROC_STACK_SIZE);
    if (!proc->stack) {
        kfree(proc);
        return NULL;
    }

    /*
     * User-mode stack — allocate one physical page from the PMM.
     * Because boot.asm identity-maps the first 4 MB, the physical
     * address IS the virtual address in the current address space.
     * We rely on the PMM starting its search above the kernel image,
     * so the returned page is genuinely free RAM.
     */
    uint64_t user_stack_phys = pmm_alloc_page();
    if (!user_stack_phys) {
        kfree(proc->stack);
        kfree(proc);
        return NULL;
    }
    proc->user_stack     = user_stack_phys;
    proc->user_stack_top = user_stack_phys + PAGE_SIZE;

    proc->pid           = next_pid++;
    proc->ppid          = current_process ? current_process->pid : 0;
    proc->state         = PROC_READY;
    proc->priority      = priority;
    proc->time_slice_ms = 20;
    proc->stack_size    = PROC_STACK_SIZE;

    int ni = 0;
    while (name[ni] && ni < 63) { proc->name[ni] = name[ni]; ni++; }
    proc->name[ni] = 0;

    /* Kernel stack top — 16-byte aligned; push entry as initial return addr */
    uint64_t kstack_top = (uint64_t)(proc->stack + PROC_STACK_SIZE) & ~0xFULL;
    kstack_top -= 8;
    *(uint64_t *)kstack_top = (uint64_t)entry;

    /*
     * Ring-3 CPU context.
     * cr3 = 0 → reuse the kernel's PML4 (set by boot.asm, inherited by
     * paging_init()).  The first 4 MB identity map makes kernel code
     * accessible from ring 3 for NexOS 0.1.
     */
    proc->context.rsp    = kstack_top;
    proc->context.rip    = (uint64_t)entry;
    proc->context.rflags = 0x202;              /* IF set */
    proc->context.cs     = GDT_USER_CODE | 3;  /* 0x1B — ring 3 */
    proc->context.ss     = GDT_USER_DATA | 3;  /* 0x23 — ring 3 */
    proc->context.cr3    = 0;                  /* reuse kernel PML4 */

    proc->cwd[0] = '/'; proc->cwd[1] = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!processes[i]) {
            processes[i] = proc;
            process_count++;
            break;
        }
    }

    klog(LOG_DEBUG, "Created process '%s' pid=%u stack=0x%x user_stack=0x%x",
         name, proc->pid,
         (uint64_t)(uintptr_t)proc->stack,
         proc->user_stack);
    return proc;
}

/*
 * Drop to ring 3 and begin executing the process.
 * Updates TSS rsp0 so INT 0x80 syscalls always land on the kernel stack.
 * This is a one-way IRET — does not return.
 */
void proc_enter_ring3(process_t *proc) {
    if (!proc) return;

    uint64_t kstack_top = (uint64_t)(proc->stack + PROC_STACK_SIZE) & ~0xFULL;
    tss_set_rsp0(kstack_top);

    proc->state    = PROC_RUNNING;
    current_process = proc;

    klog(LOG_INFO, "Dropping to ring 3 — launching %s (pid %u)",
         proc->name, proc->pid);

    enter_ring3(proc->context.rip, proc->user_stack_top);

    /* Never reached */
}

void proc_exit(int code) {
    if (!current_process) return;
    current_process->exit_code = code;
    current_process->state = PROC_ZOMBIE;
    klog(LOG_DEBUG, "Process %u exited with code %d",
         current_process->pid, code);
    for (;;) __asm__ volatile ("hlt");
}

process_t *proc_get_current(void)           { return current_process; }

process_t *proc_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (processes[i] && processes[i]->pid == pid) return processes[i];
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
        if (!proc->fds[i]) { proc->fds[i] = node; return i; }
    }
    return -1;
}

void proc_close_fd(process_t *proc, int fd) {
    if (fd >= 0 && fd < MAX_FDS) proc->fds[fd] = NULL;
}
