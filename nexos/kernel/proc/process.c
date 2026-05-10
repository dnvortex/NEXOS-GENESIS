/* NexOS — kernel/proc/process.c | Process management | MIT License
 *
 * FIX 2 / FIX 5 / FIX 6:
 *   All processes are created as kernel threads (CS=GDT_KERNEL_CODE, CPL=0)
 *   until the ring-3 infrastructure (U/S PTEs, separate address space) is
 *   complete.  This avoids the page fault that occurred when IRET tried to
 *   jump to a kernel-space address with ring-3 CS=0x1B and found no U/S
 *   mapping in the page tables.
 *
 *   proc_enter_ring3() detects the thread type from the saved CS and calls
 *   enter_kernel_thread() (a direct stack-switch + jump) for ring-0 threads
 *   instead of the ring-3 IRET path.
 *
 *   Kernel stacks are allocated from the heap via kmalloc() and zeroed
 *   (FIX 6).  No PMM pages are consumed for kernel stacks.
 */
#include "process.h"
#include "elf.h"
#include "scheduler.h"
#include "../kernel.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/paging.h"

process_t *processes[MAX_PROCESSES];
int        process_count  = 0;
process_t *current_process = NULL;

static uint32_t next_pid = 1;

/* Defined in enter_ring3.asm */
extern void enter_ring3(uint64_t entry, uint64_t user_stack_top);
extern void enter_kernel_thread(uint64_t entry, uint64_t kernel_stack_top);

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void memzero(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}

static void kstrcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ── proc_init ───────────────────────────────────────────────────────── */

void proc_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) processes[i] = NULL;
    process_count  = 0;
    current_process = NULL;
    klog(LOG_INFO, "Process manager initialized");
}

/* ── proc_create ─────────────────────────────────────────────────────── */

process_t *proc_create(const char *name, void (*entry)(void), uint8_t priority) {
    if (process_count >= MAX_PROCESSES) {
        klog(LOG_ERROR, "proc_create: process table full");
        return NULL;
    }

    /* Allocate and zero the PCB */
    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) {
        klog(LOG_ERROR, "proc_create: OOM for PCB");
        return NULL;
    }
    memzero(proc, sizeof(process_t));

    /*
     * FIX 6 — Kernel stack from heap (already was kmalloc, but now
     * explicitly zeroed and documented).
     *
     * The stack is used as:
     *   a) TSS rsp0 — the stack the CPU switches to on any ring-0
     *      entry (INT, exception, syscall).  We set this in
     *      proc_enter_ring3() via tss_set_rsp0().
     *   b) The actual execution stack for kernel threads (via
     *      enter_kernel_thread which sets RSP = stack_top).
     */
    proc->stack = (uint8_t *)kmalloc(PROC_STACK_SIZE);
    if (!proc->stack) {
        klog(LOG_ERROR, "proc_create: OOM for kernel stack");
        kfree(proc);
        return NULL;
    }
    memzero(proc->stack, PROC_STACK_SIZE);

    /* Stack top — 16-byte aligned per System V AMD64 ABI */
    uint64_t kstack_top = (uint64_t)(proc->stack + PROC_STACK_SIZE) & ~0xFULL;

    /* ── Fill in PCB ─────────────────────────────────────────────────── */
    proc->pid           = next_pid++;
    proc->ppid          = current_process ? current_process->pid : 0;
    proc->state         = PROC_READY;
    proc->priority      = priority;
    proc->time_slice_ms = 20;
    proc->stack_size    = PROC_STACK_SIZE;
    kstrcpy(proc->name, name, sizeof(proc->name));
    proc->cwd[0] = '/'; proc->cwd[1] = 0;

    /*
     * FIX 2b — Kernel thread CPU context.
     *
     * CS = GDT_KERNEL_CODE (0x08), SS = GDT_KERNEL_DATA (0x10).
     * No ring-3 selector, no U/S PTE required.
     * RFLAGS = 0x202 (IF=1, everything else clear).
     * RSP    = kernel stack top.
     * RIP    = entry function.
     * CR3    = 0 → proc_enter_ring3 leaves CR3 unchanged (reuses kernel PML4).
     */
    proc->context.cs     = GDT_KERNEL_CODE;   /* 0x08 — ring 0 */
    proc->context.ss     = GDT_KERNEL_DATA;   /* 0x10 — ring 0 */
    proc->context.rflags = 0x202;             /* IF set          */
    proc->context.rsp    = kstack_top;
    proc->context.rip    = (uint64_t)(uintptr_t)entry;
    proc->context.cr3    = 0;                 /* reuse kernel PML4 */

    /*
     * No user-mode stack for kernel threads.
     * user_stack / user_stack_top remain 0 (memzero'd).
     * When ring-3 support is added later, allocate one PMM page here
     * and set user_stack_top = base + PAGE_SIZE.
     */

    /* Register in global process table */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!processes[i]) {
            processes[i] = proc;
            process_count++;
            break;
        }
    }

    klog(LOG_DEBUG,
         "proc_create: '%s' pid=%u kstack=0x%x-0x%x entry=0x%x",
         name, proc->pid,
         (uint64_t)(uintptr_t)proc->stack,
         kstack_top,
         proc->context.rip);
    return proc;
}

