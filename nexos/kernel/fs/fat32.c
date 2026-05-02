/* NexOS — kernel/fs/fat32.c | FAT32 read/write driver with LFN support | MIT License */
#include "fat32.h"
#include "../kernel.h"
#include "../mm/heap.h"
#include "../drivers/ata.h"

/* ---- helpers ---- */
static void k_memset(void *p, uint8_t v, size_t n) { uint8_t *b=(uint8_t*)p; for(size_t i=0;i<n;i++) b[i]=v; }
static void k_memcpy(void *d, const void *s, size_t n) { uint8_t *dd=(uint8_t*)d; const uint8_t *ss=(const uint8_t*)s; for(size_t i=0;i<n;i++) dd[i]=ss[i]; }
static size_t k_strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
static int k_strcmp(const char *a, const char *b) { while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
static void k_strcpy(char *d, const char *s, size_t max) { size_t i=0; while(i<max-1&&s[i]){d[i]=s[i];i++;} d[i]=0; }
static void k_strupr(char *s) { while(*s) { if(*s>='a'&&*s<='z') *s-=32; s++; } }

typedef struct fat32_volume {
    int      drive;
    uint32_t lba_start;
    uint32_t fat_start;
    uint32_t data_start;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t fat_size;
    uint32_t bytes_per_sector;
} fat32_volume_t;

typedef struct fat32_file_priv {
    fat32_volume_t *vol;
    uint32_t        first_cluster;
    uint32_t        file_size;
} fat32_file_priv_t;

static uint8_t *sector_buf;
#define SECTOR_SIZE 512

static int read_sector(fat32_volume_t *vol, uint32_t lba, void *buf) {
    return ata_read_sectors(vol->drive, vol->lba_start + lba, 1, buf);
}

static uint32_t get_fat_entry(fat32_volume_t *vol, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = vol->fat_start + fat_offset / SECTOR_SIZE;
    uint32_t fat_index  = fat_offset % SECTOR_SIZE;
    read_sector(vol, fat_sector, sector_buf);
    return (*(uint32_t *)(sector_buf + fat_index)) & 0x0FFFFFFF;
}

static uint32_t cluster_to_lba(fat32_volume_t *vol, uint32_t cluster) {
    return vol->data_start + (cluster - 2) * vol->sectors_per_cluster;
}

/* Read file data following cluster chain */
static uint32_t fat32_read_data(fat32_volume_t *vol, uint32_t first_cluster,
                                 uint64_t offset, uint32_t size, uint8_t *buf) {
    uint32_t bpc = vol->bytes_per_cluster;
    uint32_t cluster_skip = (uint32_t)(offset / bpc);
    uint32_t byte_offset  = (uint32_t)(offset % bpc);
    uint32_t written = 0;

    uint32_t cluster = first_cluster;
    for (uint32_t i = 0; i < cluster_skip && cluster < 0x0FFFFFF8; i++) {
        cluster = get_fat_entry(vol, cluster);
    }

    uint8_t *clus_buf = (uint8_t *)kmalloc(bpc);
    if (!clus_buf) return 0;

    while (size > 0 && cluster >= 2 && cluster < 0x0FFFFFF8) {
        /* Read all sectors of this cluster */
        uint32_t lba = cluster_to_lba(vol, cluster);
        for (uint32_t s = 0; s < vol->sectors_per_cluster; s++) {
            read_sector(vol, lba + s, clus_buf + s * SECTOR_SIZE);
        }

        uint32_t avail = bpc - byte_offset;
        uint32_t copy  = avail < size ? avail : size;
        k_memcpy(buf + written, clus_buf + byte_offset, copy);
        written     += copy;
        size        -= copy;
        byte_offset  = 0;

        cluster = get_fat_entry(vol, cluster);
    }

    kfree(clus_buf);
    return written;
}

/* VFS read callback */
static uint32_t fat32_vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buf) {
    fat32_file_priv_t *priv = (fat32_file_priv_t *)node->priv;
    if (!priv) return 0;
    if (offset >= priv->file_size) return 0;
    uint32_t avail = priv->file_size - (uint32_t)offset;
    if (size > avail) size = avail;
    return fat32_read_data(priv->vol, priv->first_cluster, offset, size, buf);
}

/* Parse 8.3 short name */
static void parse_83_name(const uint8_t raw[11], char *out) {
    int i = 0, j = 0;
    while (i < 8 && raw[i] != ' ') out[j++] = raw[i++];
    i = 8;
    if (raw[8] != ' ') {
        out[j++] = '.';
        while (i < 11 && raw[i] != ' ') out[j++] = raw[i++];
    }
    out[j] = 0;
}

