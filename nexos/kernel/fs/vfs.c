/* NexOS — kernel/fs/vfs.c | Virtual Filesystem layer | MIT License */
#include "vfs.h"
#include "../kernel.h"
#include "../mm/heap.h"

/* Simple string functions for kernel use */
static int k_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static size_t k_strlen(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}
static void k_strcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

typedef struct {
    char        path[VFS_PATH_MAX];
    vfs_node_t *root;
} vfs_mount_t;

static vfs_mount_t  mounts[VFS_MAX_MOUNTS];
static int          mount_count = 0;
static vfs_node_t  *vfs_root = NULL;

void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].path[0] = 0;
        mounts[i].root    = NULL;
    }
    mount_count = 0;
    klog(LOG_INFO, "VFS initialized");
}

int vfs_mount(const char *path, vfs_node_t *fs_root) {
    if (mount_count >= VFS_MAX_MOUNTS) return -1;
    k_strcpy(mounts[mount_count].path, path, VFS_PATH_MAX);
    mounts[mount_count].root = fs_root;
    mount_count++;
    if (k_strcmp(path, "/") == 0) vfs_root = fs_root;
    klog(LOG_INFO, "VFS: mounted at %s", path);
    return 0;
}

vfs_node_t *vfs_get_root(void) { return vfs_root; }

/* Walk path from node, component by component */
static vfs_node_t *vfs_walk(vfs_node_t *node, const char *path) {
    if (!node || !path) return NULL;

    char component[VFS_NAME_MAX];
    while (*path == '/') path++;
    if (!*path) return node;

    /* Extract next component */
    size_t i = 0;
    while (*path && *path != '/' && i < VFS_NAME_MAX - 1) {
        component[i++] = *path++;
    }
    component[i] = 0;
    while (*path == '/') path++;

    vfs_node_t *child = NULL;
    if (node->finddir) child = node->finddir(node, component);
    if (!child) return NULL;

    if (!*path) return child;
    return vfs_walk(child, path);
}

/* Find the best-matching mount point for a path */
static vfs_node_t *vfs_resolve(const char *path) {
    if (!path || path[0] != '/') return NULL;

    /* Find deepest matching mount */
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < mount_count; i++) {
        size_t mlen = k_strlen(mounts[i].path);
        if (mlen > best_len && k_strlen(path) >= mlen) {
            /* Check prefix match */
            int match = 1;
            for (size_t j = 0; j < mlen; j++) {
                if (path[j] != mounts[i].path[j]) { match = 0; break; }
            }
            if (match) { best = i; best_len = mlen; }
        }
    }
    if (best < 0) return NULL;

    const char *rel = path + best_len;
    while (*rel == '/') rel++;
    return vfs_walk(mounts[best].root, rel);
}

vfs_node_t *vfs_open(const char *path, int flags) {
    UNUSED(flags);
    return vfs_resolve(path);
}

void vfs_close(vfs_node_t *node) {
    if (node && node->close) node->close(node);
}

uint32_t vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buf) {
    if (!node || !node->read) return 0;
    return node->read(node, offset, size, buf);
}

uint32_t vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size, const uint8_t *buf) {
    if (!node || !node->write) return 0;
    return node->write(node, offset, size, buf);
}

int vfs_readdir(vfs_node_t *node, uint32_t idx, vfs_dirent_t *dirent) {
    if (!node || !node->readdir) return -1;
    return node->readdir(node, idx, dirent);
}

int vfs_mkdir(const char *path) {
    /* Find parent */
    char parent_path[VFS_PATH_MAX];
    k_strcpy(parent_path, path, VFS_PATH_MAX);
    /* Find last slash */
    int last = -1;
    for (int i = 0; parent_path[i]; i++) if (parent_path[i] == '/') last = i;
    if (last < 0) return -1;
    const char *name = path + last + 1;
    if (last == 0) parent_path[1] = 0;
    else parent_path[last] = 0;

    vfs_node_t *parent = vfs_resolve(parent_path);
    if (!parent || !parent->mkdir) return -1;
    return parent->mkdir(parent, name);
}

int vfs_create(const char *path, uint32_t flags) {
    char parent_path[VFS_PATH_MAX];
    k_strcpy(parent_path, path, VFS_PATH_MAX);
    int last = -1;
    for (int i = 0; parent_path[i]; i++) if (parent_path[i] == '/') last = i;
    if (last < 0) return -1;
    const char *name = path + last + 1;
    if (last == 0) parent_path[1] = 0;
    else parent_path[last] = 0;

    vfs_node_t *parent = vfs_resolve(parent_path);
    if (!parent || !parent->create) return -1;
    return parent->create(parent, name, flags);
}

int vfs_stat(const char *path, vfs_stat_t *stat) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return -1;
    stat->inode  = node->inode;
    stat->type   = node->type;
    stat->size   = node->size;
    stat->nlinks = 1;
    return 0;
}

int vfs_unlink(const char *path) {
    char parent_path[VFS_PATH_MAX];
    k_strcpy(parent_path, path, VFS_PATH_MAX);
    int last = -1;
    for (int i = 0; parent_path[i]; i++) if (parent_path[i] == '/') last = i;
    if (last < 0) return -1;
    const char *name = path + last + 1;
    if (last == 0) parent_path[1] = 0;
    else parent_path[last] = 0;

    vfs_node_t *parent = vfs_resolve(parent_path);
    if (!parent || !parent->unlink) return -1;
    return parent->unlink(parent, name);
}
