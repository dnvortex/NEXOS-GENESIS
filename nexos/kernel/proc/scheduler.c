/* NexOS — kernel/proc/scheduler.c | Round-robin scheduler | MIT License
 *
 * FIX 4 — Kernel-thread compatibility:
 *
 * The scheduler operates on process_t structs regardless of whether the
 * process runs at CPL=0 (kernel thread, CS=0x08) or CPL=3 (ring-3 thread,
 * CS=0x1B).  scheduler_tick() selects the next READY process and updates
 * current_process; the actual context switch happens when the selected
 * process was previously interrupted (via the IRQ/exception path in
 * isr.asm) and the iretq at the end of irq_common restores its saved RIP,
 * CS, and RFLAGS.
 *
 * Because NexOS 0.1 launches only one process (init) which then calls
 * nsh_main() in-line (both running as the same kernel thread), the
 * scheduler's ready queue has exactly one entry and scheduler_tick() is
 * effectively a no-op.  No special CS-dispatch logic is needed here;
 * proc_enter_ring3() in process.c handles the first dispatch.
 *
 * When multi-process support is added, context saving/restoring (the full
 * cpu_context_t) will be performed here.  Kernel-thread context switches
 * will use a simple RSP swap + ret, while ring-3 switches use a full IRET
 * frame restoration.
 */
#include "scheduler.h"
#include "../kernel.h"
#include "../drivers/timer.h"

static process_t *ready_queue[MAX_PROCESSES];
static int        queue_head = 0;
static int        queue_tail = 0;
static int        queue_size = 0;

void scheduler_init(void) {
    queue_head = queue_tail = queue_size = 0;
    klog(LOG_INFO, "Scheduler initialized (round-robin, 20 ms quantum)");
}

void scheduler_add(process_t *proc) {
    if (!proc || queue_size >= MAX_PROCESSES) return;
    ready_queue[queue_tail] = proc;
    queue_tail = (queue_tail + 1) % MAX_PROCESSES;
    queue_size++;
}

void scheduler_remove(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (ready_queue[i] && ready_queue[i]->pid == pid) {
            ready_queue[i] = NULL;
            if (queue_size > 0) queue_size--;
            break;
        }
    }
}

/*
 * scheduler_tick — called from the PIT IRQ handler every millisecond.
 *
 * Selects the next READY process.  Works for both kernel threads (CS=0x08)
 * and ring-3 threads (CS=0x1B) because it only updates the current_process
 * pointer; the hardware saves/restores registers automatically via the
 * IRQ/IRET path in isr.asm.
 */
void scheduler_tick(void) {
    if (queue_size == 0) return;

    /* Reap zombies */
    if (current_process && current_process->state == PROC_ZOMBIE) {
        scheduler_remove(current_process->pid);
    }

    /* Find next runnable process */
    for (int attempts = 0; attempts < MAX_PROCESSES; attempts++) {
        process_t *next = ready_queue[queue_head];
        queue_head = (queue_head + 1) % MAX_PROCESSES;

        if (!next) continue;
        if (next->state == PROC_ZOMBIE || next->state == PROC_DEAD)  continue;
        if (next->state == PROC_BLOCKED) continue;

        if (current_process && current_process != next)
            current_process->state = PROC_READY;

        current_process = next;
        current_process->state = PROC_RUNNING;
        return;
    }
}

void scheduler_yield(void) {
    scheduler_tick();
}