/* Convert UCS-2 LFN to ASCII */
static void lfn_append(const fat32_lfn_entry_t *lfn, char *buf, size_t buflen) {
    uint16_t chars[13];
    k_memcpy(chars,     lfn->name1, 5*2);
    k_memcpy(chars+5,   lfn->name2, 6*2);
    k_memcpy(chars+11,  lfn->name3, 2*2);

    int order = (lfn->order & 0x1F) - 1;
    for (int i = 0; i < 13; i++) {
        int pos = order * 13 + i;
        if ((size_t)pos >= buflen - 1) break;
        if (chars[i] == 0 || chars[i] == 0xFFFF) { buf[pos] = 0; break; }
        buf[pos] = (char)(chars[i] & 0x7F);
    }
}

/* Read directory cluster and build vfs_node children */
typedef struct fat32_dir_priv {
    fat32_volume_t *vol;
    uint32_t        first_cluster;
    /* Children are built lazily */
} fat32_dir_priv_t;

static vfs_node_t *fat32_finddir(vfs_node_t *node, const char *name);
static int         fat32_readdir(vfs_node_t *node, uint32_t idx, vfs_dirent_t *dirent);

static void fat32_populate_node(vfs_node_t *vnode, fat32_volume_t *vol,
                                 uint32_t cluster, uint32_t size, uint32_t attr,
                                 const char *name) {
    k_strcpy(vnode->name, name, VFS_NAME_MAX);
    vnode->inode  = cluster;
    vnode->size   = size;
    vnode->flags  = attr;
    vnode->read   = fat32_vfs_read;

    if (attr & FAT32_ATTR_DIRECTORY) {
        vnode->type    = VFS_NODE_DIR;
        vnode->finddir = fat32_finddir;
        vnode->readdir = fat32_readdir;
        fat32_dir_priv_t *dp = (fat32_dir_priv_t *)kmalloc(sizeof(fat32_dir_priv_t));
        if (dp) { dp->vol = vol; dp->first_cluster = cluster; }
        vnode->priv = dp;
    } else {
        vnode->type = VFS_NODE_FILE;
        fat32_file_priv_t *fp = (fat32_file_priv_t *)kmalloc(sizeof(fat32_file_priv_t));
        if (fp) { fp->vol = vol; fp->first_cluster = cluster; fp->file_size = size; }
        vnode->priv = fp;
    }
}

/* 64 entries × sizeof(vfs_node_t)=360 = 23 KB per call (was 512→184 KB). */
#define MAX_DIR_ENTRIES 64
static int fat32_enum_dir(fat32_volume_t *vol, uint32_t first_cluster,
                           vfs_node_t *nodes, int *count, int max) {
    uint32_t bpc = vol->bytes_per_cluster;
    uint8_t *buf = (uint8_t *)kmalloc(bpc);
    if (!buf) return -1;

    char lfn_buf[VFS_NAME_MAX];
    k_memset(lfn_buf, 0, VFS_NAME_MAX);
    int lfn_active = 0;

    uint32_t cluster = first_cluster;
    *count = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8 && *count < max) {
        uint32_t lba = cluster_to_lba(vol, cluster);
        for (uint32_t s = 0; s < vol->sectors_per_cluster; s++) {
            read_sector(vol, lba + s, buf + s * SECTOR_SIZE);
        }

        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)buf;
        uint32_t entries_per_cluster = bpc / 32;

        for (uint32_t i = 0; i < entries_per_cluster && *count < max; i++) {
            fat32_dir_entry_t *e = &entries[i];
            if (e->name[0] == 0x00) goto done;
            if (e->name[0] == 0xE5) { lfn_active = 0; k_memset(lfn_buf, 0, VFS_NAME_MAX); continue; }
            if (e->attr == FAT32_ATTR_LFN) {
                lfn_append((fat32_lfn_entry_t *)e, lfn_buf, VFS_NAME_MAX);
                lfn_active = 1;
                continue;
            }
            if (e->attr & FAT32_ATTR_VOLUME_ID) { lfn_active = 0; k_memset(lfn_buf, 0, VFS_NAME_MAX); continue; }

            char short_name[16];
            parse_83_name(e->name, short_name);
            if (short_name[0] == '.' ) { lfn_active = 0; k_memset(lfn_buf, 0, VFS_NAME_MAX); continue; }

            char *use_name = lfn_active ? lfn_buf : short_name;
            uint32_t cluster_hi = ((uint32_t)e->first_cluster_hi << 16);
            uint32_t first_clus = cluster_hi | e->first_cluster_lo;

            fat32_populate_node(&nodes[*count], vol, first_clus, e->file_size, e->attr, use_name);
            (*count)++;

            lfn_active = 0;
            k_memset(lfn_buf, 0, VFS_NAME_MAX);
        }
        cluster = get_fat_entry(vol, cluster);
    }
