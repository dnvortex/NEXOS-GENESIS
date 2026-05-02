/* NexOS — userspace/init/init.c | PID 1 init process | MIT License */
#include "../../kernel/kernel.h"
#include "../../kernel/drivers/vga.h"
#include "../../kernel/drivers/serial.h"
#include "../../kernel/drivers/ata.h"
#include "../../kernel/fs/vfs.h"
#include "../../kernel/fs/fat32.h"
#include "../../kernel/fs/ramfs.h"
#include "../../kernel/proc/process.h"
#include "../../kernel/proc/scheduler.h"
#include "../../kernel/mm/heap.h"

extern void nsh_main(void);

static void vfs_write_file(const char *path, const char *content) {
    vfs_create(path, 0);
    vfs_node_t *node = vfs_open(path, 0);
    if (node) {
        size_t len = 0;
        while (content[len]) len++;
        vfs_write(node, 0, (uint32_t)len, (const uint8_t *)content);
        vfs_close(node);
    }
}

static void setup_etc(void) {
    /* TASK 8 — required config files */
    vfs_write_file("/etc/nexos.conf",
        "hostname=nexos\n"
        "version=0.1\n"
        "default_shell=/bin/nsh\n"
    );

    vfs_write_file("/etc/passwd",
        "root:x:0:0:root:/root:/bin/nsh\n"
        "user:x:1000:1000:user:/home/user:/bin/nsh\n"
    );

    vfs_write_file("/etc/motd",
        "Welcome to NexOS 0.1\n"
        "Type 'help' for a list of commands.\n"
    );

    vfs_write_file("/etc/nsh.rc",
        "# NexOS shell startup script\n"
        "export PATH=/bin:/usr/bin\n"
        "export HOME=/home/user\n"
        "export USER=root\n"
        "export PS1=[root@nexos]$ \n"
        "cat /etc/motd\n"
    );

    vfs_mkdir("/home/user");
    vfs_mkdir("/var/log");

    vfs_write_file("/var/log/kernel.log", "# NexOS kernel log\n");
}

static void setup_dev(void) {
    vfs_create("/dev/null",   VFS_NODE_CHARDEV);
    vfs_create("/dev/zero",   VFS_NODE_CHARDEV);
    vfs_create("/dev/tty0",   VFS_NODE_CHARDEV);
    vfs_create("/dev/stdin",  VFS_NODE_CHARDEV);
    vfs_create("/dev/stdout", VFS_NODE_CHARDEV);
    vfs_create("/dev/stderr", VFS_NODE_CHARDEV);
    klog(LOG_INFO, "init: /dev populated");
}

static void try_mount_disk(void) {
    vfs_node_t *fat_root = fat32_mount(ATA_PRIMARY_MASTER, 0);
    if (fat_root) {
        vfs_mount("/mnt", fat_root);
        klog(LOG_INFO, "init: FAT32 disk mounted at /mnt");
    } else {
        klog(LOG_WARN, "init: No FAT32 disk found, running from ramfs only");
    }
}

static void reap_zombies(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i] && processes[i]->state == PROC_ZOMBIE &&
            processes[i]->ppid == 1) {
            klog(LOG_DEBUG, "init: reaped zombie PID %u", processes[i]->pid);
            kfree(processes[i]->stack);
            kfree(processes[i]);
            processes[i] = NULL;
            if (process_count > 0) process_count--;
        }
    }
}

void init_main(void) {
    klog(LOG_INFO, "init: PID 1 starting");

    setup_etc();
    setup_dev();
    try_mount_disk();

    vfs_mkdir("/mnt");
    vfs_mkdir("/tmp");
    /* /proc is already mounted by procfs_init() in kernel_main */

    klog(LOG_INFO, "init: launching shell (nsh)");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    nsh_main();

    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    vga_puts("\n\nSystem halted. Please power off.\n");
    serial_puts("init: shell exited, system halted.\n");

    while (1) {
        reap_zombies();
        __asm__ volatile ("hlt");
    }
}
