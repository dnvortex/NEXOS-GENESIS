/* NexOS — kernel/proc/syscall.h
 * Linux x86_64 syscall numbers (from unistd_64.h)
 * Programs compiled for Linux x86_64 can call these via INT 0x80.
 * MIT License */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* ── Linux x86_64 ABI numbers ──────────────────────────────────────────── */
#define SYS_READ            0
#define SYS_WRITE           1
#define SYS_OPEN            2
#define SYS_CLOSE           3
#define SYS_STAT            4
#define SYS_FSTAT           5
#define SYS_LSEEK           8
#define SYS_MMAP            9
#define SYS_MPROTECT        10
#define SYS_MUNMAP          11
#define SYS_BRK             12
#define SYS_RT_SIGACTION    13
#define SYS_RT_SIGPROCMASK  14
#define SYS_RT_SIGRETURN    15
#define SYS_IOCTL           16
#define SYS_PREAD64         17
#define SYS_PWRITE64        18
#define SYS_READV           19
#define SYS_WRITEV          20
#define SYS_ACCESS          21
#define SYS_PIPE            22
#define SYS_SELECT          23
#define SYS_SCHED_YIELD     24
#define SYS_MADVISE         28
#define SYS_DUP             32
#define SYS_DUP2            33
#define SYS_NANOSLEEP       35
#define SYS_GETITIMER       36
#define SYS_SENDFILE        40
#define SYS_SOCKET          41
#define SYS_CONNECT         42
#define SYS_ACCEPT          43
#define SYS_SENDTO          44
#define SYS_RECVFROM        45
#define SYS_BIND            49
#define SYS_LISTEN          50
#define SYS_CLONE           56
#define SYS_FORK            57
#define SYS_EXECVE          59
#define SYS_EXIT            60
#define SYS_WAIT4           61
#define SYS_KILL            62
#define SYS_UNAME           63
#define SYS_FCNTL           72
#define SYS_FTRUNCATE       77
#define SYS_GETDENTS        78
#define SYS_GETCWD          79
#define SYS_CHDIR           80
#define SYS_RENAME          82
#define SYS_MKDIR           83
#define SYS_RMDIR           84
#define SYS_GETPID          39
#define SYS_UNLINK          87
#define SYS_READLINK        89
#define SYS_CHMOD           90
#define SYS_CHOWN           92
#define SYS_UMASK           95
#define SYS_GETTIMEOFDAY    96
#define SYS_GETRLIMIT       97
#define SYS_SYSINFO         99
#define SYS_GETUID          102
#define SYS_GETGID          104
#define SYS_GETEUID         107
#define SYS_GETEGID         108
#define SYS_GETPPID         110
#define SYS_SETSID          112
#define SYS_GETGROUPS       115
#define SYS_SIGALTSTACK     131
#define SYS_PRCTL           157
#define SYS_ARCH_PRCTL      158
#define SYS_FUTEX           202
#define SYS_GETDENTS64      217
#define SYS_SET_TID_ADDRESS 218
#define SYS_CLOCK_GETTIME   228
#define SYS_EXIT_GROUP      231
#define SYS_SET_ROBUST_LIST 273

/* ── NexOS-specific (above Linux range, won't conflict) ─────────────────── */
#define SYS_SLEEP           300   /* timer_sleep_ms(ms) */

void syscall_init(void);

#endif
