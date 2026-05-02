/* NexOS — kernel/proc/scheduler.h | Round-robin preemptive scheduler | MIT License */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

void scheduler_init(void);
void scheduler_add(process_t *proc);
void scheduler_tick(void);
void scheduler_yield(void);
void scheduler_remove(uint32_t pid);

#endif
