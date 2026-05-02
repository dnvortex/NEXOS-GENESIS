/* NexOS — kernel/proc/scheduler.c | Round-robin preemptive scheduler | MIT License */
#include "scheduler.h"
#include "../kernel.h"
#include "../drivers/timer.h"

static process_t *ready_queue[MAX_PROCESSES];
static int        queue_head = 0;
static int        queue_tail = 0;
static int        queue_size = 0;

void scheduler_init(void) {
    queue_head = queue_tail = queue_size = 0;
    klog(LOG_INFO, "Scheduler initialized (round-robin, 20ms quantum)");
}

void scheduler_add(process_t *proc) {
    if (queue_size >= MAX_PROCESSES) return;
    ready_queue[queue_tail] = proc;
    queue_tail = (queue_tail + 1) % MAX_PROCESSES;
    queue_size++;
}

void scheduler_remove(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (ready_queue[i] && ready_queue[i]->pid == pid) {
            ready_queue[i] = NULL;
            /* Will be skipped in next tick */
            if (queue_size > 0) queue_size--;
            break;
        }
    }
}

void scheduler_tick(void) {
    if (queue_size == 0) return;

    /* Clean up zombies */
    if (current_process && current_process->state == PROC_ZOMBIE) {
        scheduler_remove(current_process->pid);
    }

    /* Find next ready process */
    for (int attempts = 0; attempts < MAX_PROCESSES; attempts++) {
        process_t *next = ready_queue[queue_head];
        queue_head = (queue_head + 1) % MAX_PROCESSES;

        if (!next) continue;
        if (next->state == PROC_ZOMBIE || next->state == PROC_DEAD) continue;
        if (next->state == PROC_BLOCKED) continue;

        if (current_process && current_process != next) {
            current_process->state = PROC_READY;
        }

        current_process = next;
        current_process->state = PROC_RUNNING;
        return;
    }
}

void scheduler_yield(void) {
    scheduler_tick();
}