/* ── proc_enter_ring3 ────────────────────────────────────────────────── */
/*
 * FIX 2c / FIX 4 — Kernel vs ring-3 dispatch.
 *
 * Despite the name "enter_ring3" (kept for historical API compatibility),
 * this function now dispatches based on the saved CS:
 *
 *   CS == GDT_KERNEL_CODE (0x08):
 *     Kernel thread — call enter_kernel_thread() which switches the
 *     stack to this process's kernel stack and jumps to entry.
 *     No IRET, no privilege change, no U/S PTEs needed.
 *
 *   CS == GDT_USER_CODE | 3 (0x1B):
 *     Ring-3 thread — call enter_ring3() which performs a full IRET
 *     frame push and iretq.  Requires U/S mappings to be in place.
 *
 * In both cases the TSS rsp0 is updated first so that any subsequent
 * interrupt or syscall lands on the correct kernel stack for this process.
 */
void proc_enter_ring3(process_t *proc) {
    if (!proc) return;

    uint64_t kstack_top = (uint64_t)(proc->stack + PROC_STACK_SIZE) & ~0xFULL;

    /* Point TSS rsp0 at this process's kernel stack */
    tss_set_rsp0(kstack_top);

    proc->state    = PROC_RUNNING;
    current_process = proc;

    if (proc->context.cs == GDT_KERNEL_CODE) {
        klog(LOG_INFO,
             "Launching '%s' (pid %u) as kernel thread - entry=0x%x stack=0x%x",
             proc->name, proc->pid, proc->context.rip, kstack_top);

        /* Switch to this process's kernel stack and jump to entry.
           This call never returns (entry loops forever or halts). */
        enter_kernel_thread(proc->context.rip, kstack_top);

        /* Defensive halt — should be unreachable */
        proc->state = PROC_ZOMBIE;
        cli(); for (;;) hlt();
    } else {
        klog(LOG_INFO,
             "Launching '%s' (pid %u) ring-3 - entry=0x%x user_stack=0x%x",
             proc->name, proc->pid,
             proc->context.rip, proc->user_stack_top);
        enter_ring3(proc->context.rip, proc->user_stack_top);
    }
}

/* ── Remaining process API ───────────────────────────────────────────── */

void proc_exit(int code) {
    if (!current_process) return;
    current_process->exit_code = code;
    current_process->state = PROC_ZOMBIE;
    klog(LOG_INFO, "Process '%s' (pid %u) exited with code %d",
         current_process->name, current_process->pid, code);
    cli(); for (;;) hlt();
}

process_t *proc_get_current(void)  { return current_process; }

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

int proc_fork(void) {
    // Simple fork for kernel threads: create a new process with same context
    // But since it's kernel thread, the child will run the same code
    // For exec, it will change the entry
    process_t *child = proc_create("forked", (void (*)(void))current_process->context.rip, current_process->priority);
    if (!child) return -1;
    // Copy FDs
    for (int i = 0; i < MAX_FDS; i++) {
        child->fds[i] = current_process->fds[i];
    }
    kstrcpy(child->cwd, current_process->cwd, sizeof(child->cwd));
    // Set state to ready
    child->state = PROC_READY;
    // Add to scheduler
    extern void scheduler_add(process_t *proc);
    scheduler_add(child);
    return child->pid;
}

int proc_exec(const char *path, char **argv) {
    // Load ELF
    uint64_t entry;
    if (elf_load(path, &entry) != 0) return -1;
    // Set entry point
    current_process->context.rip = entry;
    // For now, ignore argv, assume no args
    // TODO: set up user stack and argv
    // Set up user stack at 0x7FFFFFFFE000
    uint64_t user_stack_top = 0x7FFFFFFFE000;
    uint64_t user_stack_base = user_stack_top - PAGE_SIZE;
    // Allocate page for user stack
    uint64_t phys = pmm_alloc_page();
    if (!phys) return -1;
    vmm_map(user_stack_base, phys, VMM_FLAG_USER | VMM_FLAG_WRITE);
    current_process->user_stack = user_stack_base;
    current_process->user_stack_top = user_stack_top;
    // Push argv (simplified, assume no args)
    // For now, just set rsp to top
    current_process->context.rsp = user_stack_top;
    // Set CS to user code
    current_process->context.cs = GDT_USER_CODE | 3;
    current_process->context.ss = GDT_USER_DATA | 3;
    current_process->context.rflags = 0x202;
    // Set CR3 to kernel for now, but should be separate address space
    current_process->context.cr3 = 0; // reuse kernel
    return 0;
}

int proc_wait(uint32_t pid) {
    // Simple wait: busy wait until process exits
    process_t *p = proc_get_by_pid(pid);
    while (p && p->state != PROC_ZOMBIE) {
        // Yield or something, but for now, just loop
        asm volatile ("hlt");
    }
    if (p) {
        int code = p->exit_code;
        // Remove from processes
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i] == p) {
                processes[i] = NULL;
                process_count--;
                break;
            }
        }
        // Free resources
        kfree(p->stack);
        kfree(p);
        return code;
    }
    return -1;
}
