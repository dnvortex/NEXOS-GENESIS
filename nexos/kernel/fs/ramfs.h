/* NexOS — kernel/fs/ramfs.h | RAM filesystem | MIT License */
#ifndef RAMFS_H
#define RAMFS_H

#include "vfs.h"

vfs_node_t *ramfs_create_root(void);

#endif