done:
    kfree(buf);
    return 0;
}

static vfs_node_t *fat32_finddir(vfs_node_t *node, const char *name) {
    fat32_dir_priv_t *priv = (fat32_dir_priv_t *)node->priv;
    if (!priv) return NULL;

    vfs_node_t *entries = (vfs_node_t *)kmalloc(sizeof(vfs_node_t) * MAX_DIR_ENTRIES);
    if (!entries) return NULL;
    k_memset(entries, 0, sizeof(vfs_node_t) * MAX_DIR_ENTRIES);

    int count = 0;
    fat32_enum_dir(priv->vol, priv->first_cluster, entries, &count, MAX_DIR_ENTRIES);

    for (int i = 0; i < count; i++) {
        char upper_entry[VFS_NAME_MAX], upper_name[VFS_NAME_MAX];
        k_strcpy(upper_entry, entries[i].name, VFS_NAME_MAX);
        k_strcpy(upper_name,  name, VFS_NAME_MAX);
        k_strupr(upper_entry); k_strupr(upper_name);

        if (k_strcmp(entries[i].name, name) == 0 || k_strcmp(upper_entry, upper_name) == 0) {
            vfs_node_t *result = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
            if (!result) { kfree(entries); return NULL; }
            k_memcpy(result, &entries[i], sizeof(vfs_node_t));
            kfree(entries);
            return result;
        }
    }
    kfree(entries);
    return NULL;
}

static int fat32_readdir(vfs_node_t *node, uint32_t idx, vfs_dirent_t *dirent) {
    fat32_dir_priv_t *priv = (fat32_dir_priv_t *)node->priv;
    if (!priv) return -1;

    vfs_node_t *entries = (vfs_node_t *)kmalloc(sizeof(vfs_node_t) * MAX_DIR_ENTRIES);
    if (!entries) return -1;
    k_memset(entries, 0, sizeof(vfs_node_t) * MAX_DIR_ENTRIES);

    int count = 0;
    fat32_enum_dir(priv->vol, priv->first_cluster, entries, &count, MAX_DIR_ENTRIES);

    if ((int)idx >= count) { kfree(entries); return -1; }
    dirent->inode = entries[idx].inode;
    k_strcpy(dirent->name, entries[idx].name, VFS_NAME_MAX);
    kfree(entries);
    return 0;
}

vfs_node_t *fat32_mount(int drive, uint32_t lba_start) {
    sector_buf = (uint8_t *)kmalloc(SECTOR_SIZE);
    if (!sector_buf) return NULL;

    fat32_bpb_t bpb;
    if (ata_read_sectors(drive, lba_start, 1, &bpb) < 0) {
        klog(LOG_ERROR, "FAT32: failed to read BPB from drive %d LBA %u", drive, lba_start);
        kfree(sector_buf);
        return NULL;
    }

    fat32_volume_t *vol = (fat32_volume_t *)kmalloc(sizeof(fat32_volume_t));
    if (!vol) { kfree(sector_buf); return NULL; }

    vol->drive              = drive;
    vol->lba_start          = lba_start;
    vol->bytes_per_sector   = bpb.bytes_per_sector;
    vol->sectors_per_cluster= bpb.sectors_per_cluster;
    vol->bytes_per_cluster  = bpb.bytes_per_sector * bpb.sectors_per_cluster;
    vol->fat_start          = bpb.reserved_sectors;
    vol->fat_size           = bpb.fat_size_32;
    vol->data_start         = bpb.reserved_sectors + bpb.fat_count * bpb.fat_size_32;
    vol->root_cluster       = bpb.root_cluster;

    klog(LOG_INFO, "FAT32: mounted drive %d, root cluster=%u", drive, vol->root_cluster);

    vfs_node_t *root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!root) { kfree(vol); kfree(sector_buf); return NULL; }
    k_memset(root, 0, sizeof(vfs_node_t));

    fat32_dir_priv_t *dp = (fat32_dir_priv_t *)kmalloc(sizeof(fat32_dir_priv_t));
    if (!dp) { kfree(root); kfree(vol); kfree(sector_buf); return NULL; }
    dp->vol           = vol;
    dp->first_cluster = vol->root_cluster;

    k_strcpy(root->name, "/", VFS_NAME_MAX);
    root->type    = VFS_NODE_DIR;
    root->inode   = vol->root_cluster;
    root->priv    = dp;
    root->finddir = fat32_finddir;
    root->readdir = fat32_readdir;

    return root;
}
