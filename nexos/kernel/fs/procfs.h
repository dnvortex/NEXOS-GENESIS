/* NexOS — kernel/fs/procfs.h | /proc virtual filesystem | MIT License */
#ifndef PROCFS_H
#define PROCFS_H

#include "vfs.h"

void        procfs_init(void);
vfs_node_t *procfs_get_root(void);

#endif
