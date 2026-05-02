/* NexOS — kernel/fs/ramfs.c | In-memory filesystem | MIT License */
#include "ramfs.h"
#include "../kernel.h"
#include "../mm/heap.h"

#define RAMFS_MAX_CHILDREN 64
#define RAMFS_MAX_DATA     (4 * 1024 * 1024)  /* 4MB per file max */

typedef struct ramfs_node {
    vfs_node_t      vnode;
    uint8_t        *data;
    uint64_t        data_size;
    struct ramfs_node *children[RAMFS_MAX_CHILDREN];
    int             child_count;
} ramfs_node_t;

static uint32_t next_inode = 1;

/* ---- string helpers ---- */
static size_t rf_strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
static int rf_strcmp(const char *a, const char *b) {
    while (*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}
static void rf_strcpy(char *d, const char *s, size_t max) {
    size_t i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]=0;
}
static void rf_memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd=(uint8_t*)d; const uint8_t *ss=(const uint8_t*)s;
    for(size_t i=0;i<n;i++) dd[i]=ss[i];
}

/* ---- VFS callbacks ---- */
static uint32_t ramfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buf) {
    ramfs_node_t *n = (ramfs_node_t *)node->priv;
    if (!n->data || offset >= n->data_size) return 0;
    uint32_t avail = (uint32_t)(n->data_size - offset);
    if (size > avail) size = avail;
    rf_memcpy(buf, n->data + offset, size);
    return size;
}

static uint32_t ramfs_write(vfs_node_t *node, uint64_t offset, uint32_t size, const uint8_t *buf) {
    ramfs_node_t *n = (ramfs_node_t *)node->priv;
    uint64_t end = offset + size;
    if (end > n->data_size) {
        uint8_t *newdata = (uint8_t *)krealloc(n->data, (size_t)end);
        if (!newdata) return 0;
        n->data = newdata;
        n->data_size = end;
        node->size   = end;
    }
    rf_memcpy(n->data + offset, buf, size);
    return size;
}

static vfs_node_t *ramfs_finddir(vfs_node_t *node, const char *name) {
    ramfs_node_t *n = (ramfs_node_t *)node->priv;
    for (int i = 0; i < n->child_count; i++) {
        if (rf_strcmp(n->children[i]->vnode.name, name) == 0)
            return &n->children[i]->vnode;
    }
    return NULL;
}

static int ramfs_readdir(vfs_node_t *node, uint32_t idx, vfs_dirent_t *dirent) {
    ramfs_node_t *n = (ramfs_node_t *)node->priv;
    if ((int)idx >= n->child_count) return -1;
    dirent->inode = n->children[idx]->vnode.inode;
    rf_strcpy(dirent->name, n->children[idx]->vnode.name, VFS_NAME_MAX);
    return 0;
}

static int ramfs_mkdir(vfs_node_t *node, const char *name) {
    ramfs_node_t *parent = (ramfs_node_t *)node->priv;
    if (parent->child_count >= RAMFS_MAX_CHILDREN) return -1;

    ramfs_node_t *child = (ramfs_node_t *)kmalloc(sizeof(ramfs_node_t));
    if (!child) return -1;

    uint8_t *zp = (uint8_t *)child;
    for (size_t i = 0; i < sizeof(ramfs_node_t); i++) zp[i] = 0;

    rf_strcpy(child->vnode.name, name, VFS_NAME_MAX);
    child->vnode.type     = VFS_NODE_DIR;
    child->vnode.inode    = next_inode++;
    child->vnode.priv     = child;
    child->vnode.read     = ramfs_read;
    child->vnode.write    = ramfs_write;
    child->vnode.finddir  = ramfs_finddir;
    child->vnode.readdir  = ramfs_readdir;
    child->vnode.mkdir    = ramfs_mkdir;
    child->vnode.create   = NULL; /* set below */

    parent->children[parent->child_count++] = child;
    return 0;
}

static int ramfs_create(vfs_node_t *node, const char *name, uint32_t flags) {
    UNUSED(flags);
    ramfs_node_t *parent = (ramfs_node_t *)node->priv;
    if (parent->child_count >= RAMFS_MAX_CHILDREN) return -1;

    ramfs_node_t *child = (ramfs_node_t *)kmalloc(sizeof(ramfs_node_t));
    if (!child) return -1;

    uint8_t *zp = (uint8_t *)child;
    for (size_t i = 0; i < sizeof(ramfs_node_t); i++) zp[i] = 0;

    rf_strcpy(child->vnode.name, name, VFS_NAME_MAX);
    child->vnode.type   = VFS_NODE_FILE;
    child->vnode.inode  = next_inode++;
    child->vnode.priv   = child;
    child->vnode.read   = ramfs_read;
    child->vnode.write  = ramfs_write;

    parent->children[parent->child_count++] = child;
    return 0;
}

static int ramfs_unlink(vfs_node_t *node, const char *name) {
    ramfs_node_t *parent = (ramfs_node_t *)node->priv;
    for (int i = 0; i < parent->child_count; i++) {
        if (rf_strcmp(parent->children[i]->vnode.name, name) == 0) {
            ramfs_node_t *victim = parent->children[i];
            if (victim->data) kfree(victim->data);
            kfree(victim);
            /* Compact array */
            for (int j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            return 0;
        }
    }
    return -1;
}

vfs_node_t *ramfs_create_root(void) {
    ramfs_node_t *root = (ramfs_node_t *)kmalloc(sizeof(ramfs_node_t));
    if (!root) return NULL;

    uint8_t *zp = (uint8_t *)root;
    for (size_t i = 0; i < sizeof(ramfs_node_t); i++) zp[i] = 0;

    rf_strcpy(root->vnode.name, "/", VFS_NAME_MAX);
    root->vnode.type    = VFS_NODE_DIR;
    root->vnode.inode   = next_inode++;
    root->vnode.priv    = root;
    root->vnode.read    = ramfs_read;
    root->vnode.write   = ramfs_write;
    root->vnode.finddir = ramfs_finddir;
    root->vnode.readdir = ramfs_readdir;
    root->vnode.mkdir   = ramfs_mkdir;
    root->vnode.create  = ramfs_create;
    root->vnode.unlink  = ramfs_unlink;

    /* Create standard top-level directories */
    const char *dirs[] = {"bin","dev","etc","home","lib","tmp","var","proc","boot"};
    for (int i = 0; i < 9; i++) ramfs_mkdir(&root->vnode, dirs[i]);

    klog(LOG_INFO, "ramfs: root filesystem created");
    return &root->vnode;
}
