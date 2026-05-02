/* NexOS — kernel/fs/vfs.h | Virtual Filesystem layer | MIT License */
#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_NODE_FILE    0x01
#define VFS_NODE_DIR     0x02
#define VFS_NODE_CHARDEV 0x04
#define VFS_NODE_BLKDEV  0x08
#define VFS_NODE_PIPE    0x10
#define VFS_NODE_SYMLINK 0x20
#define VFS_NODE_MOUNT   0x40

#define VFS_NAME_MAX 256
#define VFS_PATH_MAX 1024
#define VFS_MAX_MOUNTS 16
#define VFS_MAX_FDS    16

struct vfs_node;

typedef struct vfs_dirent {
    uint32_t inode;
    char     name[VFS_NAME_MAX];
} vfs_dirent_t;

typedef struct vfs_stat {
    uint32_t inode;
    uint32_t type;
    uint64_t size;
    uint32_t nlinks;
} vfs_stat_t;

typedef uint32_t (*vfs_read_fn)(struct vfs_node *, uint64_t, uint32_t, uint8_t *);
typedef uint32_t (*vfs_write_fn)(struct vfs_node *, uint64_t, uint32_t, const uint8_t *);
typedef void     (*vfs_open_fn)(struct vfs_node *, int flags);
typedef void     (*vfs_close_fn)(struct vfs_node *);
typedef struct vfs_node *(*vfs_finddir_fn)(struct vfs_node *, const char *name);
typedef int      (*vfs_readdir_fn)(struct vfs_node *, uint32_t idx, vfs_dirent_t *);
typedef int      (*vfs_mkdir_fn)(struct vfs_node *, const char *name);
typedef int      (*vfs_create_fn)(struct vfs_node *, const char *name, uint32_t flags);
typedef int      (*vfs_unlink_fn)(struct vfs_node *, const char *name);

typedef struct vfs_node {
    char         name[VFS_NAME_MAX];
    uint32_t     type;
    uint64_t     size;
    uint32_t     inode;
    uint32_t     flags;
    void        *priv;             /* filesystem-private data */

    vfs_read_fn    read;
    vfs_write_fn   write;
    vfs_open_fn    open;
    vfs_close_fn   close;
    vfs_finddir_fn finddir;
    vfs_readdir_fn readdir;
    vfs_mkdir_fn   mkdir;
    vfs_create_fn  create;
    vfs_unlink_fn  unlink;
} vfs_node_t;

void         vfs_init(void);
vfs_node_t  *vfs_open(const char *path, int flags);
void         vfs_close(vfs_node_t *node);
uint32_t     vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buf);
uint32_t     vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size, const uint8_t *buf);
int          vfs_readdir(vfs_node_t *node, uint32_t idx, vfs_dirent_t *dirent);
int          vfs_mkdir(const char *path);
int          vfs_create(const char *path, uint32_t flags);
int          vfs_stat(const char *path, vfs_stat_t *stat);
int          vfs_unlink(const char *path);
int          vfs_mount(const char *path, vfs_node_t *fs_root);
vfs_node_t  *vfs_get_root(void);

#endif
