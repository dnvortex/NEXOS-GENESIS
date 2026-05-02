/* NexOS — kernel/proc/syscall.h | System call interface | MIT License */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_EXIT    0
#define SYS_READ    1
#define SYS_WRITE   2
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_FORK    5
#define SYS_EXEC    6
#define SYS_GETPID  7
#define SYS_SLEEP   8
#define SYS_STAT    9
#define SYS_MKDIR   10
#define SYS_READDIR 11
#define SYS_GETCWD  12
#define SYS_CHDIR   13
#define SYS_SBRK    14

void syscall_init(void);

#endif
