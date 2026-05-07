#include <sys/syscall.h>
#include <unistd.h>

void _start() {
    // sys_write: fd=1, buf, len
    const char *msg = "Hello from userspace!\n";
    syscall(1, 1, msg, 22);
    // sys_exit
    syscall(60, 0);
}