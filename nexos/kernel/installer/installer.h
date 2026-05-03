/* NexOS — kernel/installer/installer.h
 * In-OS disk installer TUI.
 * Activated when kernel cmdline contains "nexos.install".
 * MIT License */
#ifndef INSTALLER_H
#define INSTALLER_H

/* Called from kernel_main() when "nexos.install" is found in cmdline. */
void installer_run(void);

#endif
