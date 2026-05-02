/* NexOS — kernel/proc/syscall.h
 * Linux x86_64 syscall numbers (from unistd_64.h)
 * Programs compiled for Linux x86_64 can call these via INT 0x80.
 * MIT License */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* ── Linux x86_64 ABI numbers ──────────────────────────────────────────── */
#define SYS_READ           0
#define SYS_WRITE          1
#define SYS_OPEN           2
#define SYS_CLOSE          3
#define SYS_STAT           4
#define SYS_FSTAT          5
#define SYS_LSEEK          8
#define SYS_MMAP           9
#define SYS_MPROTECT       10
#define SYS_MUNMAP         11
#define SYS_BRK            12
#define SYS_IOCTL          16
#define SYS_ACCESS         21
#define SYS_PIPE           22
#define SYS_SELECT         23
#define SYS_DUP            32
#define SYS_DUP2           33
#define SYS_GETPID         39
#define SYS_FORK           57
#define SYS_EXECVE         59
#define SYS_EXIT           60
#define SYS_WAIT4          61
#define SYS_UNAME          63
#define SYS_GETDENTS       78
#define SYS_GETCWD         79
#define SYS_CHDIR          80
#define SYS_RENAME         82
#define SYS_MKDIR          83
#define SYS_RMDIR          84
#define SYS_UNLINK         87
#define SYS_READLINK       89
#define SYS_CHMOD          90
#define SYS_CHOWN          92
#define SYS_UMASK          95
#define SYS_GETTIMEOFDAY   96
#define SYS_GETUID         102
#define SYS_GETGID         104
#define SYS_GETEUID        107
#define SYS_GETEGID        108
#define SYS_GETPPID        110
#define SYS_SETSID         112
#define SYS_GETGROUPS      115
#define SYS_GETDENTS64     217
#define SYS_CLOCK_GETTIME  228

/* ── NexOS-specific (above Linux range, won't conflict) ─────────────────── */
#define SYS_SLEEP          300   /* timer_sleep_ms(ms) */

void syscall_init(void);

#endif
